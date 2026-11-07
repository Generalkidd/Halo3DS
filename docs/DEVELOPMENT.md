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

The next native-data work follows the
[renderer preservation and staged validation contract](RENDERER-PRESERVATION.md):
format-aware textures, then exact offline geometry, then packed vertices.
Its source reference is preserved but unvalidated. The RGBA8 descriptor and
[optional `.ntx4` path](NTX4.md) are implemented for development and disabled by
default. Cooked geometry and packed-layout controls still reject selection;
no candidate default is promoted.
The [texture consumer inventory](TEXTURE-CONSUMERS.md) records current CPU
RGBA8 dependencies and the remaining per-map eligibility work.

[Algorithm comparison and profiling](ALGORITHM-PROFILING.md) describes the
reference and independent opt-in experiments. A bounded native-400 emulator
menu smoke test passed. Campaign, midpoint and hardware performance
validation remain outstanding. See the preservation contract for evidence scope.

The independent [dependency trace](DEPENDENCY-TRACE.md) adds bounded event
collection and an offline reader for stage, queue and acknowledgement gaps.
The [action replay fixture](ACTION-REPLAY.md) supports local campaign checks
with copied starting data.

## Building the game

The build uses Python 3.10+, Clang, devkitARM, libctru, citro3d, Picasso and
3dsxtool, plus locally supplied August 2001 XDK headers. SDK files and game
data are not included. Players do not need these dependencies.

The build has been checked on Windows. Use native Windows toolchain paths;
a DEVKITARM value from WSL is not a Windows toolchain. Validated dependency
revisions are libctru `36fe1ada5b7ebe53ba4decda36d764a55f8fefb6` and citro3d
`9f21cf7b380ce6f9e01a0420f19f0763e5443ca7`.
For either `--present-60` or `--dependency-trace`, point `--citro3d` at that
source checkout with its matching built archive. The build verifies the pinned
render-queue source hashes and compiles the frame adapter. `--present-60` uses
its frame-discard operation for abandoned optional frames. Trace-only builds
use its observations without enabling midpoint admission or changing waits.

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

### New 2DS XL performance comparisons

Follow the [performance roadmap](PERFORMANCE-ROADMAP.md) for implementation
priority, the working-reference gate and required presentation evidence. Current
profiling is groundwork; it does not establish a hardware bottleneck ranking.

Use the same source revision, SD contents, save, and camera route for each
comparison. Change one option at a time and keep each build in its own
`--output` directory. Use interactive builds without `--test-frames`, which
triggers a screen readback during the run. The options are recorded in
`boot-build.json`.

| Option | Effect |
| --- | --- |
| `--disable-portal-cache` | Recomputes portal projections to provide a control for the default per-view cache. |
| `--disable-resident-indices` | Stages model and scenery indices in the transient arena. By default on New models, small recurring model lists can reuse an upload in a 256 KiB linear arena. |
| `--fixed-world-width 400` or `--fixed-world-width 320` | Fixes the New-model gameplay viewport at 400×240 or 320×192. Both use the native HUD and the same projected model detail. The smaller world includes the normal upscale pass. |
| `--present-60` | Enables optional in-between frames during New 2DS XL campaign play. Keeps the world at native 400×240 even when the extra frame is skipped. Use with a playable build and omit `--fixed-world-width`. |
| `--texture-cache-mib 8` | Raises the New-model gameplay byte cap from 4 to 8 MiB. Slot limits are identical. The cache reclaims the extra working set under linear-memory pressure. |
| `--texture-prefetch` | Prewarms nearby terrain at map entry and enables bounded texture preparation on New models. `--present-60` also enables worker preparation and can prioritize the bitmap that canceled a midpoint. |
| `--performance-profile` | Records work percentiles, visibility time, texture stages by request type, eviction pressure, and worker time in `halo-source-engine.log`. |
| `--dependency-trace` | Independently collects bounded dependency events and exports at shutdown. Default off. Rejects readback, replay, diagnostics and Original-memory fixtures. |
| `--dependency-trace-loops N` | Requires trace; accepts 1–18,000 completed frontend loops after gameplay first starts, default 120. Stops capture without ending gameplay or enlarging event storage. |
| `--midpoint-capture-tick N` | Correctness fixture for `--present-60`. Writes at most one midpoint/regular top-target pair with RGBA, raw D24S8 and camera inputs in the 120 ticks starting at N. Use the complete manifest and keep timing and trace flags off. |
| `--action-replay record\|replay` | Record or verify up to 3,600 local campaign action ticks. Use an isolated copy of the starting save and settings. |
| `--action-replay-fixture-ticks N` | In a regular native-400 record or replay build, seed and launch the bounded idle `a10` campaign fixture. |
| `--allocation-profile` | Add completed allocation-call observations alongside loop-end samples. Requires algorithm profiling. |
| `--geometry-source cooked` | Opt into bounded `.nmg` model compaction plans with runtime validation and fallback. |
| `--vertex-layout packed\|packed-verify` | Use the 44-byte weighted model stream. The verify mode checks reconstructed CPU attributes and is excluded from timing. |
| `--texture-storage candidate-archive` | Opts into the RGBA8 `.ntx4` archive, with `.ntx` and map-decoder fallback. Default is `legacy`. Setup must separately opt into candidate generation; this changes neither mip/resolution policy nor sampling. Source-reviewed only; parity and performance remain unvalidated. |

The frame work profile reports complete windows of 300 gameplay iterations
within a BSP generation. Generation transitions are reported separately.
Texture and visibility summaries use their own 30-render-frame windows.
Frame work measures CPU wall time before the pacing sleep. GPU queue
time and texture worker time can overlap this work, and neither work time nor
queue submissions measure displayed FPS. For texture hitches, compare the
demand and queued lanes, the peak demand frame, and the cache fence time.
Prewarm can lengthen the initial load. Compare both a fresh pass and a repeat
through 343 Guilty Spark, then check a busy combat route such as the beach
landing in The Silent Cartographer. An emulator can check behavior and
relative work, while a physical capture is needed for console frame pacing.

For the trace configuration, append `--dependency-trace --fixed-world-width 400`
and a separate `--output` to the dependency command above. This is a future run
configuration, not a command executed for the trace slice. It leaves
`--present-60`, prefetch and aggregate profiling disabled unless separately
selected. The loop capture is not a test/process timeout: later automated runs
need an external bound and graceful shutdown to export retained events.
Do not interpret submissions or sampled swap acknowledgements as exact LCD
scanout times. Retain overflow, drops and missing completion records alongside
any later analysis; see [the trace contract](DEPENDENCY-TRACE.md).

### 60 Hz presentation target

The New 2DS XL has a 400×240 top screen. Its nominal refresh is about
59.83 Hz, so a display slot lasts roughly 16.71 ms. The faster ARM11 mode
helps CPU preparation. PICA200 still shares the family's GPU and 6 MiB VRAM
budget. GPU-readable linear memory comes out of the actual process grant.
[Nintendo hardware manual](https://www.nintendo.com/eu/media/downloads/support_1/new_nintendo_3ds/NewNintendo2DSXL_OperationsManual_UKV.pdf)
· [Nominal timing](https://github.com/azahar-emu/azahar/blob/master/src/core/core_timing.h#L33-L42)
· [libctru memory split](https://github.com/devkitPro/libctru/blob/36fe1ada5b7ebe53ba4decda36d764a55f8fefb6/libctru/source/system/allocateHeaps.c#L22-L70)

Aim for native 400×240 with the authored materials and full-resolution HUD.
Reuse static geometry and identical vertex descriptors first. Keep the
resident index arena optional under linear-memory pressure and retain the
transient path for overflow and dynamic geometry. Static water and scenery
can reuse GPU-readable indices. BSP visibility selects material candidates
in the original order and keys filtered indices by the clusters that own
that material. Two exact portal-mask entries also reuse material candidate
lists when visibility is unchanged. GPU-native compressed textures remain a separate experiment.
Keep authored lightmaps and cutout edges in the visual comparison.
[Native texture formats](https://github.com/devkitPro/tex3ds#format-options)

Halo updates input, collision, scripts, and audio at 30 Hz. With
`--present-60`, the renderer can draw a midpoint before the newer regular
frame. It blends adjacent camera and first-person joint snapshots, rebuilds
visibility, and draws the world and top HUD through the regular renderer.
The bottom screen updates with the regular frame. The world pose history
holds up to 128 objects and 512 joints per tick. Attached objects, effects,
and poses outside that budget keep their regular animation cadence.

The scheduler skips optional work during cuts, camera and BSP transitions,
checkpoint restores, menus, cinematics, or when native screen effects are
active. It estimates CPU and GPU queue costs separately for each frame
type and uses top-screen VBlank timing to leave room for the regular frame.
It can abandon an optional frame at render boundaries or before submission.
Midpoints use resident textures. A cold bitmap, font glyph, cube map, plasma
texture, or generated material discards the optional frame before its GPU
commands run. A late rejection can still cost the CPU work already spent
recording. The texture worker prepares bounded jobs from the archive or map.
The render thread reserves an unpublished final GPU image; archive reads and
pixel decoding write directly into it, avoiding an extra full tiled-image copy.
Only the render thread can flush and publish that image after worker completion.
Cubemap jobs read the selected mip from each of six faces, retaining the same
face order, pixel format and resolution. Unpublished allocations retain demand
headroom, and cancellation keeps them alive until worker acknowledgement.
Regular draws retain demand
loading and the same texture settings when a job is late or unsupported.
Regular queued textures preempt unrelated read-ahead, and canceled jobs release
their completed scratch even when the optional preparation budget is exhausted.
Newly generated v3 texture archives retain v2's exact lightmap color dilation,
and cover current display/menu resolutions and P8 vector-palette images. Setup
regenerates older archives; the runtime still accepts v1/v2, with runtime
lightmap decoding for v1. The regular texture-idle boundary opens the bounded
index before worker requests search it. Midpoints never open archive files.

If the display fails to latch a submitted midpoint within the bounded wait,
the regular frame still renders and the top LCD retains the last safe buffer.
`MIDPOINT PRESENT` counts midpoint submissions, observed swap acknowledgments
for both frame types, and skips by reason. It also reports the separate CPU
and queue estimates and the CPU time spent on discarded optional frames.
Interpolation can add presentation latency, so aiming response and the
achievable frame rate need console validation.
[Banjo's scheduling and fallback](https://github.com/oneup03/rt64-3ds/blob/0d783be0c9f51fa7edc696ac33a0992b5f021ecb/src/renderer/rt64_3ds_context.cpp#L2547-L2664)
· [Mario Kart 64's frame order](https://github.com/EstebanPdN/mario-kart-64-3ds/blob/7c7df014f9a00cc80e59fcbba2c3d66af702cbfc/platform/3ds/source/game_runtime_3ds.cpp#L541-L623)

`--performance-profile` reports render-path wall time and GPU queue samples
over the nominal refresh budget. Texture retirement fences the existing queue
without replacing the last real frame's timing sample. The presentation build
also waits for Citro3D's swap callback before reusing a frame.
[Citro3D queue completion and VBlank](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/renderqueue.c#L32-L99)

### Renderer work to reuse

Mario Kart 64 3DS samples expensive packing timers and reuses decoded
textures and static geometry. Halo keeps detailed mission, model,
transparency, and HUD timing in profiling and diagnostic builds. Routine
play retains stall markers and heartbeat logs.
[Mario Kart 64 3DS changelog](https://github.com/EstebanPdN/mario-kart-64-3ds/blob/7c7df014f9a00cc80e59fcbba2c3d66af702cbfc/CHANGELOG.md#L44-L60)

F-Zero X G-Diffuser moved frequently drawn race geometry into native,
GPU-resident buffers and batched HUD elements. The Super Mario 64 port's
PICA vertex shader performs projection and lighting. Both support pushing
reusable geometry work out of Halo's frame loop. Halo already keeps small
immutable index lists resident and has native vertex paths. Native HUD text
batches preserve glyph order and filtered atlas edges on mono frames, including
3DS frames with the stereo slider off. Repeated meters and two-map HUD elements
reuse exact baked outputs while their keys match. Immutable radar-mask IDs
avoid repeated pixel comparisons; mutable masks still compare their contents.
CPU glass layers share decoded geometry and pose lookups, and fallback water
and reflection calculations reuse unchanged lengths and direction angles.
Generic materials share a posed normal between angular fade and view shading.
Fog range, density and the camera's planar-fog factor are prepared once per
draw, with the original per-vertex arithmetic retained. A 32-entry frame-local
texture-animation cache reuses exact transforms for matching descriptors,
object function values, time and scale/offset inputs. Constant unrotated
animations keep their cheaper direct path.
[F-Zero X native renderer](https://hypnotize.works/gdx-3ds/postmortem/native.html)
· [Super Mario 64 vertex shader](https://github.com/Wyatt-James/sm64-3ds-port/blob/9a015d5c50b2b41e47f9dd43ee73e39da0824f85/src/pc/gfx/shaders/emu64/vertex_load/lighting.v.pica#L44-L98)

Repeated BSP material passes reuse exact two-stream buffer descriptors. Shared
attribute layouts are prepared at renderer initialization, and glass/water
passes reuse an already uploaded triangle list. Rigid and identity bone
palettes use prepared rows directly; compact palettes still reorder their rows.
Palette reuse resets across sky/world origin changes, and sorted model packets
invalidate cached object-dependent material values on entry and restoration.

Visibility filtering copies consecutive visible triangles in larger spans.
Material bounds try their last rejecting plane first, but always use the current
frustum; the hint never caches a visibility outcome. Packed indexed effects
reuse exact vertices within each chunk while retaining triangle order and all
attributes. Failed optional merges/scenery allocations have separate bounded
keys so they cannot occupy successful-mesh slots. Successful mesh budgets and
fallback draws stay intact. Cube-map proof files are emitted only by diagnostic
builds, avoiding SD writes during ordinary demand loading.

Optional mesh admission checks size and budget before scanning bone metadata.
Midpoints reuse successful batches but leave cold preparation to regular draws.
An allocation failure suspends new admissions for that cache for the remainder
of the current frame; transient failures can retry in later regular frames.
Every rejected candidate retains its ordinary full-quality draw. Decoded streams
also retain their exact required bone count, and identity palettes track the
already-prepared row prefix instead of rescanning it for each material part.

Static model index lists are staged only when a draw needs them, after resident
scenery batching has declined the draw. A bounded optional cache compacts
unreferenced vertices from already GPU-eligible opaque model streams without
changing their bone palette or rigid/weighted program. Static BSP materials can
compact base vertices, lightmap UVs and bump attributes using one shared remap.
Every triangle retains its winding and order. Position/UV validity checks still
cover every source vertex; base-stream decoding expands only retained normals,
leaving tangent/binormal expansion to the bump path that consumes it. A BSP
source first used by the generic draw API keeps its full stream. If a compact
source later needs generic indices, it retains a separate full stream until the
BSP fence. Cold compaction is left to regular frames.

BSP water uses the same conservative triangle-ownership rule as opaque terrain.
Its per-eye filtered lists are keyed by the original indices and current portal
mask, never rewritten after use in a command list, and shared by both water
passes. Shared/unassigned triangles stay visible. Midpoints use exact warm
matches or the full material; missing metadata or memory keeps the full path.
Resident water promotion always retains the original complete triangle list.

Chicago materials cache complete six-stage combiner recipes, reapplied at every
draw so intervening effects cannot leak GPU state. Generated generic/HUD images
have separate content-revision records: invalidate before writes/deletion and
publish only after flushing completed pixels. These records cannot consume map
texture identity slots. Model lighting compares ambient, each light's enable,
color and direction, and translucency backlighting separately, retaining the
same lighting calculation and ambient-only fallback light.

Unsigned single-stage generic UV materials can now lower replacement,
multiplication, addition, multiply-add and complementary interpolation to one
native combiner stage. RGB and alpha are resolved from the same original
inputs, with exact byte constants and preserved cross-component selectors.
The route samples the original textures directly instead of the coarse CPU
bake, retaining all three UV sets, point/linear filtering, wraps and angular/
animation fades. Signed, mux, dot, multistage, view-dependent or conflicting
constant packets retain their existing fallback. This is a limited native
material route, not a replacement for every Xbox material effect.

The follow-up source review fixed reflection/detail uniform restoration when a
retained model program returns from another material mode. Unused fields in
that intervening material are not evidence of the GPU's installed values.
Within the same program, view and camera-origin domain, bone palettes now upload
only changed float4 rows; a domain change still reinstalls the complete palette.
Flashlight intensity, direction, position and enable state are tracked separately,
with camera-relative model-light positions still refreshed for each changed eye.

DXT decoding precomputes exact source coordinates and tiled destination offsets
per axis, preserving the decoder, chosen texels and RGBA8 packing. BSP visibility
runs copy directly from the already-remapped resident indices, avoiding a second
remap pass; missing run metadata retains the original filtering/remapping path.
Generic stage evaluation reuses exact results within a frame, keyed by copied
stage data, consumed animation values and map generation. Eligible native
generic materials prepare their UVs and angular fade during the existing vertex
walk. Their CPU pose remains shared with the depth prepass to preserve matching
depth values.

These six changes and their follow-up optimisations initially received source
review and whitespace checks only. Subsequent current-source builds and the
bounded menu smoke are recorded in the preservation contract; they do not
validate representative gameplay or establish performance gains. Native 400×240 New 2DS XL
presentation, 30 Hz game logic and regular-frame priority are unchanged;
the 60 Hz presentation target still
requires validation on representative scenes and hardware.

The next six development changes extend reuse without reducing resolution,
material layers or simulation frequency:

1. Glass, water and generic shading share a bounded raw posed-normal cache.
   Its copied matrix/source-revision keys retain each consumer's original
   normalization, reflection and wave arithmetic. It owns separate CPU memory
   (128 KiB Original / 512 KiB New), never evicts borrowed current-frame arrays,
   and leaves cold midpoint requests on the existing scalar path.
2. Generic/Chicago midpoint bake preflight uses the same bounds, material and
   texture-content keys as the real bake lookup, before CPU posing. It cannot
   load, allocate or age a bake. Transparent groups, depth-prepass parts and
   long generic/Chicago vertex loops also check the optional-frame deadline.
3. Archive v3 prepares the currently requested texture plans and exact P8
   pixels during setup. The same plan list calculates replacement disk space
   before output creation. Plasma derivatives fill their unpublished final
   GPU image directly and publish only after finalization.
4. Generic validation/classification uses copied active stage packets,
   including animated constants. Midpoints can reuse entries without replacing
   regular-frame classifications. Native lowering additionally accepts leading
   copy/alias stages that collapse to the existing single TEV arithmetic stage.
   General multistage arithmetic, stage-local constant prefixes, signed inputs
   and double inversions retain fallback; this avoids adding intermediate GPU
   quantization. Generic CPU poses still match their depth prepass.
5. Consecutive native water/glass draws retain copied installed uniforms and
   pass state inside the sorted transparent queue. Every original pass remains
   in order. Other materials, depth prepasses, view/first-person changes and
   queue end break that state lifetime; the actual final glass pass determines
   what must be restored for the next draw.
6. Up to four adjacent compatible opaque BSP packets can share an immutable
   merged vertex/lightmap/index allocation. Full lists retain every triangle
   and their order after individual material culling. Cutouts, authored
   bump/emission materials and filtered subsets remain immediate. The cache has
   16 entries, a 128 KiB per-entry limit and a 128 KiB Original / 1 MiB New
   budget; only one cold allocation is attempted per regular frame. Midpoints
   reuse existing entries, and the BSP GPU fence precedes release.

These additions initially received source review and whitespace checks only.
Later builds and bounded emulator startup checks are recorded in the
preservation contract. Their gameplay speed and visual behavior still require
representative scene and hardware validation.

The subsequent regression review found and corrected a setup/runtime eligibility
mismatch: compressed DXT images may carry flag 16, while raw images must reject
it. Archive plans now mirror that distinction. Identical conversion inputs
(source extent, format, filter, source dimensions and output dimensions) share
one stored payload across resolution lookup keys. Generation, size estimation
and payload verification use the same identity; lookup entries and runtime
texture allocations stay independent. Plans are ordered explicitly for modern
and Windows 7 setup builds. Existing v3 archives remain readable; newly generated
ones benefit from payload sharing without lossy recompression or new mip levels.

Decoded model metadata now retains raw UV bounds and the exact weighted-bone
mask. Generic/Chicago material bounds and cinematic grids reuse that original
reduction; absent metadata retains the old loops. Sparse pose hits compare only
referenced full matrices. Raw normals compare referenced scale/orientation
bytes, allowing reuse across translation-only changes without altering their
subsequent normalization or material shading. Source revision, owner, node count,
budget and current-frame storage protection remain authoritative.

The bottom LCD target is color-only. All current lower-screen HUD, controller
and loading paths draw with depth/stencil disabled, and target/restoration guards
enforce that state. Removing its 240×320 depth24/stencil8 attachment saves exactly
307,200 bytes (300 KiB) of allocated VRAM and avoids its regular-frame depth clear;
the RGBA8 color target, screen dimensions and top-screen depth remain unchanged.
The pinned Citro3D implementation accepts a negative depth format and enables
depth-buffer access only when one exists.
[Target allocation](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/renderqueue.c)
· [Buffer sizes/access](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/framebuffer.c)

Sky projection calculations retain exact per-eye camera/FOV/eye-offset keys;
callers still upload their required GPU uniforms. Target switches explicitly
invalidate retained water/glass state. These follow-ups have source review and
whitespace checks only, with no builds, tests, emulator runs or measured FPS.

CPU texture samplers use exact power-of-two wrapping and reuse tap coordinates,
with the original clamp and non-power-of-two paths retained. Plasma volumes
decode their red/alpha bytes once inside the existing two 64 KiB slots; sampling
keeps the same volume dimensions, swizzle and trilinear accumulation. Secondary
effect layers reuse completed vertices within each chunk, including projective
UVs. Primary effect transforms compare copied matrix values before reusing rows.
Rejected midpoints stop their remaining effect chunks and depth-prepass work.
World-state restoration copies the complete prebuilt combiner baseline on every
restore, preserving the original Citro3D dirty-state behavior.

Halo 64 runs native Halo gameplay for multiplayer, Firefight, and a partial
campaign level. Its converter and renderer show how to prepare map geometry
ahead of time, reuse decal clipping calculations, and batch material state.
Those techniques can apply to this port's full campaign with its current
textures and lighting.
[Halo 64 scope](https://github.com/JackTulli/halo-64/blob/c888de759b1a399c72636397e254681f69993d56/README.md#L33-L78)
· [Halo 64 performance history](https://github.com/JackTulli/halo-64/blob/c888de759b1a399c72636397e254681f69993d56/port/n64/HISTORY.md#L120-L149)

New-model audio can prepare one encoded sample in the background. The usual
idle or blocking cache request still determines when the sample becomes
available. With sufficient heap headroom, checkpoints use up to two raw RAM
snapshots for immediate revert and move persistent compression to the SD
writer. The on-disk format and compressed fallback remain available.
Each packing operation reuses one private LZ4 state across its blocks. Raw
revert validates the whole snapshot before a single direct copy into the live
arena. Spatial audio also reuses distance and pan until the location changes.
These changes remove repeated work; frame-rate and visual validation remain
pending until testing is requested.

## Building the setup tools

The setup interfaces prepare 3DS game data using the shared converter in
`port/n3ds/setup/Core/`.

The cross-platform command-line interface in `port/n3ds/setup/Cli/` also uses
this converter. See the [CLI guide](SETUP-CLI.md) for guided setup, commands,
tests and self-contained Windows/Linux/macOS packaging. It requires .NET SDK 10
for development, without the Windows UI workloads. Run both `Tests` and
`Cli.Tests` after changing shared setup code.

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

Graphics Options can pin the gameplay viewport to 400×240, 320×192, 280×168,
or 200×120. These are all exact 5:3. The renderer latches the preference at the
next world frame; returning to Auto restarts the existing adaptive policy at
the console's baseline. Manual modes retain that baseline's model detail and
material policy. The smaller viewports share the same offscreen target and
bilinear upscale, so pixel counts describe world drawing area. They do not
measure GPU time, VRAM use or frame rate. Fixed-width New-model comparisons,
renderer diagnostics, and active New 2DS XL midpoint builds lock this setting.

New models retain the full-resolution baseline and can reduce world resolution
only under GPU wait pressure outside the fixed-resolution profile. New 2DS XL
with `--present-60` keeps native 400×240 even when optional frames are skipped.
Original models start the world at 320x192 and
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
New models retain every foliage island within the selected authored LOD and
do not apply the original-console cutoff to small decorative scenery. Removing
the earlier New-model thinning can increase work in foliage-heavy views; the
60 Hz target relies on reuse and skipping optional frames under load.

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
