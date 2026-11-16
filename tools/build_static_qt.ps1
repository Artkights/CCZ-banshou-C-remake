# 构建 Qt 5.14.2 静态库（x86 + 静态 CRT），供 BANSHOU_SINGLE_EXE 单文件发布使用。
#
# 背景：本机只安装了动态版 Qt，而 single-exe-static 需要静态 Qt。Qt 官方安装器
# 不提供 Windows 静态预编译包，因此必须自行编译。本脚本把这一步固化为可复现流程，
# 输出目录包含 Qt5Config.cmake 与静态平台插件包，可直接被 CMake 消费。
#
# 用法：
#   .\tools\build_static_qt.ps1 -VerifyOnly      # 只检查工具链与源码是否齐备
#   .\tools\build_static_qt.ps1                  # 完整配置 + 编译 + 安装
#   .\tools\build_static_qt.ps1 -Prefix D:\Qt\5.14.2\static
#
# 编译完成后用发布脚本切换为单文件模式：
#   .\tools\build_release.ps1 -StaticQt -QtPrefix <Prefix>

param(
    [string]$QtSource = 'D:\Qt\5.14.2\Src\qtbase',
    [string]$Prefix = 'F:\qt5-static',
    [string]$BuildDirectory = 'F:\qt5-static-build',
    [string]$ToolsetVersion = '14.29',
    [string]$SdkVersion = '',
    [int]$Jobs = 8,
    [string[]]$ExtraConfigureArgs = @(),
    [switch]$Clean,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

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

# 受限环境下 Get-ChildItem 可能被过滤，统一使用 .NET 目录 API。
function Resolve-MsvcToolset {
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
        if ($match) {
            return (Join-Path $tools $match)
        }
    }
    return $null
}

function Resolve-WindowsSdk {
    $roots = @(
        'C:\Program Files (x86)\Windows Kits\10',
        'C:\Program Files\Windows Kits\10',
        'D:\Windows Kits\10'
    )
    foreach ($root in $roots) {
        if (-not [IO.Directory]::Exists($root)) { continue }
        $include = Join-Path $root 'Include'
        if (-not [IO.Directory]::Exists($include)) { continue }
        $versions = @([IO.Directory]::GetDirectories($include) |
            ForEach-Object { Split-Path -Leaf $_ } |
            Where-Object { $_ -match '^10\.' } |
            Where-Object { [string]::IsNullOrWhiteSpace($SdkVersion) -or $_ -eq $SdkVersion } |
            Sort-Object -Descending)
        foreach ($version in $versions) {
            $ucrt = Join-Path $include (Join-Path $version 'ucrt')
            $umLib = Join-Path $root "Lib\$version\um\x86"
            if ([IO.Directory]::Exists($ucrt) -and [IO.Directory]::Exists($umLib)) {
                return [pscustomobject]@{ Root = $root; Version = $version }
            }
        }
    }
    return $null
}

function Resolve-Perl {
    $command = Get-Command perl.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidates = @(
        'D:\Git\usr\bin\perl.exe',
        'C:\Program Files\Git\usr\bin\perl.exe',
        'C:\Strawberry\perl\bin\perl.exe',
        'D:\Strawberry\perl\bin\perl.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Resolve-Jom {
    $command = Get-Command jom.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidates = @(
        'D:\Qt\Tools\QtCreator\bin\jom.exe',
        'C:\Qt\Tools\QtCreator\bin\jom.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

Write-Step '解析工具链'
$msvc = Resolve-MsvcToolset
if (-not $msvc) { throw "未找到 MSVC v$ToolsetVersion 工具集。" }
$sdk = Resolve-WindowsSdk
if (-not $sdk) { throw '未找到可用的 Windows 10 SDK（需 ucrt 与 um/x86 库）。' }
$perl = Resolve-Perl
if (-not $perl) { throw 'Qt 构建需要 perl.exe，但未找到。' }
$jom = Resolve-Jom
if (-not $jom) { throw '未找到 jom.exe（Qt 并行构建工具）。' }
$configure = Join-Path $QtSource 'configure.bat'
if (-not (Test-Path -LiteralPath $configure)) {
    throw "Qt 源码不完整，缺少 configure.bat：$QtSource"
}

Write-Host "MSVC : $msvc"
Write-Host "SDK  : $($sdk.Root) $($sdk.Version)"
Write-Host "Perl : $perl"
Write-Host "jom  : $jom"
Write-Host "Src  : $QtSource"
Write-Host "输出 : $Prefix"
Write-Host "构建 : $BuildDirectory"

foreach ($expected in @(
        (Join-Path $Prefix 'lib\cmake\Qt5\Qt5Config.cmake'),
        (Join-Path $Prefix 'lib\cmake\Qt5Gui\Qt5Gui_QWindowsIntegrationPlugin.cmake'),
        (Join-Path $Prefix 'lib\cmake\Qt5Gui\Qt5Gui_QOffscreenIntegrationPlugin.cmake'))) {
    $state = if (Test-Path -LiteralPath $expected) { '已存在' } else { '待生成' }
    Write-Host ("产物 {0} -> {1}" -f $state, $expected)
}

if ($VerifyOnly) {
    Write-Host ''
    Write-Host 'VerifyOnly: 工具链与源码检查通过。'
    exit 0
}

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    Write-Step '清理旧构建目录'
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

# 手工装配 MSVC 环境，避免依赖 VsDevCmd 使用的 reg.exe（受限环境会被拦截）。
$env:INCLUDE = (@(
        (Join-Path $msvc 'include'),
        (Join-Path $sdk.Root "Include\$($sdk.Version)\ucrt"),
        (Join-Path $sdk.Root "Include\$($sdk.Version)\shared"),
        (Join-Path $sdk.Root "Include\$($sdk.Version)\um"),
        (Join-Path $sdk.Root "Include\$($sdk.Version)\winrt"),
        (Join-Path $sdk.Root "Include\$($sdk.Version)\cppwinrt")
    ) -join ';')
$env:LIB = (@(
        (Join-Path $msvc 'lib\x86'),
        (Join-Path $sdk.Root "Lib\$($sdk.Version)\ucrt\x86"),
        (Join-Path $sdk.Root "Lib\$($sdk.Version)\um\x86")
    ) -join ';')
$env:PATH = (@(
        (Join-Path $msvc 'bin\Hostx86\x86'),
        (Join-Path $sdk.Root "bin\$($sdk.Version)\x86"),
        (Split-Path -Parent $perl)
    ) -join ';') + ';' + $env:PATH
# 大小写重复的代理变量会让 MSBuild 的任务宿主抛异常。
foreach ($name in @('HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY')) {
    Remove-Item -Path "Env:$name" -ErrorAction SilentlyContinue
    $lower = $name.ToLowerInvariant()
    Remove-Item -Path "Env:$lower" -ErrorAction SilentlyContinue
}

$configureArgs = @(
    '-static', '-release', '-static-runtime',
    '-opensource', '-confirm-license',
    '-platform', 'win32-msvc',
    '-nomake', 'examples', '-nomake', 'tests',
    '-no-opengl', '-no-icu',
    '-qt-zlib', '-qt-pcre', '-qt-libpng', '-qt-libjpeg',
    '-prefix', $Prefix
)
$configureArgs += $ExtraConfigureArgs

Write-Step 'configure'
Push-Location $BuildDirectory
try {
    if ((Invoke-Native -FilePath $configure -Arguments $configureArgs) -ne 0) {
        throw 'Qt configure 失败。'
    }
    Write-Step '编译（静态 CRT，x86）'
    if ((Invoke-Native -FilePath $jom -Arguments @("/J", "$Jobs")) -ne 0) {
        throw 'Qt 编译失败。'
    }
    Write-Step '安装'
    if ((Invoke-Native -FilePath $jom -Arguments @('install')) -ne 0) {
        throw 'Qt 安装失败。'
    }
} finally {
    Pop-Location
}

Write-Step '校验输出'
$missing = @()
foreach ($expected in @(
        (Join-Path $Prefix 'lib\cmake\Qt5\Qt5Config.cmake'),
        (Join-Path $Prefix 'lib\cmake\Qt5Gui\Qt5Gui_QWindowsIntegrationPlugin.cmake'),
        (Join-Path $Prefix 'lib\cmake\Qt5Gui\Qt5Gui_QOffscreenIntegrationPlugin.cmake'))) {
    if (-not (Test-Path -LiteralPath $expected)) { $missing += $expected }
}
if ($missing.Count -gt 0) {
    throw ('静态 Qt 安装不完整，缺少：' + ($missing -join ', '))
}
Write-Host '静态 Qt 构建完成。'
Write-Host ("下一步：.\tools\build_release.ps1 -StaticQt -QtPrefix {0}" -f $Prefix)
