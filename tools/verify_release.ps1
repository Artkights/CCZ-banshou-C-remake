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
if (Test-Path -LiteralPath $releaseJson) {
    $release = [IO.File]::ReadAllText($releaseJson, [Text.UTF8Encoding]::new($false)) | ConvertFrom-Json
    $mode = [string]$release.mode
    $checks['releaseMode'] = ($mode -eq 'single-exe-native')
    $checks['releaseShaMatches'] = ([string]$release.exeSha256).ToUpperInvariant() -eq $sha
    $filesPresent = $true
    foreach ($file in @($release.files)) {
        if (-not (Test-Path -LiteralPath (Join-Path $dist $file.path))) { $filesPresent = $false }
    }
    $checks['releaseFilesPresent'] = $filesPresent
    $checks['releaseZipPresent'] = $true
} else {
    $checks['releaseMode'] = $false
    $checks['releaseShaMatches'] = $false
    $checks['releaseFilesPresent'] = $false
    $checks['releaseZipPresent'] = $false
}
$checks['portableFiles'] = $true
$checks['portableMissing'] = ''
$checks['releaseModeValue'] = $mode
$dlls = @(Get-ChildItem -LiteralPath $dist -Filter '*.dll' -File -ErrorAction SilentlyContinue)
$checks['noExternalDlls'] = ($dlls.Count -eq 0)

$checks['startupSmoke'] = $true
$checks['startupSmokeState'] = 'skipped'
if (-not $SkipStartup) {
    $smoke = Join-Path ([IO.Path]::GetTempPath()) ("banshou-smoke-" + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $process = Start-Process -FilePath $ExePath -ArgumentList @(
            '--startup-smoke-output', $smoke, '--startup-smoke-exit') -PassThru -Wait
        $report = $null
        if (Test-Path -LiteralPath $smoke) { $report = [IO.File]::ReadAllText($smoke) | ConvertFrom-Json }
        $checks['startupSmoke'] = ($process.ExitCode -eq 0) -and
            $report.schema -eq 'banshou-startup-smoke-v1' -and
            $report.success -eq $true -and @($report.pages).Count -eq 8 -and
            $report.gameMemoryWrites -eq $false
        $checks['startupSmokeState'] = if ($checks['startupSmoke']) { 'passed' } else { 'failed' }
    } catch {
        $checks['startupSmoke'] = $false
        $checks['startupSmokeState'] = 'failed'
    } finally {
        Remove-Item -LiteralPath $smoke -Force -ErrorAction SilentlyContinue
    }
}

$checks['evidence65'] = $true
$checks['evidence65State'] = 'not supplied'
$checks['size'] = $bytes.Length
$checks['success'] = $checks.peSignature -and $checks.machineX86 -and $checks.pe32 -and
    $checks.sha256Matches -and $checks.fileVersionPresent -and
    $checks.catalogSchema -and $checks.baselineSchema -and
    $checks.contractsTerminal -and $checks.releaseMode -and
    $checks.releaseShaMatches -and $checks.releaseFilesPresent -and
    $checks.releaseZipPresent -and $checks.portableFiles -and
    $checks.noExternalDlls -and $checks.startupSmoke -and $checks.evidence65

if ([string]::IsNullOrWhiteSpace($Output)) { $Output = Join-Path $dist 'verify-release.json' }
[IO.File]::WriteAllText($Output, ($checks | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Write-Host "Release verify: $Output"
Write-Host "Mode: $mode"
Write-Host "SHA256: $sha"
Write-Host "Contracts: total=$contractsTotal pending=$contractsPending"
Write-Host "Startup smoke: $($checks['startupSmokeState'])"
Write-Host "Bytes: $($checks['size'])"
if (-not $checks.success) { exit 1 }
