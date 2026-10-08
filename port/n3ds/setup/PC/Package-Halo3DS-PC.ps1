<# Package only the verified PC launcher, manifest, and installer sources. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$LauncherPath,
    [Parameter(Mandatory = $true)][string]$RuntimeManifest,
    [Parameter(Mandatory = $true)][string]$OutputZip
)
$ErrorActionPreference = 'Stop'
$launcher = (Resolve-Path -LiteralPath $LauncherPath).Path
$manifestPath = (Resolve-Path -LiteralPath $RuntimeManifest).Path
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.MapFormat -ne 'HaloCE-PC-retail-v7' -or $manifest.Runtime -ne 'Halo3DS-PC' -or
    $manifest.LauncherSha256 -notmatch '^[a-fA-F0-9]{64}$' -or
    (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash -ine $manifest.LauncherSha256) {
    throw 'A matching PC retail runtime and manifest are required. The Xbox-only launcher is not accepted.'
}
$stream = [IO.File]::OpenRead($launcher)
try {
    $head = New-Object byte[] 4
    if ($stream.Read($head, 0, 4) -ne 4 -or [Text.Encoding]::ASCII.GetString($head) -ne '3DSX') {
        throw 'PC launcher is not a 3DSX.'
    }
} finally { $stream.Dispose() }
$output = [IO.Path]::GetFullPath($OutputZip)
if (Test-Path -LiteralPath $output) { throw 'Output ZIP already exists.' }
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($output)) -Force | Out-Null
$stage = Join-Path ([IO.Path]::GetTempPath()) ('Halo3DS-PC-package-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    $inputFiles = @(
        @{ Source = (Join-Path $PSScriptRoot 'Install-Halo3DS-PC.ps1'); Name = 'Install-Halo3DS-PC.ps1' },
        @{ Source = (Join-Path $PSScriptRoot 'Extract-HaloCE-PC.ps1'); Name = 'Extract-HaloCE-PC.ps1' },
        @{ Source = (Join-Path $PSScriptRoot 'README.md'); Name = 'README.md' },
        @{ Source = $launcher; Name = 'Halo3DS-PC.3dsx' },
        @{ Source = $manifestPath; Name = 'pc-runtime.json' }
    )
    foreach ($entry in $inputFiles) { Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $stage $entry.Name) }
    Compress-Archive -LiteralPath @($inputFiles | ForEach-Object { Join-Path $stage $_.Name }) -DestinationPath $output -CompressionLevel Optimal
    $archive = [IO.Compression.ZipFile]::OpenRead($output)
    try {
        $expected = @($inputFiles | ForEach-Object Name | Sort-Object)
        $actual = @($archive.Entries | ForEach-Object FullName | Sort-Object)
        if ($actual.Count -ne $expected.Count -or (Compare-Object $expected $actual)) {
            throw 'Package contains unexpected files.'
        }
    } finally { $archive.Dispose() }
    Write-Output "Asset-free PC installer package: $output"
} finally {
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($stage)
    if (!$resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^Halo3DS-PC-package-[0-9a-f]{32}$') {
        throw 'Refusing to remove an unexpected package staging folder.'
    }
    if (Test-Path -LiteralPath $resolved -PathType Container) { [IO.Directory]::Delete($resolved, $true) }
}
