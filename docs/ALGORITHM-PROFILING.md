# Algorithm comparison groundwork

The [performance roadmap](PERFORMANCE-ROADMAP.md) now governs prioritisation:
establish the physical-device reference and dependency trace, then prioritise
CPU/GPU overlap, retained preparation and geometry work elimination. The slice
labels and reserved controls below describe existing groundwork, not a measured
ranking or an instruction to implement small kernels first. The roadmap's event
trace, replay and memory-accounting additions remain proposed, not implemented.

Status: source snapshot, independent comparison controls and opt-in
instrumentation are implemented. The initial slice was development only;
the user subsequently authorized builds and emulator tests on 2026-11-07.
Current-source diagnostic and native-400 builds now compile and link. A bounded
240-loop frontend smoke test passed in macOS Azahar, with a visual menu check
and owned-process cleanup. The full graphics diagnostic suite did not pass:
Vulkan lacks custom texture-border colors, software rendering differs in cube
sampling, and this installed build cannot provide OpenGL. Campaign correctness,
hardware timing, instrumentation overhead and midpoint behavior remain unvalidated.
No algorithm candidate is implemented or enabled. No percentage improvement or
sustained 60 FPS forecast is supported by the current evidence.

Private receipts and commands are retained in
`build/emulator-validation-20261107`, including `radar-diagnosis.json` and
`azahar-native400-menu-05/result.json`. Dependency recovery uses an explicitly
mixed Arm GNU/Clang toolchain, retained devkitARM runtime libraries and external
OpenCE SDK-free declarations with recorded local additions; it does not establish
equivalence to the original compiler or Microsoft XDK. The working-reference gate
below remains open. The earlier snapshots describe their capture-time state.

## Preserved source and open reference gate

The pre-instrumentation source is frozen outside this repository:

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

All controls below default to `reference` (ID 0). `candidate` (ID 1) and
`verify` (ID 2) are reserved and **rejected**, both by the build CLI before
creating output and by direct compiler guards. They are not placeholder
implementations that silently execute the reference.

| Option | Proposed slice |
| --- | --- |
| `--algo-root-pose` | CPU-A: borrow cached root samples |
| `--algo-vfp-schedule` | CPU-B: schedule independent VFP accumulators |
| `--algo-support-corners` | CPU-C: two-corner rectangle classification |
| `--algo-water-base` | GPU-A: omit unused reflection work from water base pass |
| `--algo-adpcm-table` | AUDIO-A1: exact packed lookup words |
| `--algo-adpcm-loops` | AUDIO-A2: exact mono/stereo loop specialization |
| `--algo-root-decode` | CPU-D: partial root-only cache population |
| `--algo-collision-inverse` | CPU-E1: reuse identical posed inverses |
| `--algo-collision-topology` | CPU-E2: reuse immutable topology preparation |
| `--algo-material-candidates` | CPU-F: incremental owning-cluster counts |
| `--algo-cpu-skin-stream` | CPU-G: compact scalar input stream |
| `--algo-pose-eviction` | CPU-H: bounded recency bookkeeping |
| `--algo-bsp-resolved-batch` | GPU-B: compare complete emitted state |
| `--algo-audio-split` | Fixed-cost contiguous audio split |

The build manifest records every selection and ID. Startup logs record the
current all-reference configuration. Future implementations must update both
the available-mode guard and reporting; verification must then disqualify the
run from performance evidence. Asset representation controls remain separate.

Two profiling additions are available in source and disabled by default:

- `--algorithm-profile` requires `--performance-profile` and enables the
  bounded counters described below. Ordinary performance profiling remains
  independently available for overhead comparison.
- `--profile-frames N` requires profiling, accepts 1–18,000 completed frontend
  loops, and exits through normal teardown without selecting readback fixtures.
  It rejects `--test-frames`, diagnostics and validation fixtures. It counts
  loops including menus/pause, not simulation ticks or displayed frames.
  Zero retains the existing unbounded interactive lifecycle. This does not
  replace an external timeout for later automated runs or hangs.

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
| Animation | Root matrix/velocity requests; full-pose calls; cache hit/miss/bypass; compressed/raw decode requests and nodes; explicit pose copies; interpolation/hierarchy work | Root calls still use the existing full decoder; no root-only implementation or tracking of later full requests |
| Transforms | Portal point work and collision-model point/vector/inverse calls | Not global VFP traffic; worker kernels are not instrumented per vertex |
| Visibility | Rectangle and plane work, cluster visits, portal cache/project work, candidate-mask hits/misses/bypass and triangle-owner scans | No incremental membership algorithm or complete visibility-set comparison |
| Model preparation | Decode/pose/normal hits and misses, attempted vertex batches, explicit shadow/matrix copies, budget evictions and fallback/failure counts | Attempted batches may stop early; counts alone cannot establish CPU cost |
| Audio | Complete native update and planning/decode-join/submission ticks, input bytes, jobs, output frames/packets, active voices and refill-drain events | Native update can run more than once per loop; drain events are not proof of audible dropouts; whole `sound_render` timing remains separate |
| Parallel work | Serial and worker batch/item counts by audio/decode/pose category; complete batch, wake, main/helper/join ticks and batch maximum | Main/helper execution overlaps; the complete batch is the comparison unit; scheduling is unchanged |
| Sound cache | Request/hit/load origins, staged-copy and synchronous-read bytes/time, allocation/eviction/admission and scan counters | Does not time the background encoded reader or filesystem contention |
| GPU | Actual submissions/completed queues, queue/preparation/retire-wait time, local draw/triangle/pass counts, frame-arena flush bytes, command-use peak, successful adjacent merge savings | Queue includes transfers; retire wait can include VBlank; local draw counters exclude other GPU modules and can include discarded midpoint preparation |

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

These are aggregate context ranges, not individual per-frame traces. Existing
30/120/300-frame reports and these windows cannot be joined as if their samples
are identical. They supply initial demand/work attribution; per-frame tail
correlation and deterministic replay remain later work.

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
collision feature-cache/topology costs and repeated inverse identity,
incremental changed memberships, scalar fallback input/validation traffic,
texture-worker dispatch/queue age and allocation high-water marks, and
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
starting state/RNG, actions and end condition. Input replay and semantic state
comparison are not implemented here. First establish self-repeatability of the
reference. Keep correctness captures/dual-path verification separate from
uncaptured physical New 2DS XL comparisons; emulator timing is not hardware
evidence. Later promotion should use five alternating paired 90–120 second
runs with fixed warm-up/cold-warm policy, run-level uncertainty and retained
slow frames. Measure instrumentation overhead first. Preserve the fixed asset
configuration and then repeat after independently accepted asset changes.

No candidates can be ranked, promoted or combined based on this source work.
