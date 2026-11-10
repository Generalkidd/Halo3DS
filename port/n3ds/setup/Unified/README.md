# Halo3DS unified installer

This installer prepares owner supplied Halo: Combat Evolved Xbox and PC retail
data for one 3DS/2DS launcher. It installs Xbox campaign and multiplayer maps,
PC retail multiplayer maps, and `3ds/Halo3DS/Halo3DS.3dsx`. The package contains
no disc image, map, sound, HUD, XBE, DSP firmware, or extracted game asset.

For most players, use the Windows GUI from the [main setup guide](../../../../README.md#setup).
The commands below are the advanced two-source script path.

**At a glance:** extract the ZIP on your Windows PC, run the command below
with your own game sources and SD card root, check its `-WhatIf` result, then
repeat without `-WhatIf` to install. The installer prints three steps so you
can see which source it is checking. If a check fails, correct that source and
rerun it; a failed preflight does not write to the SD card.

Use the supported English North American Xbox retail XISO and PC retail v7
maps. For the PC source, give either its ISO or a complete extracted `maps`
folder containing `ui.map`, `bitmaps.map`, `sounds.map`, and `bloodgulch.map`.
If you select only some PC game maps, Blood Gulch is included because the
optional campaign Rocket Warthog draws its assets from that owner supplied map.

Extract the package to a normal PC folder and run a dry run first:

```powershell
.\Install-Halo3DS-Unified.ps1 -XboxIso 'C:\path\Halo Xbox.iso' -PcIso 'C:\path\Halo CE PC.iso' -SdRoot 'D:\' -WhatIf
```

For an already extracted PC `maps` folder:

```powershell
.\Install-Halo3DS-Unified.ps1 -XboxIso 'C:\path\Halo Xbox.iso' -PcMapsFolder 'C:\path\Halo CE\maps' -SdRoot 'D:\' -WhatIf
```

When validation succeeds, repeat without `-WhatIf`. Choose the SD card root,
not its `3ds` folder. The script does not format the card. It verifies source
formats and hashes, checks destination conflicts, and verifies each copy.
Different existing game data is preserved. If the Xbox source lacks supported
HUD data, pass `-XboxCompatibilityFile` pointing to a verified HUD file from
your own prior installation. ISO extraction needs 7-Zip; pass `-SevenZipPath`
if it is not found. The Xbox converter uses .NET Framework 3.5.

For gameplay, controls and ordinary GUI setup, see the [main README](../../../../README.md).
The shared map browser distinguishes installed Xbox and PC maps; after choosing a
local map and gametype, the bottom screen offers bots and equipment settings.
System Link hosts can add up to three bots; local matches support up to fifteen.
Co-op blocks campaign extras. Physical wireless validation remains pending for
the current integration test build.

Return to Lobby preserves the successful local match's map format and settings.
PC-only top-screen thumbnails and lower-screen geometry/gear previews read the
owner's installed files. No Halo artwork is bundled with the installer.

The older SELECT PC selector remains available as an alternate local menu.
Implementation details and current verification limits are in the
[developer notes](../../../../docs/DEVELOPMENT.md) and
[integration status](../../../../docs/INTEGRATION_PLAN.md).

One launcher replaces the normal Xbox setup launcher only after both sources
pass preflight. The previous 3DSX is archived under
`halo-source/launcher-backups/`. Saves and unrelated Homebrew Launcher folders
are preserved. A failed install can be rerun after correcting its input. Keep
the card connected until the script reports completion.

The unified menu and optional campaign spawns were exercised in Azahar 2126.2.
A six-bot Timberland match reached its postgame state on a New 3DS XL. A user
reported that the newer Death Island build no longer crashed in a local match;
the exact Scorpion firing sequence and latest previews still need a device
retest. Busy
combat scenes have not established a smooth 30 FPS. Keep a known good SD
backup while testing and retain `halo-source-engine.log` when reporting a
problem.

`Halo3DS-Source.zip` contains the tracked source tree used for the launcher;
`Xbox-Setup-Source.zip` contains the separate Xbox installer source.
`LICENSING.md`, `LICENSE.md`, `LICENSE-KEVIN-MIT.md`, `THIRD-PARTY.md`, and
`GPL-3.0.txt` explain the mixed-license package. The original project's CC0
notice, Kevin Boggs's MIT notice for his original additions, and the GPLv3
notice and corresponding source for the setup component remain separate.
These notices do not grant rights to game assets.
