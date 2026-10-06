param(
    [Parameter(Mandatory=$true)][string]$GameBuild,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Python = 'python',
    [switch]$CoreOnly,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$setupRoot=Split-Path $here -Parent
. "$setupRoot/tools/source-files.ps1"
$repositoryRoot=Get-HaloSetupRepositoryRoot $setupRoot
$GameBuild=(Resolve-Path -LiteralPath $GameBuild).Path
$Output=[IO.Path]::GetFullPath($Output)
if($Output.StartsWith([IO.Path]::GetFullPath("$here/..").TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Choose a build output folder outside the setup source tree'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
& $Python "$here/adapt-core.py"
if($LASTEXITCODE -ne 0){throw 'Core adaptation failed'}
$sdk=(& dotnet --list-sdks | Select-Object -Last 1)
if($sdk -notmatch '^([^ ]+) \[(.+)\]$'){throw '.NET SDK with Roslyn compiler required for building'}
$compiler=Join-Path (Join-Path $Matches[2] $Matches[1]) 'Roslyn/bincore/csc.dll'
$fx="$env:WINDIR/Microsoft.NET/Framework/v2.0.50727"
$refs="${env:ProgramFiles(x86)}/Reference Assemblies/Microsoft/Framework/v3.5"
$references=@("$fx/mscorlib.dll","$fx/System.dll","$fx/System.Drawing.dll","$fx/System.Windows.Forms.dll","$refs/System.Core.dll","$refs/System.Web.Extensions.dll")
foreach($r in $references){if(!(Test-Path -LiteralPath $r)){throw "Install .NET Framework 3.5 build references: missing $r"}}
$common=@('/nologo','/codepage:65001','/noconfig','/nostdlib+','/langversion:latest','/nullable:enable','/nowarn:8632,8600,8601,8602,8603,8604,8618,8625,9113','/platform:x86','/subsystemversion:6.00')
if($Configuration -eq 'Debug'){$common+=@('/optimize-','/debug:full')}else{$common+='/optimize+'}
$common+= $references | ForEach-Object {"/reference:$_"}
$common+=@("/resource:$here/../Resources/catalog.json,HaloSetup.catalog.json","/resource:$here/../Resources/schemas.json,HaloSetup.schemas.json")
$core=@((Get-ChildItem "$here/Core/*.cs").FullName)+"$here/Compatibility.cs"
if($CoreOnly){
    & dotnet $compiler @common /target:library "/out:$Output/HaloSetup.Legacy.dll" @core
}else{
    # Sources and third-party notices travel inside the EXE. No user game data.
    $sourceZip=Join-Path $Output 'Source.zip'
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream=[IO.File]::Open($sourceZip,[IO.FileMode]::Create)
    $archive=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Create)
    try {
        Get-HaloSetupSourceFiles $setupRoot | ForEach-Object {
            $relative=$_.FullName.Substring($repositoryRoot.Length+1).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$_.FullName,$relative) | Out-Null
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    & dotnet $compiler @common /target:winexe "/out:$Output/Halo3DS Setup Windows 7.exe" "/win32manifest:$here/app.manifest" "/resource:$GameBuild,HaloSetup.Halo3DS.3dsx" "/resource:$sourceZip,HaloSetup.Source.zip" @core "$here/App.cs" "$here/AssemblyInfo.cs"
    if($LASTEXITCODE -ne 0){throw 'Windows 7 app compilation failed'}
    & dotnet $compiler @common /target:exe "/out:$Output/LegacyTests.exe" @core "$here/Tests.cs"
}
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
