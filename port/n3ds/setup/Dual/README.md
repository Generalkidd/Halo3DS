# Halo3DS two-ISO installer

This package runs one installer flow for an original Xbox Halo: Combat Evolved XISO and a Halo CE PC retail ISO. It prepares the Xbox maps for the normal Halo3DS launcher, copies validated PC retail maps for the experimental PC launcher, and keeps the two launchers in separate Homebrew Launcher folders. It does not run software from either disc.

If your PC disc was already extracted, pass its complete `maps` folder with `-PcMapsFolder` instead of `-PcIso`. The folder must contain PC retail v7 `ui.map`, `bitmaps.map`, `sounds.map`, and at least one game map. Both PC inputs use the same cache checks, destination conflict checks, and hash verification. Exactly one PC source is required.

The ZIP contains installer code, two game launchers, the matching PC runtime manifest, source for the Xbox setup converter, and license notices. **It contains no disc image, map, sound, HUD, XBE, DSP firmware, or extracted game asset.** Supply your own two disc images. The Xbox release must be the supported English North American retail build; the PC release must have v7 maps. Preflight checks compatibility and destination conflicts. The Xbox converter verifies each map hash before committing that map; the PC installer verifies its extracted files and checks each SD copy.

## Run

Extract the ZIP into a normal PC folder. Keep its files together. On Windows, open PowerShell in that folder and run a dry run first. The Xbox converter uses .NET Framework 3.5; Windows may need that optional feature enabled.

```powershell
.\Install-Halo3DS-Both.ps1 -XboxIso 'C:\path\Halo Xbox.iso' -PcIso 'C:\path\Halo CE PC.iso' -SdRoot 'D:\' -XboxCompatibilityFile 'C:\path\ntsc2276-hud-strings.bin' -BotCount 2 -WhatIf
```

When the dry run succeeds, repeat the same command without `-WhatIf`. `-XboxCompatibilityFile` is needed only when the Xbox source does not contain the supported HUD data; an existing verified HUD file on the SD root can also satisfy it. The file is read from your own prior installation and is never included in this package. `-BotCount` may be 0 through 15, and can be omitted. Use `-PcMapNames bloodgulch,boardingaction` to install a subset; otherwise all PC game maps on the disc are selected. 7-Zip is needed to read the PC ISO. If it is not installed, supply `-SevenZipPath 'C:\path\7z.exe'`.

For an already extracted PC maps folder, the same dry run is:

```powershell
.\Install-Halo3DS-Both.ps1 -XboxIso 'C:\path\Halo Xbox.iso' -PcMapsFolder 'C:\path\Halo CE\maps' -SdRoot 'D:\' -WhatIf
```

Select the **SD card root**, not its `3ds` folder. The 3DS uses FAT32. The installer does not format the card. The two ISOs must stay outside the SD card. It checks hashes after copying PC files, refuses to overwrite different PC map or resource files, and prepares Xbox maps and sidecars with the original setup converter. Rerunning the installer updates an older PC launcher at its dedicated path after archiving the previous 3DSX under `halo-source/launcher-backups/pc-...`; saves and other Homebrew Launcher folders are preserved. Keep the SD card connected until it reports completion. A failed PC step after the Xbox step can be retried without deleting the Xbox installation.

The normal launcher is `3ds/Halo3DS/Halo3DS.3dsx`; the experimental PC launcher is `3ds/Halo3DS-PC/Halo3DS-PC.3dsx`. From the PC main menu, press **SELECT** for Local Multiplayer, choose one of 19 stock PC multiplayer maps, a mode, and 0–15 bots, then press A. The PC runtime reached active matches on all 19 maps in Azahar smoke tests. Full matches and all scoring paths still need testing. On a physical New 3DS XL, Timberland with more than two bots was reported slow from the start, audio was reported missing, and the PC main menu Exit action stayed on screen. This package includes a PC sound resource fix that submitted audio in Azahar, a PC model vertex cache that reduced six-bot Timberland render time to about 20 ms per frame in Azahar, and a native Exit action that completed Quit → OK and normal teardown in Azahar. Physical audio and performance remain under investigation. Keep the 3D slider off while gathering a performance baseline.

If Windows cannot read the card, stop before installing and verify its drive letter after reseating the reader. The combined installer was checked on a FAT32 card using the supported Xbox ISO and a complete extracted PC retail `maps` folder. It preserved existing saves and demo folders, verified 24 Xbox maps and 32 PC map/resource files, and checked both launcher hashes. After a hardware test, the card's `halo-source-engine.log` at its root can help diagnose audio and frame costs. Keep that log and identify which launcher was used when reporting a problem.

## Source and notices

`Xbox-Setup-Source.zip` contains the matching Xbox converter source, format descriptions, and license notices. `LICENSE.md`, `THIRD-PARTY.md`, and `GPL-3.0.txt` are also at the top level for review. These notices do not grant rights to game assets or guarantee that distributing the compiled port is permitted in every jurisdiction. The ZIP inventory and embedded resources were checked for game data, but legal clearance is a separate decision.
