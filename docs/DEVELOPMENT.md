# Developer notes

Build instructions, testing and maintenance notes for contributors. Player
instructions are in the [main README](../README.md).
Commands below run from the repository root unless stated otherwise.

- [Building the game](#building-the-game)
- [Building the setup tools](#building-the-setup-tools)
- [Setup formats and recovery](#setup-formats-and-recovery)
- [Engine and platform boundary](#engine-and-platform-boundary)
- [Rendering rules](#rendering-rules)
- [Original-model optimization packs](#original-model-optimization-packs)
- [Stability and timing rules](#stability-and-timing-rules)
- [Circle Pad Pro input](#circle-pad-pro-input)
- [Local wireless multiplayer](#local-wireless-multiplayer)
- [Verification](#verification)
- [Setup and repository maintenance](#setup-and-repository-maintenance)

## Building the game

The build uses Python 3.10+, Clang, devkitARM, libctru, citro3d, Picasso and
3dsxtool, plus locally supplied August 2001 XDK headers. SDK files and game
data are not included. Players do not need these dependencies.

The build has been checked on Windows. Use native Windows toolchain paths;
a DEVKITARM value from WSL is not a Windows toolchain. Validated dependency
revisions are libctru `36fe1ada5b7ebe53ba4decda36d764a55f8fefb6` and citro3d
`9f21cf7b380ce6f9e01a0420f19f0763e5443ca7`.

Replace the dependency paths with yours, quoting any that contain spaces:

```powershell
python port/n3ds/build.py `
  --devkitarm C:/devkitPro/devkitARM `
  --clang C:/tools/llvm/bin/clang.exe `
  --libctru C:/deps/libctru/libctru `
  --citro3d C:/deps/citro3d `
  --xdk C:/private/xdk/include `
  --picasso C:/tools/picasso.exe `
  --packer C:/tools/3dsxtool.exe
```

Output is `build/n3ds/Halo3DS.3dsx`, the playable main-menu build for both
Original and New models. `--help` lists environment overrides and output
options.

`port/n3ds/sources.json` lists the native replacements and required original
engine units. `tools/compat.py` and `tools/shaders.py` in that directory prepare
the SDK overlay/linkage adapters and assemble PICA shaders. Generated headers,
response files, logs, ELF/map and the build/hash manifest stay in the output
directory. Retain the matching ELF/map for crash diagnosis.

Incremental builds track source/header content. Choose a new `--output` directory
for clean verification. The Xbox-only release uses default options. The unified
New 3DS release adds `--experimental-pc-runtime --unified-ui` and emits
`unified-runtime.json` bound to the built 3DSX hash. Neither release uses
diagnostic flags, a frame limit or a test heap.

### Diagnostic builds

Add `--test-frames 240 --diagnostics --output build/n3ds-diagnostics` to the
dependency command above for a bounded menu run with renderer checks. Success
writes `RESULT: PASS` to the game log. This build exits automatically.

`--original-memory` constrains the heap for Original-model emulator tests.
Also select an Original model in the emulator; neither setting replaces a
test on actual Original 3DS/2DS hardware.

### Experimental release comparison

Add `--release --output build/n3ds-release` to build without AI debug records,
profiler sampling, math-validation macros or routine log output. This is an
opt-in performance experiment; the normal build keeps its diagnostic behavior.
It removes the 6,650,880-byte actor debug array without changing actor state,
render quality or the 30 Hz simulation clock. General engine assertions still
evaluate their expressions because some contain required game actions. Fatal
and delayed errors, port resource checks and the loading display remain active.
Timing used by dynamic resolution and frame pacing is also retained.

Use matching builds on both wireless peers. Release mode changes executable
layout and has a separate checkpoint/build identity; restart a mission rather
than carrying a raw checkpoint across modes. Keep the matching ELF/map when
sharing a test build. Routine logs are intentionally sparse in this mode.

The release guard regression test runs with a host compiler:
`python port/n3ds/tests/test_release.py -- clang -O2`.

### PC retail multiplayer status

PC retail v7 support is opt-in with `--experimental-pc-runtime`; the default
launcher still uses the original Xbox cache path. The opt-in build reads PC
maps from `halo-source/pc-retail`. Xbox and PC maps are explicit choices:
an unqualified map path means Xbox, while `pc\levels\test\name\name` means
PC. Clients must load the format selected by the host, with no silent fallback
to a different version. Missing or mismatched map identities prevent joining.
The `--unified-ui` flag keeps the original Xbox main menu and campaign while
exposing PC Local Multiplayer through SELECT when PC maps are installed. This is the combined launcher
used by `setup/Unified/`; an isolated emulator replay entered Xbox a10 from
the original campaign menu and selected PC Beaver Creek from the same 3DSX.
The build keeps the Xbox UI cache so the PC-only difficulty widget mismatch
does not affect Campaign. The PC-only launcher remains available for separate
testing.

For the PC-only UI build, original PC `ui.map` supplies the six expansion map
names, descriptions and graphics. The map-list adapter exposes an expansion
entry only when its UI assets and cache are present. The original thirteen Xbox entries stay defined;
the PC menu's Create Game action still does not launch a local match.

The System Link map selector lists installed Xbox and PC maps separately, with
PC entries labelled `(PC)`. Without PC maps, PC map choices, the local selector,
PC textures and campaign extras controls are hidden. The local PC selector is
also unavailable during a wireless session. PC wireless matches still need
paired hardware testing; compiling the unified build does not establish
compatibility for every PC map.

On the PC opt-in build, press **SELECT** while the main menu is open to show
the native Local Multiplayer selector. Up/down chooses Map, Mode, Bots,
Difficulty or, on New 3DS, PC textures. Left/right changes the selected value;
A starts and B returns. Touching Difficulty cycles Easy, Normal and Hard for
ordinary bots. Touching the PC textures row changes and saves it before launch.
X or the bottom right Gear button opens the weapon and vehicle set page.
Normal preserves the stock PC map item collections; PC-only Flamethrowers and
Fuel Rods sets replace weapon pickups. Vehicle choices include the original
mode default and All PC vehicles. PC maps with stock Flamethrower collections
now keep those weapons instead of applying the Xbox Flamethrower to Rocket
Launcher remap. The stock PC maps choose their own placements; Beaver Creek
does not place a Flamethrower, while the special PC-only set can add one there.
The choice applies on the next map load. It lists all nineteen
stock PC multiplayer maps, the five original modes and zero to fifteen bots.
It uses the original local game queue and map loader. The selector supplies
the bot count for that match; `bots.txt` remains the fallback for other launch
paths. The PC `Create Game â†’ LAN` widget does not currently start a local
match. An isolated emulator input replay selected Blood Gulch CTF with two
bots from the selector, loaded the PC cache, registered both bots and captured
an active gameplay frame. This is a local single-console match, not LAN play.
The same replay succeeded with the isolated emulator's player profile set aside,
so a saved profile is not required to enter this local match.
ChaoticSPARTAN1 and KodeBreaker16 are independent named bot rolls, each with
a 50% match chance when at least two AI slots are available. Both can appear
together. KodeBreaker16 has a 25% chance on each spawn to receive the game's
timed active camouflage powerup for 60 seconds. His armor uses the original
teal profile color. ChaoticSPARTAN1 keeps the green profile color and has a
10% active camouflage chance per spawn.

During multiplayer, SELECT displays the original scoreboard. Hold the
lower-screen SCORE panel for the expanded kills/deaths/medals roster; this
also works in wireless matches. The
Controller Settings screen offers a saved X/Y button swap and an optional
bottom-screen FPS readout. Switch X/Y starts on for new profiles; FPS starts
off. Saved profiles keep their chosen values. The Decals switch starts on and
controls creation and drawing of surface marks without changing their authored
lifetimes. Weapon meters use compact bars below the ammo count to remain
legible at the 3DS resolution. Magazine weapons read loaded and reserve rounds
from the live original weapon datum for the lower-screen ammo label.
After a local multiplayer match ends, the upper part of the bottom screen
shows Return to Lobby. Tap it or press B to reopen shared match settings with
the successfully launched Xbox/PC map, full gametype, bot difficulty and gear
choices. Cancelled edits do not replace that snapshot. Wireless matches use
their existing network lobby flow.

The multiplayer map browser reads the owner's selected Xbox or PC cache for a
disposable 3D preview. The map page samples broad BSP render surfaces and
the original collision BSP's walkable surfaces (pathfinding flag `0x40`),
with a blue alpha-blended overlay that the bottom-screen NAV button toggles.
After choosing a map and gametype, press X or tap **Gear previews** in the
shared local match settings. The gear page previews an example weapon or
vehicle from the selected set, using the chosen Xbox or PC map directly.
Xbox-only installs need no PC assets. A/B/X returns to settings without
starting the match; weapon and vehicle choices stay synchronized between
the two pages. Flamethrower/Fuel Rod presets are offered only for PC maps.
Unavailable previews do not prevent selecting settings or starting a match.
Touch dragging
rotates the camera, L/R adjusts distance, and idle frames advance a slow
turntable. Render geometry is capped at 21,840 triangles (8,192 for Xbox maps
on Original models), with up to 9,600 navigation triangles.

After selection settles, a low-priority worker reads and decodes the preview.
Scrolling cancels obsolete work; it never joins the worker or reads map data
inside the draw call. Large BSP and bitmap reads check cancellation every
16 KiB. The renderer publishes only the current selection, flushing at most
64 KiB of geometry or uploading one texture per frame. PC-only top-screen
thumbnails come from the owner's PC `ui.map`/`bitmaps.map`; their 128-by-128
texture is uploaded separately before the 64-by-64 mesh textures. Cancellation
invalidates both the geometry and the thumbnail. Low-memory
or missing-data failures leave the map selectable without a preview.
Worker completion and GPU frame completion both precede resource disposal;
map teardown drains any remaining cancelled worker. The preview camera clears
only the bottom target, preserving the top-screen menu background.
`test_preview_jobs.py` covers cancellation, coalescing, publication, bounded
uploads and failure handling; `test_preview_reader.py` covers both cache
formats, truncated data and chunk cancellation. `test_preview_models.py`
covers Xbox compressed/uncompressed and PC model vertices, streamed region
references, invalid buffers and indices. `test_local_match_menu.py` covers
gear navigation, shared choices, PC-only presets and cancellation.
Isolated Azahar captures showed Blood Gulch with the nav overlay on and off,
plus a pistol and vehicle preview on the gear page. A separate selector sweep
loaded geometry and walkable overlays on all 19 stock PC multiplayer maps
without a fatal error. An arsenal match capture showed the
assault rifle's lower-screen ammo label at 44 loaded and 180 reserve rounds.
Preview responsiveness and readability still need device checks.

For bounded local probes, `--diagnostic-pc-play MAP` accepts the nineteen stock
PC multiplayer maps. `--diagnostic-pc-mode` selects Slayer, CTF, Oddball, King
or Race; `--diagnostic-pc-bots N` adds one to fifteen local bots. The isolated
Azahar smoke sweep entered active Slayer matches with two bots on all nineteen
stock PC multiplayer maps. Short runs also exercised the five modes on Blood
Gulch and Timberland, including bot routes and a Warthog driver. A runtime
matrix fixture additionally reached frame 300 with two bots and routes on
Gephyrophobia CTF, Death Island Oddball, Ice Fields King, Infinity Race and
Timberland Slayer. PC Blood Gulch CTF registered fifteen of fifteen bots,
maintained sixteen active bipeds through frame 600, and generated routes.
An Xbox Blood Gulch CTF diagnostic likewise registered fifteen of fifteen
bots, generated routes and reached frame 1591 without a fatal error. These
checks do not establish full match completion, scoring, every mode and map
combination, or New 3DS hardware performance.

PC retail sound samples can reside in `sounds.map`. The sound cache validates
the offset against the active PC resource file before allocating a sample;
the Xbox path still uses its map bounds. A diagnostic Timberland run changed
from zero DSP submissions to queued and submitted audio in Azahar 2126.2.
Listening and frame timing on the physical New 3DS XL remain to be checked.

PC retail model streams use uncompressed vertices. The native model cache now
borrows checked map-owned streams and prepares GPU inputs once, while dynamic
streams keep their existing path. In identical two-bot Timberland emulator
scenes, steady render time fell from about 67.6 to 19.5 ms per frame and world
object draw from about 43.5 to 3.1 ms. A six-bot run measured about 19.8 ms
render and 9.6 ms simulation at match time 180 and continued past frame 1700.
The static upper portion of the captured scene matched the earlier build
pixel for pixel. Blood Gulch with two bots and a scripted plasma pistol shot
queued weapon and impact audio, and measured about 29 ms render and 9 ms
simulation at match time 180. Map costs differ; hardware results are still
required.

The New3DS particle budget now caches each particle definition's combat
classification for the current map generation. In the same six-bot Timberland
Azahar fixture, the late 300-frame window measured 33.66 ms before and
33.13 ms after this change; bot state at match time 1200 matched. The
`--diagnostic-particle-profile` build option traces animation and physics
timing without changing the release behavior. These emulator timings do not
establish 30 FPS on hardware.

`--diagnostic-pc-matrix` builds the runtime matrix fixture. In a private test
SD, `halo-source/pc-matrix.txt` contains one stock map name and one mode, such
as `gephyrophobia ctf`. The fixture rejects unknown names and runs with two
bots. It is meant for repeated emulator checks with one identical binary; the
normal launcher does not read this file.

`--diagnostic-pc-arsenal` exercises ten map-owned Blood Gulch weapons through
the original input path, then the PC Rocket Warthog gunner seat. The isolated
emulator recorded an advanced `game_time_last_fired` for the Assault Rifle,
Pistol, Shotgun, Plasma Rifle, Plasma Pistol, Needler, Sniper Rifle,
Flamethrower, multiplayer Fuel Rod Gun, Rocket Launcher and the Rocket Warthog
mounted gun. Captures showed their visible effects, but effect fidelity and
damage behavior have not been compared exhaustively with the retail game.
`--diagnostic-pc-rocket-warthog` isolates the vehicle stage. These flags build
test executables and are not player release options.

The source for the original game rules remains in `source/`. The PC vehicle
filter is conditional on a PC cache: PC globals list six multiplayer vehicles,
including the Rocket Warthog, while the original Xbox filter only accepts the
first three. Xbox caches continue to use the original rule. PC runtime and
setup packaging remain experimental until broader gameplay, saved-state and
hardware validation are complete.

### Campaign additions in the unified build

The difficulty page draws a bottom-screen touch switch for optional PC weapons
and vehicles. `engine_campaign_options.c` starts enabled for testing and stores
the choice in the title save archive with a version and checksum. The switch
persists between levels and emulator restarts until the player changes it.
Co-op lobbies and network games suppress the control, object spawning and PC
tag imports regardless of the saved preference. Returning to offline play
restores the preference without rewriting it.
The Xbox scenario files and original object placements stay intact. At runtime,
`engine_campaign_extras.c` creates a Flamethrower beside the c20 shotgun, a
Fuel Rod Gun beside c40 pickups, and both weapons beside d20 pickups. The
module waits for the player to approach and for the active BSP to contain each
point. Its placed state is registered with game-state checkpoints; disabling
the switch removes only an uncollected extra pickup.

Xbox campaign tags already contain those hidden weapon definitions. The
Rocket Warthog does not occur in any campaign cache. For b30 only,
`engine_campaign_overlay.c` reads the owner's PC retail Blood Gulch cache,
imports the 16 missing Rocket Warthog definitions, remaps references to Xbox
definitions where possible, and uses the PC HUD reticle bitmap because its
17 sequences include the Rocket Warthog crosshair. The original Xbox b30 cache
is not written. If PC Blood Gulch or a dependency check fails, b30 continues
without the extra vehicle. The b30 overlay currently retains a full converted
PC tag arena and model buffer; profile its memory and load time on hardware
before broadening this mechanism to other campaign maps.

The isolated Azahar check created all four campaign pickups in valid clusters.
In b30, the Rocket Warthog appeared beside the original vehicle. After the
opening transport released the player, the player entered its gunner seat and
its mounted gun advanced `game_time_last_fired` under the normal input path.
A separate emulator replay exited a resumed vehicle checkpoint and used the
normal X-button action to enter its driver seat; the parent vehicle datum
matched the extra Warthog. Longer mission play, checkpoint restore edge cases,
and physical New 3DS XL performance are still in the verification queue.

## Building the setup tools

The setup interfaces prepare 3DS game data using the shared converter in
`port/n3ds/setup/Core/`.

The cross-platform command-line interface in `port/n3ds/setup/Cli/` also uses
this converter. See the [CLI guide](SETUP-CLI.md) for guided setup, commands,
tests and self-contained Windows/Linux/macOS packaging. It requires .NET SDK 10
for development, without the Windows UI workloads. Run both `Tests` and
`Cli.Tests` after changing shared setup code.

The integration GUI builds (1.20.0) can optionally copy an installed retail Halo PC
`maps` folder, or a game folder containing `maps`, through `Core/PcMaps.cs`.
Both Windows interfaces use the same native C# importer; no PowerShell or external
extractor is needed for this folder path. PC ISO extraction remains available in
the separate Unified/PC scripts. The general cross-platform CLI is unchanged.

A fresh installation still needs Xbox data. The optional PC import copies the
supported retail v7 multiplayer maps plus `ui.map`, `bitmaps.map`, `sounds.map`
and optional `loc.map` to `halo-source/pc-retail`. It checks headers, names and
bounds, then hashes each source and verifies its copied bytes. These checks do
not certify that every supplied PC map is an unmodified retail asset. No PC game
data or executables are bundled. Xbox files and saves are kept; identical PC
files are skipped and conflicting installed PC files are rejected. Cancellation
removes the active partial copy; rerunning completes any remaining files.

The single EXEs built for this integration include the unified game launcher.
When packaging locally, pass `build/integration-unified/Halo3DS.3dsx` as
`-GameBuild` to retain PC and Xbox support. An Xbox-only build override cannot
use the optional PC data. `Tests/PcMapsTests.cs` runs on both installer runtimes;
run the core tests, CLI tests and Windows 7 safety/package tests after changes.

### Visual Studio

Open **File > Open > Project/Solution** and choose the solution for the edition
you want to build:

| Edition | Solution | Configuration | Startup project |
| --- | --- | --- | --- |
| Windows 10/11 | [Halo3DS.Setup.WinUI.sln](../port/n3ds/setup/Halo3DS.Setup.WinUI.sln) | Debug / x64 | `Halo3DS.Setup` |
| Windows 7 | [Halo3DS.Setup.Windows7.sln](../port/n3ds/setup/Halo3DS.Setup.Windows7.sln) | Debug / x86 | `Halo3DS.Setup.Windows7` |

For WinUI, install Visual Studio 2026 (18.x or newer) with the **WinUI application
development** workload, .NET 10 and Windows SDK tools. See Microsoft's
[WinUI tools guide](https://learn.microsoft.com/en-us/windows/apps/get-started/start-here)
and [.NET/Visual Studio support matrix](https://learn.microsoft.com/en-us/dotnet/core/porting/versioning-sdk-msbuild-vs).
For the Windows 7 solution, install the **.NET desktop development** workload,
the .NET SDK, .NET 3.5 reference assemblies and Python 3 on your development PC.

1. In `port/n3ds/setup/`, copy [Setup.local.props.example](../port/n3ds/setup/Setup.local.props.example)
   to `Setup.local.props`. Set `GameBuild` to the full path of your
   `Halo3DS.3dsx`, and `PythonExecutable` to `python` or your Python executable's
   full path. This local settings file is excluded from Git and source packages.
2. In Solution Explorer, right-click the startup project listed above and choose
   **Set as Startup Project**. Select the configuration and platform in the toolbar.
3. For WinUI, allow NuGet packages to restore. Choose **Build > Build Solution**
   (**Ctrl + Shift + B**) to build either edition.
4. For WinUI, press **F5** to run, then select your game executable under
   **Additional options**. A normal IDE build does not bundle the game.
   For Windows 7, run `build/setup-win7/Debug/Halo3DS Setup Windows 7.exe`
   from the repository root; this build already embeds your selected game.

Switch **Debug** to **Release** for a release build. The Windows 7 EXE will be
under `build/setup-win7/Release/`. Its solution calls the same build script as
command-line packaging. For a single-file WinUI release, use the
[packaging command below](#windows-1011-release); the bundle launcher is excluded
from normal solution builds because it needs a packaged ZIP.

### Windows 10/11 release

Build on Windows with .NET SDK 10 and Windows SDK build tools:

```powershell
dotnet publish port/n3ds/setup/App -c Release -r win-x64 -p:Platform=x64 -o build/setup
```

`dotnet publish App` produces a folder of files. For the distributable single
EXE, run the packaging script instead, using a new output directory:

```powershell
./port/n3ds/setup/tools/package.ps1 `
  -Output C:/build/halo-setup `
  -GameBuild C:/build/Halo3DS.3dsx `
  -VCRuntimeDirectory 'C:/path/to/VC/Redist/MSVC/version/x64/Microsoft.VC143.CRT'
```

### Windows 7 release

Build dependencies are the .NET SDK's Roslyn compiler, .NET 3.5 reference
assemblies and Python 3. The finished x86 EXE runs on 32-bit or 64-bit Windows 7
with its .NET Framework 3.5.1 component enabled. It does not ship modern .NET,
WinUI or Visual C++ runtimes.

```powershell
./port/n3ds/setup/Windows7/build.ps1 `
  -GameBuild C:/build/Halo3DS.3dsx `
  -Output C:/build/halo-setup-win7 `
  -Python python
```

Output must be outside the setup source tree. Distribute only
`Halo3DS Setup Windows 7.exe`; `LegacyTests.exe` and `Source.zip` are developer
outputs. The app's **Source / licenses** button can export its embedded source ZIP.

`Windows7/adapt-core.py` generates `Windows7/Core/` from the shared core.
Edit conversion logic in `Core/`, not generated copies. `Compatibility.cs`
provides bounded reads, SHA-256, JSON, raw DEFLATE behind a validated zlib header,
and same-volume replacement using `MoveFileExW`. The compiler uses `/nostdlib`,
only .NET 2.0/3.5 references, `/codepage:65001`, a Windows 6.0 PE subsystem
minimum and a DPI-aware manifest.

### Setup source and release checks

The public unified release ZIP is ready to install with a player's own supported
Xbox and PC retail sources. It includes the 3DSX, the Xbox installer, the PC
installer, the unified entry script, the runtime hash manifest, corresponding
source archives, and license notices. Players do not need a game compiler or
Xbox SDK to use it. The game files themselves are never bundled.

To make a new asset-free package from a checked-out commit and the binaries in
an existing release ZIP, extract that ZIP to a temporary folder and run:

```powershell
$release = 'C:/Downloads/Halo3DS-Unified.zip'
$stage = 'build/unified-repack'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Expand-Archive -LiteralPath $release -DestinationPath "$stage/input"
git archive --format=zip "--output=$stage/Halo3DS-Source.zip" HEAD
./port/n3ds/setup/Unified/Package-Halo3DS-Unified.ps1 `
  -XboxInstallerPath "$stage/input/Halo3DS-Xbox-Install.exe" `
  -XboxSourceZip "$stage/input/Xbox-Setup-Source.zip" `
  -SourceSnapshotZip "$stage/Halo3DS-Source.zip" `
  -UnifiedLauncherPath "$stage/input/Halo3DS.3dsx" `
  -RuntimeManifest "$stage/input/unified-runtime.json" `
  -OutputZip "$stage/Halo3DS-Unified.zip"
```

The package script checks both executable signatures,
the launcher-manifest hash, required source files, and its fixed output list;
it refuses an existing output ZIP. This repacks the supplied binaries and the
current tracked source. To rebuild the binaries, use the game build instructions
above and the Windows 7 setup build command below. That development path needs
the documented toolchains and locally supplied XDK declarations; no restricted
SDK files are included in the repository or release.

The setup source is organized as follows:

| Directory under `port/n3ds/setup/` | Purpose |
| --- | --- |
| `App/`, `Windows7/` | WinUI and Windows Forms interfaces |
| `Core/` | XDVDFS reads, map conversion, HUD/loading extraction, validation and installation |
| `Tests/` | Safety checks and local-data integration tests |
| `Resources/` | Supported-file hashes and format descriptions |
| `Launcher/` | Self-contained WinUI bundle launcher |
| `tools/` | Packaging and developer-only metadata generation |

`tools/source-files.ps1` defines the source files shared by both packages.
Packaged sources preserve repository-relative paths and include this document
and the main README. They contain the setup utility, not the complete game
source; use the repository to build the game. Python is never needed at runtime.

The local dual-ISO package uses `Dual/Install-Halo3DS-Both.ps1` as one entry
point. `Dual/Package-Halo3DS-Both.ps1` builds a fixed-file ZIP containing the
Xbox console setup tool with its embedded normal launcher, the experimental PC
launcher and hash manifest, the PC extraction scripts, corresponding Xbox setup
source, and notices. The user supplies both ISOs at install time. Package
round trips must inspect the ZIP allowlist, exercise `-WhatIf` from an extracted
copy, and verify both launcher hashes in an isolated output folder. The current
local test used the supported Xbox XISO and a small PC retail-format ISO
fixture. The dual installer also accepts a complete owner PC retail `maps`
folder with `-PcMapsFolder`. An installation from the supported Xbox ISO and
a complete extracted PC retail map set verified 24 existing Xbox maps and
32 PC map/resource files on a FAT32 SD card. Both launcher hashes matched
the release candidates; existing saves and demo folders remained.
The physical New 3DS XL runtime test and a full PC ISO extraction remain open.

Verify the game hash extracted from both final EXEs. Check that source archives
contain the converter, adapters, solutions, scripts, schemas and notices, but no
maps, Xbox executables, generated relocation files, SDK headers or DSP firmware.
The Invader schemas are GPL-3.0-only, revision
`a497b7457640dc2ee99fe8ad480d669e57347ef1`; preserve the
[component notices](../port/n3ds/setup/THIRD-PARTY.md) and corresponding setup source.

Run the asset-free modern tests above and these legacy modes. The last argument
must name a fresh test directory:

```text
LegacyTests.exe safety OUTPUT
LegacyTests.exe package "Halo3DS Setup Windows 7.exe" Halo3DS.3dsx OUTPUT
LegacyTests.exe audit EXPANDED_MAPS OUTPUT
LegacyTests.exe exercise XISO EXPANDED_MAPS Halo3DS.3dsx OUTPUT
```

The `package` check inspects the compiled interface for garbled text and verifies
its embedded game. Integration checks cover compressed/expanded inputs, all 24
maps, cancellation, repair, repeat installs, launcher updates and save preservation.
These have been exercised on the development PC; the legacy app has run under
CLR `2.0.50727.9179` in a 32-bit process. An actual Windows 7 OS/VM test and a
full Linux/Wine runtime test have not been performed.

## Setup formats and recovery

Official setup support covers the USA original, Rev 1 and Rev 2 XISO images
listed at <https://archive.org/details/halo-combat-evolved-usa-rev-2.xiso>.
`Data.DiscWarning` identifies those images by size and SHA-1; other images and
unverified extracted folders receive a compatibility warning. Xbox cache format,
map identity, decompression limits, tag/relocation bounds and destination read-back
checks remain mandatory. Recognized maps retain the catalog's SHA-256 checks;
other version-5 maps are prepared from their own tag data. This allows testing
other revisions, not a promise that their content will work in the engine.
The three supported discs have identical headers for all 24 maps and use build
`01.10.12.2276`, but have different XBEs. HUD text and loading artwork are located
by content fingerprints rather than fixed executable offsets. No game content is
bundled. The optional PC section accepts retail PC multiplayer files separately;
PC/MCC disc images cannot be used as the Xbox base.
A complete Xbox dump has 24 maps: `ui`, 10 campaign and 13 multiplayer.
A partial set needs `ui.map` and at least one playable map.

- XISO input accepts a single `.iso` or `.xiso`, including recognized game
  partition offsets. Extract ZIP/7z files and join split images first. Images
  are read directly, never mounted. The format was checked against
  [XboxDev's extract-xiso](https://github.com/XboxDev/extract-xiso/blob/master/extract-xiso.c);
  that extractor is not linked or executed.
- Folder input searches the maps folder or its parent game directory for the
  matching `default.xbe`. It supplies the HUD strings and optional loading art;
  it is never executed or installed. Maps-only input can use an existing
  `ntsc2276-hud-strings.bin`, including a verified copy on the destination.
- Setup creates `halo-source/ntsc2276-loading.bin` from a supported XBE when
  available. Existing artwork alongside maps is also accepted. Without it,
  text-only loading still works. A normal setup run adds artwork while skipping
  correct maps; a launcher-only update does not extract assets.
- A normal output folder can be used instead of a card. Copy its `3ds` and
  `halo-source` folders to the card root afterward. Under Wine, selected paths
  and their ancestors may be links/mapped drives; links inside the destination
  are rejected.
- The DSP check only tests for a nonempty `3ds/dspfirm.cdc`. It does not verify
  firmware authenticity or download firmware. Setup provides Rosalina directions.

Rerunning full setup verifies and skips correct maps, repairs damaged groups,
and keeps `halo-source/state`, other homebrew and unrelated files. A legacy
`HaloSourceEngine.3dsx` is backed up under `halo-source/launcher-backups` so it
doesn't remain as a duplicate Homebrew Launcher entry.

Cancellation keeps completed maps and removes the current temporary map.
After a disconnect or power loss, rerun setup before launching. Exceptional
replacement failures can leave `.halo-setup-*` recovery folders; these are not
silently deleted. Reads and conversion are bounded; source, expanded and
relocation hashes are checked and destination files are read back before commit.
Setup logs remain in `%LOCALAPPDATA%\Halo3DS Setup\Logs`; nothing is uploaded.

## Engine and platform boundary

`source/` retains the upstream engine's subsystem layout: AI, physics, objects,
game state, scripts, rendering and tag definitions. Keeping this organization
makes upstream comparisons and crash diagnosis easier. The native implementations
live under `port/n3ds/`; they are not a separate reimplementation of gameplay.

The Xbox data layout remains 32-bit with 16-bit wide characters. Engine units are
compiled with Clang's MSVC extensions; native libctru/citro3d units use devkitARM.
The two compiler groups are explicit in `sources.json`. Do not casually change
signed-char, short-wchar, common-symbol, floating-point or alignment options.

`compat/` contains the small CRT and weak-global definitions still needed from
upstream. `tools/compat.py` copies locally supplied XDK declarations into the
build directory, replaces three x86-only shift helpers with C, and generates
the forward declarations/weak-inline annotations required by the original C
linkage rules. It does not ship SDK headers or invoke Xbox code.

### Keeping upstream fixes

Keep the GitHub `main` branch as the base of the local integration branch. Put
PC cache conversion, device backends, and diagnostics under `port/n3ds/`.
Changes to `source/` should be small, conditional access points for port
features; use the original game code for its match rules, weapons, vehicles,
collision and scoring. Check that boundary with
`git diff --name-only origin/main -- source` before integrating a release.

For a new upstream revision, fetch `origin/main`, review its commits and the
`source/` diff, merge it into the integration branch, then rebuild the normal
launcher and rerun the PC map probes and multiplayer diagnostics. Resolve any
overlap in the small integration hooks in favor of the upstream behavior.
Keep generated binaries and owner supplied game files outside Git. This source
merge workflow lets the PC port receive upstream fixes without replacing the
original engine with a parallel gameplay implementation.

Run `port/n3ds/tools/prepare_upstream_update.ps1` from `local-integration` to
fetch and compare upstream. When new commits exist, it creates a sibling
`Halo3DS-Upstream-Review-<commit>` worktree and merges there. The working branch
is untouched, including if there are merge conflicts. In the review worktree,
inspect `git diff origin/main -- source`, build both the default Xbox and
`--experimental-pc-runtime` launchers, and repeat the relevant emulator and
installer checks. Only after those pass, return to `local-integration` and run
`git merge --ff-only upstream-review-<commit>`. If that fast-forward fails,
new local work has appeared; refresh the review branch and test again. Never
use `reset --hard`, force push, or rebase the integration branch to pick up
upstream changes.

## Where to make changes

| Area | Main owners |
| --- | --- |
| Menu and game lifecycle | `engine_frontend_boot.c`, `engine_main.c`, `engine_mission_render.c` |
| GPU submission and materials | `engine_gpu_renderer.c`, `engine_gpu_chicago.c`, `engine_gpu_widgets.c` |
| Models and scenery | `engine_models.c` and its model/scenery helpers |
| Textures and map loading | `engine_textures.c`, `engine_cache.c`, `cache_reader.c` |
| HUD, touch aim and controller settings | `engine_hud.c`, `engine_input.c`, `engine_controls.c` |
| Audio and worker jobs | `engine_audio_platform.c`, `engine_parallel_platform.c` |
| Persistent game state | `game_state_n3ds.c`, `engine_persistent_state.c`, checkpoint helpers |
| Asset preparation | `setup/Core/`; Windows 7 adaptations are generated from it |

The `.inl` files are deliberate pieces of an owning translation unit, often
sharing private renderer state. They are not independent libraries. Consolidate
only when responsibilities match; combining unrelated material paths into a
larger file does not simplify their state and memory contracts.

### Captured screenshots

The `halo-native-*-front.rgb` diagnostic files contain libctru's sideways
`GSP_BGR8_OES` framebuffer bytes. Decode the bytes as BGR and rotate 90 degrees
when making a PNG, for example:

```sh
python port/n3ds/tools/decode_front_capture.py halo-native-front.rgb screenshot.png
```

Pass `--bottom` for a bottom screen capture. Reading these bytes as RGB swaps
red and blue, making ordinary Blood Gulch terrain appear blue and its sky pink.

## Rendering rules

- PICA200 arithmetic has less precision than CPU floats. Model palettes, CPU
  model uploads, glow sprites and particle transforms use camera-relative
  coordinates. Keep shared animation/pose data in world space and rebase upload
  copies, using the current eye's camera. UI vertices remain in screen space.
- Depth and color passes must use the same pose and coordinate system. Lighting,
  projective texture coordinates and fog must be calculated in their matching
  coordinate system before rebasing. Repeated draws must not rebase source data
  in place.
- GPU commands retain vertex/index addresses until completion. Do not wrap,
  free or rewrite queued storage. Texture/vertex changes must respect cache
  flushes and the existing frame barriers.
- Restore material/sampler state and invalidate the submission caches when a
  different path changes their assumptions. Native combiners and lighting can
  avoid passes, but hardware limits still constrain each material.
- Flag cloth and King hill geometry can open a model scope inside the object
  renderer's scope. Keep the outer GPU batch active, flush queued scenery
  before the inner draw, and restore batching afterward. A long Timberland
  CTF emulator run caught the earlier assertion when a bot carried a flag;
  the nested-scope path continued past that frame after the fix.
- Original models have smaller memory and rendering budgets. Use model checks;
  do not lower New-model quality to fix an Original-model allocation failure.

## Original-model optimization packs

The setup tool generates optional `halo-source/<map>.hld` files from the user's
expanded Xbox maps. These contain vertex remaps and triangle lists for smaller
render meshes. The maps, collision geometry, skeletons and gameplay data stay
unchanged. New 3DS ignores the packs; first-person weapons, UI, skies and special
effect materials also retain their existing geometry.

Simplification preserves open boundaries, duplicate-position seams and bone
palettes, limits weight/UV changes and checks topology and triangle orientation.
Only meshes saving at least 10% of both vertices and triangles are included.
Packs are bounded to 512 KiB per map; the runtime's compacted vertex cache is
bounded to 256 KiB. Missing, incompatible or damaged packs fall back to the
original geometry. Packs are read at map load, not during rendering. Keep the
pack format version synchronized between `setup/Core/ModelLods.cs` and
`include/engine_model_lod.inl` when changing it.

Replacing the 3DSX is enough for the collision, pose and effects changes. To add
the geometry packs, run normal setup again with the source maps or XISO; it
keeps verified installed maps and preserves saves/settings. **Update game only**
does not generate packs. PC maps currently retain their existing geometry.

Both model families reuse exact stationary poses and bounded collision BSP
traversals. The collision cache omits only branches outside an enclosing sphere;
remaining planes, breakable surfaces and moving objects keep their original
tests. Map/BSP changes clear these caches. Original models additionally reuse
eligible distant off-axis base poses on alternate ticks and reduce decorative
particle simulation/density. Players, scripted clocks and gameplay projectiles
remain at the original simulation rate. New-model effect and geometry quality
are unchanged.

Physical Original and New-model runs are required to establish FPS.

## Stability and timing rules

Compressed animation streams can contain unaligned floats. Keep byte-safe loads
and aligned local results; casting packed data to float/vector pointers can cause
ARM11 faults. Use typed, aligned collision scratch structures for the same reason.

Gameplay state, scripts and rendering have shared ownership. Worker jobs operate
on disjoint data with explicit lifetime fences; do not move arbitrary AI or object
mutations to another core. Cache keys must include pose/instance mutations and
must not survive invalidated map or tag storage.

CPU animation phase/floor helpers preserve float results rather than lowering
animation frequency. The model-name cache stores immutable marker/node indices,
checks the complete name, separates marker and node searches, and invalidates
by map-cache generation. It does not cache live attachment matrices.

Checkpoint capture shares immutable compressed blocks, uses aligned word reads
to detect empty arena blocks, and reuses one LZ4 workspace. Pointer relocation records use a bounded two-pass
radix sort, with the comparison sort retained as an allocation-failure fallback. Equality of nonempty
blocks requires matching compressed bytes, not just a hash. Reference counts are
owned by the main thread; SD workers only read retained snapshots. The original
1.5 MiB packed snapshot cap remains on Original models, with the existing disk
fallback when memory is insufficient. Selecting a profile pre-resolves its checkpoint directory and invalidates any
previous cached path, including reuse of the same numeric profile index. This
avoids on-card profile-index reads at each checkpoint. Background writes release the file lock
between at most 8 KiB chunks. This reduces contention; capture, filesystem flushes,
and a low-memory fallback can still cause pauses. The on-card save format is unchanged.

Run `python port/n3ds/tests/test_cpu_checkpoint.py -- clang -O2` in a host C compiler
environment (for Windows, a Visual Studio developer prompt). It covers exact
float results, model-name cache invalidation/collisions, immutable snapshot
ownership, corruption and allocation failures. Use the existing checkpoint
integration fixture as well for overlapping saves, revert, Save and Quit,
relaunch and backup recovery; the host codec tests alone do not verify SD I/O.

Checkpoint work uses bounded buffers, validated data and staged replacement.
Preserve the existing cancellation, shutdown, Save and Quit, and map-transition
ordering. A changed build identity may invalidate incompatible checkpoints;
preserving a file is not proof it is safe to resume in a different engine layout.

The dynamic-resolution policy must account for sustained load and recovery
delays. Do not react to every isolated slow frame: frequent resolution changes
can create visible oscillation and stalls. Keep UI resolution and world rendering
quality decisions separate.

New models retain the full-resolution baseline and reduce world resolution
only under GPU wait pressure. Original models start the world at 320x192 and
can trial 256x152 or 200x120 when GPU waits or sustained CPU rendering cost
justify it. Lower projected size also selects cheaper authored model LODs;
resolution alone cannot fix slow AI. Trials must demonstrate a timing benefit,
and recovery needs sustained headroom. Original world targets use RGB565 to
reduce VRAM use and color bandwidth, with format-correct framebuffer clears.
Menus and HUD remain full-resolution RGBA8 on both families. Map conversion
does not select these budgets: the same executable detects the console model.
Original gameplay also omits model reflection/detail textures, uses stricter
authored mesh LODs, thins disconnected foliage islands and limits decorative
particles. Base textures, cutouts, team colors, emission and native lighting
remain. These reductions do not change collision meshes or AI update rates.

Outdoor distance fog uses the PICA fog lookup table with linear W-depth; the
near plane remains unchanged. The same far plane rejects whole environment
batches and bounds-tested models before submission. A single background draw
blends the sky horizon into the fog color so culled terrain does not expose a
black sky underside. First-person weapons, sky materials and HUD overlays keep
their own depth/fog state. Premultiplied-alpha effects retain their authored fade.

Original models use 60 world units in Halo, Silent Cartographer and 343 Guilty
Spark, and 80 elsewhere; New models use 110 and 160. Cinematics have fixed longer
ranges (at least 100/150) on those three demanding maps. Other cinematics retain
their original range to preserve distant scripted set pieces. Menus, indoor
views and scopes are exempt.
Gameplay can trial two further 18% reductions under sustained rendering load,
with slow distance changes and long holds. Fog and resolution trials do not run
simultaneously. A fog trial that fails to improve measured work/preparation cost
is rejected. This affects rendering only: simulation, collision, enemy activity
and wireless state remain unchanged. Emulator timings are not hardware FPS.
Run `port/n3ds/tests/test_fog.py -- <host C compiler> [flags]` for controller
checks; bounded renderer diagnostics also exercise fog/depth/weapon GPU pixels.

## Circle Pad Pro input

The optional accessory driver is `engine_circle_pad_pro.c`. Enable it in
Controller Settings; it defaults off so normal play does not open `ir:USER` or
allocate a worker. New models retain the built-in IRRST C-stick as a fallback;
fresh CPP data takes priority when the option is enabled. CPP axes
use the same dead zone and Right-stick aim sensitivity as the C-stick; button
mapping and touch aiming are unchanged. The software option is available on
all models, but physical cradle fit and IR alignment on New models and the
Original 2DS are not assured. Nintendo only supports Original 3DS/XL cradles.

The worker owns IR IPC, one 4 KiB shared page and an 8 KiB stack. The game thread
only copies a locked sample. Calibration reads are checked for CRC, response
length/offset and finite scales; frames are decoded without unaligned loads.
Receive metadata and ring boundaries are validated before access. A missing
accessory retries after a one-second wait; unavailable IR does not fail boot.
Samples older than 200 ms are ignored. HOME/sleep clears input, invalidates the
session and reconnects after restore. Shutdown signals and joins the worker
before freeing its resources. No calibration-write commands are sent.

Circle Pad Pro originally used the extra field in controller settings version
2. The combined format is now version 6, with separate fields for Circle Pad
Pro, PC textures and effects quality; it reads older versions without changing
their effective face-button mapping. `circle_pad_pro_tests.c` runs
on a host compiler and covers calibration, CRC, packed axes and corrupt/wrapped
packets. Controller settings tests also cover version 1 migration and version 2
validation. A bounded Azahar test covers menu boot, emulated CPP calibration,
disable/re-enable and worker shutdown on both Original and New models. Hardware
calibration, reconnection, sleep/HOME and simultaneous UDS play still require an
actual accessory; emulator checks cannot validate the physical IR connection.

Protocol references: [Circle Pad Pro](https://3dbrew.org/wiki/Circle_Pad_Pro),
[IR shared memory](https://3dbrew.org/wiki/IRUSER_Shared_Memory) and
[initialization](https://3dbrew.org/wiki/IRUSER:InitializeIrnopShared).
The calibration field layout was cross-checked against the
[Red Viper driver](https://github.com/skyfloogle/red-viper/blob/master/source/3ds/cpp.c).
The driver here is a separate implementation with a bounded parser and explicit
thread/resource ownership.

## Local wireless multiplayer

System Link uses libctru UDS for nearby 3DS-to-3DS communication. It does not
use internet sockets or connect to Xbox consoles. The original Halo menus,
host/client managers, game variants, action updates and synchronization checks
remain in use. Native frame servicing runs the original network callbacks
before simulation and sends local updates afterward. Solo play does not
initialize the radio or allocate its queues.

The adapter has three parts:

- `engine_network.c` implements the original transport interface with
  session-local node addresses and nonblocking endpoints.
- `engine_wireless_protocol.c` implements bounded datagram queues and reliable
  byte streams, including fragmentation, acknowledgments, retries and timeouts.
  The explicit little-endian wire header is versioned and validated before use.
- `engine_wireless_platform.c` owns UDS discovery, connections and the radio
  worker. Game objects remain on the main thread; endpoint state is locked.
  The worker continues servicing packets while a map loads and has a bounded
  receive batch so it cannot monopolize the CPU.

The radio advertises an application ID, protocol marker and build fingerprint.
Only matching builds connect. Halo map settings carry an identity derived from
the validated prepared-map relocation header; clients check it before loading.
Normal map loading still performs the full relocation validation. These are
compatibility checks, not cryptographic authentication. The UDS passphrase is
a public application namespace, not a user password.

The initial limit is four consoles, one local player each. Discovery attaches
to the first compatible non-full radio host; the original game browser then
lists that host's session. It does not merge listings from multiple radio hosts.
Campaign co-op, online play and joining a match already in progress have not
been validated. Sleep/HOME suspension ends the local connection. Client and
server loading allowances are longer than Xbox defaults to accommodate slow
SD cards, while transport heartbeats still detect a silent peer.

Do not restore the original per-packet traffic spreadsheets: their synchronous
flushes cause SD I/O during gameplay. Session events use the existing engine log.

The asset-free protocol test can be built with a host C11 compiler, for example
from a shell configured for Clang and its system C runtime:

```sh
clang -std=c11 -Wall -Wextra -Werror -Iport/n3ds/include \
  port/n3ds/src/engine_wireless_protocol.c \
  port/n3ds/tests/src/wireless_protocol_tests.c -o build/wireless-tests
```

Run the resulting executable. Add `-fsanitize=address` when supported. Tests
cover packet loss, duplicate/reordered delivery, fragmentation, full/wrapped
queues, malformed packets, discovery before connection, loopback, reconnect,
peer loss and four-console stream isolation.

Integration testing uses separate emulator SD folders and a private local
Azahar room to carry emulated UDS traffic. Test-only controller injection and
simulation-state sampling must not be linked into the distributed executable.
Two New-model emulators have completed discovery, lobby join and Blood Gulch
loading, movement and charged plasma fire, with matching random-state samples
at 127 shared simulation ticks. Host disappearance and client menu exit were
also checked. A mixed New/Original-model emulator match loaded Blood Gulch,
rendered the other player and exercised movement and firing, with matching
random-state samples at 207 shared ticks. The Original-model client used
24 MiB ordinary and 32 MiB linear heap limits. These samples are not full
game-state checksums. That client also returned to the menu, released its
wireless buffers and loaded Pillar of Autumn's opening cinematic offline.
This is not proof of radio reliability, four-console gameplay,
all game variants, or performance on physical consoles. Keep the wireless build
separate from the stable installers until hardware testing is complete.
The texture loader currently uploads one selected mip level per bitmap. On
Timberland the New-model 64-pixel world setting used about 2 MB of its 4 MB
gameplay texture cache after loading 123 textures. Citro3D supports mipmapped
textures, so a near/high and far/low experiment can use a bounded mip chain.
Measure cache occupancy, loading hitches, steady frame time and close-range
clarity before changing the release setting.

## Verification

### Multiplayer post-game lifecycle

The native frontend must call `game_engine_update_non_deterministic` once per
frame while a game is loaded, even when simulation ticks have stopped. This
advances the carnage report's five-second delay and handles the original
buttons: A/Start returns the host to match setup, B/Back leaves the session.
Leaving this call out strands the host on the report after the match ends.
Keep it outside the tick loop; it must not depend on incoming player actions.
Regression checks should cover the last guest leaving, a normal match end,
returning to match setup, and leaving for the main menu on both model families.

### Campaign co-op development

Campaign co-op is under development and is not part of the stable installers.
The target is two consoles, with one player on each. Multiplayer > Co-op opens
the wireless profile/browser flow with installed campaign maps and their
original preview images; the following selection chooses difficulty. System
Link retains competitive maps only. Both peers must use the same build and
map data.

Menu intent is separate from the selected map: it must survive profile and
lobby navigation, even before a host exists. Co-op and competitive rooms use
different UDS sub-IDs, so discovery cannot attach to a room from the other
menu. The radio worker uses a session-fixed sub-ID. Campaign thumbnails reuse
the existing `sp_levels` bitmap at render time without modifying map files.
Lobby machine names default to the CFG-reported model, including Original
3DS/XL and 2DS, with a neutral fallback when the model query fails.
The menu update was checked with an Original-model emulator hosting and a New
model joining: campaign previews matched the installed missions, the lobby
identified the host as Nintendo 3DS XL, and 169 shared simulation-state samples
matched through tick 1,232 after entering Assault on the Control Room. System
Link separately showed only the two installed competitive maps and their
previews. The Original peer also switched from Co-op to competitive discovery
without restarting. Physical model naming and wireless behavior still need
the corresponding hardware check.

The design reference is [OpenCE's networking notes](https://github.com/OpenCommunityEdition/OpenCE/blob/07302a866f8b36c162203fe58dcf632d4708d65c/port/linux/NETCODE.md)
(the project formerly called halo-ce-universal). OpenCE replaces Xbox lockstep
with host-authoritative AI, scripting and object replication. This prototype
retains the existing lockstep transport: both peers simulate campaign logic.
Camera timing and campaign services therefore follow shared simulation ticks,
and distance-based simulation shortcuts that depend on the local view are
disabled in co-op. Rendering optimizations remain local.

Audio-driven mouth animation is omitted in co-op. Its local playback timing
otherwise changes simulation bones and weapon attachment markers, allowing
combat to diverge even when player inputs match. Speech still plays, and solo
lip animation is unchanged. Fallen-body settling also uses a common tick
schedule on Original and New models.

Checkpoint snapshots stay local to each running session and do not overwrite
solo disk saves. Network update numbers must remain monotonic across a campaign
clock rewind. Skip and revert requests travel in reserved player-action bits,
which are stripped before applying unit controls. A pause menu does not pause
the other console. Restart returns the pair to the lobby; mission completion
selects the next campaign map there. These lifecycle paths require integration
validation before promoting a co-op build to stable.

The client must wait for a validated settings packet before reporting map
readiness. The pregame loop can run between the join acknowledgement and that
packet; using the menu's previous map at that point could enter the legacy
damaged-media loop.

Paired-emulator checks exercised PoA movement, shared cutscene skipping,
manual checkpoint restore and restarting through the lobby. A Silent
Cartographer combat run matched random-state and object-state samples through
5,700 network ticks, individual respawn, all-player death/checkpoint recovery,
and a forced mission-completion handoff to Assault on the Control Room.
An Original/New configuration pair, with 24 MiB ordinary and 32 MiB linear
heaps on the Original client, matched through another 3,300-tick run including
combat and all-player recovery. The detailed combat trace compared more than
540,000 object records, including weapon attachment bounds. Forced deaths and
completion exercise the real lifecycle functions but are not a campaign
playthrough. These tests do not establish physical-console performance.

A mixed-model Assault on the Control Room test crossed a loading zone from
BSP 3 to BSP 0, bringing the remote player across, with 691 matching state
samples through 3,330 network ticks and 265,478 matching detailed object records.
The probe used a collision-validated position inside the loading volume;
placing a player at the raw volume center was outside valid destination geometry
and correctly hit the original engine's missing-player assertion. Campaign maps
must open their campaign pause widget, since the competitive pause widget is
not included in their tag data.
The final mixed-model check opened that menu while both simulations continued,
matched 646 state samples through 1,980 ticks, and used the client's Save and
Quit entry to return both peers to the main menu without a fatal or sync error.
The original widget still says Save and Quit (and briefly shows its saving
message), but co-op exits without writing a persistent solo checkpoint.

Compare random-state and object-state samples at the same **network update
number**, not just campaign time: checkpoint reverts repeat campaign ticks.
Test independent movement, combat, doors, one-player respawn, all-player death,
manual checkpoint restore, BSP transitions, cutscene skips, mission completion,
leaving and mixed Original/New hardware. Idle cutscene synchronization alone
does not establish playable co-op. Never ship the input-injection fixture.

### Regression checklist

Test these paths after changes to memory, rendering, loading or controls:

1. Menu loading text/artwork, animation, text clarity, sound, profiles and settings.
2. Pillar of Autumn at a selected difficulty: opening, cryopod tutorial, touch
   aiming before the radar appears, and the Keyes cutscene.
3. Solo Blood Gulch: movement, aiming, pickup, firing, reload, sniper scope,
   flashlight/night vision, HUD and grenade selection.
4. Controller sensitivity/layout settings, Original-model L/R and touch input,
   and New-model C-stick and ZL/ZR input.
5. Stereo on/off on a 3D-capable model, pause/resume, Save and Quit, menu return,
   checkpoint resume across launches, BSP changes and HOME > Close.
6. Sustained campaign progression, dense scenery/AI/effects, The Maw timer,
   cinematic overlays and the particular scene affected by the change.

Use both model families, keeping a known-good executable. Compare the same
location/action, stereo state and controller settings. Emulator results are
useful for regressions and matched-workload timings, but are not hardware FPS
measurements or proof of campaign stability. Precision tests model PICA input
rounding because an emulator may retain more precision and hide a hardware error.

Native checks live in `port/n3ds/tests/`; some use `.inl` includes to access their
owning C file's private state. Release linking discards unused test functions.
Use the [diagnostic build options](#diagnostic-builds) for bounded runs and
user-supplied game data for local integration tests. Player log locations and
reporting steps are in the [README](../README.md#reporting-problems). Record
the exact executable hash, whether the issue repeats, and a photo if no dump exists.

When changing native code, keep a known-good build, record the new executable
hash, retain its ELF/map privately, and test both model families. Build outputs,
SDK headers, maps, saves and tester logs belong outside Git.

## Setup and repository maintenance

Change conversion logic in `setup/Core/`, then rebuild the Windows 7 adaptation
with its build script. Verify that both final setup EXEs embed the intended
3DSX; source version numbers alone do not prove the bundled payload is current.
Use UTF-8 explicitly in maintenance scripts. Legacy UI punctuation uses plain
text, with a compiled-interface regression check for the previous encoding bug.

Before distributing source, run `python tools/check_repository_contents.py --working-tree`,
then run it again without that flag after staging. `--git`
selects a Git executable if needed. This checks known exclusions and common
secret formats in the current tree/index, not all possible secrets or licensing
requirements. Only the current working tree or index is scanned.

The PC retail `ui.map` references event function indices beyond the original
Xbox table of 102 entries. `port/n3ds/tools/inspect_pc_ui_exit.py` can inspect
an owner's read-only PC `ui.map`; its Quit OK widget uniquely uses index 107.
The 3DS port maps that confirmed PC action to normal application shutdown.
Other PC-only menu actions need individual mapping and testing before relying
on them; the SELECT Local Multiplayer menu remains the tested PC match path.

On New 3DS, the bottom-screen Controller Settings page has a **PC high
textures** switch. It defaults OFF and saves with the other controller
settings. Turn it ON, save, then reload the PC map to compare 128-pixel
texture bases, a generated 64-pixel mip level, and an 8 MiB gameplay cache
against the standard 64-pixel profile. The original 3DS and Xbox maps keep
their established profile. Version 6 reads settings versions 1 through 5, then
upgrades on save. New independent X/Y and A/B swaps default OFF so they do not
cancel an existing Xbox-layout choice. Explicit swaps in versions 3 through 5
are retained. The experimental PC version 2 and dev version 2 used the same
bytes for different settings; that file cannot distinguish them. Migration
preserves dev's Circle Pad Pro value and leaves PC high textures OFF.

The **Effects** setting defaults to **Balanced**, retaining dev's particle
budgets and tracer behavior. **Quality** enables the contributor's fuller
combat particles, longer/more visible rifle trails and optional decals.
**Performance** reduces cosmetic particle budgets (New: 144 to 72; Original:
32 to 16) and omits model detail/reflection layers while preserving base textures,
alpha, team colors, lighting and emission. It retains the existing simulation
and networking rates. Its hardware frame-time benefit remains unmeasured. Both console generations default to Balanced; this
setting does not replace their separate texture and resolution budgets.

`python port/n3ds/tests/test_integration.py -- clang` runs host regressions for
settings versions 1 through 6, effective button mappings, independent fields,
quality policies, PC menu availability, format-qualified maps and co-op
blocking. On Windows, supply the compiler's SDK include and library paths if
not running from its developer command prompt.

`--diagnostic-high-textures` still
forces the experimental profile in a PC match fixture; `--diagnostic-texture-clip`
captures a repeatable Timberland walk for visual comparison.
PC lightmap atlases are linear RGB565 data, unlike the swizzled Xbox atlases.
The upload filter reads each layout correctly and removes red/yellow checker
padding each time a lightmap is loaded, including after texture cache eviction.

In the earlier opt-in six-bot Timberland experiment, Azahar reached 900 frames
without a fatal error, with about 7.7 MiB cached and a steady 18.6 ms render
interval at game time 900. Initial loading cost increased. New 3DS XL frame
time and memory headroom need physical measurement before considering a new
default.

For New-model PC multiplayer maps, the native cache now prepares seven small
gameplay HUD bitmap groups before the match. Damage arrows and reticle warnings
remain resident; other HUD images retain normal eviction. This adds about
340 KiB in the Timberland emulator fixture and removes a first-hit damage
arrow decode, but did not reduce the overall outdoor hitch count. Texture
eviction gives recently used actors, weapons, vehicles and effects a small
priority over scenery without evicting a visible surface to hold an old effect.

The native portal traversal caches a projected portal hull only for the
current visibility pass. It copies at most 24 used vertices and leaves larger
portals on the original path. Sampled cluster hashes matched the uncached
Xbox traversal at several Timberland camera positions. In the seven-bot
uncaptured Azahar route, outdoor visibility work fell by roughly 2.5â€“3.5 ms
per frame, while busy total frame work remained above 33 ms. This is an
emulator result, not a New 3DS XL frame-rate measurement. Conservative PVS
and rectangle traversal experiments were rejected: PVS increased object and
effect work, and the rectangle path omitted clusters found by the original.

An uncaptured seven-bot Timberland trace split the remaining simulation work.
The game update averaged about 15â€“20 ms in the outdoor route. Object updates
accounted for roughly 10â€“13 ms of the game tick, with moving vehicles,
projectiles and short corpse-physics spikes the largest type costs; 168 static
scenery objects together took about 0.3 ms in the sampled ticks. Particle
simulation added roughly 2â€“4 ms per frame. These are Azahar host timings and
do not establish physical New 3DS frame rate. The r35 build is staged for a
New 3DS XL frame-pacing log with the 128-pixel+mip texture profile.

A diagnostic single-ground-material 512-pixel test required a downgrade on
far respawn to avoid cache exhaustion and did not sustain the 30 FPS target.
The ordinary launcher retains the bounded texture profile. The guarded
near-world and smooth-promotion build options remain experiments, not a
replacement for the release texture policy.

In the unified PC match selector, **All PC vehicles** enables every authored
vehicle type and adds two Banshees each to Danger Canyon, Ice Fields and
Timberland. The added vehicles use assets already present in those retail PC
caches. The native runtime checks ground slope, nearby terrain, player starts
and existing vehicles before placing them. The isolated Azahar menu fixture
spawned both Banshees on all three maps without a fatal error. A Timberland
driver-entry check succeeded and a later capture showed the player seated in
the vehicle. A further scripted Timberland run stayed seated and moved about
26 m, briefly rising about 4 m. Danger Canyon moved more than 13 m and rose
about 4 m in its first 120 frames. Straight-ahead replay then met terrain;
the Ice Fields first placement met nearby scenery. These emulator checks show
the original driver and motion paths advance, but do not establish open-air
steering or physical New 3DS XL handling.

In a separate Danger Canyon driver-seat replay, the Banshee's authored
`mp_banshee gun` had two triggers. The primary trigger advanced its original
last-fired time from 0 to 299. After its cooldown, the secondary trigger
advanced that time to 379 and referenced a valid projectile tag. Both were
driven through the native controller and original player/vehicle weapon code.

The native flamethrower renderer now uses the first-person model's animated
`spawn fire` nozzle marker. It aims a short world-space sprite history from
that marker toward the camera reticle's collision point, then applies a
20-frame lifetime, downward acceleration, and fading alpha. The original
projectiles still determine gameplay; their displaced world-weapon sprite
positions are hidden for the local first-person view. A turn leaves older
flame behind instead of rotating the whole jet with the camera. In the
isolated Azahar arsenal replay, the marker resolved and all 11 weapon and
mounted-gun paths fired without a fatal error. Captured turn and Fuel Rod
frames showed flame lag and blast decay. The native game tick also advances
authored decal decay, and additive particles apply vertex alpha to their
light contribution so explosions fade. The
physical display and sustained frame rate still need checking.

An isolated Blood Gulch sniper fixture used a map-authored 136.84 m clear
sightline. At the shot's visible contrail sample, the original trail held 36
points whose first and last positions were 136.28 m apart. A six-frame contact
sheet also showed the long shot over the open field. This measures emulator
geometry and visuals; the device display still needs a distant-range check.

A moving six-bot Sidewinder Race fixture reached an authored transparent
effect that combines a screen-anchored secondary bitmap with premultiplied
alpha. The native two-map effect renderer now carries both texture alphas into
the framebuffer blend. The replay completed 241 captured frames without a
fatal error. GPU readback checks compare 50 secondary-effect cases, including
the premultiplied mode, against independently calculated pixels.

### Distant idle AI decisions

The 3DS decision scheduler in `source/ai/actors.c` keeps perception, damage and
danger handling, action timers, movement, and combat running every simulation
tick. For stationary hostile actors that have not entered combat, only the
decision-selection loop can run less often: every four ticks beyond 40 world
units on Original models (50 on New models), and every six ticks beyond 65
units (80 on New models). At the normal 30 Hz simulation rate, these are 7.5
and 5 decision checks per second. Actor indices stagger the checks.

The extra reduction excludes scripts, conversations, flying or moving actors,
targets, stimuli, damage, dangers, nearby fighting friends, and forced actors.
The nearest local player determines distance; friendly actors retain their
previous schedule. Cinematics and network games bypass decision budgeting.
Perception runs before the budget check, so newly detected threats restore
decision processing in the same tick. This is independent of fog and does not
freeze actors or alter saved-state layouts. It mainly reduces background idle
work, not the cost of enemies already fighting. `IDLE AI` log lines count
deferred decisions cumulatively for the process.

Run `python port/n3ds/tests/test_idle_ai.py -- clang -O2` from a Visual Studio
developer prompt to check cadence, staggering, wakeups, distance thresholds,
and protected states using the production scheduler with mocked engine inputs.

### Halo64 physics and culling experiment (November 2026)

The reference is [Halo64 at b629d0a](https://github.com/JackTulli/halo-64/tree/b629d0a),
including the optimization pass in `32fda59`. Its RSP/tiny3d matrix stack and
render-state changes cannot be copied directly to PICA200. The 3DS port already
has object-sphere/frustum tests, material-batch boxes, portal visibility and
cached visible index buffers; adding another identical culling stage would
duplicate work. This pass reuses the last rejecting box plane as a per-eye
ordering hint and computes absolute plane normals once per view. Every box is
still tested against the current view. No draw-distance or fog changes apply.

The collision collector now shares the object lookup with marking and tests
the common miss before calling the recursive collector, following Halo64's
approach. Objects with sibling chains still take the original path. Rejection
uses the original flags and enclosing sphere; it does not reduce collision
accuracy. `HALO_N3DS_COLLISION_PREFILTER_VERIFY` checks rejected roots against
the original collector in a diagnostic fixture.

Build with `--release --experimental-physics-15hz` to try selective 15 Hz
movement. This is **not** a 15 Hz game clock. Eligible grounded AI bipeds
run movement/contact physics every other tick, staggered by object
identity. Their next step covers the skipped tick: acceleration and movement
distance are scaled, and biped collision sweeps cover the entire distance.
Vehicles now always use the original 30 Hz integration, including offline
ground vehicles. Biped penetration/contact retries preserve the step duration.

Players on foot, projectiles, airborne bipeds, landing, jumping, crouching, melee,
special animations, elevators, flying vehicles and cinematics keep full-rate
movement. Wireless activity, co-op or any non-local game connection disables
the reduction on both console families. Input, scripts, weapon timers, AI
thinking and the network clock remain at 30 Hz. Eligible AI motion is
coarser; hardware playtesting is required before making this
the default. Transient movement credit is cleared on map initialization,
object reuse and checkpoint restore. It is not stored in the save format.

`--release` also strips diagnostic-only biped vector checks and root-pose
matrix checks, in addition to debug records, profiler sampling and routine
error/log formatting. Fatal/load-failure handling and required assertion
side effects remain. Do not use this stripped build to collect a full crash
report. The physics build option contributes to the build/checkpoint identity.

From a Visual Studio developer prompt, run
`python port/n3ds/tests/test_physics_schedule.py -- clang -O2 -ffp-contract=off`
for timestep accounting, identity reuse, rewinds, full-rate fallback and two
million per-eye culling comparisons. `test_release.py` checks diagnostic
removal and fatal/delayed error behavior. Encounter measurements must use the
same map, player route and build settings; emulator frame times are not
hardware FPS. Keep the pre-fog build and the previous release experiment as
separate baselines.

### Hardware test loop

With Wi-Fi enabled, open Homebrew Launcher and press Y for netloader. Upload
the normal playable build with Python (no additional Python packages needed):

```powershell
python port/n3ds/tools/hardware.py path/to/Halo3DS.3dsx --address CONSOLE_IP
```

Every upload replaces `sdmc:/3ds/Halo3DS/Halo3DS.3dsx`, regardless of its local
filename. Do not send separately named test executables with the default
3dslink destination: hbmenu saves those as additional launcher entries.

For a developer fixture that reports through `link3dsConnectToHost` after
measurement and then exits normally, add `--results path/to/new-result.txt`.
The receiver starts before upload, accepts results only from that console,
checks the final PASS/exit status, and records the executable SHA-256 beside
the result. Timeout, missing results and FAIL are failures, not measurements.
The required completion marker defaults to `RESULT: PASS encounter`; use
`--expect "RESULT: PASS menu idle and diagnostic gate"` for the menu fixture.
An initialization-only PASS does not count as completing the encounter.
The normal playable executable does not send this test report.

After a successful test returns to Homebrew Launcher, `--enter-netloader`
checks hbmenu's UDP discovery reply and, if needed, sends up to two bounded
Y presses through Rosalina InputRedirection. It releases the buttons and
requires a reply from the selected console before uploading. Use that option
only when the console is already in the launcher. Discovery replies go to
fixed UDP port 17491, not an ephemeral source port. A crash, powered-off
console or lost Wi-Fi can still require recovery on the device. The last
upload should always be the normal playable build.
Keep debugger attachment and screenshot/file/network transfers outside timed
windows. Verify the route, camera and active actors before accepting encounter
comparisons. `python port/n3ds/tests/test_hardware.py` checks the transfer and
result-validation protocol without connecting to a console.

The November hardware pass also keeps collision diagnostics disabled across
BSP loads, caches validated immutable tag references, and avoids restoring
world state between HUD quads inside a complete HUD pass. Xbox encoded sound
samples use one bounded background reader with a separate read-only handle;
map teardown cancels and joins it before releasing sample memory. PC sound
reads keep their existing path. Sound obstruction may reuse its result for
two subsequent ticks only while endpoints remain nearly stationary and the
clusters/BSP generation match. This does not affect AI sight or bullet tests.
The unsupported Xbox attract-movie transition is disabled on 3DS so it cannot
fade the menu music after 73.5 seconds.

Hardware checks on November 3 compared this candidate with the preceding
stripped, selective-physics test build, not the public release. The repeated
900-frame beach route measured 50.6 ms/frame versus 54.0 ms. The corrected
Halo route stays near the lifepod and triggers the Spirit through normal
mission volumes; its clean 1800-frame comparison measured 36.1 versus
38.4 ms/frame. Both Halo runs stayed at 200x120 world resolution. These are
modest gains and do not establish stable 30 FPS in heavy encounters.

The Halo candidate's GPU queue averaged 23.1 ms, with 10.5 ms waiting at
FrameBegin. Queue duration includes transfers and overlaps simulation;
do not add it to CPU stage timings or interpret it as utilization. There
is a CPU/GPU overlap problem to investigate as well as rendering cost.
Keep the existing fences until command, vertex, index and texture lifetimes
are explicitly safe for earlier submission or concurrent preparation.
The lighter route beyond the lifepod and exploratory runs with screenshots
inside the measurement window are not valid heavy-encounter comparisons.

Campaign Resume uses physical R plus the mapped confirm button at difficulty
selection. The hint is shown only on that screen. The menu event queue must
shift older events toward higher indices before inserting at zero; shifting
the other way discarded the confirm event when R was held. The input suite
now covers this combination, ordering, held values, suppression and flush.
An emulator menu test reproduced the no-op before the fix and restored a
marked Heroic checkpoint after it, including Original-model emulation. Test
the real menu dispatch as well as the checkpoint loader when changing this
path; calling the loader directly cannot catch lost button events.

The subsequent November 3 beach crowd pass keeps closest-hit collision
results for movement, weapons and the initial AI visibility ray. Secondary
AI visibility rays need only a blocked/clear answer and now stop at the first
obstruction. Root object misses use the existing sphere predicate before
entering recursive collision code; sibling chains retain the original path.
Reference-list traversal uses the same checked datum lookup inline.
`HALO_N3DS_COLLISION_RAY_VERIFY` compares rejected roots with the old query;
`HALO_N3DS_COLLISION_ANY_VERIFY` compares each boolean visibility query with
the closest-hit path. These are correctness checks, not timing modes. Do not
confuse the ray flag with the older feature-list verification flag, which
copies large feature lists and makes a full encounter prohibitively slow.

New models reuse the lit skin program for rigid equipment using a fixed
bone-zero weight. Buffer layout remains separate from the shader-state key,
so rigid/skinned transitions still update vertex attributes. Original models
retain separate programs. With `HALO_N3DS_RENDERER_TESTS`, the model batch
pixel check compares the original programs against the shared program;
lighting, reflections, sky, decals, view changes and weapon transforms must
produce identical pixels. No texture quality or simulation-rate reductions
were added in this pass.

On the real New 3DS, the same 900-frame beach route measured 50.839 ms for
the baseline, 49.644 ms for collision root rejection/traversal, 50.231 ms for
the shared program alone, and 49.482 ms for all changes including boolean
visibility queries. These are individual runs, not statistical confidence
intervals. The first two candidates matched the baseline's sampled actor
counts; the combined run diverged at its final sample (17 versus 20 actors),
so its 2.7% frame-time reduction is indicative rather than an exact isolated
speedup. Original-model emulation is a compatibility check, not a hardware
performance claim. The heavy encounter remains around 20 FPS.

Clean baseline stage timing was about 25.1 ms simulation (8.1 ms actors,
16.2 ms objects) and 20.2 ms render preparation. Detailed sampled profiles
point to visibility/collision traversal, object poses and model submission;
base animation decoding was only about 0.6 ms. Nested instrumented scopes
overlap and include timer overhead: do not sum them or substitute them for
the clean benchmark. FrameBegin wait near 0.05 ms indicates little blocking
at that fence in this route, not a GPU-utilization percentage. Live system
link was not re-tested in this pass; protocol and update rates are unchanged.


### Experimental frame ownership work (November 3)

The working hardware baseline remains SHA-256
`5c2cdbf46c2fab280a4fe962af0809bc3cc625e8c4e459ed621ee649f60a860e`.
The following paths are research switches, disabled by default. They are **not**
a completed concurrent simulation/renderer or a recommended performance update.

- `--experimental-render-packets serial`: capture opaque map-owned model draws
  with owned poses, four change colours and four shader-function values. Reuse
  one pose per model submission. Replay preserves order inside the same view.
  Drain before environment, view-space, transparent, effect and nested-widget
  boundaries. Map teardown requires an empty queue. Arena limits are 192 KiB on
  New models and 48 KiB on Original models; capacity drains and allocation
  failure fallbacks preserve draws. Geometry references borrow the map lifetime.
- `--experimental-render-packets parallel`: also prepare bone rows from those
  immutable poses through the existing fork/join worker. Original models prepare
  locally. This does **not** overlap a simulation tick with drawing, and the
  hardware experiment did not justify enabling it.
- `--experimental-render-packets batch`: combine repeated opaque skinned mesh
  parts with independent poses in one PICA palette. Respect the 29-bone limit,
  require exact materials/lights, retain transparent ordering, and bound optional
  resident geometry to 384 KiB New / 64 KiB Original. This shares draw submissions,
  not animation or hitboxes. Skinning remains on PICA. Missing cache space uses
  the existing draw path.
- `--experimental-collision-layout`: compile collision skeletons into their
  contributing node indices and reuse inverse matrices only for byte-identical
  source transforms. Node order, current permutations and collision results stay
  unchanged. Clear on map disposal. `HALO_N3DS_COLLISION_LAYOUT_VERIFY` recomputes
  every cache hit with the original inverse routine; it is not a timing mode.

Single hardware beach runs, using the same 900-frame route:

| Variant | Frame time | Approximate FPS |
| --- | ---: | ---: |
| Immediate control | 49.637 ms | 20.15 |
| Owned packets, serial replay | 49.553 ms | 20.18 |
| Packet bone preparation on worker | 49.782 ms | 20.09 |
| Packets plus repeated skinned-mesh batching | 49.386 ms | 20.25 |
| Packets plus collision layout/inverse reuse | 49.899 ms | 20.04 |

These are not significant gains toward the 33.3 ms target. Actor populations
vary slightly late in the route; do not present the small differences as reliable
speedups. Batching saved 34,718 submissions over the whole hardware run including
intro/warmup, but that did not translate into a substantial frame-time reduction.
Collision verification passed 167,344 inverse comparisons in the emulator.
Original-model emulation exercised the smaller packet arena and batch cache;
it does not establish Original hardware performance. No live networking session
was run; simulation rates and protocol were not changed.

The full frame handoff remains unfinished. The opaque packet arena is only the
first ownership boundary: transparent animation arrays, dynamic geometry,
particle/lens-flare preparation, HUD/menu state, camera/time and texture residency
must all be captured or fenced before the next simulation tick can overlap GPU
submission. Citro3D has a shared context; do not call it concurrently from both
threads. A wholesale move of `n3ds_engine_mission_render` to a worker is unsafe.
The serial packet path must remain available as the reference for that work.

### Owned backend commands (November 4)

`--experimental-render-commands` adds an independent, serial command stream
below the model packet layer. It remains off by default. This is preparation
for a render/simulation handoff, **not an implementation of concurrent frames**.

The backend copies pose rows, UV scale, material values and pointed-to lighting.
Indices move into the existing frame upload arena before recording. Vertices
may borrow only the current frame's renderer-owned upload blocks or resident
model streams protected against same-frame replacement. Unknown vertex owners
use the immediate path. The latest source covers rigid, skinned, CPU-posed
world-space and CPU-posed camera-relative models. It preserves command order.

The queue is bounded to 512 KiB on New models and 64 KiB on Original models.
Capacity exhaustion drains in order; allocation failure uses the immediate
path. Camera/target changes, unsupported draws, texture operations, geometry
mutation, presentation and resource disposal explicitly drain pending commands.
These CPU ownership fences do not replace Citro3D's GPU completion fence.
Texture-bound queries also drain so their answers reflect preceding draws.

Validation so far:

- Queue tests cover copied inputs, ordering, repeated capacity drains,
  oversized-command fallback and nested fence calls during replay.
- Emulator pixel comparisons for both model settings cover all four model
  paths. Deferred pixels match immediate pixels exactly after the caller's
  pose, UV, material, lighting and index arrays have been overwritten.
- The rigid/skinned prototype completed the full beach route in the emulator
  and on New 3DS hardware. Its hardware run recorded 301,155 commands, with an
  83,144-byte peak arena and at most 103 consecutive draws before a fence.
  These totals include the intro/warmup, not just timed gameplay.
- The latest default configuration compiled and linked all 417 translation
  units. Attribution, Python build syntax and whitespace checks passed.

Fresh 900-frame hardware comparison, one run per variant:

| Variant | Frame time | Approximate FPS |
| --- | ---: | ---: |
| Immediate control | 49.854 ms | 20.06 |
| Owned backend commands, rigid/skinned serial replay | 50.193 ms | 19.92 |

This is a small added cost, not a speedup. Do not enable this path in releases
on the strength of these results. The later addition of CPU-posed command
coverage has exact emulator pixel validation, but no separate hardware timing
yet. Full render preparation still accesses live engine state; only the
recorded backend model inputs are currently owned. Moving only this backend
work cannot recover the entire measured render-preparation time.

Next, capture transparent/effect/HUD passes, view/time state and resource
lifetimes into a coherent frame, and separate CPU preparation from GPU replay.
Keep a single Citro3D owner. Only then can the next simulation tick safely
overlap that work. Other experimental batching and collision paths remain
disabled. No simulation rate, networking protocol or graphics-quality policy
was changed for this command-stream test.

Raw results, binary hashes and test captures are retained locally in
`outputs/Halo3DS-Frame-Overhaul-2026-11-03/`. After hardware testing, the exact
preserved playable baseline was uploaded to the existing Halo3DS launcher
entry. Transfer acceptance was verified; there was no live screen confirmation
of that final restored upload.

### Render worker and prepared effects (November 4, subsequent pass)

`--experimental-render-worker` implies the owned command stream and adds one
New-model core2 worker. It remains disabled by default. This overlaps GPU command
generation with later **render preparation**, not with the next simulation tick.
Original models use the serial implementation and smaller command arena.

The producer and consumer exchange two bounded 512 KiB command arenas. Only
the consumer calls Citro3D while a job is outstanding. Unsupported GPU state
operations, view changes, resource mutation/disposal and presentation join it.
Queue exhaustion drains in order; unavailable threads or spare memory fall back
to serial execution. There is no spin loop or extra GPU frame in flight.

Effects now separate CPU preparation from GPU submission. The command owns its
projection and material flags; vertices are copied once into the existing
append-only frame stream, and indices into the existing index stream. The
sampler is bound through a copied descriptor instead of temporarily mutating the
texture owner. Resource fences protect borrowed texture storage. Immutable
merged scenery vertices are explicitly published and retired before freeing;
unrecognized geometry owners still take the immediate path.

The first worker experiment submitted at model-batch end. The revised experiment
publishes spans of 32 commands while later model parts are being prepared. Camera
values remain read-only between fenced view transitions: material restoration
no longer rewrites them. Worker draws consume already-published index ranges;
only the producer appends to the index arena. A worker attempting to allocate
new indices is rejected. These ownership changes remove unnecessary joins
without relaxing GPU completion fences or changing animation/physics rates.

One New 3DS beach hardware run per variant (900 timed gameplay frames):

| Variant | Frame time | Approximate FPS | Main-thread mission-render time |
| --- | ---: | ---: | ---: |
| Immediate control, prepared effects | 49.489 ms | 20.21 | 19.590 ms |
| Owned commands, serial | 50.881 ms | 19.65 | 20.674 ms |
| Worker at model-batch end | 50.277 ms | 19.89 | 20.294 ms |
| Worker with earlier spans | 48.953 ms | 20.43 | 18.993 ms |

The earlier handoff reduced total joining time from 3,216 to 1,152 ms over the
whole run, including intro and warmup. Those totals are **not** divided by 900.
The last run's sampled simulation time was 24.878 ms (actors 7.854 ms, objects
16.240 ms). Its full frame is still far above the 33.3 ms target. It saves only
0.536 ms against the immediate control in these individual runs; scene outcomes
and actor populations vary slightly. This is not enough evidence to enable the
worker by default or claim a substantial improvement.

Validation:

- All four model paths match immediate pixels exactly on New and Original
  emulation, with 80 draws per case crossing multiple worker handoffs. Caller
  pose, UV, lighting, material and index arrays are overwritten before the final
  drain. Original-model tests report zero worker jobs.
- Thirty-six fixed effect cases match the previous path's pixel hashes and
  coverage on both model settings. These cover float/packed/indexed geometry,
  six blend modes, sampler options and camera-relative affine transforms;
  MIN/MAX blending is excluded from this particular comparison.
- Both worker versions complete the 900-frame beach route in emulation and on
  New 3DS hardware, with an empty pending queue. The earlier-span test records
  8,921 worker jobs over the full hardware run. Hardware timing builds do not
  capture screenshots during the measured interval.
- The final default configuration compiles/links all 417 translation units.
  Attribution, Python syntax and whitespace checks pass. The final defensive
  worker-index-allocation rejection is covered by pixel tests; the hardware
  timing binary predates that guard, whose successful execution path is unchanged.

The complete frame handoff is still unfinished. The next dependency to remove
is environment/material submission: capture its geometry and sampler state so
it can run while characters are prepared. Transparent Chicago passes, HUD/text,
view transitions and all borrowed resource lifetimes also need owned commands
before simulation can advance independently. High-level object visibility,
lighting and cached render-state preparation still access mutable engine data;
moving the whole mission-render function to a thread remains unsafe. No new
live system-link test or Original-model hardware timing was performed here.

### Environment command ownership and hardware breakdown (November 4)

The experimental backend queue now captures plain/detail, emission, plasma and
bump environment draws. Map-owned vertices and per-eye index ranges retain their
existing GPU-frame lifetime. Commands copy animated material values. Prepared
sampler commands own their texture descriptor and cube-face descriptor; resource
creation, replacement and destruction remain fenced. Generic texture binds stay
immediate because text and HUD callers still draw directly. This distinction is
necessary to preserve draw order. No shader quality or simulation rates changed.

Sixteen environment cases compare queued versus immediate pixels exactly on
New and Original model emulation. Tests cross multiple 32-command handoffs,
overwrite caller emission values, and separately overwrite caller sampler and
cube descriptors before replay. Original-model tests submit zero worker jobs.
The pixel test is retained in `port/n3ds/tests/include/engine_environment_command_tests.inl`.
The complete beach route also passes in emulation and on New 3DS hardware.

| Backend variant | Frame time | Approximate FPS |
| --- | ---: | ---: |
| Environment commands, serial | 51.299 ms | 19.49 |
| Environment commands, worker | 49.052 ms | 20.39 |
| Worker with additional stage counters | 49.555 ms | 20.18 |

These are single 900-frame hardware runs with slightly different combat outcomes.
The worker recovers command-recording overhead, but is still essentially level
with the earlier 49.489 ms immediate control and 48.953 ms model-worker run.
This is **not** a meaningful improvement toward 30 FPS. Worker replay and join
totals include intro and warmup and must not be divided by 900.

The additional-counter run measured 24.858 ms of sampled simulation time,
including 7.762 ms in actors and 16.306 ms in objects. Main-thread render stages
averaged 10.942 ms objects, 0.319 ms BSP, 3.912 ms effects, 2.079 ms HUD,
0.499 ms presentation and 1.472 ms preparation. Within object rendering,
world draws took 6.198 ms and first-person draws 2.101 ms. The transparent queue
accounted for 3.144 ms of effects. These are nested CPU intervals, not additive
GPU timings; worker overlap and waits can be included. GPU saturation was not
established by these measurements.

All experiments remain off by default. The next larger dependency is high-level
model/transparent preparation: immutable poses alone do not isolate camera/time,
material evaluation, render caches and texture residency from the engine. Full
simulation/render overlap remains unfinished. Do not move mission rendering to
a thread until those ownership boundaries are complete. There was no new live
network test or Original-model hardware timing in this pass. Measured binaries,
hashes, results and the untimed emulator image are recorded under
`outputs/Halo3DS-Frame-Overhaul-2026-11-03` in the local workspace.

### Explicit model view inputs (November 4)

Model material preparation now receives a value snapshot containing camera
position/direction, animation time, fog parameters, projection, cinematic state
and material-quality policy. Opaque packets own this snapshot alongside their
poses and animation arrays. Chicago, generic/hologram, plasma, water-model and
glass-model paths consume explicit view inputs instead of sampling the live
engine camera and clock. The unrelated BSP-water path remains in the ordered
environment stage. Model fog snapshots omit the unused screen-fog resource
pointer. This changes ownership, not shaders or quality policy.

The serial model packet replay installs and restores its captured view together
with the pose. At this stage, transparent packets still borrowed animation
arrays and captured their view when consumed; the following pass fixes those
dependencies. Renderer cache ownership, dynamic geometry and other remaining
frame inputs still prevent advancing simulation concurrently. Same-view
rendering remains ordered.

Validation includes overwriting captured pose/view inputs, alternating two
independent views through generic lighting/reflection and hologram fog/projection,
and completing the 900-frame beach encounter. New 3DS hardware passed at
49.443 ms (20.23 FPS); Original-model emulation passed with zero worker jobs and
49,128 bytes peak packet storage, within its 48 KiB limit. These figures do not
establish a speedup. The final normal release compiles all 417 translation units;
experimental packet and worker options remain disabled by default. No new live
network test or Original-model hardware timing was performed.

A separate hardware diagnostic sampled one in sixteen draws within each shader
class. Extrapolated CPU time per frame was about 4.60 ms for ordinary model parts,
1.78 ms for generic parts, 1.19 ms for Chicago parts and 0.95 ms for first-person
parts. Ordinary parts included approximately 0.96 ms geometry preparation,
0.73 ms material evaluation, 0.41 ms bone setup, 0.19 ms texture lookup and
2.14 ms submission. These sampled intervals can include waits and cache misses;
they are not GPU timings or independent additions to the earlier render-stage
totals. The result argues against treating texture lookup alone as the large
remaining bottleneck. Detailed logs and artifact hashes remain with the local
overhaul measurements.

### Owned transparent materials and ordered state transitions (November 4)

Transparent model submissions now own their bones, base and modifier animation
arrays, and the view at submission. A side table indexed by the physical group
slot preserves the original transparent-group ABI and sorted draw order. View
reset and pool disposal fence command consumption before recycling those
values. Material/bone caches reset when replay switches object inputs. The
900-frame New 3DS beach test passed at 49.793 ms per frame, with 22,404 bytes peak
transparent pose storage and no rejected poses. This is not a speedup.

The Chicago GPU path now separates validated preparation from submission.
Commands own material parameters, projection/origin, bone/UV rows, index copies
and sampler descriptors (including cube faces). Immutable uploaded model data
retains its existing frame/map lifetime, with complete byte-range validation.
Pretransformed vertices use an optional bounded copy pool (128 KiB New,
64 KiB Original), reset after the next frame's GPU wait. Allocation failure or
exhaustion uses the immediate path, leaving the original vertex arena's full
capacity available to engine allocations. Preparation tracks queued bindings separately
from the backend's executed bindings, so reading a pending sampler does not
join the worker. Backend binds cannot overwrite that producer state.

The engine's material transition can also queue its state restoration in order.
Immediate GPU callers retain the synchronous `n3ds_gpu_model_batch_flush`
contract. No transparent draw reordering, shader reduction, simulation-rate
change or extra render pass is introduced. Map changes and unsupported direct
GPU operations still fence. This remains backend submission overlap with
render preparation, **not overlap with the next simulation tick**.

Validation covers 36 exact pixel comparisons after overwriting caller inputs,
including CPU/world/relative/rigid/skinned paths, shields, meters and fades.
The final suite also interleaves an opaque model and 80 transparent submissions
across worker handoffs. Existing independent Chicago arithmetic and transform
tests run alongside it. The intermediate path passed both model settings and
the full beach encounter; Original emulation used zero worker jobs and stayed
within its 48 KiB model-packet and 64 KiB command limits. Its peak transparent
pose allocation was 12,020 bytes. These emulator timings are not hardware FPS.

The first queued-material hardware run took 50.211 ms per frame; the final
ordered-transition/bounded-pool run took 50.084 ms (19.97 FPS), with 24.865 ms
sampled simulation time. Its Wi-Fi result transfer was incomplete; the complete
saved SD log was recovered, matched the received prefix and contained the
900-frame encounter PASS marker. The original process exit code was not saved
in that file. The final Original-model encounter and both model settings'
pixel/copy-pool-exhaustion tests passed. No substantial gain has been established. Current experiments
remain disabled by default; the normal 417-translation-unit configuration
still builds. High-level model preparation, remaining dynamic/HUD/effect frame
ownership and actual simulation/render concurrency are still unfinished.


## Combined experimental test build (2026-11-04)

The cumulative test enables collision layout caching, owned model packets,
parallel pose-row preparation, repeated-character batching and the New-model
render command worker together. Reproduce with `--release
--experimental-physics-15hz --experimental-render-packets combined
--experimental-collision-layout --experimental-render-worker
--experimental-pc-runtime --unified-ui` plus the normal toolchain paths.
Vehicles, player movement and network simulation retain their existing 30 Hz
policy; the selective offline AI-biped schedule is unchanged. Fog is disabled.
Original models use smaller queues and serial execution. This is still same-view
render preparation/submission overlap, not concurrent next-tick simulation.
The preserved pre-overhaul binary remains the comparison baseline.

The bottom screen is blank at the main menu. Campaign profile/level selection
shows the selected profile's validated checkpoint and a touch Resume button.
The query is cached during menu browsing and refreshed on entry/profile change;
resuming uses the saved mission and difficulty. Difficulty selection still starts
a fresh mission by default, with the existing R + confirm shortcut retained.
Campaign co-op cannot load an offline checkpoint through this button.

The combined hardware beach fixture completed 900 measured gameplay frames at
50.516 ms/frame (19.80 FPS), versus 49.489 ms/frame (20.21 FPS) in the earlier
immediate-render comparison. This is not a meaningful cumulative improvement.
Encounter outcomes can vary; these are individual runs, not a statistically
controlled speedup estimate. Original-model emulation and a real campaign
checkpoint touch-resume test passed. The combined flags remain opt-in.

## Wireless bot integration

The System Link host can choose zero to three bots and their difficulty before
countdown, using the bottom-screen lobby controls. Bots occupy the host's unused
controller slots, leaving controller zero for each human console. Bot decisions
run only on the host; actions travel through the normal player input stream.
Local-only shortcuts that directly alter damage, pickups or camouflage are
disabled in wireless matches. Co-op removes bots and blocks campaign extras.
Local single-console matches still support up to fifteen bots. Physical mixed-model
synchronization, combat and match-end testing remain required.
