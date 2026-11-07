# Renderer preservation and staged native data work

Status: the RGBA8 descriptor and opt-in `.ntx4` path are implemented in source;
the candidate archive remains disabled by default. Following later authorization,
current-source builds and bounded emulator checks were performed on 2026-11-07.
A native-400, legacy-texture frontend smoke passed 240 loops in macOS Azahar,
with the live menu visually inspected and test processes stopped. The full
graphics diagnostic suite remains unsuccessful because of recorded emulator
sampling limitations. No candidate `.ntx4`, campaign, midpoint or physical-device
validation has been completed. The renderer still contains unvalidated changes
and is not yet a validated reference or measured performance baseline.

The local build, dependency, test and backend-diagnosis receipts are under
`build/emulator-validation-20261107`. The source toolchain uses recorded
alternative OpenCE declarations and a mixed Arm GNU/Clang compiler setup, rather
than recovered original Microsoft XDK headers. No diagnostic tolerance or game
rendering behavior was changed to accommodate an emulator limitation.

The subsequent [algorithm profiling groundwork](ALGORITHM-PROFILING.md)
freezes this WIP again, adds reference-only comparison selectors and opt-in
bounded counters, and keeps the working-reference gate open. Algorithm
candidates and their performance target remain unvalidated and unimplemented.

The implementation order is **format-aware textures → exact offline geometry
→ packed vertices**. Validate each stage independently before combining it
with another stage. ETC compression, mipmaps and attribute quantization are
later, separate experiments. RGBA8 is already GPU-native; the initial texture
work makes format and allocation contracts explicit without changing output.

## Source preservation receipt

Before adding the controls described below, a local source snapshot was made:

| Field | Captured value |
| --- | --- |
| Snapshot directory name | `Halo2DS-reference-20261107T045603Z` (outside the repository) |
| Git HEAD | `a9d7b2fe53f488c657e03ac09a34947deb6bbcb3` |
| Source files | 1,026 tracked and non-ignored untracked files, including existing uncommitted work |
| Source bytes | 14,652,712 |
| `source.tar.gz` SHA-256 | `686271aba73de6c8b358245ea25bc0eb27e99a36ce12d5e21f6fc9d7149642b1` |
| Executable and executed build options | Unavailable: no executable or `boot-build.json` found in this checkout |
| Maps and test saves | Unavailable in this checkout; no per-map identities or route fixtures established |
| Dependencies | Configured devkitARM, libctru, Citro3D, XDK, Picasso and 3dsxtool paths absent; local Clang file hash recorded without invoking it |
| Validation gate | Closed |

The private `manifest.json` records file hashes and modes, deleted paths,
configured dependency paths, expected revisions separately from observed
revisions, and missing evidence. `working-tree.patch`, `index.patch` and
`status.z` retain the tracked changes and index state. Extract the full archive
into an empty directory; applying the patch alone does not restore untracked
files. The archive excludes `.git`, ignored outputs, private SDKs and game
assets. Its source bytes can be restored, but a reproducible executable cannot
yet be claimed. The controls and documents added after this snapshot need their
own candidate snapshot before later comparison.

For each later reference/candidate, retain the same source inventory, dependency
checkout revisions **and dirty state**, library/tool hashes, compiler versions,
build commands/options, executable/ELF/link-map hashes, setup version and
conversion policy. Preserve verified map hashes, sidecar hashes and a private
copy of each save with its build/checkpoint identity. A documented upstream
revision is not proof of the installed library. Do full map hashing in setup or
reference preparation, never implicitly on a texture demand path.

## Preservation contract

| Area | Invariant |
| --- | --- |
| Gameplay | Keep 30 Hz simulation, authoritative input, AI, collision, scripts, weapon behavior, difficulty and animation timing. |
| Visual content | Preserve the current authored streams, LOD selection, foliage, material passes, lightmaps, texture resolution, transparency order, team colors, emission and effects. Do not reduce quality to pay for a representation change. |
| Presentation | Native 400×240 New 2DS XL world, full-resolution HUD, existing optional midpoint policy. Regular frames take priority; midpoint draws skip on cold texture/generated-material preparation or budget rejection. Existing scalar CPU fallbacks remain; future candidate loaders must be resident-only on midpoints. |
| Persistence | No new fields in authoritative game state or checkpoint payloads. Renderer metadata is reconstructed after restore. Existing build-compatibility checks remain in force. |
| Ownership | Queued GPU storage stays immutable until completion. Workers write only bounded, unpublished storage; the render owner validates generation, flushes and publishes. Cancellation retains worker-owned allocations until acknowledgement. |

“Preserve quality” means no new loss relative to the eventual accepted
reference. It does not establish parity with every original Xbox effect.
These inherited limitations are visible in source and require reference
captures before their actual scene impact can be documented:

| Existing limitation | Source |
| --- | --- |
| Opaque atmospheric fog is absent; generic materials have a separate fog path. | [Window setup](../port/n3ds/src/engine_rasterizer.c), [generic material evaluation](../port/n3ds/src/engine_generic_model.c) |
| Projected object shadows reject the pass. | [Shadow capability](../port/n3ds/src/engine_shadows.c) |
| Glass bump/detail uses a smooth approximation; water ripple composition, dependent bump reflection and water fog are approximated. | [Glass](../port/n3ds/src/engine_glass.c), [water](../port/n3ds/src/engine_water.c) |
| Plasma noise has a low-frequency approximation; CPU reflection fallback uses a separate 32-pixel cube cache. | [Plasma](../port/n3ds/src/engine_plasma.c), [CPU cubes](../port/n3ds/src/engine_glass.c) |
| Some generic cinematic shading uses an 8×8 UV grid. | [Generic model path](../port/n3ds/src/engine_generic_model.c) |
| New models retain existing render LOD caps and reductions for dense, unattached decorative emitters. | [Model policy](../source/models/models.c), [particle policy](../source/effects/particle_systems.c) |

These are recorded, not changed by this slice. Existing logs and test source
containing `PASS` do not demonstrate that this source snapshot passed anything.

## Independent developer controls

| Build option | Existing path (default, ID 0) | Reserved path (ID 1) |
| --- | --- | --- |
| `--texture-storage` | `legacy`: existing `.ntx` then original map decoder | `candidate-archive`: opt-in RGBA8 `.ntx4` → `.ntx` → map decoder |
| `--geometry-source` | `runtime`: current decoding/preparation | `cooked`: future exact `.ngm` path with runtime fallback |
| `--vertex-layout` | `float`: current GPU layouts | `packed`: future exact 44-byte skinned stream |

Cooked geometry and packed layouts are **not implemented**. Selecting either
fails before build output is created; corresponding direct compiler definitions
also fail closed. The candidate texture selection is now available for the
[RGBA8-only infrastructure stage](NTX4.md); validation remains pending.
The controls are separate from texture resolution, cache size, prefetch and
midpoint presentation. When a stage becomes available, enable that stage's
implementation and fallback without enabling another stage. Keep baseline
selection available for rollback.

Build/profile output records the selected names and IDs and whether diagnostic
readback makes the run unsuitable for performance evidence. Any positive
`--test-frames` value triggers readback, even without `--diagnostics`.
An uncaptured profiling configuration is only eligible to collect evidence;
it is not proof of hardware performance or of absence of external capture.

Candidate `.ntx4` and `.ngm` outputs are excluded from Git like existing `.ntx`
and maps. Setup's separate `--candidate-textures` flag creates `.ntx4` alongside
`.ntx`; it is also disabled by default. No smaller texture format, cooker or
packed stream is produced by this stage.

## Workstream admission gates

| Stage | First reviewable implementation after its prerequisite gate | Evidence needed before promotion or combination |
| --- | --- | --- |
| Reference | Frozen sources, consumer inventory, controls and comparison records | Working matched executable, verified assets/saves, repeatable routes and recorded known visual issues |
| Format infrastructure | Engine-owned descriptor; single-level 2D RGBA8 `.ntx4`; retained RGBA8 wrappers; staged installer and fallback chain | Same canonical texels, lightmap processing, source mip, sampling, fallback and ownership across demand, queue and prefetch |
| Exact smaller formats | Final-texel representability checks plus union of all bitmap consumer requirements; native byte budgets before allocation | Exact channel reconstruction and GPU material/sampling comparisons; corrupt/missing/stale archive and cancellation coverage |
| Exact BSP cooking | Versioned fixed-width `.ngm`; float base vertices, separate float lightmap UVs and existing ushort topology | ARM binary32 parity, source identity, deterministic output, original triangle/material order and bounded loading/fallback |
| Batching/model cooking | Inputs for existing adjacent batching; later immutable model streams with exact bone metadata and palettes | Live compatibility checks retained, no duplicate payload retention, generic consumers and checkpoint/map transitions interchangeable |
| Exact packing | Explicit layout IDs throughout every consumer; 48→44 bytes using two byte bone-row offsets and unchanged float fields | All passes support the 44-byte fetch stride, identical palettes/topology, depth/color agreement and unchanged shader uniform capacities |
| Later experiments | Restricted ETC1; then independent alpha/compression, mip, normal, UV or weight trials | Separate quality and performance decisions for each asset/layout; rejection and exact fallback remain possible |

For packing, the first candidate stores row offsets directly (normally
0, 3, …, 84) at byte 32, two padding bytes, and the original float weights at
byte 36. No normalization, multiplication or new float uniform is needed.
Canonical CPU structures remain unchanged. Do not infer total memory savings
from the 8.33% reduction in this one GPU stream.

The [texture consumer inventory](TEXTURE-CONSUMERS.md) is source-level work.
No map-specific consumer graph exists yet. Unknown/shared CPU uses retain
RGBA8, even when the first observed material is GPU-only.

## Deferred comparison records

No route below has been executed for this snapshot. A later route record must
include its source/build/dependency identity, map and sidecar hashes, saved
checkpoint hash, difficulty, RNG/input conditions, start/end camera and actions,
console model/memory grant, stereo state, feature controls, texture resolution,
cache/prefetch/presentation settings, logging mode and capture status. A map
name alone is not a repeatable workload; reproducible input replay has not been
established here.

| Route to prepare | Principal coverage |
| --- | --- |
| 343 Guilty Spark, fresh load then repeated traversal | Cold demand, archive I/O, prefetch, foliage cutouts and warm residency |
| Silent Cartographer beach combat | AI/effects load, transparent order, memory/frame-time tails and regular-frame priority |
| Pillar of Autumn interiors and weapon close-ups | Lightmap seams, HUD/text, alpha masks, animation/skin palettes and precision |
| Matched glass, plasma and reflective surfaces | Channel interpretation, wrap/filter behavior, lighting/emission and fallback materials |
| Map/BSP transitions, checkpoint revert, Save and Quit, prolonged play | Generation changes, worker cancellation, fences, reclamation and persistent-state reconstruction |
| Sequential Original-model and stereo-capable checks | Existing compatibility policies, both eyes and resource limits |

Use controlled captures for correctness and separate uncaptured runs for
performance. Readback diagnostics may check renderer behavior but cannot
supply a performance claim. Existing [setup checks](DEVELOPMENT.md#building-the-setup-tools),
[GPU texture checks](../port/n3ds/tests/src/engine_gpu_texture_tests.c),
[model precision checks](../port/n3ds/tests/include/engine_model_precision_tests.inl)
and [checkpoint fixture](../port/n3ds/tests/include/engine_checkpoint_test.inl)
are available building blocks, not completed validation of this snapshot.

Begin future comparisons with native 400×240 regular frames and each feature
alone. Combine accepted textures with accepted cooked geometry, then accepted
packing. Repeat with optional midpoint presentation afterward. Preserve all
non-feature settings between pairs. Record saved bytes separately from runtime
benefit; measure CPU preparation, GPU queue time, demand stalls, peak memory,
displayed cadence and input latency separately. CPU/GPU/worker times overlap
and must not be summed into a synthetic frame time.

Keep later device/emulator batches bounded and sequential, with owned capture
and test processes stopped afterward. The current instruction remains
development only. The RGBA8 descriptor/archive source work was explicitly
authorized while reference validation remains pending. The evidence gates
still apply before promotion or combination; no defaults have been promoted.
