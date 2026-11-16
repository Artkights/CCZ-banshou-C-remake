param(
    [switch]$StaticQt,
    [string]$QtPrefix = '',
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

if ($StaticQt -and [string]::IsNullOrWhiteSpace($QtPrefix)) {
    throw 'Static Qt build requires -QtPrefix pointing to the Qt 5.15.2 static installation.'
}
if (-not $StaticQt -and [string]::IsNullOrWhiteSpace($QtPrefix)) {
    $candidates = @()
    if ($env:BANSHOU_STATIC_QT) { $candidates += $env:BANSHOU_STATIC_QT }
    $candidates += @('D:\Qt\5.15.2\msvc2019', 'C:\Qt\5.15.2\msvc2019')
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'lib\cmake\Qt5\Qt5Config.cmake')) {
            if (Find-V142Toolset) {
                $StaticQt = $true
                $QtPrefix = $candidate
            }
            break
        }
    }
}
if (-not $StaticQt -and [string]::IsNullOrWhiteSpace($QtPrefix)) {
    $QtPrefix = 'D:\Qt\5.14.2\msvc2017'
}
if (-not (Test-Path -LiteralPath $QtPrefix)) {
    throw "Qt prefix does not exist: $QtPrefix"
}
$mode = if ($StaticQt) { 'single-exe-static' } else { 'portable-dynamic-qt' }

$toolset = $null
if ($StaticQt) {
    $qtConfig = Join-Path $QtPrefix 'lib\cmake\Qt5\Qt5Config.cmake'
    $qtPluginConfig = Join-Path $QtPrefix 'lib\cmake\Qt5Gui\Qt5Gui_QWindowsIntegrationPlugin.cmake'
    if (-not (Test-Path -LiteralPath $qtConfig)) {
        throw "Qt 5.15.2 static CMake package is missing: $qtConfig"
    }
    if (-not (Test-Path -LiteralPath $qtPluginConfig)) {
        throw "Qt Windows static plugin package is missing: $qtPluginConfig"
    }
    $v142 = Find-V142Toolset
    if (-not $v142) {
        throw "MSVC v$ToolsetVersion is required for the static Win7 release."
    }
    $toolset = "v142,version=$v142"
}

$build = Join-Path $project ($(if ($StaticQt) { 'build/qt-5.15.2-static-release' } else { 'build/qt-release' }))
$staticValue = if ($StaticQt) { 'ON' } else { 'OFF' }
$configureArgs = @(
    '-S', $project, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'Win32',
    "-DCMAKE_PREFIX_PATH=$QtPrefix",
    "-DBANSHOU_QT_STATIC=$staticValue",
    "-DBANSHOU_SINGLE_EXE=$staticValue"
)
if ($toolset) {
    $configureArgs += @('-T', $toolset)
}
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
        '--target', 'native_tests', 'lifecycle_tests', 'qt_ui_tests', 'parity_tests')) -ne 0) {
    throw 'Test target build failed'
}
if ((Invoke-Native -FilePath $cmake -Arguments @('--build', $build, '--config', 'Release', '--target', 'RUN_TESTS')) -ne 0) {
    throw 'CTest failed'
}
if ($StaticQt) {
    $audit = Join-Path $project 'tools\check_single_exe.ps1'
    & $audit -ExePath $exe -DistributionDirectory $dist
    if ($LASTEXITCODE -ne 0) {
        throw 'Single EXE dependency audit failed.'
    }
}

$files = @()
$zipPath = $null
if ($StaticQt) {
    $files += [ordered]@{ path = '扳手.exe'; sha256 = $hash; size = $size }
} else {
    $portable = Join-Path $dist 'portable'
    if (Test-Path -LiteralPath $portable) {
        Remove-Item -LiteralPath $portable -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $portable | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $portable 'platforms') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $portable 'styles') | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $portable '扳手.exe') -Force
    foreach ($dll in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll')) {
        $source = Join-Path $QtPrefix "bin\$dll"
        if (!(Test-Path -LiteralPath $source)) { throw "Qt runtime DLL missing: $source" }
        Copy-Item -LiteralPath $source -Destination (Join-Path $portable $dll) -Force
    }
    $platform = Join-Path $QtPrefix 'plugins\platforms\qwindows.dll'
    if (!(Test-Path -LiteralPath $platform)) { throw "Qt platform plugin missing: $platform" }
    Copy-Item -LiteralPath $platform -Destination (Join-Path $portable 'platforms\qwindows.dll') -Force
    $style = Join-Path $QtPrefix 'plugins\styles\qwindowsvistastyle.dll'
    if (Test-Path -LiteralPath $style) {
        Copy-Item -LiteralPath $style -Destination (Join-Path $portable 'styles\qwindowsvistastyle.dll') -Force
    }
    foreach ($runtime in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll')) {
        $source = Join-Path $env:WINDIR "SysWOW64\$runtime"
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $portable $runtime) -Force
        }
    }
    $redist = 'D:\Qt\vcredist\vcredist_msvc2017_x86.exe'
    if (Test-Path -LiteralPath $redist) {
        Copy-Item -LiteralPath $redist -Destination (Join-Path $portable 'vc_redist.x86.exe') -Force
    }
    foreach ($license in @('LICENSE.LGPLv3', 'LICENSE.LGPLv21', 'LICENSE.GPLv3')) {
        $source = Join-Path $QtPrefix $license
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $portable $license) -Force
        }
    }
    $readme = @(
        '曹操传扳手 C++ 版（便携动态 Qt）',
        '',
        '模式：portable-dynamic-qt（非单文件静态版）',
        '运行：双击 扳手.exe。若提示缺少运行库，请先安装 vc_redist.x86.exe。',
        '依赖：Qt5Core.dll / Qt5Gui.dll / Qt5Widgets.dll、platforms\qwindows.dll。',
        '说明：本包为便携目录，移动时请保持目录结构完整。'
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $portable 'README-便携版.txt'), $readme,
        [Text.UTF8Encoding]::new($true))
    Get-ChildItem -LiteralPath $portable -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($portable.Length + 1)
        $files += [ordered]@{
            path = "portable\$relative"
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToUpperInvariant()
            size = $_.Length
        }
    }
    $zipPath = Join-Path $dist '扳手-C++版-portable-x86.zip'
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $portable '*') -DestinationPath $zipPath -CompressionLevel Optimal
}

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
