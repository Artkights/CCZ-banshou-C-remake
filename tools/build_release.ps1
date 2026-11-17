param(
    [string]$ToolsetVersion = '14.29'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $cmake = (& python -c "import cmake, os; print(os.path.join(os.path.dirname(cmake.__file__), 'data', 'bin', 'cmake.exe'))" 2>$null)
    if (-not $cmake -or -not (Test-Path -LiteralPath $cmake)) {
        throw 'cmake.exe was not found on PATH and the Python cmake package is not installed.'
    }
}
$dist = Join-Path $project 'dist'
$releaseExe = Join-Path $dist '扳手.exe'
$releaseHash = $releaseExe + '.sha256'
$releaseJson = Join-Path $dist 'release.json'

# CMake/MSBuild 会把告警写到 stderr，而 PowerShell 5.1 在 Stop 偏好下会把
# 原生命令的 stderr 当作终止错误，导致构建被误判为失败。统一走这里调用。
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Find-V142Toolset {
    # 允许显式指定，避免受限环境下目录枚举被拦截时无法定位工具集。
    if ($env:BANSHOU_MSVC_TOOLSET) {
        return $env:BANSHOU_MSVC_TOOLSET
    }
    $roots = @(
        'C:\Program Files\Microsoft Visual Studio\2022',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022',
        'C:\Program Files\Microsoft Visual Studio\2019',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019',
        'D:\Program Files\Microsoft Visual Studio\2022',
        'D:\Program Files (x86)\Microsoft Visual Studio\2022'
    )
    $installRoots = New-Object System.Collections.Generic.List[string]
    foreach ($root in $roots) {
        if (-not [IO.Directory]::Exists($root)) { continue }
        $installRoots.Add($root)
        foreach ($edition in [IO.Directory]::GetDirectories($root)) {
            $installRoots.Add($edition)
        }
    }
    foreach ($installRoot in $installRoots) {
        $tools = Join-Path $installRoot 'VC\Tools\MSVC'
        if (-not [IO.Directory]::Exists($tools)) { continue }
        $versions = @([IO.Directory]::GetDirectories($tools) |
            ForEach-Object { Split-Path -Leaf $_ } |
            Sort-Object -Descending)
        $match = $versions | Where-Object { $_ -like "$ToolsetVersion.*" } |
            Select-Object -First 1
        if ($match) { return $match }
    }
    return $null
}

$mode = 'single-exe-native'

$build = Join-Path $project 'build/native'
$configureArgs = @(
    '-S', $project, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'Win32',
    '-DCMAKE_AUTOUIC=OFF', '-DCMAKE_AUTORCC=OFF', '-DCMAKE_AUTOMOC=OFF'
)
if ((Invoke-Native -FilePath $cmake -Arguments $configureArgs) -ne 0) {
    throw 'CMake configure failed'
}
if ((Invoke-Native -FilePath $cmake -Arguments @('--build', $build, '--config', 'Release', '--parallel', '2')) -ne 0) {
    throw 'Release build failed'
}

$exe = $releaseExe
if (!(Test-Path -LiteralPath $exe)) { throw 'Release executable was not produced' }
$symbolDir = Join-Path $dist 'symbols'
New-Item -ItemType Directory -Force -Path $symbolDir | Out-Null
$symbolPdb = Join-Path $symbolDir '扳手.pdb'
$buildPdb = Join-Path $build 'Release\扳手.pdb'
if (Test-Path -LiteralPath $buildPdb) {
    Copy-Item -LiteralPath $buildPdb -Destination $symbolPdb -Force
}
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToUpperInvariant()
[IO.File]::WriteAllText($releaseHash, "$hash  扳手.exe", [Text.UTF8Encoding]::new($false))
$size = (Get-Item -LiteralPath $exe).Length

if ((Invoke-Native -FilePath $cmake -Arguments @('--build', $build, '--config', 'Release',
        '--target', 'native_tests', 'lifecycle_tests', 'win32_ui_tests', 'parity_tests')) -ne 0) {
    throw 'Test target build failed'
}
if ((Invoke-Native -FilePath $cmake -Arguments @('--build', $build, '--config', 'Release', '--target', 'RUN_TESTS')) -ne 0) {
    throw 'CTest failed'
}
$audit = Join-Path $project 'tools\check_single_exe.ps1'
& $audit -ExePath $exe -DistributionDirectory $dist
if ($LASTEXITCODE -ne 0) {
    throw 'Single EXE dependency audit failed.'
}

$files = @()
$files += [ordered]@{ path = '扳手.exe'; sha256 = $hash; size = $size }
$zipPath = $null

$release = [ordered]@{
    mode = $mode
    exe = '扳手.exe'
    exeSha256 = $hash
    size = $size
    files = $files
    zip = if ($zipPath) { Split-Path -Leaf $zipPath } else { $null }
    builtAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK')
}
[IO.File]::WriteAllText($releaseJson,
    ($release | ConvertTo-Json -Depth 6),
    [Text.UTF8Encoding]::new($false))

Write-Host "Release: $exe"
Write-Host "Mode:    $mode"
Write-Host "SHA256:  $hash"
Write-Host "Bytes:   $size"
if ($zipPath) { Write-Host "Portable: $zipPath" }
