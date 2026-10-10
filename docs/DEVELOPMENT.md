# Developer notes

Build instructions, testing and maintenance notes for contributors, including
coding agents. Player instructions are in the [main README](../README.md).
Commands below run from the repository root unless stated otherwise.

- [Building the game](#building-the-game)
- [Building the setup tools](#building-the-setup-tools)
- [Setup formats and recovery](#setup-formats-and-recovery)
- [Engine and platform boundary](#engine-and-platform-boundary)
- [Rendering rules](#rendering-rules)
- [Stability and timing rules](#stability-and-timing-rules)
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

### PC retail multiplayer status

PC retail v7 support is opt-in with `--experimental-pc-runtime`; the default
launcher still uses the original Xbox cache path. The opt-in build reads PC
maps from `halo-source/pc-retail` and falls back to installed Xbox maps. The
`--unified-ui` flag keeps the original Xbox main menu and campaign while
exposing PC Local Multiplayer through SELECT. This is the combined launcher
used by `setup/Unified/`; an isolated emulator replay entered Xbox a10 from
the original campaign menu and selected PC Beaver Creek from the same 3DSX.
The build keeps the Xbox UI cache so the PC-only difficulty widget mismatch
does not affect Campaign. The PC-only launcher remains available for separate
testing.

For the PC-only UI build, original PC `ui.map` supplies the six expansion map
names, descriptions and graphics. The map-list adapter exposes an expansion
entry only when its UI assets and cache are present. The original thirteen Xbox entries stay defined;
the PC menu's Create Game action still does not launch a local match.

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
paths. The PC `Create Game → LAN` widget does not currently start a local
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

During a solo PC match, hold SELECT or the lower-right SCORE touch panel to
display the player scoreboard on the top screen. Three other panels surround
the motion tracker; they show press feedback but have no action yet. The
Controller Settings screen offers a saved X/Y button swap and an optional
bottom-screen FPS readout. Switch X/Y starts on for new profiles; FPS starts
off. Saved profiles keep their chosen values. The Decals switch starts on and
controls creation and drawing of surface marks without changing their authored
lifetimes. Weapon meters use compact bars below the ammo count to remain
legible at the 3DS resolution.
After a local multiplayer match ends, the upper part of the bottom screen
shows Return to Lobby. Tap it or press B to reopen the Local Multiplayer
selector with the previous map, mode and bot choices.

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

Both setup interfaces prepare 3DS game data using the shared converter in
`port/n3ds/setup/Core/`.

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
dotnet run --project port/n3ds/setup/Tests
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

The accepted Xbox release is English North American retail build
`01.10.12.2276`. Setup validates exact hashes, including compressed and unchanged
expanded map versions. Other revisions/regions, modified maps and PC/MCC assets
are rejected. A complete dump has 24 maps: `ui`, 10 campaign and 13 multiplayer.
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

## Stability and timing rules

Compressed animation streams can contain unaligned floats. Keep byte-safe loads
and aligned local results; casting packed data to float/vector pointers can cause
ARM11 faults. Use typed, aligned collision scratch structures for the same reason.

Gameplay state, scripts and rendering have shared ownership. Worker jobs operate
on disjoint data with explicit lifetime fences; do not move arbitrary AI or object
mutations to another core. Cache keys must include pose/instance mutations and
must not survive invalidated map or tag storage.

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

The texture loader currently uploads one selected mip level per bitmap. On
Timberland the New-model 64-pixel world setting used about 2 MB of its 4 MB
gameplay texture cache after loading 123 textures. Citro3D supports mipmapped
textures, so a near/high and far/low experiment can use a bounded mip chain.
Measure cache occupancy, loading hitches, steady frame time and close-range
clarity before changing the release setting.

## Verification

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
their established profile. Existing version-one through version-four controller
settings load with their prior values preserved. Fields absent from older files
take their defaults: Switch X/Y ON, Swap A/B ON, FPS OFF, Decals ON. The file
upgrades on save.

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
uncaptured Azahar route, outdoor visibility work fell by roughly 2.5–3.5 ms
per frame, while busy total frame work remained above 33 ms. This is an
emulator result, not a New 3DS XL frame-rate measurement. Conservative PVS
and rectangle traversal experiments were rejected: PVS increased object and
effect work, and the rectangle path omitted clusters found by the original.

An uncaptured seven-bot Timberland trace split the remaining simulation work.
The game update averaged about 15–20 ms in the outdoor route. Object updates
accounted for roughly 10–13 ms of the game tick, with moving vehicles,
projectiles and short corpse-physics spikes the largest type costs; 168 static
scenery objects together took about 0.3 ms in the sampled ticks. Particle
simulation added roughly 2–4 ms per frame. These are Azahar host timings and
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
the vehicle. Physical New 3DS XL vehicle handling still needs testing.

The native flamethrower renderer now draws the PC map's own flame sprite at
simulated projectile positions and keeps a short history of world-space muzzle
emissions. A turn leaves the older flame behind instead of rotating the entire
jet with the camera. The native game tick also advances authored decal decay,
and additive particles apply vertex alpha to their light contribution so
explosions fade. The isolated Azahar arsenal fixture fired all 11 weapons;
captured turn and Fuel Rod frames showed the flame lag and blast decay. The
physical display and sustained frame rate still need checking.

A moving six-bot Sidewinder Race fixture reached an authored transparent
effect that combines a screen-anchored secondary bitmap with premultiplied
alpha. The native two-map effect renderer now carries both texture alphas into
the framebuffer blend. The replay completed 241 captured frames without a
fatal error. GPU readback checks compare 50 secondary-effect cases, including
the premultiplied mode, against independently calculated pixels.
