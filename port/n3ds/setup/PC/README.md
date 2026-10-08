# Halo3DS PC retail installer

This installer is for a **PC retail v7 runtime build**. It requires your own Halo: Combat Evolved **PC ISO** and 7-Zip. It rejects Xbox disc images and never runs the disc's setup program. The package contains no ISO, maps, Xbox files, or other game assets.

Place a verified PC runtime `Halo3DS-PC.3dsx` and its matching `pc-runtime.json` beside these scripts. The manifest must state `MapFormat: HaloCE-PC-retail-v7`, `Runtime: Halo3DS-PC`, and the SHA-256 of the 3DSX. The current official Xbox-only 3DSX does **not** meet this requirement. The PC runtime is still under development; this installer is not a playable release on its own.

From PowerShell, run:

```powershell
./Install-Halo3DS-PC.ps1 -PcIso 'C:\path\to\Halo CE PC.iso' -SdRoot 'D:\' -WhatIf
./Install-Halo3DS-PC.ps1 -PcIso 'C:\path\to\Halo CE PC.iso' -SdRoot 'D:\'
```

If 7-Zip is not installed, supply `-SevenZipPath` with a trusted copy of `7z.exe`. All available campaign and multiplayer maps are selected by default; use `-MapNames a10,bloodgulch` to select a subset. The installer extracts `ui.map`, `bitmaps.map`, `sounds.map`, optional `loc.map`, and selected game maps from the ISO into a temporary folder. It checks PC cache headers, compares SHA-256 after each SD copy, and refuses to overwrite different files. It installs to `3ds/Halo3DS-PC/` and `halo-source/pc-retail/`; existing `3ds/Halo3DS/` launchers, Xbox maps, saves, and prior PC demos are left in place.

Use `-BotCount 0` through `-BotCount 15` to set the number of local solo multiplayer bots. Omitting it keeps the existing setting; a fresh installation defaults to zero. The count is stored in `halo-source/bots.txt` so it can also be edited between matches. If the installer changes an existing count, it keeps a `.previous-*` backup beside that file. The current bot runtime has emulator checks for Slayer, CTF, Oddball, King, and Race on Xbox-format Blood Gulch; PC-format gameplay still requires the unfinished PC runtime integration.

`Package-Halo3DS-PC.ps1` creates an asset-free ZIP from the two installer scripts, this README, and the verified PC launcher and manifest. It accepts explicit file paths and never scans game-data folders.
