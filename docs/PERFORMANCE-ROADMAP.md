# New 2DS XL performance roadmap

The local frozen native-400 reference can be rebuilt byte for byte. The
[algorithm build controls](ALGORITHM-PROFILING.md) expose experiments for
prepared views, retained skinning and pose data, exact geometry, collision and
UV output packing. The physical-device reference, campaign repeatability,
bottleneck report and promotion gates remain open.

The [implementation status](PERFORMANCE-IMPLEMENTATION-STATUS.md) accounts for
every step below. This document is the requested roadmap, not a completion
receipt; many sub-slices remain unimplemented.

Prioritise **CPU preparation overlapping GPU execution**, **preparation retained
across presentations**, and **geometry work eliminated before submission**.
Smaller arithmetic, shader and scheduling changes follow as independently
measurable slices. Collision work moves forward if simulation limits delivery.

The target is one coherent presentation per New 2DS XL refresh: approximately
59.83 Hz / 16.71 ms, with Halo's authoritative simulation held at 30 Hz. These
are different clocks; do not lock simulation to half the display refresh or
count a submission, frontend loop or repeated image as a distinct presentation.

## Reference evidence and preservation contract

| Item | Established state |
| --- | --- |
| Frozen source | `a43d94f35fbd0bdfdd0c30d7f5f5949f57c48904` |
| Frozen native-400 3DSX SHA-256 | `b89168c853add4111c6aca13f1d84a51582fe14ff98ac7269d7178dbc3bc69e5` |
| Reference settings | Native 400×240, legacy textures, 4 MiB texture budget, standard memory, portal cache and resident indices enabled. Prefetch and optional midpoints are off in the regular reference. |
| Build evidence | The timestamp-pinned rebuild from the archived source and compiler inputs reproduced the native-400 launcher exactly. |
| Runtime evidence | The current profiled reference completed a bounded 240-loop Azahar menu smoke test. Earlier emulator graphics diagnostics encountered backend sampling differences. |
| Open evidence | Campaign and midpoint images, replay repeatability on campaign data, hardware timings and instrumentation overhead. |

The frozen snapshot under `build/performance-reference` includes the regular
and trace builds, matching local game data, settings, saves and dependency
provenance. `manifest.json` identifies private data and `reproduction.json`
records the rebuild. The older launcher and emulator receipts describe their
own binaries. Keep game data and SDK copies in private local storage.

Keep native 400×240 and full-resolution HUD; current texture resolution,
filtering, LODs, foliage, particles, material/lightmap passes, transparency order,
emission and effects; actual process memory mode and budgets; and the existing
midpoint admission policy. Keep authoritative input, simulation, AI, collision,
scripts, RNG consumption, animation events/timing and checkpoint layout.
Keep existing arithmetic order/precision unless a separately controlled slice
explicitly evaluates a change. Renderer caches stay reconstructible and outside
saved state. GPU-visible allocations remain immutable until their existing fence;
canceled workers retain owned allocations until completion is acknowledged.

The [inherited visual limitations](RENDERER-PRESERVATION.md#preservation-contract)
still require reference captures. Preserving the fork's accepted appearance does
not establish complete Xbox-renderer parity. Asset controls remain independent:
RGBA8 `.ntx4`, scoped `.nmg` compaction plans and the packed weighted vertex
layout have separate opt-in controls.

## 1. Reference and dependency trace

Freeze a complete source/build reference with untracked WIP, dependency revisions
and dirty state, compiler/library/tool binaries, settings, executable/ELF/link-map
hashes, map/sidecar identities and private starting saves. Existing receipts are
inputs, not proof of a complete reproducible campaign baseline. Restore/build the
reference separately, define routes and known visual captures, then establish
reference-versus-reference repeatability before evaluating candidates.

The optional `--dependency-trace` source implementation complements the
[existing aggregate profiler](ALGORITHM-PROFILING.md). It is independent of
counters, reference/candidate/verification modes, asset controls and midpoint
presentation. `--dependency-trace-loops N` defaults to 120 completed frontend
loops after gameplay first starts. The build manifest records its selection
and fixed capacities. It rejects readback and validation fixtures; reaching
the capture limit stops collection without ending the game. Export occurs
during shutdown. See [the trace contract](DEPENDENCY-TRACE.md) for scope and
limitations. Measure trace/counter overhead before interpreting timings.

Each record needs a run ID, monotonic timestamp, event/sequence ID, loop ID,
simulation tick plus restore epoch, render-attempt/view ID, actual submission ID
when one exists, regular/midpoint kind, map/BSP generation and optional job ID.
Do not use tick alone as identity: restores can rewind it, one loop can execute
multiple ticks, and a canceled midpoint has no submission. Retain originating
context for delayed completion records instead of relabeling them as the current
frame. Use explicit fixed-width fields and schema/clock versions on disk.

| Boundary | Existing source seam | Required distinction |
| --- | --- | --- |
| Simulation | `game_time_update` / `game_tick` in `source/game/game_time.c` | Tick start/end and restore epoch; simulation executes once |
| CPU preparation | `mission_render_frame` in `port/n3ds/src/engine_mission_render.c` | Attempt/view IDs; services, camera/frusta, portal visibility and object discovery separate from encoding |
| Worker dispatch/start/end/join | `n3ds_parallel_range` / `worker_main` in `port/n3ds/src/engine_parallel_platform.c` | Owner publishes job context; worker writes bounded per-job timestamps; owner collects after the existing join |
| GPU acquisition | `begin_frame` in `port/n3ds/src/engine_gpu_renderer.c` | Request/acquired timestamps, prior queue ID, configured synchronization, midpoint rejection |
| Submission | `n3ds_gpu_present` / `algorithm_gpu_submit` | Allocate identity before asynchronous callbacks can observe it; record only actual submissions and whether top output was queued |
| Completion | `algorithm_gpu_retire` and audited Citro3D completion path | Owner-observed retirement is an upper bound on completion time, not the hardware completion instant |
| Presentation | `n3ds_gpu_top_swap_posted` / `top_swap_visible` | Swap posted, acknowledgement observed and refresh index are different facts; correlate to the submitted top image |

Include fences inside texture services as well as `FrameBegin`, and distinguish
the later top-swap wait in `n3ds_gpu_present`. Existing GPU preparation timing
spans recording, final flushes and possible presentation waits; it is not a pure
CPU preparation measurement. Existing submit-to-swap-posted timing does not
measure the latch. The pinned Citro3D completion callback can supply a separate
queue-completion observation after its ownership and timer order are audited.

The trace source extends swap observation to regular-only comparisons without
enabling midpoint admission. `top_swap_visible` observes
`!gspIsPresentPending(GFX_TOP)` after a known posted sequence; this is a sampled
acknowledgement, not an exact latch/scanout timestamp. The observation path must
not change admission or add waits. A VBlank callback
alone does not identify a newly displayed image. Preserve atomic publication
ordering; callback/worker producers must not call the owner-only collector.
Use bounded per-producer storage, drained by the owner, or another reviewed
publication protocol. Record timestamp precision and observation uncertainty.

Allocate fixed-capacity trace storage before gameplay. Keep clocks at stage/job
boundaries, never in inner vertex loops. Do not log synchronously per event, add
GPU waits, read back, overwrite unread events silently or block producers when
full. Report drops, overflow, missing pairs and pending final work; incomplete
coverage cannot be interpreted as zero stalls. The implemented owner storage
retains at most 8,192 64-byte events, plus bounded callback/VBlank rings and
submission metadata. Its default 120-loop capture is a short dependency sample,
not coverage of a 90–120-second route. Raising the loop limit does not enlarge
storage. Label actual retained coverage and run whole-route timing separately.
Preserve the existing exit timeout and cleanup rules; a trace capture limit
alone is not a process timeout.

Allocation accounting includes loop-end ordinary/linear heap samples, opt-in
completed allocation-call peaks and event-based texture staging peaks.
Internal and concurrent transient allocations can escape these samples.
Track ordinary-heap, linear and staging
live/high-water bytes separately, plus
reservations, failures and GPU-retained allocations. Staging can be a subset of
ordinary/linear storage: label it rather than adding it twice. Free-space samples
are sampled minima, not exact allocation peaks. Account for successful
allocation/free and realloc failure/zero-size semantics without changing budget
decisions; report untracked allocators and fragmentation limits explicitly.

The [action journal](ACTION-REPLAY.md) implements bounded tick-indexed replay
at the authoritative input-consumption boundary and compares selected semantic
state fields. Full gameplay/presentation coverage and reference repeatability
remain open. It does not replay frontend-loop controller polls. Preserve `update_client_dequeue`
in `source/game/player_queues_new.c`, preserving its latched-control bookkeeping.
Record starting state/seeds and
the tick's consumed controls; distinguish presentation-only camera input. Compare
canonical object identities, transforms, velocities, health, ammunition, timers
and relevant RNG/script state. Exclude pointers, padding and renderer caches;
retain float bits where exactness is required. Version the semantic record and
document uncovered state. Account for both global and local RNG streams: the
checkpoint header's global seed alone does not capture the separate local stream
initialised through time/`rand` in `source/math/random_math.c`. Establish which
consumers affect replay, or disclose that dependency as uncovered. Shadow only
pure calculations on immutable inputs,
never a second execution of simulation. First prove the reference reproduces
itself across the defined routes.

**Gate:** reproducible working hardware reference, fixed workloads, known visual
captures, independent rollback controls and an initial completion-path report.

## 2. CPU preparation overlapping the preceding GPU submission

`mission_render_frame` currently calls `rasterizer_frame_begin` before frustum,
portal visibility and object discovery. `_rasterizer_frame_begin` performs
regular-frame services and acquires the GPU; `begin_frame` then retires prior
work and resets GPU arenas. Extract CPU preparation; do not simply move the
existing frame-begin function or its arena resets.

Some overlap already exists: the previous asynchronous submission can execute
during pacing, next-loop services, simulation and audio. The new opportunity is
the eligible preparation still after acquisition, not that entire GPU duration.
Texture maintenance before acquisition may itself fence, so moving work past
one wait must not merely relocate that wait elsewhere.

Start with the regular mono view immediately following a submitted midpoint.
Keep one simulation thread. Own camera/frusta, ordered visibility results,
candidate object IDs and generation information in a bounded prepared-view
record. Use logical identities or explicitly pinned resources, not pointers
invalidated by map changes or texture maintenance. Capacity/failure/staleness
must fall back to the ordinary preparation path before consuming partial data.

Split once-per-keyframe services, preparation and GPU acquisition explicitly.
Preserve frame/scene indices, presentation time, first-person updates and
render-side effects. Services requiring retirement, GPU arena reuse, command
emission and resource publication stay behind their fence. Audit object-list
discovery separately from drawing and render-state allocation.
Keep midpoint commands unsubmitted until admission succeeds: early chunk
submission would invalidate the current whole-frame discard contract.

The isolated bound is `G + P + E → max(G, P) + E`; savings cannot exceed
`min(G, P)`. Compare ordered visibility/object results at identical camera/time
inputs, then rendering. Record moved preparation, residual acquisition wait,
discarded/stale records, retained bytes and actual completion/presentation
delivery. Reduced wait alone is not a frame-time win.

## 3. Preparation retained by dependency

Extend existing part/pose caches, beginning with ordinary opaque objects.

| Prepared data | Exact invalidation dependency |
| --- | --- |
| Topology, part metadata, bone use | Model/map generation |
| Regions, object colours, effect parameters | Consumed object state |
| Posed positions/normals, inverse-bind products | Consumed pose values |
| Visibility, LOD, camera-relative data, sort keys | Current view |
| Animated material values | Presentation-time inputs |

Two views can ideally change `2(I + V) → I + 2V`; view-dependent work remains.
Do not reuse portal clipping based on cluster identity or union endpoint
visibility to approximate a midpoint.

Independently retain `{position, raw_normal}` for exact geometry/pose identity
in CPU skinning. Derive each consumer's lighting, UVs and normalization in its
existing operation order. Keep validation and failure/fallback behavior, and
bounded midpoint storage that cannot overwrite queued or reusable regular data.
Compare all intermediates and final attributes; record reuse/rebuild reasons,
transforms avoided, copies, time, retained memory and eviction/fallback changes.

## 4. Exact animation and pose reuse

Implement independent switches in this order, subject to measured cost:

1. Borrow complete cached poses for root matrix/velocity queries, copying node
   zero immediately and the first root before a second lookup. Decode full poses
   on misses initially; preserve every current eligibility/fallback check.
2. Prepare validated compressed-track descriptors, constants and bounded key
   caches. Check the existing interval before search; preserve seek/wrap/reverse
   behavior. Keep quaternion endpoints unnormalized where interpolation expects
   them, final normalization and the distinct exact-key path.
3. Cache complete inverse-bind product arrays by model generation and consumed
   matrix values. Audit every material/centroid/fallback consumer before pruning
   bones.
4. Lazily cache endpoint matrix-to-quaternion conversions in both snapshot banks.
   At 512 nodes this is approximately 16 KiB of values plus validity metadata;
   preserve discontinuity and interpolation gates.
5. Evaluate unchanged local hierarchy conversions after overlays/preprocessing.
   World reuse also requires unchanged parent world transforms; retain all
   postprocessing and bounds updates.

Compare roots, orientations, relative matrices and midpoint poses across sparse
and constant tracks, exact/fractional keys, overlays, pressure, object reuse,
teleports and restores. Measure actual decode/conversion/product costs and reuse.
Root-only miss decoding follows only if later full-pose requests do not usually
erase its benefit; partial cache entries must never appear complete.

## 5. Exact geometry and submission traffic

Extend `engine_model_compact.inl`: deduplicate bit-identical complete attribute
tuples only within compatible draw contexts, resolve hash collisions by exact
comparison, order storage by first use and remap indices. Preserve primitive and
corner order, winding, seams, weights and every auxiliary stream. Reconstruct
each original indexed corner to prove parity. This is separate from the initial
offline archive, which must first reproduce current decoding/compaction exactly.

Build/version the cooker against runtime conversion, retain source identity and
fallback, and establish host/ARM float parity. Never infer parity from similar
looking geometry. Keep admission bounded and midpoints resident-only.

Separately evaluate stable compact bone palettes without disturbing identity
palettes, exact influences or 28/29-bone shader limits. Extend adjacent batching
only on complete emitted-state equality, with exact constants, current texture
handles and unchanged primitive order/material restrictions. Track duplicate
prevalence, resident/upload bytes, preparation time, palette uniform writes,
command bytes, actual draw savings and completed queue time.

Exact 48→44-byte GPU layouts remain a separate later change supporting every
colour/depth/special pass. Their 8.33% stream saving is not a frame-time forecast.
Native texture formats remain independently controlled; prioritise them when
residency/demand stalls warrant it. Within the asset workstream keep format-aware
textures → exact cooked geometry → packed layouts, with separate parity gates.
Compression, mipmaps and quantization remain independently evaluated extensions.

## 6. Small GPU program slices

| Order | Candidate | Required constraint |
| --- | --- | --- |
| 1 | Combine identical UV operations across output-register halves | Preserve every enabled output lane |
| 2 | Boolean-uniform selectors for draw-wide zero/one decisions | Install/restore state across all program/material transitions; retain per-vertex tests |
| 3 | PICA non-fused `MAD` for the second weighted term | Hardware arithmetic confirmation; consistent colour/depth/cloak positions |
| 4 | Water base pass skips unused reflection work | Keep both draws, blending/order, UV/position operations and valid outputs |

Expected source reductions are only local hypotheses (roughly one instruction
for combined UVs and two for selectors or position/normal skin accumulation).
Inspect assembled programs, verify arithmetic and matching colour/depth output,
then measure whole-scene queues. Do not combine with reassociation, approximate
reciprocal square roots or global fast-math.

## 7. Collision searches

If collision limits delivery, advance these independent slices:

1. Query-local bounded feature membership replaces accepted-list scans, retaining
   first-seen order and each 256-entry cap. Return immediately for a full list;
   keep a small-list linear path if its measured cost is lower.
2. Active-plane lookup uses the full oriented designator, first active match and
   exact duplicate/push/pop behavior. Reuse the ancestor's original distance/dot
   results without changing arithmetic.
3. Cache identical posed inverses or immutable topology only where measured
   reuse covers lookup/storage cost. Keep mutable flags and traversal order live.

Scratch stays query-owned with the original context ABI. Do not cache outcomes
or suppress queries. Compare complete ordered features, counts, hits, selected
surfaces/materials, planes and fractions, including tangency, signed zero,
repeated planes, breakables and saturation. Report whole-query time and probes.

## 8. Conservative block rejection

Evaluate static one-sided terrain backface blocks before software occlusion.
Cook bounds/cones for contiguous blocks and reject only when every triangle is
provably back-facing under the current projection/cull convention. Merge retained
spans through `engine_bsp_index_visibility.inl`, retaining triangle order rather
than emitting a draw per block. Extend view-dependent cache identity: current
portal-mask keys are insufficient for camera-dependent cone results. Measure
lost full-list batch merges as well as rejected geometry.

Uncertain bounds, broad cones, numerical boundaries and unsupported geometry
remain visible. If hidden-draw pressure persists, separately evaluate current-view
conservative occlusion with validated opaque occluders. Larger-is-nearer depth
requires `candidate maximum depth < occluder minimum depth` at every covered
sample; holes, equality and uncertainty retain the draw. Simulation, animation
and collision continue. Require a conservative proof plus adversarial colour/depth
comparisons, CPU build/query/copy cost, actual draws and GPU queue measurements.

## 9. Worker scheduling and remaining kernels

The current parallel API joins before returning. Only after preparation owns
its inputs, allow bounded pure jobs to overlap unrelated owner work and join at
first use. Keep the existing worker resources, draw order, generation/cancellation
checks, source pins and owner-only GPU publication. Stack contexts and reusable
midpoint scratch cannot outlive their call. Measure dispatch age, whole-batch
time, first-use wait and delivery; do not add overlapping main/helper durations.

Admit remaining changes from the new profile: interleaved scalar VFP with the
same per-component arithmetic; support-corner classification with exceptional
fallback; incremental cluster/material memberships; compact CPU-skin inputs;
cheaper pose eviction; exact contrail indices and unused billboard/LOD work.
Citro3D dirty-uniform tracking requires measured scan cost and an audit of every
public write path, constants, program changes and suspend/resume restoration,
preserving commands and synchronization.

## 10. Coherent presentation, comparison and promotion

Measure regular rendering first. Repeat accepted changes with the midpoint policy
unchanged. Audit attachments, widgets and effects that current pose history does
not cover: more images alone do not establish coherent motion. Required render-only
snapshots must preserve parent/child transforms, birth/death, discontinuities,
authoritative events and timing. Treat coverage as a separate correctness slice;
do not introduce new effect shaders or quality reductions.

Use `--present-60 --midpoint-capture-tick N` for a dedicated correctness run.
The fixture searches 120 simulation ticks from N for one submitted midpoint
and its following regular render. It writes native 400×240 top-target RGBA,
raw D24S8 and precise camera inputs for both views. Require the complete-pair
manifest. The readback perturbs the schedule and is excluded from timing. Integrate
accepted slices individually, compare
against both previous accepted and original reference builds, and re-profile
after major changes.

| Promotion gate | Required evidence |
| --- | --- |
| Correctness | Exact relevant CPU/geometry outputs, representation/rendering parity, no canonical gameplay divergence |
| Ownership | Map/BSP/restore/cancellation/allocation-pressure coverage and valid GPU-storage lifetime |
| Local effect | Intended work actually decreases; storage reduction is not itself a speed claim |
| Frame benefit | Repeatable improvement on the delivery path beyond baseline variability and instrumentation overhead |
| Regression | No unexplained worsening of tails, regular-frame delivery, input latency, pressure or fallback frequency |

Use bounded physical New 2DS XL runs with verified CPU/L2 mode and memory grant,
fixed assets/options, documented warm-up/cold/warm policies and alternating A/B
order. Start with five paired 90–120-second runs per representative route;
extend only when uncertainty warrants it. Treat runs as experimental units,
retain slow frames, and report paired differences, uncertainty, p50/p95/p99,
worst cases, deadlines and transitions separately.

| Route | Coverage |
| --- | --- |
| Pillar of Autumn (`a10`), from an interior checkpoint through a corridor and weapon reload | Animation, transforms, seams, precision, HUD |
| Silent Cartographer (`b30`), from the approach to a combat encounter through its end | Simulation, collision, audio/effects and worker contention |
| Silent Cartographer coast (`b30`), shoreline traversal with deliberate pans toward and across water | Material passes, shader parity, blending and transparency |
| 343 Guilty Spark (`c10`), fixed checkpoint and traversal across a residency boundary | Visibility, residency, demand stalls |
| Dense actors, checkpoints/BSP transitions, extended play | Pressure, invalidation, lifetime |

Report distinct delivered presentations, missed/repeated refresh intervals and
consecutive stalls alongside simulation deadlines. Submission rate and average
FPS are supporting metrics. Keep cold loads and problematic routes visible.
Stop owned capture/test processes after each bounded sequential batch.

## First physical comparisons

1. Run the packaged regular reference on New 2DS XL from the fixed 400×240
   configuration. Record CPU/L2 mode, settings, the initial save and asset
   hashes, and an image per route. Use a fresh process for cold trials and an
   identical uncaptured lead-in for warm trials. Preserve cold stalls in the
   results. The route table defines coverage and starting points. Record the
   exact route actions and end tick on hardware.
2. Use the [action replay procedure](ACTION-REPLAY.md) twice with the reference
   and the same copied starting data. Treat an early exit, unavailable action
   file or a state mismatch as a failed route.
3. Run short trace captures for stage attribution and compare instrumentation
   overhead against an untraced build. Trace intervals and sampled swap
   acknowledgements describe observed dependencies. Use independent display
   observation for distinct images, repeated refresh intervals and stall runs.
4. Compare each eligible candidate alone against the matching current-source
   reference, then against the frozen launcher. Start the overlap A/B with
   `--present-60` on both sides because the first prepared-view case follows
   a queued midpoint. Keep midpoints off for the first regular-only comparison
   of skinning, geometry, collision and the UV variant.
5. Audit motion on the same route. The pose history admits a bounded subset
   of units, items, scenery and devices. Attachments, widgets and many effects
   still need render-only snapshot coverage. Any extension must preserve
   parent/child identity, birth/death, discontinuities and authoritative events.

New switches must reject unavailable candidate/verification modes rather than
silently running reference code. No candidate becomes default merely because
its implementation or emulator smoke succeeds.
