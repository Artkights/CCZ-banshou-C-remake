$ErrorActionPreference = 'Stop'
$thirdParty = Join-Path $PSScriptRoot '../third_party'
New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
$archiveDir = Join-Path $thirdParty 'archives'
New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
$capstoneArchive = Join-Path $archiveDir 'capstone-4.0.2.tar.gz'
& curl.exe --fail --location --retry 3 'https://codeload.github.com/capstone-engine/capstone/tar.gz/refs/tags/4.0.2' --output $capstoneArchive
if ($LASTEXITCODE -ne 0) { throw 'Capstone download failed' }
& tar.exe -xf $capstoneArchive -C $thirdParty
if ($LASTEXITCODE -ne 0) { throw 'Capstone extraction failed' }
if (-not (Test-Path -LiteralPath (Join-Path $thirdParty 'capstone'))) {
    Move-Item -LiteralPath (Join-Path $thirdParty 'capstone-4.0.2') -Destination (Join-Path $thirdParty 'capstone')
}
foreach ($entry in @(
    @('json/json.hpp', 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp'),
    @('json/LICENSE.MIT', 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT'),
    @('csv/csv.hpp', 'https://raw.githubusercontent.com/vincentlaucsb/csv-parser/2.3.0/single_include/csv.hpp'),
    @('csv/LICENSE', 'https://raw.githubusercontent.com/vincentlaucsb/csv-parser/2.3.0/LICENSE')
)) {
    $outputPath = Join-Path $thirdParty $entry[0]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null
    & curl.exe --fail --location --retry 3 $entry[1] --output $outputPath
    if ($LASTEXITCODE -ne 0) { throw "Dependency download failed: $($entry[0])" }
}
Get-ChildItem -LiteralPath $thirdParty -Recurse -File | Where-Object { $_.Extension -in '.hpp','.gz' } | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path
