# Algorithm comparison and profiling

The [performance roadmap](PERFORMANCE-ROADMAP.md) prioritises CPU/GPU overlap,
retained preparation and geometry work elimination. The build includes opt-in
experiments in those areas and smaller independent slices. It also includes
the [dependency trace](DEPENDENCY-TRACE.md), [action replay](ACTION-REPLAY.md),
heap and texture staging metrics, and a midpoint target capture fixture.

The current native-400 reference configuration passed a bounded 240-loop
Azahar menu smoke test. The earlier graphics diagnostic suite encountered
backend sampling limitations. Campaign repeatability, hardware timing,
instrumentation overhead and midpoint behavior are still open.

See the [complete scope inventory](PERFORMANCE-IMPLEMENTATION-STATUS.md) for
implemented, partial and unimplemented parts of the ten-step request. The nine
selectors do not constitute completion of those ten steps.

Private receipts and commands are retained in
`build/emulator-validation-20261107`, including `radar-diagnosis.json` and
`azahar-native400-menu-05/result.json`. Dependency recovery uses an explicitly
mixed Arm GNU/Clang toolchain, retained devkitARM runtime libraries and external
OpenCE SDK-free declarations with recorded local additions; it does not establish
equivalence to the original compiler or Microsoft XDK. The working-reference gate
below remains open. The earlier snapshots describe their capture-time state.

## Preserved source and open reference gate

The current baseline receipt lives under `build/performance-reference`.
`latest.txt` points to the private snapshot of clean source at
`a43d94f35fbd0bdfdd0c30d7f5f5949f57c48904`, compiler inputs, regular
native-400 and trace executables, maps, sidecars and saves. The manifest
records build commands, dependency provenance and SHA-256 identities. The
timestamp-pinned rebuild reproduced the regular 3DSX byte for byte. Save
compatibility and campaign visuals still need physical verification.

The earlier pre-instrumentation source snapshot records this historical state.

| Field | Receipt |
| --- | --- |
| Directory | `Halo2DS-algorithm-reference-20261107T082415Z` |
| HEAD | `a9d7b2fe53f488c657e03ac09a34947deb6bbcb3` |
| Scope | 1,035 tracked and non-ignored untracked files, including WIP; 14,740,261 source bytes |
| `source.tar.gz` SHA-256 | `065e224f7e6c573eaf7accddfd2e40bf5032691700b26dc3ceb5cb8cadff1aa0` |
| Receipts | `manifest.json`, working-tree/index patches and NUL-delimited Git status |
| Executables, build manifests, maps, sidecars, saves | None found in this checkout |
| Configured dependencies | devkitARM, libctru, Citro3D, XDK, Picasso and 3dsxtool absent; Clang file hashed without executing it |
| Reference gate | Open work remains: working executable, dependency revisions, matched assets/saves, reproducible routes and known visual captures |

The tarball restores source into an empty directory; patches alone omit
untracked files. Ignored/private game assets and SDKs are not in the tarball.
The snapshot is reproducible source, not a validated executable. A later
instrumented build must retain its own source receipt, dependency versions and
dirty state, command/options, executable/ELF/link-map hashes and asset/save
identities. Do not substitute documented library revisions for observed ones.

The [preservation contract and inherited visual limitations](RENDERER-PRESERVATION.md)
remain applicable. Keep native 400×240, 30 Hz authoritative logic, animation
timing, arithmetic order, material/texture/LOD choices, memory admission rules,
GPU fences and checkpoint representation fixed. These counters live outside
saved state. `.ntx4` stays independently opt-in and disabled by default.

## Independent controls

Every algorithm defaults to `reference` (ID 0). `candidate` (ID 1) executes
one experiment. `verify` (ID 2) additionally checks equivalent pure work or
validates the derived data at publication. Verification is excluded from timing.

| Available option | Candidate scope |
| --- | --- |
| `--algo-prepared-view` | Move frusta, portal visibility and ordered object discovery before the GPU fence for a regular mono view following a queued midpoint. Requires `--present-60`. Verification draws using the late reference results. |
| `--algo-shared-skinning` | Reuse exact cached raw normals for paired CPU stream consumers in water and native glass. Skip lighting those paths replace. Midpoint misses leave reusable regular pose entries intact. |
| `--algo-inverse-bind` | Reuse complete inverse-bind product arrays when generation, model, object and matrix bytes match. Separate regular and midpoint banks. |
| `--algo-midpoint-quaternions` | Retain exact endpoint quaternions lazily in each snapshot bank. Requires `--present-60`. |
| `--algo-root-pose` | Copy node zero from a complete pose entry. Cache misses still decode full poses. |
| `--algo-model-compact` | First-use order and exact tuple deduplication in the existing opaque resident compactor. Hash collisions resolve by full comparison. |
| `--algo-collision-features` | Query-local membership for accepted features in first-seen order. |
| `--algo-collision-active-planes` | Query-local lookup by oriented plane, preserving the active stack's first match. |
| `--algo-uv-outputs` | Candidate only. Picasso assembles one fewer instruction in each of rigid and skin lit/reflect. Physical colour/depth comparison remains required. |
| `--algo-retained-skin` | Reuse posed positions and raw normals across UV and lighting changes. Normal consumers receive a frame-owned copy. Additional normal storage is capped at 512 KiB on New models and 128 KiB on Original models. |
| `--algo-region-selection` | Retain resolved permutation geometry indices across LOD changes within the existing part cache. Each view still selects LOD and preserves part order. |
| `--algo-compressed-tracks` | Retain validated constant values and raw key endpoints for map-owned animation streams. Seek and interval misses retain the reference search and exact-key path. |
| `--algo-local-hierarchy` | Reuse unchanged local child transforms after overlays. Reuse world results when the parent matrix bytes also match. |
| `--algo-vfp-schedule` | Group loads and stores and interleave independent ARM11 VFP calculations in matrix multiply, point/vector transforms and orientation conversion. Each output keeps the reference operation order. Verify mode compares the scheduled results with the reference. Overlapping orientation storage uses the reference path. |
| `--algo-stable-palette` | Order compact palettes by original bone index. Identity palettes, influences and shader limits are preserved. |
| `--algo-deferred-decode` | Dispatch the owned tail context to the existing core 2 worker. Classify the decoded prefix before joining at the first tail vertex. |
| `--algo-bsp-resolved-batch` | Candidate only. Extend adjacency merging within the opaque terrain pass when emitted texture, sampler and material state match. |
| `--algo-boolean-selectors` | Candidate only. Boolean uniforms replace float tests in reflection, water and glass programs. |
| `--algo-skin-mad` | Candidate only. Use PICA MAD for the second weighted term across skin programs. Native arithmetic equivalence requires device evidence. |
| `--algo-water-base` | Candidate only. Water base pass bypasses reflection work. The reflection path pays one additional branch. |
| `--algo-midpoint-particles` | Candidate only. Interpolate eligible detached positions and borrow same-view nodes from accepted ordinary parent draws for eligible attached sprites. Requires `--present-60`. |

The following reserved options accept `reference` only. The build CLI and
compiler guards reject unavailable modes.

| Option | Proposed slice |
| --- | --- |
| `--algo-support-corners` | CPU-C: two-corner rectangle classification |
| `--algo-adpcm-table` | AUDIO-A1: exact packed lookup words |
| `--algo-adpcm-loops` | AUDIO-A2: exact mono/stereo loop specialization |
| `--algo-root-decode` | CPU-D: partial root-only cache population |
| `--algo-collision-inverse` | CPU-E1: reuse identical posed inverses |
| `--algo-collision-topology` | CPU-E2: reuse immutable topology preparation |
| `--algo-material-candidates` | CPU-F: incremental owning-cluster counts |
| `--algo-cpu-skin-stream` | CPU-G: compact scalar input stream |
| `--algo-pose-eviction` | CPU-H: bounded recency bookkeeping |
| `--algo-audio-split` | Fixed-cost contiguous audio split |

The build manifest records every selection and ID. Startup logs record each
implemented selector and whether verification is active. Asset representation
controls remain separate.

Profiling additions default to off.

- `--algorithm-profile` requires `--performance-profile` and enables the
  bounded counters described below. Ordinary performance profiling remains
  independently available for overhead comparison.
- `--allocation-profile` requires `--algorithm-profile`. It observes ordinary
  and linear arena usage after covered allocation calls and reports event
  peaks alongside loop samples. Its per-call allocator queries exclude it
  from promotion timing. Public ordinary allocation wrappers and the native
  linear allocator feed a fixed owner-drained collector. Internal libc and
  simultaneous transient allocation peaks can escape these observations.
- `--profile-frames N` requires profiling, accepts 1–18,000 completed frontend
  loops, and exits through normal teardown without selecting readback fixtures.
  It rejects `--test-frames`, diagnostics and validation fixtures. It counts
  loops including menus/pause, not simulation ticks or displayed frames.
  Zero retains the existing unbounded interactive lifecycle. This does not
  replace an external timeout for later automated runs or hangs.

The separate `--dependency-trace` option requires neither profiling flag.
`--dependency-trace-loops N` controls its bounded capture, defaulting to 120
completed frontend loops after gameplay first starts. It freezes collection
without ending gameplay, then exports during shutdown. Readback and validation
fixtures are rejected. Trace configurations compile, and the offline reader has
synthetic regression tests; the menu smoke receipt does not validate a gameplay
trace or establish console timings. See the
[trace contract](DEPENDENCY-TRACE.md) for fixed storage, missing observations
and the distinction between queue completion, swap acknowledgement and scanout.

A future configuration example (not executed):

```text
python3 port/n3ds/build.py --performance-profile --algorithm-profile --profile-frames 3600 --fixed-world-width 400
```

Dependencies and route setup are still required. Any positive `--test-frames`
continues to select the existing readback path; do not use it for uncaptured
timing. Optional midpoint runs use their separate presentation control and
must follow regular-frame comparisons.

## New measurement coverage

The CPU, I/O and GPU metric-list headers define exact names and units. Counts
and timings do not alter decoder, cache, worker-split or rendering decisions.

| Domain | Added owner-side evidence | Limits |
| --- | --- | --- |
| Animation | Root matrix/velocity requests; full-pose calls; cache hit/miss/bypass; compressed/raw decode requests and nodes; explicit pose copies; interpolation/hierarchy work | Root-pose candidates copy only node zero on complete-pose hits; misses still use the full decoder. No root-only miss decoder or tracking of later full requests. |
| Transforms | Portal point work and collision-model point/vector/inverse calls | Not global VFP traffic; worker kernels are not instrumented per vertex |
| Visibility | Rectangle and plane work, cluster visits, portal cache/project work, candidate-mask hits/misses/bypass and triangle-owner scans; prepared-view verify compares ordered owned results | Prepared-view comparison covers only the eligible mono view following a submitted midpoint. No incremental membership algorithm. |
| Model preparation | Decode/pose/normal hits and misses, attempted vertex batches, explicit shadow/matrix copies, budget evictions and fallback/failure counts | Attempted batches may stop early; counts alone cannot establish CPU cost |
| Audio | Complete native update and planning/decode-join/submission ticks, input bytes, jobs, output frames/packets, active voices and refill-drain events | Native update can run more than once per loop; drain events are not proof of audible dropouts; whole `sound_render` timing remains separate |
| Parallel work | Serial and worker batch/item counts by audio/decode/pose category; complete batch, wake, main/helper/join ticks and batch maximum | Main/helper execution overlaps; the complete batch is the comparison unit; scheduling is unchanged |
| Sound cache | Request/hit/load origins, staged-copy and synchronous-read bytes/time, allocation/eviction/admission and scan counters | Does not time the background encoded reader or filesystem contention |
| GPU | Actual submissions/completed queues, queue/preparation/retire-wait time, local draw/triangle/pass counts, frame-arena flush bytes, command-use peak, successful adjacent merge savings | Queue includes transfers; retire wait can include VBlank; local draw counters exclude other GPU modules and can include discarded midpoint preparation |
| Memory | Ordinary and linear heap sampled peaks at loop end, and event-based texture staging peak for synchronous and asynchronous inputs and unpublished output payloads | Heap samples may miss transient peaks. Texture staging excludes allocator overhead, audio scratch and unrelated GPU staging. |

The shared collector is main-owner-only. Workers write existing per-job
storage; the submitting owner records it after the existing completion fence.
There are no new locks, allocations, worker threads, per-vertex clocks or GPU
waits. Disabled instrumentation macros discard arguments without evaluating
them. Profiling builds necessarily add work; its overhead is unmeasured.

## Attribution and bounded reporting

`ALGO RUN` supplies a run-local clock identifier, timer frequency and counter
storage size. Retain it with the source/build and external run receipt; it is
not a globally unique experiment identity. Fixed RAM windows contain 64-bit
saturating counters. `overflow=1` invalidates exact totals for that window.
Zero counters are omitted; maxima have a `_max`/`peak` metric name rather than
being mixed with sums.

`ALGO WINDOW` identifies each counter group by run, loop range, simulation tick
range, cache/BSP generation, BSP and kind (`0` frontend, `1` regular rendering,
`2` midpoint). Tick context is refreshed before each existing simulation tick
and frame update; location context changes at tag/BSP publication and unload.
`loop_gameplay` records gameplay eligibility at the **start** of the loop, not
every event's instantaneous menu/pause state. Windows split on
location/eligibility changes when counters are recorded.
Tick boundary labels can move backwards after a checkpoint restore or when a
delayed GPU record arrives for the same loop. Reporting flushes after 120
completed frontend loops; a delayed completion can extend the printed loop
range beyond that count. The range is not a sample denominator. Final shutdown
stops collection before teardown so cleanup cannot masquerade as gameplay.

These are aggregate context ranges. Existing
30/120/300-frame reports and these windows cannot be joined as if their samples
are identical. They supply initial demand/work attribution. The separately
selected dependency trace adds event identity and an offline reader for interval
analysis. Hardware capture and campaign reference repeatability remain open.

GPU completion metrics carry a copy of the actual prior submission context,
including midpoint/regular identity. Actual submission IDs are assigned only
at submission. Preparation has ID zero; the printed nonzero submission range
describes submission/completion metrics, not a claim that every draw was sent
to PICA. A canceled midpoint gets a cancellation count. The last unretired
queue is reported pending at exit rather than adding a wait to measure it.
Resource teardown keeps its existing fences.

Summary formatting occurs after the normal paced loop or when a window must
split. It uses the existing bounded log queue; final `dropped`/`io_errors`
receipts must accompany any later results. Missing log records, overflow or
incomplete final queues must not be silently counted as zeros. CPU, helper,
GPU and I/O times overlap and must not be added into a synthetic frame time.

## Bottleneck inventory: measurements pending

No current-snapshot gameplay trace exists. There is therefore no defensible
ranking by milliseconds or predicted frame-rate gain. Existing historical
comments and source `PASS` strings are not evidence for this checkout.

The added counters can establish demand and worker/cache behavior once runs
are authorized. Remaining instrumentation includes sampled caller costs for
global transforms/animation, subsequent full-pose requests after root misses,
collision topology and repeated inverse identity,
incremental changed memberships, scalar fallback input/validation traffic,
texture-worker dispatch/queue age, transient heap peaks between samples, and
per-frame GPU queue distributions. Admit substantial candidates only after
those measurements identify meaningful completion-path work.

Prepare these workload definitions before executing comparisons; none is a
recorded/replayed route yet:

| Workload | Required coverage |
| --- | --- |
| Pillar of Autumn interiors/weapon close-ups | Animation, transform callers, material precision and HUD |
| Silent Cartographer combat | Collision/AI, effects, audio and worker contention |
| Fixed water-heavy view | Both water passes, moving camera, masks, waves and blending |
| 343 Guilty Spark cold and repeated traversal | Visibility, streaming and cache behavior |
| Dense repeated actors | Pose reuse, scalar fallback and eviction |
| Revert, BSP transition, pause/resume, extended play | Ownership, invalidation and persistence |

Every route needs matched assets/sidecars, save/build identity, difficulty,
starting state/RNG, actions and end condition. Use the
[action journal and state comparison](ACTION-REPLAY.md) to establish
self-repeatability of the reference. Keep correctness captures separate from
uncaptured physical New 2DS XL comparisons; emulator timing is not hardware
evidence. Later promotion should use five alternating paired 90–120 second
runs with fixed warm-up/cold-warm policy, run-level uncertainty and retained
slow frames. Measure instrumentation overhead first. Preserve the fixed asset
configuration and then repeat after independently accepted asset changes.

No candidates can be ranked, promoted or combined based on this source work.
