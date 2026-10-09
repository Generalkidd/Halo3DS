<#
Install a PC retail build from the owner's Halo CE PC ISO. The package contains
only this installer, the PC-capable 3DSX, and its build manifest. Game content
is read from the ISO at install time and is never bundled with the package.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$PcIso,
    [string]$PcMapsFolder,
    [Parameter(Mandatory = $true)][string]$SdRoot,
    [string[]]$MapNames = @(),
    [Nullable[int]]$BotCount = $null,
    [string]$SevenZipPath,
    [string]$LauncherPath,
    [string]$RuntimeManifest
)

$ErrorActionPreference = 'Stop'
if ([bool]$PcIso -eq [bool]$PcMapsFolder) {
    throw 'Choose exactly one PC source: -PcIso or -PcMapsFolder.'
}
if (!$LauncherPath) { $LauncherPath = Join-Path $PSScriptRoot 'Halo3DS-PC.3dsx' }
if (!$RuntimeManifest) { $RuntimeManifest = Join-Path $PSScriptRoot 'pc-runtime.json' }
# PowerShell forwards WhatIfPreference into provider-backed reads such as
# Resolve-Path/Get-FileHash. Validate the inputs and stage ISO extraction
# normally, then return before destination writes for a dry run.
$dryRun = [bool]$WhatIfPreference
$WhatIfPreference = $false
if ($null -ne $BotCount -and ($BotCount -lt 0 -or $BotCount -gt 15)) {
    throw 'Bot count must be from 0 to 15 (one player slot is reserved for you).'
}
$iso = $null
$sourceFolder = $null
if ($PcIso) {
    $iso = (Resolve-Path -LiteralPath $PcIso).Path
    if ([IO.Path]::GetExtension($iso) -ine '.iso' -or !(Test-Path -LiteralPath $iso -PathType Leaf)) {
        throw 'Select your Halo CE PC retail .iso file. Xbox XISO images are not accepted.'
    }
} else {
    $sourceFolder = (Resolve-Path -LiteralPath $PcMapsFolder).Path
    if (!(Test-Path -LiteralPath $sourceFolder -PathType Container)) {
        throw 'Select an existing folder containing extracted Halo CE PC retail .map files.'
    }
}
$sd = (Resolve-Path -LiteralPath $SdRoot).Path
if (!(Test-Path -LiteralPath $sd -PathType Container)) { throw 'Select an existing SD card root.' }
if ([IO.Path]::GetFileName($sd.TrimEnd('\', '/')) -in @('3ds', 'halo-source', 'pc-retail')) {
    throw 'Select the SD card root, not one of its subfolders.'
}
$sdPrefix = $sd.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (($iso -and $iso.StartsWith($sdPrefix, [StringComparison]::OrdinalIgnoreCase)) -or
    ($sourceFolder -and ($sourceFolder.Equals($sd,[StringComparison]::OrdinalIgnoreCase) -or
        $sourceFolder.StartsWith($sdPrefix,[StringComparison]::OrdinalIgnoreCase)))) {
    throw 'Keep the PC source outside the SD card destination.'
}
$launcher = (Resolve-Path -LiteralPath $LauncherPath).Path
$manifestFile = (Resolve-Path -LiteralPath $RuntimeManifest).Path
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
if ($manifest.MapFormat -ne 'HaloCE-PC-retail-v7' -or $manifest.Runtime -ne 'Halo3DS-PC' -or
    $manifest.LauncherSha256 -notmatch '^[a-fA-F0-9]{64}$') {
    throw 'A PC retail runtime manifest is required. The Xbox build cannot be installed with PC maps.'
}
if ((Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash -ine $manifest.LauncherSha256) {
    throw 'The PC launcher does not match its runtime manifest.'
}
$launcherFile = [IO.File]::OpenRead($launcher)
try {
    $header = New-Object byte[] 4
    if ($launcherFile.Read($header, 0, 4) -ne 4 -or [Text.Encoding]::ASCII.GetString($header) -ne '3DSX') {
        throw 'The selected PC launcher is not a 3DSX file.'
    }
} finally { $launcherFile.Dispose() }

$relativePaths = @('3ds/Halo3DS-PC/Halo3DS-PC.3dsx')
$launcherRelative = $relativePaths[0]

function Test-PcGameMap([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 2048) { return $false }
        $header = New-Object byte[] 2048
        if ($stream.Read($header, 0, 2048) -ne 2048) { return $false }
        $version = [BitConverter]::ToUInt32($header, 4)
        $declared = [BitConverter]::ToUInt32($header, 8)
        $tagOffset = [BitConverter]::ToUInt32($header, 16)
        $tagSize = [BitConverter]::ToUInt32($header, 20)
        return [Text.Encoding]::ASCII.GetString($header, 0, 4) -eq 'daeh' -and
            [Text.Encoding]::ASCII.GetString($header, 2044, 4) -eq 'toof' -and
            $version -eq 7 -and $declared -eq $stream.Length -and
            $tagOffset -ge 2048 -and $tagSize -ge 40 -and
            ([long]$tagOffset + [long]$tagSize) -le $stream.Length
    } finally { $stream.Dispose() }
}

function Test-Resource([string]$path, [uint32]$version) {
    $stream = [IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 4096) { return $false }
        $header = New-Object byte[] 4
        return $stream.Read($header, 0, 4) -eq 4 -and [BitConverter]::ToUInt32($header, 0) -eq $version
    } finally { $stream.Dispose() }
}

function Test-Within([string]$root, [string]$path) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $root $path))
    $prefix = [IO.Path]::GetFullPath($root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (!$absolute.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe destination path.' }
    $parent = [IO.Path]::GetDirectoryName($absolute)
    while ($parent -and $parent.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $parent) {
            if ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Destination contains a redirected folder: $parent"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
    return $absolute
}

$stage = Join-Path ([IO.Path]::GetTempPath()) ('Halo3DS-PC-' + [guid]::NewGuid().ToString('N'))
try {
    if ($iso) {
        $extractArgs = @{ IsoPath = $iso; OutputDirectory = $stage; MapNames = $MapNames }
        if ($SevenZipPath) { $extractArgs.SevenZipPath = $SevenZipPath }
        & (Join-Path $PSScriptRoot 'Extract-HaloCE-PC.ps1') @extractArgs | Out-Null
        $maps = @(Get-ChildItem -LiteralPath $stage -Filter '*.map' -File | Sort-Object Name)
    } else {
        $all = @(Get-ChildItem -LiteralPath $sourceFolder -Filter '*.map' -File)
        $requested = @($MapNames | ForEach-Object { $_.Trim().ToLowerInvariant() } | Select-Object -Unique)
        foreach ($name in $requested) {
            if ($name -notmatch '^[a-z0-9_-]{1,32}$' -or
                !@($all | Where-Object { $_.BaseName -ieq $name }).Count) {
                throw "PC retail maps folder does not contain $name.map"
            }
        }
        $resources = @('ui','bitmaps','sounds')
        if (@($all | Where-Object { $_.BaseName -ieq 'loc' }).Count) { $resources += 'loc' }
        $maps = @($all | Where-Object {
            $_.BaseName -in $resources -or !$requested.Count -or $_.BaseName -in $requested
        } | Sort-Object Name)
    }
    foreach ($map in $maps) {
        if ($map.BaseName -notmatch '^[a-z0-9_-]{1,32}$') { throw "Unsafe PC map filename: $($map.Name)" }
    }
    $names = @($maps | ForEach-Object BaseName)
    if ('ui' -notin $names -or 'bitmaps' -notin $names -or 'sounds' -notin $names -or
        @($names | Where-Object { $_ -notin @('ui', 'bitmaps', 'sounds', 'loc') }).Count -eq 0) {
        throw 'The PC source must contain ui, bitmaps, sounds and at least one game map.'
    }
    foreach ($map in $maps) {
        $valid = switch ($map.BaseName) {
            'bitmaps' { Test-Resource $map.FullName 1; break }
            'sounds' { Test-Resource $map.FullName 2; break }
            'loc' { $map.Length -gt 4096; break }
            default { Test-PcGameMap $map.FullName }
        }
        if (!$valid) { throw "The PC source contains an invalid Halo CE PC retail map: $($map.Name)" }
        $relativePaths += 'halo-source/pc-retail/' + $map.Name
    }
    $entries = @()
    foreach ($map in $maps) {
        $entries += [pscustomobject]@{ Source = $map.FullName; Relative = 'halo-source/pc-retail/' + $map.Name }
    }
    $entries += [pscustomobject]@{ Source = $launcher; Relative = '3ds/Halo3DS-PC/Halo3DS-PC.3dsx' }
    $needed = [long]0
    foreach ($entry in $entries) {
        $entry | Add-Member -NotePropertyName Hash -NotePropertyValue (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash
        $entry | Add-Member -NotePropertyName Destination -NotePropertyValue (Test-Within $sd $entry.Relative)
        $entry | Add-Member -NotePropertyName ReplaceLauncher -NotePropertyValue $false
        $entry | Add-Member -NotePropertyName ExistingHash -NotePropertyValue $null
        if ((Test-Path -LiteralPath $entry.Destination) -and !(Test-Path -LiteralPath $entry.Destination -PathType Leaf)) {
            throw "Destination is not a file: $($entry.Destination)"
        }
        if (Test-Path -LiteralPath $entry.Destination -PathType Leaf) {
            if ((Get-Item -LiteralPath $entry.Destination -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Destination file is redirected: $($entry.Destination)"
            }
            $entry.ExistingHash = (Get-FileHash -LiteralPath $entry.Destination -Algorithm SHA256).Hash
            if ($entry.ExistingHash -ine $entry.Hash) {
                if ($entry.Relative -ne $launcherRelative) {
                    throw "Destination differs; no existing game file was replaced: $($entry.Destination)"
                }
                $oldLauncher = [IO.File]::OpenRead($entry.Destination)
                try {
                    $header = New-Object byte[] 4
                    if ($oldLauncher.Read($header, 0, 4) -ne 4 -or [Text.Encoding]::ASCII.GetString($header) -ne '3DSX') {
                        throw 'Existing PC launcher is not a 3DSX file; refusing to replace it.'
                    }
                } finally { $oldLauncher.Dispose() }
                $entry.ReplaceLauncher = $true
                $needed += (Get-Item -LiteralPath $entry.Source).Length
            }
        } else { $needed += (Get-Item -LiteralPath $entry.Source).Length }
    }
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($sd))
    if ($drive.AvailableFreeSpace -lt $needed + 64MB) { throw 'The SD card needs more free space for the selected PC maps.' }
    if ($dryRun) {
        Write-Output "What if: verified $($maps.Count) PC retail files from the selected source; would install under 3ds/Halo3DS-PC and halo-source/pc-retail on $sd"
        if (@($entries | Where-Object ReplaceLauncher).Count) { Write-Output 'What if: archive the previous PC launcher and install the verified new 3DSX' }
        if ($null -ne $BotCount) { Write-Output "What if: set solo multiplayer bot count to $BotCount" }
        return
    }
    if (!$PSCmdlet.ShouldProcess($sd, "Install owner-supplied Halo CE PC maps and PC launcher")) { return }
    foreach ($entry in $entries) {
        if ((Test-Path -LiteralPath $entry.Destination -PathType Leaf) -and !$entry.ReplaceLauncher) { continue }
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($entry.Destination)) -Force | Out-Null
        $partial = $entry.Destination + '.partial-' + [guid]::NewGuid().ToString('N')
        try {
            Copy-Item -LiteralPath $entry.Source -Destination $partial
            if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ine $entry.Hash) {
                throw "SD card copy failed verification: $($entry.Relative)"
            }
            if ($entry.ReplaceLauncher) {
                $backup = Test-Within $sd ('halo-source/launcher-backups/pc-' + [guid]::NewGuid().ToString('N') + '/Halo3DS-PC.3dsx')
                New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($backup)) -Force | Out-Null
                [IO.File]::Move($entry.Destination, $backup)
                try {
                    if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ine $entry.ExistingHash) {
                        throw 'Previous PC launcher backup failed read-back verification.'
                    }
                    [IO.File]::Move($partial, $entry.Destination)
                    if ((Get-FileHash -LiteralPath $entry.Destination -Algorithm SHA256).Hash -ine $entry.Hash) {
                        throw 'New PC launcher failed SD read-back verification.'
                    }
                } catch {
                    if (Test-Path -LiteralPath $entry.Destination -PathType Leaf) {
                        [IO.File]::Move($entry.Destination, $backup + '.failed-' + [guid]::NewGuid().ToString('N'))
                    }
                    if (!(Test-Path -LiteralPath $entry.Destination) -and (Test-Path -LiteralPath $backup -PathType Leaf)) {
                        [IO.File]::Move($backup, $entry.Destination)
                    }
                    throw
                }
                Write-Output "Updated and verified PC launcher; previous build archived at $backup"
            } else {
                [IO.File]::Move($partial, $entry.Destination)
                Write-Output "Installed and verified: $($entry.Relative)"
            }
        } finally { if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial } }
    }
    if ($null -ne $BotCount) {
        $botSettings = Test-Within $sd 'halo-source/bots.txt'
        $desired = [Text.Encoding]::ASCII.GetBytes([string]$BotCount + "`n")
        $partial = $botSettings + '.partial-' + [guid]::NewGuid().ToString('N')
        $previous = $null
        $alreadySet = $false
        try {
            if (Test-Path -LiteralPath $botSettings -PathType Leaf) {
                if ((Get-Item -LiteralPath $botSettings -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'Bot settings path is redirected.'
                }
                if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($botSettings)) -eq [Convert]::ToBase64String($desired)) {
                    Write-Output "Bot count already set to $BotCount"
                    $alreadySet = $true
                }
            }
            if (!$alreadySet) {
                [IO.File]::WriteAllBytes($partial, $desired)
                if (Test-Path -LiteralPath $botSettings -PathType Leaf) {
                    $previous = $botSettings + '.previous-' + [guid]::NewGuid().ToString('N')
                    [IO.File]::Move($botSettings, $previous)
                }
                [IO.File]::Move($partial, $botSettings)
                Write-Output "Solo multiplayer bot count set to $BotCount"
            }
        } catch {
            if ($previous -and !(Test-Path -LiteralPath $botSettings) -and (Test-Path -LiteralPath $previous)) {
                [IO.File]::Move($previous, $botSettings)
            }
            throw
        } finally { if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial } }
    }
    Write-Output 'PC retail installation complete. Launch 3ds/Halo3DS-PC/Halo3DS-PC.3dsx.'
} finally {
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if (!$resolvedStage.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedStage) -notmatch '^Halo3DS-PC-[0-9a-f]{32}$') {
        throw 'Refusing to remove an unexpected staging folder.'
    }
    if ($iso -and (Test-Path -LiteralPath $resolvedStage -PathType Container)) {
        [IO.Directory]::Delete($resolvedStage, $true)
    }
}
