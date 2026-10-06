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
for clean verification. Player releases must use the default options, without
diagnostics, a frame limit or a test heap.

### Diagnostic builds

Add `--test-frames 240 --diagnostics --output build/n3ds-diagnostics` to the
dependency command above for a bounded menu run with renderer checks. Success
writes `RESULT: PASS` to the game log. This build exits automatically.

`--original-memory` constrains the heap for Original-model emulator tests.
Also select an Original model in the emulator; neither setting replaces a
test on actual Original 3DS/2DS hardware.

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
