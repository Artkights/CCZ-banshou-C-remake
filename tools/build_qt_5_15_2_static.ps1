param(
    [string]$BuildRoot = 'C:\banshou-build'
)

$ErrorActionPreference = 'Stop'

# Qt 5.15.2 is the last Qt 5 LTS source release used by the native UI.
$qtUrl = 'https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtbase-everywhere-src-5.15.2.tar.xz'
$qtSha256 = '909FAD2591EE367993A75D7E2EA50AD4DB332F05E1C38DD7A5A274E156A4E0F8'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$thirdParty = Join-Path $root 'third_party'
$archiveDir = Join-Path $thirdParty 'archives'
$archive = Join-Path $archiveDir 'qtbase-5.15.2.tar.xz'
$qtBuildRoot = Join-Path $BuildRoot 'qt-5.15.2'
$source = Join-Path $qtBuildRoot 'qtbase-5.15.2'
$prefix = Join-Path $qtBuildRoot 'prefix'
$publishedPrefix = Join-Path $thirdParty 'qt-5.15.2-static-msvc142-x86'
$build = Join-Path $qtBuildRoot 'build'

if (-not (Test-Path $archive)) {
    New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri $qtUrl -OutFile $archive
}
$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToUpperInvariant()
if ($actual -ne $qtSha256) {
    throw "Qt archive SHA256 mismatch: expected $qtSha256, got $actual"
}

$vsRoots = @(
    'C:\Program Files\Microsoft Visual Studio\2022',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022',
    'C:\Program Files\Microsoft Visual Studio\2019',
    'C:\Program Files (x86)\Microsoft Visual Studio\2019',
    'D:\Program Files\Microsoft Visual Studio\2022',
    'D:\Program Files (x86)\Microsoft Visual Studio\2022',
    'D:\Program Files\Microsoft Visual Studio\2019',
    'D:\Program Files (x86)\Microsoft Visual Studio\2019'
)
$vcvars = @(
    foreach ($root in $vsRoots) {
        if (Test-Path -LiteralPath $root) {
            $resolvedRoot = (Resolve-Path -LiteralPath $root).Path
            Get-ChildItem $resolvedRoot -Filter vcvarsall.bat -Recurse -ErrorAction SilentlyContinue
        }
    }
) | Select-Object -First 1 -ExpandProperty FullName
if (-not $vcvars) { throw 'Visual Studio vcvarsall.bat was not found' }
$vsRoot = (Resolve-Path (Join-Path (Split-Path $vcvars -Parent) '..\..\..')).Path
$v142 = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like '14.29.*' } | Select-Object -First 1
if (-not $v142) {
    throw 'MSVC v142 14.29 is required for the Win7 release; install the v142 build tools before running this script.'
}

if (-not (Test-Path $source)) {
    New-Item -ItemType Directory -Force -Path $qtBuildRoot | Out-Null
    $localArchive = Join-Path $qtBuildRoot 'qtbase-5.15.2.tar.xz'
    Copy-Item -LiteralPath $archive -Destination $localArchive -Force
    & tar.exe -xf $localArchive -C $qtBuildRoot
    $extracted = Get-ChildItem $qtBuildRoot -Directory | Where-Object { $_.Name -like 'qtbase-everywhere-src-5.15.2*' } | Select-Object -First 1
    if (-not $extracted) { throw 'Qt source extraction failed' }
    Move-Item -LiteralPath $extracted.FullName -Destination $source
}

if (-not (Test-Path (Join-Path $publishedPrefix 'lib\cmake\Qt5\Qt5Config.cmake'))) {
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    $configure = Join-Path $source 'configure.bat'
    $jom = 'D:\Qt\Tools\QtCreator\bin\jom.exe'
    $makeCommand = if (Test-Path -LiteralPath $jom) { '"' + $jom + '" -j2' } else { 'nmake' }
    $configureArgs = @(
        '-release', '-static', '-static-runtime', '-platform', 'win32-msvc',
        '-x86', '-opensource', '-confirm-license', '-prefix', $prefix,
        '-nomake', 'examples', '-nomake', 'tests', '-no-openssl',
        '-no-network', '-no-sql-sqlite', '-no-sql-odbc', '-no-sql-psql',
        '-no-sql-mysql', '-no-sql-oci', '-no-sql-tds', '-no-qml',
        '-no-quick', '-no-wmf', '-opengl', 'desktop', '-qt-png'
    )
    $argLine = ($configureArgs | ForEach-Object {
        '"' + $_.Replace('"', '""') + '"'
    }) -join ' '
    Push-Location $source
    try {
        & cmd.exe /d /s /c "call `"$vcvars`" x86 -vcvars_ver=14.29 && `"$configure`" $argLine"
        if ($LASTEXITCODE -ne 0) { throw 'Qt configure failed' }
        & cmd.exe /d /s /c "call `"$vcvars`" x86 -vcvars_ver=14.29 && $makeCommand"
        if ($LASTEXITCODE -ne 0) { throw 'Qt build failed' }
        & cmd.exe /d /s /c "call `"$vcvars`" x86 -vcvars_ver=14.29 && nmake install"
        if ($LASTEXITCODE -ne 0) { throw 'Qt install failed' }
    } finally {
        Pop-Location
    }
    $installedConfig = Join-Path $prefix 'lib\cmake\Qt5\Qt5Config.cmake'
    if (-not (Test-Path -LiteralPath $installedConfig)) {
        throw "Qt install completed without a CMake package: $installedConfig"
    }
    New-Item -ItemType Directory -Force -Path $publishedPrefix | Out-Null
    Copy-Item -Path (Join-Path $prefix '*') -Destination $publishedPrefix -Recurse -Force
}

if (-not (Test-Path (Join-Path $publishedPrefix 'lib\cmake\Qt5\Qt5Config.cmake'))) {
    throw "Published static Qt prefix is incomplete: $publishedPrefix"
}
Write-Host "Static Qt prefix: $publishedPrefix"
