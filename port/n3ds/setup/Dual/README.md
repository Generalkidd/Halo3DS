# Halo3DS two-ISO installer

This package runs one installer flow for an original Xbox Halo: Combat Evolved XISO and a Halo CE PC retail ISO. It prepares the Xbox maps for the normal Halo3DS launcher, copies validated PC retail maps for the experimental PC launcher, and keeps the two launchers in separate Homebrew Launcher folders. It does not run software from either disc.

The ZIP contains installer code, two game launchers, the matching PC runtime manifest, source for the Xbox setup converter, and license notices. **It contains no disc image, map, sound, HUD, XBE, DSP firmware, or extracted game asset.** Supply your own two disc images. The Xbox release must be the supported English North American retail build; the PC release must have v7 maps. Preflight checks compatibility and destination conflicts. The Xbox converter verifies each map hash before committing that map; the PC installer verifies its extracted files and checks each SD copy.

## Run

Extract the ZIP into a normal PC folder. Keep its files together. On Windows, open PowerShell in that folder and run a dry run first. The Xbox converter uses .NET Framework 3.5; Windows may need that optional feature enabled.

```powershell
.\Install-Halo3DS-Both.ps1 -XboxIso 'C:\path\Halo Xbox.iso' -PcIso 'C:\path\Halo CE PC.iso' -SdRoot 'D:\' -XboxCompatibilityFile 'C:\path\ntsc2276-hud-strings.bin' -BotCount 2 -WhatIf
```

When the dry run succeeds, repeat the same command without `-WhatIf`. `-XboxCompatibilityFile` is needed only when the Xbox source does not contain the supported HUD data; an existing verified HUD file on the SD root can also satisfy it. The file is read from your own prior installation and is never included in this package. `-BotCount` may be 0 through 15, and can be omitted. Use `-PcMapNames bloodgulch,boardingaction` to install a subset; otherwise all PC game maps on the disc are selected. 7-Zip is needed to read the PC ISO. If it is not installed, supply `-SevenZipPath 'C:\path\7z.exe'`.

Select the **SD card root**, not its `3ds` folder. The 3DS uses FAT32. The installer does not format the card. The two ISOs must stay outside the SD card. It checks hashes after copying PC files, refuses to overwrite different PC files, and prepares Xbox maps and sidecars with the original setup converter. An existing Xbox installation can be repaired or updated by rerunning the installer. Keep the SD card connected until it reports completion. A failed PC step after the Xbox step can be retried without deleting the Xbox installation.

The normal launcher is `3ds/Halo3DS/Halo3DS.3dsx`; the experimental PC launcher is `3ds/Halo3DS-PC/Halo3DS-PC.3dsx`. From the PC main menu, press **SELECT** for Local Multiplayer, choose one of 19 stock PC multiplayer maps, a mode, and 0–15 bots, then press A. The PC runtime reached active matches on all 19 maps in Azahar smoke tests. Full matches, all scoring paths, and physical New 3DS XL operation still need testing.

If Windows cannot read the card, stop before installing. The removable `D:` drive on the development PC was reporting **No Media** when this package was prepared, so the installer was tested in an isolated output folder and has not been written to that card. Verify the assigned drive letter after reseating the card reader.

## Source and notices

`Xbox-Setup-Source.zip` contains the matching Xbox converter source, format descriptions, and license notices. `LICENSE.md`, `THIRD-PARTY.md`, and `GPL-3.0.txt` are also at the top level for review. These notices do not grant rights to game assets or guarantee that distributing the compiled port is permitted in every jurisdiction. The ZIP inventory and embedded resources were checked for game data, but legal clearance is a separate decision.
