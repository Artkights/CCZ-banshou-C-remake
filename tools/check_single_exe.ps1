param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [Parameter(Mandatory = $true)]
    [string]$DistributionDirectory
)

$ErrorActionPreference = 'Stop'

function Read-UInt16At([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-UInt32At([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "EXE does not exist: $ExePath"
}

$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ExePath).Path)
if ($bytes.Length -lt 256) {
    throw 'EXE is too small to be a valid PE image.'
}
$peOffset = [int](Read-UInt32At $bytes 0x3c)
if ($peOffset -lt 0 -or $peOffset + 0x60 -ge $bytes.Length) {
    throw 'PE header is outside the file.'
}
$signature = [Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4)
if ($signature -ne ('PE' + [char]0 + [char]0)) {
    throw 'Invalid PE signature.'
}
$machine = Read-UInt16At $bytes ($peOffset + 4)
$optionalSize = Read-UInt16At $bytes ($peOffset + 20)
$optionalOffset = $peOffset + 24
$optionalMagic = Read-UInt16At $bytes $optionalOffset
$subsystem = Read-UInt16At $bytes ($optionalOffset + 68)
$majorOs = Read-UInt16At $bytes ($optionalOffset + 40)
$minorOs = Read-UInt16At $bytes ($optionalOffset + 42)

if ($machine -ne 0x014c) {
    throw ('Target is not x86 (IMAGE_FILE_MACHINE_I386): 0x{0:X4}' -f $machine)
}
if ($optionalMagic -ne 0x010b) {
    throw ('Target is not PE32: optional header magic 0x{0:X4}' -f $optionalMagic)
}
if ($subsystem -ne 2) {
    throw ('Target is not a Windows GUI application: subsystem 0x{0:X4}' -f $subsystem)
}
if (($majorOs -gt 6) -or ($majorOs -eq 6 -and $minorOs -gt 1)) {
    throw ('Target OS version {0}.{1} is newer than Windows 7.' -f $majorOs, $minorOs)
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
$objdump = Get-Command llvm-objdump.exe -ErrorAction SilentlyContinue
if (-not $objdump) { $objdump = Get-Command objdump.exe -ErrorAction SilentlyContinue }
if (-not $dumpbin) {
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
    $found = @()
    foreach ($root in $vsRoots) {
        if (-not [IO.Directory]::Exists($root)) { continue }
        # VS installs nest the edition (Community/Professional/...) below the
        # year directory, so both levels have to be probed.  .NET APIs are used
        # because restricted shells can filter Get-ChildItem.
        $installRoots = @($root)
        $installRoots += [IO.Directory]::GetDirectories($root)
        foreach ($installRoot in $installRoots) {
            $toolsDir = Join-Path $installRoot 'VC\Tools\MSVC'
            if (-not [IO.Directory]::Exists($toolsDir)) { continue }
            $versions = @([IO.Directory]::GetDirectories($toolsDir)) |
                Sort-Object -Descending
            foreach ($version in $versions) {
                foreach ($hostDir in @('Hostx64\x86', 'Hostx64\x64', 'Hostx86\x86')) {
                    $candidate = Join-Path $version ('bin\' + $hostDir + '\dumpbin.exe')
                    if (Test-Path -LiteralPath $candidate) { $found += $candidate }
                }
            }
        }
        if ($found.Count -gt 0) { break }
    }
    if ($found.Count -gt 0) { $dumpbin = Get-Command $found[0] }
}
$imports = @()
if ($dumpbin) {
    # dumpbin /dependents prints one bare DLL name per line; /imports uses a
    # different layout, so dependents is the stable format to parse here.
    $lines = & $dumpbin.Source /nologo /dependents $ExePath 2>$null
    $imports = @($lines | ForEach-Object {
        if ($_ -match '^\s*([A-Za-z0-9_.\-]+\.dll)\s*$') { $Matches[1] }
    })
}
if ($imports.Count -eq 0 -and $objdump) {
    $lines = & $objdump.Source -p $ExePath 2>$null
    $imports = @($lines | ForEach-Object {
        if ($_ -match '^\s*DLL Name:\s*(.+\.dll)\s*$') { $Matches[1].Trim() }
    })
}
if ($imports.Count -eq 0) {
    throw 'Neither dumpbin.exe nor objdump.exe could report imports for the release binary.'
}

$imports = @($imports | ForEach-Object { $_.ToUpperInvariant() } | Sort-Object -Unique)
if ($imports.Count -eq 0) {
    throw 'No PE imports were found; import inspection could not be completed.'
}

$allowed = @(
    'ADVAPI32.DLL', 'BCRYPT.DLL', 'CFGMGR32.DLL', 'COMBASE.DLL', 'COMCTL32.DLL', 'COMDLG32.DLL',
    'CRYPT32.DLL', 'D2D1.DLL', 'D3D9.DLL', 'D3D11.DLL', 'D3DCOMPILER_47.DLL', 'DBGHELP.DLL',
    'DWMAPI.DLL', 'DWRITE.DLL', 'DXGI.DLL', 'DXVA2.DLL',
    'GDI32.DLL', 'GLU32.DLL', 'IMM32.DLL', 'IPHLPAPI.DLL', 'MSCTF.DLL',
    'KERNEL32.DLL', 'KERNELBASE.DLL', 'MPR.DLL', 'MSIMG32.DLL',
    'NETAPI32.DLL', 'OLE32.DLL', 'OLEAUT32.DLL', 'OPENGL32.DLL', 'POWRPROF.DLL',
    'PROPSYS.DLL', 'PSAPI.DLL', 'RPCRT4.DLL',
    'SECUR32.DLL', 'SETUPAPI.DLL', 'SHCORE.DLL', 'SHELL32.DLL', 'SHLWAPI.DLL',
    'USER32.DLL', 'USERENV.DLL', 'UXTHEME.DLL', 'VERSION.DLL', 'WINMM.DLL',
    'WINSPOOL.DRV', 'WINSTA.DLL', 'WS2_32.DLL',
    'WTSAPI32.DLL', 'NTDLL.DLL'
)

$forbidden = @(
    'QT5CORE.DLL', 'QT5GUI.DLL', 'QT5WIDGETS.DLL', 'QWINDOWS.DLL',
    'VCRUNTIME140.DLL', 'VCRUNTIME140_1.DLL', 'MSVCP140.DLL', 'CONCRT140.DLL',
    'CAPSTONE.DLL', 'UCRTBASE.DLL', 'API-MS-WIN-CRT-'
)
$badImports = @($imports | Where-Object {
    $name = $_
    ($forbidden | Where-Object { $name -like "$($_)*" }).Count -gt 0 -or
    (($allowed -notcontains $name) -and ($name -notlike 'API-MS-WIN-CORE-*') -and ($name -notlike 'EXT-MS-WIN-*'))
})
if ($badImports.Count -gt 0) {
    throw ('Non-system or forbidden imports found: ' + ($badImports -join ', '))
}

$dlls = @(Get-ChildItem -LiteralPath $DistributionDirectory -Filter '*.dll' -File -ErrorAction SilentlyContinue)
if ($dlls.Count -gt 0) {
    throw ('The single-file distribution contains DLLs: ' + (($dlls | Select-Object -ExpandProperty Name) -join ', '))
}

Write-Host ('PE: x86 GUI, minimum OS {0}.{1}' -f $majorOs, $minorOs)
Write-Host ('Imports: ' + ($imports -join ', '))
Write-Host 'Single EXE dependency audit passed.'
