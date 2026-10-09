<# Install owner-supplied Xbox and PC retail data with one unified launcher. #>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$XboxIso,
    [string]$PcIso,
    [string]$PcMapsFolder,
    [Parameter(Mandatory = $true)][string]$SdRoot,
    [string]$XboxCompatibilityFile,
    [string[]]$PcMapNames = @(),
    [Nullable[int]]$BotCount = $null,
    [string]$SevenZipPath
)
$ErrorActionPreference = 'Stop'
if ([bool]$PcIso -eq [bool]$PcMapsFolder) {
    throw 'Choose exactly one PC source: -PcIso or -PcMapsFolder.'
}
# Keep provider-backed validation reads active under -WhatIf. Both converters
# still return before destination writes during preflight.
$dryRun = [bool]$WhatIfPreference
$WhatIfPreference = $false

$xbox = (Resolve-Path -LiteralPath $XboxIso).Path
$pc = if ($PcIso) { (Resolve-Path -LiteralPath $PcIso).Path } else { (Resolve-Path -LiteralPath $PcMapsFolder).Path }
$sd = (Resolve-Path -LiteralPath $SdRoot).Path
if (!(Test-Path -LiteralPath $xbox -PathType Leaf) -or
    [IO.Path]::GetExtension($xbox) -notin @('.iso','.xiso')) {
    throw 'Select an existing original Xbox Halo XISO.'
}
if ($PcIso -and (!(Test-Path -LiteralPath $pc -PathType Leaf) -or [IO.Path]::GetExtension($pc) -ine '.iso')) {
    throw 'Select an existing Halo CE PC retail ISO.'
}
if ($PcMapsFolder -and !(Test-Path -LiteralPath $pc -PathType Container)) {
    throw 'Select an existing Halo CE PC retail maps folder.'
}
if ($xbox.Equals($pc,[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose two distinct disc images.' }
if (!(Test-Path -LiteralPath $sd -PathType Container) -or
    [IO.Path]::GetFileName($sd.TrimEnd('\','/')) -in @('3ds','halo-source','pc-retail')) {
    throw 'Choose the existing SD card root, not a Halo3DS subfolder.'
}
$sdPrefix = [IO.Path]::GetFullPath($sd).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if ($xbox.StartsWith($sdPrefix,[StringComparison]::OrdinalIgnoreCase) -or
    $pc.Equals($sd,[StringComparison]::OrdinalIgnoreCase) -or
    $pc.StartsWith($sdPrefix,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep both game sources outside the SD card destination.'
}
if ($null -ne $BotCount -and ($BotCount -lt 0 -or $BotCount -gt 15)) { throw 'Bot count must be 0 through 15.' }
$xboxCli = Join-Path $PSScriptRoot 'Halo3DS-Xbox-Install.exe'
$pcInstaller = Join-Path $PSScriptRoot 'Install-Halo3DS-PC.ps1'
$pcLauncher = Join-Path $PSScriptRoot 'Halo3DS.3dsx'
$pcManifest = Join-Path $PSScriptRoot 'unified-runtime.json'
foreach ($file in @($xboxCli,$pcInstaller,$pcLauncher,$pcManifest)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Installer package is incomplete: $file" }
}
if ($XboxCompatibilityFile) { $XboxCompatibilityFile = (Resolve-Path -LiteralPath $XboxCompatibilityFile).Path }

$xboxArgs = @('--validate',$xbox,$sd)
if ($XboxCompatibilityFile) { $xboxArgs += $XboxCompatibilityFile }
& $xboxCli @xboxArgs
if ($LASTEXITCODE -ne 0) { throw 'Xbox source validation failed; nothing was installed.' }

$pcArgs = @{ SdRoot=$sd; LauncherPath=$pcLauncher; RuntimeManifest=$pcManifest; UnifiedLauncher=$true; WhatIf=$true }
if ($PcIso) { $pcArgs.PcIso = $pc } else { $pcArgs.PcMapsFolder = $pc }
if ($PcMapNames.Count) { $pcArgs.MapNames = @($PcMapNames) + @('bloodgulch') | Select-Object -Unique }
if ($null -ne $BotCount) { $pcArgs.BotCount = $BotCount }
if ($SevenZipPath) { $pcArgs.SevenZipPath = $SevenZipPath }
& $pcInstaller @pcArgs
if ($LASTEXITCODE -ne 0 -or !$?) { throw 'PC source validation failed; nothing was installed.' }

if ($dryRun) {
    Write-Output 'What if: both game sources and the unified launcher passed preflight; one Halo3DS launcher would be installed on the selected SD card.'
    return
}
if (!$PSCmdlet.ShouldProcess($sd,'Install Xbox and PC Halo3DS data with one launcher')) { return }

$xboxArgs[0] = '--install'
& $xboxCli @xboxArgs
if ($LASTEXITCODE -ne 0) { throw 'Xbox installation failed. No PC installation was started; rerun after correcting the Xbox source.' }
$pcArgs.Remove('WhatIf')
try { & $pcInstaller @pcArgs }
catch {
    throw "Xbox installation completed, but PC installation failed. The owner maps and launcher are safe to retry: $($_.Exception.Message)"
}
if ($LASTEXITCODE -ne 0 -or !$?) { throw 'PC installation failed after Xbox setup; rerun after correcting the PC source.' }
Write-Output 'Both installations complete. Launch 3ds/Halo3DS/Halo3DS.3dsx for Xbox Campaign and PC Local Multiplayer.'
