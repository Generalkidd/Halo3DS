# Halo3DS

Halo 3DS is a port of Halo Combat Evolved from the OG Xbox using recently decompiled source code from the OpenCE project!
It supports all models of the 3DS family including both the OG and New models as well as 2DS models. 
This project also aims to support most of the 3DS's native features including 3D mode and 3DS wireless multiplayer!  

## Playing

You will need a modded 3DS/2DS console, a FAT32 SD card with at least 4 GB of free space and your own game files.
The current `dev` test build supports Xbox campaign and multiplayer, optional
retail PC multiplayer maps, and local wireless multiplayer/co-op. PC maps are
optional and do not replace the Xbox game data. Custom Edition and MCC maps
are not supported.

## Setup

For command-line setup on **Linux, macOS or Windows**, see the
[cross-platform CLI guide](docs/SETUP-CLI.md). The CLI can be built from source
with .NET SDK 10; its packaged builds include the runtime. The Windows graphical
setup instructions follow.

1. Download the setup tool for **Windows 10/11** or **Windows 7** from the
   [releases page](https://github.com/Generalkidd/Halo3DS/releases). Both include the game executable. The Windows 7 version is
   also compatible with Wine on Linux and Mac OS. 
2. Open the tool and select your **Xbox ISO/XISO** or **dumped maps folder**.
3. Select your SD card's drive, such as `H:\`, and click **Prepare SD card**.
   Leave the card connected until setup finishes.
4. Safely eject the card, put it back in your console, and launch **Halo3DS**
   from the Homebrew Launcher. The first load may take a while.

USA original, Rev 1 and Rev 2 Xbox discs are supported. Other recognizable
Xbox releases can be tried with a compatibility warning. If you're using a maps
folder, keep its matching `default.xbe` in the parent folder. If setup can't
find it, select it under **Additional options > HUD compatibility file**.
No debug executable is needed.

Open **PC options (optional)** to add retail PC multiplayer maps. Choose the `maps` folder from your installed retail Halo PC game
(not Custom Edition or MCC). To add it to an existing SD installation, leave
the Xbox source empty. PC files are optional; Xbox-only setup still works.

If setup asks you to dump DSP firmware for sound, do this once on your console:
press **L + D-pad Down + SELECT**, then choose
**Miscellaneous options > Dump DSP firmware** in Rosalina.

## Updating

In the updated setup tool, select your SD card and choose **Keep installed Xbox maps**
(**Update the launcher only** in older tools). Leave the PC folder blank for a launcher-only update.
You can select a newer `Halo3DS.3dsx` under **Additional options** if you want
to keep using an older setup tool.

You can also replace `3ds/Halo3DS/Halo3DS.3dsx` on the SD card yourself.
These updates keep your maps and saved files. Unless a release says otherwise,
you don't need to run the full setup again.

## Local wireless multiplayer (experimental)

Use the same wireless-test build and matching maps on every console, and turn
wireless communication on. No router or internet connection is needed.

1. Choose **Multiplayer > System Link** for competitive play, or
   **Multiplayer > Co-op** for campaign, then select a player profile.
2. On the host, choose **Create Game**, then select the map and game type.
3. On the other consoles, select the host's game and join the lobby.
4. Once everyone is listed, the host can start the match.

Competitive multiplayer supports up to four consoles, with one player each.
**Co-op** lists campaign missions with their preview images and a difficulty
selection. **System Link** lists multiplayer maps only. Co-op checkpoints last for the
current session; leaving ends the game for both players. Completing a mission
returns to the lobby with the next mission selected.

Keep the consoles awake during a match;
closing the lid or suspending the game disconnects that console. The first
version searches for one nearby compatible host at a time.

Paired-emulator matches have been tested, including an Original/New model pair.
Wireless reliability on real consoles and full campaign progression still need
testing before this should be treated as a stable release. The stable setup
tools do not include this experimental wireless build.

## Circle Pad Pro (experimental)

Turn on **Circle Pad Pro** in Controller
Settings to use the accessory's stick for aiming. **Right-stick aim** adjusts
its sensitivity. Touch aiming and the existing button layout still work.
Press an accessory shoulder button to wake it if necessary.

It was designed to fit the original **3DS or 3DS XL**. The option is also
available experimentally on other models, but physical fit and infrared
alignment are not guaranteed. On New models, the built-in C-stick remains
available when there is no fresh accessory input. Accessory support still
needs hardware testing.

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
The [integration plan for the original author](docs/INTEGRATION_PLAN.md) explains
the proposed review order, what is ready, and what still needs hardware testing.
The [modding guide](port/n3ds/MODDING.md) points to gameplay, bot, renderer,
cache and campaign extension code.
For the setup tools, start with the [Visual Studio solution instructions](docs/DEVELOPMENT.md#visual-studio).

## Origin and notices

Based on [bnunu/halo-1](https://github.com/bnunu/halo-1), revision
`8d0c1ff46ef92511e4809fff422bacd94c3b7c67`.

See the [license scope and credits](LICENSING.md), [upstream CC0 notice](LICENSE.md),
the [Kevin Boggs MIT notice](LICENSE-KEVIN-MIT.md), and the [setup component notices](port/n3ds/setup/THIRD-PARTY.md),
and the licenses for [LZ4](port/n3ds/vendor/lz4/LICENSE) and the
[allocator](port/n3ds/vendor/libctru-allocator/LICENSE.txt).
Original game assets, SDK files and Nintendo DSP firmware are not included.
