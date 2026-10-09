# Halo3DS unified installer

This installer prepares owner supplied Halo: Combat Evolved Xbox and PC retail
data for one New 3DS launcher. It installs Xbox campaign and multiplayer maps,
PC retail multiplayer maps, and `3ds/Halo3DS/Halo3DS.3dsx`. The package contains
no disc image, map, sound, HUD, XBE, DSP firmware, or extracted game asset.

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

The Xbox main menu starts Campaign and Xbox multiplayer. Press **SELECT** for
PC Local Multiplayer, then choose a PC map, mode, and bot count. `-BotCount`
sets a default from 0 to 15. On the campaign difficulty screen, the bottom
touch screen toggles **PC weapons and vehicles in campaign**. It starts ON for
testing and persists until changed there. The added pickups are beside
original pickups in c20, c40, and d20. A Rocket Warthog is added beside an
original Warthog in b30. Original map files and placements are not edited.
The Rocket Warthog needs PC Blood Gulch on the SD card; if those assets are
unavailable, the Xbox mission still loads without the extra vehicle.

On New 3DS, open Controller Settings and tap **PC high textures** to compare
the experimental 128-pixel textures and 64-pixel mip with standard 64-pixel
textures. Save the setting, then reload the PC map. It starts OFF; the high
mode can take longer to load and may affect frame rate. Xbox maps and original
3DS use their standard texture profile.

One launcher replaces the normal Xbox setup launcher only after both sources
pass preflight. The previous 3DSX is archived under
`halo-source/launcher-backups/`. Saves and unrelated Homebrew Launcher folders
are preserved. A failed install can be rerun after correcting its input. Keep
the card connected until the script reports completion.

The unified menu and optional campaign spawns were exercised in Azahar 2126.2.
Physical New 3DS XL performance and longer campaign and multiplayer sessions
still require device testing. Keep a known good SD backup while testing and
retain `halo-source-engine.log` when reporting a problem.

`Xbox-Setup-Source.zip`, `LICENSE.md`, `THIRD-PARTY.md`, and `GPL-3.0.txt` are
included for review. These notices do not grant rights to game assets.
