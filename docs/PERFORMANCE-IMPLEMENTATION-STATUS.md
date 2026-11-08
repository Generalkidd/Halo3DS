# Performance implementation status

The continuation adds independent source experiments and local correctness
evidence. Each selector defaults to the reference. Physical promotion remains
open, including the 59.83 Hz assessment with Halo's 30 Hz authoritative simulation.

The local `a10` fixture recorded and replayed ticks 0–99 from fresh private
Azahar profiles. The same journal matched with the combined regular verification
build. This covers the opening campaign state and fields recorded in the journal.
The fixture uses neutral input and a fixed local RNG starting seed before campaign
creation. See [action replay](ACTION-REPLAY.md).

The continuation's frozen snapshot built in five configurations covering
regular reference, combined regular verification, midpoint capture verification,
midpoint profiling and allocation observation. The final verification run
matched the 100-tick journal, and the allocation run emitted ordinary and
linear event peaks. The 29 Python checks and focused ARM and host fixtures
passed. The four bounded Azahar runs shut down cleanly. The source inventory,
artifact hashes and test receipts live in `build/performance-continuation`.
The matrix covers the frozen integration source and its explicit performance
overlays. Later concurrent changes to the shared checkout require their own
build receipt.

This inventory distinguishes source implementation from validation. The frozen
reference and build receipts are local private evidence under
`build/performance-reference` and `build/performance-implementation/isolated`.
They do not establish a working physical-console campaign baseline.

The follow-up review's nine successful build configurations and exact source
inventory are under `build/performance-review/final`. They use one immutable
source snapshot. Independent controls/HUD editing was in progress in the shared
checkout; the snapshot keeps that subsystem at committed `b712aec` and records
every excluded path. The shared checkout's independent edits were left intact.
The two earlier whole-checkout snapshot attempts failed on that incomplete
header/caller transition and are retained separately, not counted as passes.
The final snapshot passed 13 Python checks, the focused host/ARM fixtures, and
sequential 240-loop reference and verification Azahar menu smoke runs. Both
emulator processes were cleaned up. `validation.json` records the exact scope
and identifies later independent texture-fixture edits in shared files; those
later edits are not covered by the isolated build receipts. Neither menu run
exercised a campaign route or physical New 2DS XL presentation.

## Coverage of the original request

| Step | Implemented scope | Remaining implementation | Required evidence still missing |
| --- | --- | --- | --- |
| 1. Reference and measurement | Frozen source, dependencies, executable/ELF/map, private maps, sidecars, saves and settings. Byte-identical regular launcher reproduction. Bounded dependency trace, allocation observations, texture staging accounting and a canonical action journal. The deterministic reference fixture repeats 100 opening campaign ticks in Azahar. | Allocator internals, other staging and complete canonical gameplay state remain outside current measurements. Representative route starting saves, inputs and camera receipts still need capture. | Working physical reference, repeatability across the representative routes, colour/depth evidence, instrumentation overhead and initial bottleneck attribution. |
| 2. CPU/GPU overlap | Owned camera/frusta, ordered portal results and logical object IDs for a regular mono view following a submitted midpoint. Preparation occurs before frame services can fence. Existing GPU acquisition, arenas and publication remain fenced. Candidate/verify selectors and discarded/preparation counters. | The first requested scope is present; broader view types are outside this initial experiment. | Campaign ordered-list verification, matching colour/depth, stale/transition behavior and measured total delivery improvement. A shorter acquisition wait is insufficient. |
| 3. Retained preparation | Existing caches retain posed positions and raw normals across UV and lighting changes. Normal consumers receive frame-owned copies. Region selection retains geometry choices across LOD misses within the part cache. Midpoint misses use separate scratch. | Broader object colour/effect metadata retention remains open. The retained normal stream falls back under pressure. | Campaign attributes and rendering, reuse and copy costs, invalidation and eviction rates, and midpoint memory pressure. |
| 4. Animation and pose reuse | Complete-pose root borrowing, exact inverse-bind arrays, lazy endpoint quaternions, prepared compressed tracks and exact local/parent hierarchy reuse. ARM fixtures compare both new candidates to the original arithmetic. | Root-only miss decoding and unused-bone pruning remain conditional on measured use. | Real sparse/constant tracks, overlays, seeks, object reuse, teleports/restores, cache pressure and sampled costs across representative routes. |
| 5. Geometry/submission | Exact runtime compaction, an offline integer `.nmg` plan for the existing opaque compactor, stable compact palettes, resolved-state opaque terrain batching and 44-byte weighted vertices. The 24-map audit found no plan that met the unchanged 4 KiB admission rule. Synthetic fixtures cover positive admission and fallback. | Cooking covers eligible model parts. Native texture work has separate controls. | All weighted depth, colour and special material consumers on PICA, palette writes, command/draw costs, memory pressure and completed GPU queue timings. |
| 6. GPU programs | Separate UV, Boolean selector, second-term MAD and water-base modes. Picasso checks assembled operands and branch targets. | Physical arithmetic and renderer state proof gate integration. | PICA MAD equivalence, Boolean transitions and restoration, water pass parity and representative GPU queue timing. |
| 7. Collision | Independent bounded feature membership and oriented active-plane lookup, preserving order, caps and ancestor values. Query-owned scratch; unchanged original context ABI. | Posed inverse and immutable topology caches remain conditional on measured reuse. | Campaign queries, gameplay replay and whole-query/frame benefit. The synthetic ARM fixture covers repeated planes, tangency, signed zero, breakable geometry and saturation, but is not route validation. |
| 8. GPU work rejection | Host cooker and exact geometric interval proof for ordered static terrain blocks. | Runtime rejection, view-dependent filter ownership and span integration require a proven PICA facing predicate and explicit one-sided draw admission. Software occlusion remains conditional on hidden-draw pressure. | Native clipping, projection, quantization and winding bounds. Verify colour/depth and account for rejected work, CPU costs and lost batching. See [terrain proof](BSP-BACKFACE-PROOF.md). |
| 9. Workers and remaining kernels | The core 2 decode experiment copies its job context and pins the stream until first use. The owner classifies the decoded prefix before joining. Failed prefix/tail work is acknowledged before release. | Additional deferred work and smaller kernels depend on the remaining bottleneck. | Dispatch age, batch time, first-use waits, transition/pressure behavior and delivery. Main/helper times overlap. |
| 10. Coherent presentation and integration | Bounded colour, raw depth/stencil and camera capture with a complete-pair manifest. The particle experiment interpolates detached positions and uses an accepted same-view parent pose for eligible attached sprites. A host reader compares capture targets and a separate analyzer summarizes observed physical refreshes and simulation deadlines. | Broader object attachment, widget, particle-system and effect coverage, route input/camera capture and rollout of accepted changes. | Distinct delivered presentations, repeated refresh intervals, consecutive stalls, simulation deadlines, input latency and cold-load tails on physical New 2DS XL. |

## Promotion remains gated

Every experiment still needs correctness, ownership, local effect, frame benefit
and regression evidence. Separate pure shadow calculations and correctness
captures from uncaptured timing. The authoritative simulation is executed once.
The action journal's selected digests cannot prove equality of fields it does
not include; see [ACTION-REPLAY.md](ACTION-REPLAY.md).

For each route in [PERFORMANCE-ROADMAP.md](PERFORMANCE-ROADMAP.md), fix the exact
starting save, executable, assets, settings, input/camera path and cold/warm
policy. First repeat the reference against itself. Then use at least five paired
90–120-second physical runs in alternating A/B order, retaining slow frames and
reporting paired uncertainty, p50/p95/p99, worst cases and missed deadlines.
Compare each accepted combined build against both its predecessor and the
original reference. Re-profile after each major change.

Missing hardware evidence is a gate on promotion and measured prioritisation.
It does not mean that the unimplemented source items in the table are complete.

## Capture and physical run analysis

Use `tools/compare_midpoint_capture.py REFERENCE_COMPLETE_JSON CANDIDATE_COMPLETE_JSON`
for an exact representation comparison. The reader requires matching ticks,
camera inputs, projection matrices and target layout. It reports changed RGBA
and raw depth/stencil words separately for regular and midpoint views. For
motion changes, compare the intended positions and continuity in the captures
instead of requiring an identical midpoint image.

The midpoint coverage audit distinguishes the first-person weapon pose history,
the eligible ordinary object pose history, detached sprite positions, and
attached sprites with a same-view parent draw. Child object trees, most widgets,
particle systems and contrails still consume their existing state. Births,
deaths, state changes, discontinuities, missing poses and storage limits hold
the current state for the affected sprite. Keep authoritative events and
existing visibility, particle admission and sort order when expanding coverage.

Feed `tools/paired_presentations.py OBSERVATIONS_JSON` one route and one cold
or warm policy at a time. The schema is `{"schema":1,"runs":[...]}`. Each run
contains the following fields.

| Field | Meaning |
| --- | --- |
| `pair`, `variant` | One-based pair ID and `reference` or `candidate`. Array order is actual execution order, alternating A/B and B/A. |
| `device`, `observation`, `timing_mode` | `New Nintendo 2DS XL`, `physical-refresh-ids`, and `uncaptured`. Use independent scanout observations with guest readback disabled. |
| `route`, `policy`, `settings_sha256`, `workload_sha256`, `binary_sha256` | Route identity, cold/warm policy and hashes of the settings receipt, workload receipt and executable. The workload receipt identifies the starting save, assets, input and camera route. |
| `refreshes` | Consecutive `[refresh_index, timestamp_us, displayed_presentation_id]` records across 90–120 seconds. The first sample anchors the window. Reuse the presentation ID for each refresh that displays the same delivered frame. |
| `simulation` | Consecutive `[tick, deadline_us, completion_us]` records on the observation timebase. Supply expected deadlines during the active simulation window and use `null` for a completion still unobserved at the boundary. |

Retain the observation source and its timing precision with the raw input.
The analyzer rejects missing refresh indices and mismatched pair receipts.
It preserves repeated intervals and the unclosed final stall, reports p50,
p95, p99 and worst delivery gaps, and bootstraps the mean differences over
complete run pairs. Sparse trace acknowledgements cannot supply its required
physical refresh stream.

## Review findings and reproducible checks

The follow-up review fixed integration defects that successful builds and
the earlier synthetic fixtures did not catch: the replay journal's native SD
path was routed through the Xbox path translator, and the pre-render obscurer
query advanced object markers before prepared-list consumption. The review also
found that optional quaternion storage inherited fatal debug allocation, and
that midpoint skinning misses could replace regular pose-cache data. Quaternion
allocation now fails independently to direct conversion. Midpoint CPU output
has a separate exact-key scratch owner and is copied by existing encoders before
reuse. These fixes have focused regression checks in addition to target builds.

The build identity must distinguish configurations that change executable
layout. The earlier receipt's common identity was not a compatibility guarantee:
unusual checkpoints can contain image-relative pointers. The reviewed builder
includes actual shader bytes, selectors and dependency identity rather than
forcing all experiments to share an image identity. Normal portable checkpoints
use symbolic callbacks and `build=0`; their representation is unchanged. Never
override a rejected image-relative save to make an experiment run.

The midpoint skin scratch can retain up to 2 MiB of additional vertex storage,
plus fixed metadata, until model-cache flush. The regular pose budget is unchanged.
The optional quaternion cache is 16,512 bytes across both banks. These are
candidate memory costs, not savings. Compare allocation failures, fallback rates
and full memory pressure before accepting either slice. Collision scratch is
query-owned: the tested combined configuration uses 4,960 bytes in the sphere
entry and 1,888 in the pill entry, plus recursive frames (72/168 bytes in that
compiler configuration). Reference entries were 608/568 bytes with 48/160-byte
recursive frames. Stack depth remains an ownership/regression consideration.

Midpoint readback now checks APT/GPU ownership and fences the exact transfer
queue and callback before accessing or releasing its destination. The generic
PPF event alone can describe an earlier transfer. Failed retirement retains the
destination and fails the fixture rather than freeing potentially live DMA
storage. Texture prefetch now accounts for successful scratch allocation even
when a later GPU reservation fails. The dedicated capture writes colour, depth/stencil and camera inputs. Use an
external timeout for automated capture runs.

The repository retains the reader and build-control checks in `tools/test_*.py`;
the UV assembly check needs `PICASSO` set to the recorded assembler. Run
`python3 port/n3ds/tests/verify_model_preparation.py` for compaction and midpoint
ownership under ASan/UBSan. `tools/verify_action_replay.py` exercises production
ARM journal code and the actual compatibility translator with stubbed campaign
and native SD I/O; its command and required build inputs are documented in
[ACTION-REPLAY.md](ACTION-REPLAY.md). The 12-case marker fixture is
`port/n3ds/tests/host/prepared_object_marker_test.c`; it models the obscurer
query's writes and includes the production marker-dependency helpers.
`python3 tools/verify_capture_staging.py` exercises the production capture and
prefetch bodies with UBSan platform stubs, including old PPF notification, failed
retirement, APT/GPU-right loss and allocation/submission failures.
Skipped tool-dependent tests are not passes. The local
frozen compiler inputs and build receipts remain necessary for target builds;
host tests cannot replace PICA or console validation.
