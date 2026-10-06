# One source-file list for both release packages. Never include local IDE settings,
# build output, game data or credentials from a developer's working directory.
function Get-HaloSetupRepositoryRoot([string]$SetupRoot) {
    [System.IO.Path]::GetFullPath((Join-Path $SetupRoot '../../..'))
}

function Get-HaloSetupSourceFiles([string]$SetupRoot) {
    foreach ($directory in @('App','Core','Tests','Resources','tools','Licenses','vendor','Launcher','Windows7')) {
        Get-ChildItem -LiteralPath (Join-Path $SetupRoot $directory) -Recurse -File | Where-Object {
            $_.FullName -notmatch '[\\/](bin|obj|build|publish|__pycache__|\.vs)[\\/]' -and
            $_.Name -notmatch '\.(user|suo|exe|dll|pdb|zip|7z|rar|3dsx|elf|map|xbe|iso|xiso|dmp|log|pfx|key)$'
        }
    }
    foreach ($name in @('THIRD-PARTY.md','Setup.local.props.example','Halo3DS.Setup.WinUI.sln','Halo3DS.Setup.Windows7.sln')) {
        Get-Item -LiteralPath (Join-Path $SetupRoot $name)
    }
    $repositoryRoot = Get-HaloSetupRepositoryRoot $SetupRoot
    foreach ($name in @('README.md','docs/DEVELOPMENT.md','LICENSE.md','port/n3ds/vendor/lz4/LICENSE','port/n3ds/vendor/libctru-allocator/LICENSE.txt')) {
        Get-Item -LiteralPath (Join-Path $repositoryRoot $name)
    }
}
