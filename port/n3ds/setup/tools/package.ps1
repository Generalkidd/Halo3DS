param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$GameBuild,
    [Parameter(Mandatory=$true)][string]$VCRuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$setupRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'source-files.ps1')
$repositoryRoot = Get-HaloSetupRepositoryRoot $setupRoot
$releasePath = [System.IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $releasePath) { throw 'Use a new, empty output path for each release.' }
foreach ($requiredFile in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $VCRuntimeDirectory $requiredFile))) { throw "Missing runtime: $requiredFile" }
}
dotnet publish (Join-Path $setupRoot 'App') -c Release -r win-x64 -p:Platform=x64 -o $releasePath
if ($LASTEXITCODE -ne 0) { throw 'Publish failed' }
Copy-Item -LiteralPath $GameBuild -Destination (Join-Path $releasePath 'Halo3DS.3dsx')
Get-ChildItem -LiteralPath $VCRuntimeDirectory -Filter '*.dll' | Copy-Item -Destination $releasePath
Copy-Item -LiteralPath (Join-Path $setupRoot 'THIRD-PARTY.md') -Destination $releasePath
Copy-Item -LiteralPath (Join-Path $setupRoot 'Licenses') -Destination $releasePath -Recurse
$sourcePath = Join-Path $releasePath 'Source'
New-Item -ItemType Directory -Path $sourcePath | Out-Null
Get-HaloSetupSourceFiles $setupRoot | ForEach-Object {
        $relative = [System.IO.Path]::GetRelativePath($repositoryRoot,$_.FullName)
        $target = Join-Path $sourcePath $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target
}
# Keep the instructions in one place, including inside the extracted release.
Set-Content -LiteralPath (Join-Path $releasePath 'README.md') -Encoding utf8 -Value '# Halo3DS Setup', '', 'See [the project README](Source/README.md) for setup and update instructions.'
Compress-Archive -LiteralPath $releasePath -DestinationPath ($releasePath + '.zip') -CompressionLevel Optimal
Write-Host 'Bundling a single executable...'
$payloadPath = $releasePath + '.payload.zip'
# The launcher expects application files at the ZIP root, not inside a wrapper folder.
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($releasePath, $payloadPath)
$singlePath = $releasePath + '-SingleFile'
dotnet publish (Join-Path $setupRoot 'Launcher') -c Release -r win-x64 -p:PayloadPath=$payloadPath -o $singlePath
if ($LASTEXITCODE -ne 0) { throw 'Single-file launcher publish failed' }
Write-Host "Single-file release: $(Join-Path $singlePath 'Halo3DS Setup.exe')"
