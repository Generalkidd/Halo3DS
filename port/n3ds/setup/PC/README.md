# Halo3DS PC retail installer

This installer is for a **PC retail v7 runtime build**. It requires your own Halo: Combat Evolved **PC ISO** and 7-Zip. It rejects Xbox disc images and never runs the disc's setup program. The package contains no ISO, maps, Xbox files, or other game assets.

An existing complete PC retail `maps` folder is also accepted through `-PcMapsFolder` in place of `-PcIso`; this path does not need 7-Zip. Exactly one PC source must be selected. Both paths validate v7 map headers and destination hashes before writing to the SD card.

Place the experimental PC runtime `Halo3DS-PC.3dsx` and its matching `pc-runtime.json` beside these scripts. `build.py --experimental-pc-runtime` generates the manifest beside the 3DSX with `MapFormat: HaloCE-PC-retail-v7`, `Runtime: Halo3DS-PC`, and its SHA-256. The normal Xbox launcher does **not** meet this requirement. The PC runtime currently reaches local multiplayer in Azahar; physical New 3DS testing and full match validation are still pending.

From PowerShell, run:

```powershell
./Install-Halo3DS-PC.ps1 -PcIso 'C:\path\to\Halo CE PC.iso' -SdRoot 'D:\' -WhatIf
./Install-Halo3DS-PC.ps1 -PcIso 'C:\path\to\Halo CE PC.iso' -SdRoot 'D:\'
```

If 7-Zip is not installed, supply `-SevenZipPath` with a trusted copy of `7z.exe`. All available campaign and multiplayer maps are selected by default; use `-MapNames a10,bloodgulch` to select a subset. The installer extracts `ui.map`, `bitmaps.map`, `sounds.map`, optional `loc.map`, and selected game maps from the ISO into a temporary folder. It checks PC cache headers, compares SHA-256 after each SD copy, and refuses to overwrite different files. It installs to `3ds/Halo3DS-PC/` and `halo-source/pc-retail/`; existing `3ds/Halo3DS/` launchers, Xbox maps, saves, and prior PC demos are left in place.

On the PC launcher, press **SELECT** from the main menu for Local Multiplayer. Use up/down to select map, mode or bots; left/right to change the value; A to start; B to close. All nineteen stock PC multiplayer maps and the five original modes are listed. The match runs locally with zero to fifteen bots. `-BotCount 0` through `-BotCount 15` sets the fallback bot count in `halo-source/bots.txt` for other launch paths; the menu selection controls its own match. Omitting the installer option keeps the existing setting; a fresh installation defaults to zero. If the installer changes an existing count, it keeps a `.previous-*` backup beside that file. Emulator checks entered active matches on all nineteen PC multiplayer maps, and a menu input replay started PC Blood Gulch CTF with two bots. Full match completion, scoring and physical hardware are still unverified.

`Package-Halo3DS-PC.ps1` creates an asset-free ZIP from the two installer scripts, this README, and the verified PC launcher and manifest. It accepts explicit file paths and never scans game-data folders.
