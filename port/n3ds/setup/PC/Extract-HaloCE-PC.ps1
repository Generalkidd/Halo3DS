# New3DS integration additions by Kevin Boggs.
<#
Extract owner-supplied Halo CE PC retail maps from ISO9660/Joliet, including
FilesCab cabinets. No disc is mounted, no setup executable is run, and archive
paths are never used as output paths. This script never accepts Xbox XISO.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$IsoPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string[]]$MapNames = @(),
    [string]$SevenZipPath
)

$ErrorActionPreference = 'Stop'
$iso = (Resolve-Path -LiteralPath $IsoPath).Path
if ([IO.Path]::GetExtension($iso) -ine '.iso' -or !(Test-Path -LiteralPath $iso -PathType Leaf)) {
    throw 'Select a Halo CE PC retail .iso file'
}
$target = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $target) { throw 'ISO staging folder must not already exist' }
$sevenZip = if ($SevenZipPath) { (Resolve-Path -LiteralPath $SevenZipPath).Path } else { Join-Path $PSScriptRoot 'tools\7z.exe' }
if (!(Test-Path -LiteralPath $sevenZip -PathType Leaf) -and $env:ProgramFiles) {
    $sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
}
if (!(Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if (!$command) { throw '7-Zip is needed to read the PC retail ISO. Pass -SevenZipPath or install 7-Zip.' }
    $sevenZip = $command.Source
}

# Kevin Boggs: Get-ArchiveMembers - New3DS integration.
function Get-ArchiveMembers([string]$archive) {
    $lines = @(& $sevenZip l -slt $archive 2>$null)
    if ($LASTEXITCODE -ne 0) { throw "Could not read disc archive: $archive" }
    $members = @()
    $started = $false
    foreach ($line in $lines) {
        if ($line -eq '----------') { $started = $true; continue }
        if ($started -and $line.StartsWith('Path = ')) {
            $members += $line.Substring(7)
        }
    }
    return $members
}

# Kevin Boggs: Copy-ArchiveMember - New3DS integration.
function Copy-ArchiveMember([string]$archive, [string]$member, [string]$destination) {
    if ($member -notmatch '^[a-zA-Z0-9 _./\\;+-]+$' -or
        @($member -split '[/\\]' | Where-Object { $_ -eq '..' }).Count) {
        throw 'ISO archive contains an unsafe map entry name'
    }
    $args = @('e', '-so', '-y', ('"' + $archive + '"'), ('"' + $member + '"'))
    $run = Start-Process -FilePath $sevenZip -ArgumentList $args -WindowStyle Hidden `
        -RedirectStandardOutput $destination -PassThru -Wait
    if ($run.ExitCode -ne 0 -or !(Test-Path -LiteralPath $destination -PathType Leaf) -or
        (Get-Item -LiteralPath $destination).Length -eq 0) {
        throw "Failed to read $member from the PC retail disc archive"
    }
}

$isoMembers = @(Get-ArchiveMembers $iso)
if (!$isoMembers.Count) {
    throw 'ISO has no standard PC retail files. An Xbox disc image is not a PC retail source.'
}
$catalog = @{}
$cabs = @()
foreach ($member in $isoMembers) {
    $normalized = $member -replace '\\', '/'
    if ($normalized -match '(?i)(?:^|/)([a-z0-9_-]{1,32})\.map(?:;[0-9]+)?$') {
        $name = $Matches[1].ToLowerInvariant()
        if (!$catalog.ContainsKey($name)) {
            $catalog[$name] = [pscustomobject]@{ Archive = $iso; Member = $member }
        }
    } elseif ($normalized -match '(?i)(?:^|/)(cab[0-9]{1,2})\.cab(?:;[0-9]+)?$') {
        $cabs += $member
    }
}
if ($cabs.Count -gt 32) { throw 'PC retail ISO has too many cabinet archives' }
New-Item -ItemType Directory -Path $target | Out-Null
$cabPaths = @()
try {
    if (!$catalog.ContainsKey('bitmaps')) {
        $cabIndex = 0
        foreach ($member in $cabs) {
            $cabPath = Join-Path $target ('source-cab-' + $cabIndex + '.cab')
            Copy-ArchiveMember $iso $member $cabPath
            $cabPaths += $cabPath
            foreach ($entry in @(Get-ArchiveMembers $cabPath)) {
                $normalized = $entry -replace '\\', '/'
                if ($normalized -match '(?i)(?:^|/)([a-z0-9_-]{1,32})\.map(?:;[0-9]+)?$') {
                    $name = $Matches[1].ToLowerInvariant()
                    if (!$catalog.ContainsKey($name)) {
                        $catalog[$name] = [pscustomobject]@{ Archive = $cabPath; Member = $entry }
                    }
                }
            }
            $cabIndex++
        }
    }
    if (!$catalog.ContainsKey('bitmaps')) {
        throw 'ISO has no PC retail bitmaps.map. An Xbox disc image is not a PC retail source.'
    }
    if (!$MapNames.Count) {
        $MapNames = @($catalog.Keys | Where-Object { $_ -notin @('bitmaps', 'sounds', 'loc', 'ui') } | Sort-Object)
    }
    $selected = @($MapNames | ForEach-Object { $_.Trim().ToLowerInvariant() } | Select-Object -Unique)
    if (!$selected.Count) { throw 'The PC retail ISO has no game maps' }
    $resources = @('ui', 'bitmaps', 'sounds')
    if ($catalog.ContainsKey('loc')) { $resources += 'loc' }
    foreach ($name in @($selected) + $resources) {
        if ($name -notmatch '^[a-z0-9_-]{1,32}$' -or !$catalog.ContainsKey($name)) {
            throw "PC retail ISO does not contain $name.map"
        }
    }
    foreach ($name in @($selected) + $resources) {
        $entry = $catalog[$name]
        Copy-ArchiveMember $entry.Archive $entry.Member (Join-Path $target ($name + '.map'))
    }
    Write-Output $target
} finally {
    foreach ($cabPath in $cabPaths) {
        if (Test-Path -LiteralPath $cabPath) { [IO.File]::Delete($cabPath) }
    }
}
