param(
    [string]$ExePath = '',
    [string]$Output = '',
    [string]$Sample65 = '',
    [string]$Sample66 = '',
    [switch]$SkipStartup
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dist = Join-Path $project 'dist'
$releaseJson = Join-Path $dist 'release.json'
if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $dist '扳手.exe'
}
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Release executable not found: $ExePath"
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$bytes = [IO.File]::ReadAllBytes($ExePath)
if ($bytes.Length -lt 512) {
    throw 'Executable is too short to be a PE image.'
}
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
$optional = $pe + 24
$magic = [BitConverter]::ToUInt16($bytes, $optional)
$checks = [ordered]@{
    exists = $true
    peSignature = ([BitConverter]::ToUInt32($bytes, $pe) -eq 0x00004550)
    machineX86 = ($machine -eq 0x014C)
    pe32 = ($magic -eq 0x010B)
    machine = ('0x{0:X4}' -f $machine)
    magic = ('0x{0:X4}' -f $magic)
}
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $ExePath).Hash.ToUpperInvariant()
$checks['sha256'] = $sha
$hashFile = $ExePath + '.sha256'
if (Test-Path -LiteralPath $hashFile) {
    $recorded = ((Get-Content -LiteralPath $hashFile -TotalCount 1) -split '\s+')[0]
    $checks['sha256Matches'] = ($recorded.ToUpperInvariant() -eq $sha)
} else {
    $checks['sha256Matches'] = $false
}
$versionInfo = (Get-Item -LiteralPath $ExePath).VersionInfo
$checks['fileVersion'] = $versionInfo.FileVersion
$checks['fileVersionPresent'] = -not [string]::IsNullOrWhiteSpace($versionInfo.FileVersion)
$symbols = Join-Path (Split-Path -Parent $ExePath) 'symbols'
$pdb = if (Test-Path -LiteralPath $symbols) {
    Get-ChildItem -LiteralPath $symbols -Filter '*.pdb' -File |
        Select-Object -First 1
} else { $null }
$checks['pdb'] = [bool]$pdb
$catalogPath = Join-Path $project 'resources\catalog.json'
$baselinePath = Join-Path $project 'resources\baseline.json'
$catalogRaw = if (Test-Path $catalogPath) { Get-Content -LiteralPath $catalogPath -Raw } else { '' }
$baselineRaw = if (Test-Path $baselinePath) { Get-Content -LiteralPath $baselinePath -Raw } else { '' }
$checks['catalogSchema'] = ($catalogRaw -match '"schema"\s*:\s*2')
$checks['baselineSchema'] = ($baselineRaw -match '"schema"\s*:\s*2')
$contractsTotal = 0
$contractsPending = 1
$summaryScript = @'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    baseline = json.load(stream)
tests = baseline.get("tests", [])
print(len(tests))
print(sum(1 for test in tests if test.get("nativeStatus") == "pending"))
'@
$summaryPath = Join-Path ([IO.Path]::GetTempPath()) ("banshou-contracts-" + [Guid]::NewGuid().ToString('N') + '.py')
try {
    [IO.File]::WriteAllText($summaryPath, $summaryScript, [Text.UTF8Encoding]::new($false))
    $lines = & python $summaryPath $baselinePath 2>$null
    if ($LASTEXITCODE -eq 0 -and @($lines).Count -ge 2) {
        $contractsTotal = [int]$lines[0]
        $contractsPending = [int]$lines[1]
    }
} catch {
} finally {
    Remove-Item -LiteralPath $summaryPath -Force -ErrorAction SilentlyContinue
}
$checks['contractsTotal'] = $contractsTotal
$checks['contractsPending'] = $contractsPending
$checks['contractsTerminal'] = ($contractsTotal -eq 399 -and $contractsPending -eq 0)

$mode = 'unknown'
$portableDir = Join-Path $dist 'portable'
if (Test-Path -LiteralPath $releaseJson) {
    $release = [IO.File]::ReadAllText($releaseJson, [Text.UTF8Encoding]::new($false)) | ConvertFrom-Json
    $mode = [string]$release.mode
    $checks['releaseMode'] = $mode
    $checks['releaseShaMatches'] = ([string]$release.exeSha256).ToUpperInvariant() -eq $sha
    $filesPresent = $true
    foreach ($file in @($release.files)) {
        if (-not (Test-Path -LiteralPath (Join-Path $dist $file.path))) {
            $filesPresent = $false
        }
    }
    $checks['releaseFilesPresent'] = $filesPresent
    if ($release.zip) {
        $checks['releaseZipPresent'] = Test-Path -LiteralPath (Join-Path $dist $release.zip)
    } else {
        $checks['releaseZipPresent'] = $true
    }
} else {
    $checks['releaseMode'] = 'missing'
    $checks['releaseShaMatches'] = $false
    $checks['releaseFilesPresent'] = $false
    $checks['releaseZipPresent'] = $false
}
if ($mode -eq 'portable-dynamic-qt') {
    $required = @('扳手.exe', 'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll',
        'platforms\qwindows.dll', 'README-便携版.txt')
    $missing = @()
    foreach ($item in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $portableDir $item))) {
            $missing += $item
        }
    }
    $checks['portableFiles'] = ($missing.Count -eq 0)
    $checks['portableMissing'] = ($missing -join ', ')
} elseif ($mode -eq 'single-exe-static') {
    $checks['portableFiles'] = $true
    $checks['portableMissing'] = ''
} else {
    $checks['portableFiles'] = $false
    $checks['portableMissing'] = 'release.json missing'
}

$runtimePath = ''
$runExe = $ExePath
if ($mode -eq 'portable-dynamic-qt') {
    $runtimePath = $portableDir
    $portableExe = Join-Path $portableDir '扳手.exe'
    if (Test-Path -LiteralPath $portableExe) { $runExe = $portableExe }
} else {
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) { $runtimePath = Split-Path -Parent $qmake.Source }
}
$smokeOk = $false
$smokeState = 'skipped'
if (-not $SkipStartup) {
    $smokeDir = Join-Path ([IO.Path]::GetTempPath()) ("banshou-verify-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null
    $smokeOutput = Join-Path $smokeDir 'smoke.json'
    $oldPath = $env:PATH
    $oldPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    try {
        if ($runtimePath) { $env:PATH = "$runtimePath;$env:PATH" }
        if ($mode -eq 'portable-dynamic-qt') {
            $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $portableDir 'platforms'
        }
        $process = Start-Process -FilePath $runExe -Wait -PassThru -NoNewWindow `
            -ArgumentList @('--startup-smoke-output', $smokeOutput, '--startup-smoke-exit')
        if ($process.ExitCode -eq 0 -and (Test-Path -LiteralPath $smokeOutput)) {
            $report = Get-Content -LiteralPath $smokeOutput -Raw | ConvertFrom-Json
            $smokeOk = ($report.schema -eq 'banshou-startup-smoke-v1' -and
                $report.success -eq $true -and
                @($report.pages).Count -eq 8 -and
                $report.gameMemoryWrites -eq $false)
            $smokeState = if ($smokeOk) { 'passed' } else { 'invalid report' }
        } else {
            $smokeState = "exit=$($process.ExitCode)"
        }
    } finally {
        $env:PATH = $oldPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlugins
        Remove-Item -LiteralPath $smokeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$checks['startupSmoke'] = $smokeOk
$checks['startupSmokeState'] = $smokeState

if ([string]::IsNullOrWhiteSpace($Sample65)) {
    $candidate = Join-Path $project '..\..\CCZModStudio_TestCopies\20260820_104551_加强版6.5未加密版\Ekd5.exe'
    if (Test-Path -LiteralPath $candidate) { $Sample65 = (Resolve-Path -LiteralPath $candidate).Path }
}
if ([string]::IsNullOrWhiteSpace($Sample66)) {
    $candidate = Join-Path $project '..\..\CCZModStudio_TestCopies\Ccz66ItemIconSmoke_20260820_084836\Ekd5.exe'
    if (Test-Path -LiteralPath $candidate) { $Sample66 = (Resolve-Path -LiteralPath $candidate).Path }
}
$evidenceOk = $true
$evidenceState = 'skipped'
if ($Sample65 -and (Test-Path -LiteralPath $Sample65)) {
    $evidenceDir = Join-Path ([IO.Path]::GetTempPath()) ("banshou-evidence-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
    $oldPath = $env:PATH
    try {
        if ($runtimePath) { $env:PATH = "$runtimePath;$env:PATH" }
        $process = Start-Process -FilePath $runExe -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput (Join-Path $evidenceDir 'stdout.txt') `
            -RedirectStandardError (Join-Path $evidenceDir 'stderr.txt') `
            -ArgumentList @('--evidence-static', '--exe', $Sample65, '--output', $evidenceDir)
        $bundles = @(Get-ChildItem -LiteralPath $evidenceDir -Filter '*-static.json' -File)
        $valid = $bundles.Count -gt 0
        foreach ($bundle in $bundles) {
            $json = Get-Content -LiteralPath $bundle.FullName -Raw | ConvertFrom-Json
            if ($json.schemaVersion -ne '1.0' -or
                ([string]$json.bundleDigest).Length -ne 64 -or
                ([string]$json.target.sha256).Length -ne 64) {
                $valid = $false
            }
        }
        $evidenceOk = ($process.ExitCode -eq 0 -and $valid)
        $evidenceState = if ($evidenceOk) { "bundles=$($bundles.Count)" } else { "exit=$($process.ExitCode)" }
    } finally {
        $env:PATH = $oldPath
        Remove-Item -LiteralPath $evidenceDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$checks['evidence65'] = $evidenceOk
$checks['evidence65State'] = $evidenceState
$checks['size'] = $bytes.Length
$checks['success'] = [bool]($checks.peSignature -and $checks.machineX86 -and
    $checks.pe32 -and $checks.sha256Matches -and $checks.catalogSchema -and
    $checks.baselineSchema -and $checks.contractsTerminal -and
    $checks.releaseShaMatches -and $checks.releaseFilesPresent -and
    $checks.releaseZipPresent -and $checks.portableFiles -and
    $checks.startupSmoke -and $checks.evidence65)
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $dist 'verify-release.json'
}
$directory = Split-Path -Parent $Output
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$checks | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host ("Release verify: " + $Output)
Write-Host ("Mode: " + $mode)
Write-Host ("SHA256: " + $sha)
Write-Host ("Contracts: total=$($checks.contractsTotal) pending=$($checks.contractsPending)")
Write-Host ("Startup smoke: " + $checks.startupSmokeState)
Write-Host ("Evidence 6.5: " + $checks.evidence65State)
if (-not $checks.success) {
    exit 1
}
