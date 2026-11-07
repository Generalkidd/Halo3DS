# Halo3DS

Halo 3DS is a port of Halo Combat Evolved from the OG Xbox using recently decompiled source code from the OpenCE project!
It supports all models of the 3DS family including both the OG and New models as well as 2DS models. 
This project also aims to support most of the 3DS's native features including 3D mode and 3DS wireless multiplayer!  

## Playing

You will need a modded 3DS/2DS console, a FAT32 SD card with at least 4 GB of free space and your own game files.
Currently only the 1.0 Halo CE ISO is supported but support for others is being looking into. PC/MCC
maps are not supported.

Press **SELECT** or tap **Controller Settings** on the main or pause menu.
On New 3DS and New 2DS models, **L fires** and **ZL throws grenades** by default.
On original 3DS and 2DS models, **L fires** and **R throws grenades**.

- **Layout:** Preview a preset, then press **A** or tap **Apply** to replace
  this model's bindings. You can also set the menu's face-button convention.
  On New models, **Classic ZR fire** restores the previous default layout.
- **Buttons:** Assign an action to each face button, shoulder button, and
  D-pad direction, or leave it unassigned. The game remembers separate
  layouts for New and original models. Tap a row or press **A** to open the
  action picker. Use **Prev** and **Next** to reach more rows by touch.
- **Aim:** Adjust touch and C-stick sensitivity separately, reverse vertical
  input for either, and set the C-stick dead zone. On New models you can turn
  off touch drag aiming and still use the touchscreen shortcuts.

Use **L/R** or tap the tabs. Use the D-pad or tap **−/+** to change a value.
Press **START** to save, **B** to go back, or **X** to reset the current tab.

Press **Y** or tap **Graphics Options** on the main or pause menu to change
**World resolution**. Press **A** or **START** to save and apply the choice
without reloading the map. The game remembers the setting. The HUD, menus,
and touch screen stay at full resolution.

| World resolution | When to use it | World pixels vs. native |
| --- | --- | ---: |
| Auto (default) | Let the game adjust under sustained load | Varies |
| 400×240 | Sharpest picture | 100% |
| 320×192 | Try this first if the frame rate dips | 64% |
| 280×168 | Give the GPU more room in busy scenes | 49% |
| 200×120 | Largest reduction, with a noticeably softer picture | 25% |

Lower settings can help scenes limited by drawing pixels. The actual frame
rate also depends on the scene and console. New models also offer a separate
**Texture detail** tab, which takes effect on the next map load.

## Setup

For command-line setup on **Linux, macOS or Windows**, see the
[cross-platform CLI guide](docs/SETUP-CLI.md). The CLI can be built from source
with .NET SDK 10; its packaged builds include the runtime. The Windows graphical
setup instructions follow.

1. Download the setup tool for **Windows 10/11** or **Windows 7** from the
   [releases page](https://github.com/Generalkidd/Halo3DS/releases). Both include the game executable. The Windows 7 version is
   also compatible with Wine on Linux and Mac OS. 
3. Open the tool and select your **Xbox ISO/XISO** or **dumped maps folder**.
4. Select your SD card's drive, such as `H:\`, and click **Prepare SD card**.
   Leave the card connected until setup finishes.
5. Safely eject the card, put it back in your console, and launch **Halo3DS**
   from the Homebrew Launcher. The first load may take a while.

Use the English North American retail Xbox release. If you're using a maps
folder, keep its matching `default.xbe` in the parent folder. If setup can't
find it, select it under **Additional options > HUD compatibility file**.
No debug executable is needed.

If setup asks you to dump DSP firmware for sound, do this once on your console:
press **L + D-pad Down + SELECT**, then choose
**Miscellaneous options > Dump DSP firmware** in Rosalina.

## Updating

In the setup tool, select your SD card and choose **Update the launcher only**.
You can select a newer `Halo3DS.3dsx` under **Additional options** if you want
to keep using an older setup tool.

You can also replace `3ds/Halo3DS/Halo3DS.3dsx` on the SD card yourself.
These updates keep your maps and saved files. Unless a release says otherwise,
you don't need to run the full setup again.

## Reporting problems

Include your console model, build version, mission/map, difficulty, whether 3D
was on, and what happened. For a crash or freeze, power off before removing
the SD card and copy the logs before launching Halo3DS again:

- `halo-source-engine.log` and `halo-source-engine.previous.log` at the SD root
- `halo-source-fatal.log` and `halo-source-stall.log`, if present
- The newest `.dmp` in `luma/dumps/arm11/`, if one was saved

A freeze or return to the Homebrew Launcher may not leave a crash dump.
Check logs before sharing them, and don't include game files or firmware.

## Development

[Developer notes](docs/DEVELOPMENT.md) cover building the game and both setup
tools, the code layout, testing, and details to watch out for when making changes.
For the setup tools, start with the [Visual Studio solution instructions](docs/DEVELOPMENT.md#visual-studio).

## Origin and notices

Based on [bnunu/halo-1](https://github.com/bnunu/halo-1), revision
`8d0c1ff46ef92511e4809fff422bacd94c3b7c67`.

See [LICENSE.md](LICENSE.md), the [setup component notices](port/n3ds/setup/THIRD-PARTY.md),
and the licenses for [LZ4](port/n3ds/vendor/lz4/LICENSE) and the
[allocator](port/n3ds/vendor/libctru-allocator/LICENSE.txt).
Original game assets, SDK files and Nintendo DSP firmware are not included.
