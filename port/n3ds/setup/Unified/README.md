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
PC Local Multiplayer, then choose a PC map, mode, bot count, difficulty, and on New 3DS,
tap **PC textures** to switch between standard 64-pixel textures and the
experimental 128-pixel texture with mip. This choice saves immediately and
applies when the map loads. Difficulty changes ordinary bot accuracy and firing
rate. ChaoticSPARTAN1 and KodeBreaker16 each have an independent 50% chance
per match when at least two bot slots are available; both can appear together.
KodeBreaker16 has teal armor and a 25% chance of 60 seconds of active
camouflage on each spawn. ChaoticSPARTAN1 has green armor and a 10% chance.
Press **X** or tap
**X: Gear** to choose a weapon set and vehicle set before the match. **Normal
(PC map)** keeps each PC map's original pickups, including Flamethrower and
Fuel Rod placements where the map has them. The extra **Flamethrowers** and
**Fuel Rods** sets replace weapon pickups across the chosen PC map. **All PC
vehicles** enables every vehicle type that the map places, including Rocket
Warthog and Banshee where present. `-BotCount`
sets a default from 0 to 15. On the campaign difficulty screen, the bottom
touch screen toggles **PC weapons and vehicles in campaign**. It starts ON for
testing and persists until changed there. The added pickups are beside
original pickups in c20, c40, and d20. A Rocket Warthog is added beside an
original Warthog in b30. Original map files and placements are not edited.
The Rocket Warthog needs PC Blood Gulch on the SD card; if those assets are
unavailable, the Xbox mission still loads without the extra vehicle.

On New 3DS, the same choice is available in Controller Settings as **PC high
textures** to compare
the experimental 128-pixel textures and 64-pixel mip with standard 64-pixel
textures. Save the setting, then reload the PC map. It starts OFF; the high
mode can take longer to load and may affect frame rate. Xbox maps and original
3DS use their standard texture profile.

During a solo PC match, hold **SELECT** or the lower-right **SCORE** touch
panel around the motion tracker to show the scoreboard on the top screen.
The three other touch panels give press feedback but have no assigned action.
Controller Settings also offers separate **Swap X/Y** and **Swap A/B** options,
**Show FPS** on the bottom screen, and **Decals**. Both face-button swaps and
Decals start ON for new profiles; FPS starts OFF. The Xbox face layout preset
is applied before the individual swaps, so turning on the preset and a swap
for the same pair restores that pair's Nintendo positions. Existing profiles
keep their other choices and receive Swap A/B ON when upgraded. Surface marks
follow the lifetimes supplied by the game maps. After a local multiplayer
match ends, tap **Return to Lobby** at the top of the bottom screen or press
**B** to reopen the map, mode, and bot selector.

One launcher replaces the normal Xbox setup launcher only after both sources
pass preflight. The previous 3DSX is archived under
`halo-source/launcher-backups/`. Saves and unrelated Homebrew Launcher folders
are preserved. A failed install can be rerun after correcting its input. Keep
the card connected until the script reports completion.

The unified menu and optional campaign spawns were exercised in Azahar 2126.2.
A six-bot Timberland match reached its postgame state on a New 3DS XL. The
latest postgame, bot, and visual changes still need a device retest. Busy
combat scenes have not established a smooth 30 FPS. Keep a known good SD
backup while testing and retain `halo-source-engine.log` when reporting a
problem.

`Halo3DS-Source.zip` contains the tracked source tree used for the launcher;
`Xbox-Setup-Source.zip` contains the separate Xbox installer source.
`LICENSE.md`, `THIRD-PARTY.md`, and `GPL-3.0.txt` are included for review.
These notices do not grant rights to game assets.
