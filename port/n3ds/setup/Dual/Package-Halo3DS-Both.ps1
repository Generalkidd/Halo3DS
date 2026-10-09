# New3DS integration additions by Kevin Boggs.
<# Build a fixed-file, game-data-free dual-ISO installer archive. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$XboxInstallerPath,
    [Parameter(Mandatory = $true)][string]$XboxSourceZip,
    [Parameter(Mandatory = $true)][string]$PcLauncherPath,
    [Parameter(Mandatory = $true)][string]$RuntimeManifest,
    [Parameter(Mandatory = $true)][string]$OutputZip
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$setup = Split-Path $PSScriptRoot -Parent
$repo = [IO.Path]::GetFullPath((Join-Path $setup '../../..'))
$xbox = (Resolve-Path -LiteralPath $XboxInstallerPath).Path
$source = (Resolve-Path -LiteralPath $XboxSourceZip).Path
$pc = (Resolve-Path -LiteralPath $PcLauncherPath).Path
$manifestPath = (Resolve-Path -LiteralPath $RuntimeManifest).Path
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.Runtime -ne 'Halo3DS-PC' -or $manifest.MapFormat -ne 'HaloCE-PC-retail-v7' -or
    $manifest.LauncherSha256 -notmatch '^[a-fA-F0-9]{64}$' -or
    (Get-FileHash -LiteralPath $pc -Algorithm SHA256).Hash -ine $manifest.LauncherSha256) {
    throw 'The PC launcher and runtime manifest do not match.'
}
$stream = [IO.File]::OpenRead($pc)
try {
    $head = New-Object byte[] 4
    if ($stream.Read($head,0,4) -ne 4 -or [Text.Encoding]::ASCII.GetString($head) -ne '3DSX') {
        throw 'The PC launcher is not a 3DSX file.'
    }
} finally { $stream.Dispose() }
$stream = [IO.File]::OpenRead($xbox)
try {
    $head = New-Object byte[] 2
    if ($stream.Read($head,0,2) -ne 2 -or [Text.Encoding]::ASCII.GetString($head) -ne 'MZ') {
        throw 'The Xbox console installer is not a Windows executable.'
    }
} finally { $stream.Dispose() }
$sourceArchive = [IO.Compression.ZipFile]::OpenRead($source)
try {
    $names = @($sourceArchive.Entries | ForEach-Object FullName)
    foreach ($required in @('port/n3ds/setup/Windows7/InstallCli.cs','port/n3ds/setup/Licenses/GPL-3.0.txt','port/n3ds/setup/THIRD-PARTY.md')) {
        if ($required -notin $names) { throw "Xbox installer source archive is missing $required" }
    }
} finally { $sourceArchive.Dispose() }

$inputs = @(
    @{ Source=(Join-Path $PSScriptRoot 'Install-Halo3DS-Both.ps1'); Name='Install-Halo3DS-Both.ps1' },
    @{ Source=(Join-Path $setup 'PC/Install-Halo3DS-PC.ps1'); Name='Install-Halo3DS-PC.ps1' },
    @{ Source=(Join-Path $setup 'PC/Extract-HaloCE-PC.ps1'); Name='Extract-HaloCE-PC.ps1' },
    @{ Source=(Join-Path $PSScriptRoot 'README.md'); Name='README.md' },
    @{ Source=$xbox; Name='Halo3DS-Xbox-Install.exe' },
    @{ Source=$source; Name='Xbox-Setup-Source.zip' },
    @{ Source=$pc; Name='Halo3DS-PC.3dsx' },
    @{ Source=$manifestPath; Name='pc-runtime.json' },
    @{ Source=(Join-Path $repo 'LICENSE.md'); Name='LICENSE.md' },
    @{ Source=(Join-Path $setup 'THIRD-PARTY.md'); Name='THIRD-PARTY.md' },
    @{ Source=(Join-Path $setup 'Licenses/GPL-3.0.txt'); Name='GPL-3.0.txt' }
)
foreach ($entry in $inputs) { if (!(Test-Path -LiteralPath $entry.Source -PathType Leaf)) { throw "Missing package input: $($entry.Source)" } }
$output = [IO.Path]::GetFullPath($OutputZip)
if (Test-Path -LiteralPath $output) { throw 'Output ZIP already exists.' }
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($output)) -Force | Out-Null
$stage = Join-Path ([IO.Path]::GetTempPath()) ('Halo3DS-Both-package-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($entry in $inputs) { Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $stage $entry.Name) }
    Compress-Archive -LiteralPath @($inputs | ForEach-Object { Join-Path $stage $_.Name }) -DestinationPath $output -CompressionLevel Optimal
    $archive = [IO.Compression.ZipFile]::OpenRead($output)
    try {
        $expected = @($inputs | ForEach-Object Name | Sort-Object)
        $actual = @($archive.Entries | ForEach-Object FullName | Sort-Object)
        if ($actual.Count -ne $expected.Count -or (Compare-Object $expected $actual)) { throw 'Package contains unexpected files.' }
        foreach ($entry in $archive.Entries) {
            $inputFile = Join-Path $stage $entry.FullName
            $read = $entry.Open()
            try {
                $hash = [Security.Cryptography.SHA256]::Create()
                try { $actualHash = [BitConverter]::ToString($hash.ComputeHash($read)).Replace('-','') }
                finally { $hash.Dispose() }
            } finally { $read.Dispose() }
            if ($actualHash -ine (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash) {
                throw "Package verification failed: $($entry.FullName)"
            }
        }
    } finally { $archive.Dispose() }
    Write-Output "Game-data-free dual-ISO installer package: $output"
} finally {
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($stage)
    if (!$resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^Halo3DS-Both-package-[0-9a-f]{32}$') {
        throw 'Refusing to remove an unexpected staging folder.'
    }
    if (Test-Path -LiteralPath $resolved -PathType Container) { [IO.Directory]::Delete($resolved,$true) }
}
