# Bounded dependency trace

Status: implemented in source, unbuilt and untested. No trace capture, overhead
measurement, hardware timing or replay result exists for this slice. Earlier
native-400 builds, setup tests and the emulator menu smoke predate it. They do
not validate the new instrumentation. The installed launcher is unchanged.

This trace supports the first step of the
[performance roadmap](PERFORMANCE-ROADMAP.md): distinguish CPU preparation,
existing waits, submitted GPU work and observed presentation dependencies before
moving work across their boundaries. It does not implement an optimisation
candidate, alter Halo's 30 Hz game logic or change rendering quality.

## Independent build controls

| Option | Contract |
| --- | --- |
| `--dependency-trace` | Disabled by default. Enables bounded event collection independently of `--performance-profile` and `--algorithm-profile`. |
| `--dependency-trace-loops N` | Requires trace; accepts 1–18,000 completed frontend loops. If omitted, the trace captures at most 120 completed loops after gameplay first starts. |

Trace rejects `--test-frames`, `--diagnostics` and `--original-memory`, which
select readback or validation fixtures. It leaves `--fixed-world-width`,
`--present-60`, texture prefetch, cache budgets and asset/algorithm selections
unchanged. For the native-resolution regular-frame reference, explicitly select
`--fixed-world-width 400`; trace alone does not select a resolution policy.
Midpoint comparisons remain a separately selected later step. Algorithm
`candidate` and `verify` modes remain unavailable.

The trace-only build compiles the same Citro3D frame adapter used by
`--present-60`, but does not enable midpoint rendering or frame-discard decisions.
Both options require the pinned Citro3D source checkout and matching library:
`9f21cf7b380ce6f9e01a0420f19f0763e5443ca7`. The build verifies `renderqueue.c`
and `internal.h` against their recorded hashes before selecting the adapter.
This check establishes source inputs, not library equivalence or runtime
correctness.

The build emits `HALO_N3DS_DEPENDENCY_TRACE=1` and
`HALO_N3DS_DEPENDENCY_TRACE_LOOPS=N` only when trace is selected. Its
`boot-build.json` has an independent `dependency_trace` section, separate from
the aggregate counters and renderer candidates. It records configuration and
fixed capacities, not measured performance or successful validation. External
capture remains unverified by the build manifest.

## Capture lifetime and fixed storage

Collection starts at the first gameplay loop. The loop limit counts completed
frontend loops from that point; it is not a count of simulation ticks, rendered
frames or newly displayed images. Reaching the limit freezes collection without
ending gameplay. Storage is fixed and does not grow when the loop limit grows.

| Storage | Capacity |
| --- | --- |
| Owner event array | 8,192 records of 64 bytes: 524,288 bytes |
| GPU callback ring | 256 × 64-byte records plus four 32-bit scalars: 16,400 bytes |
| VBlank observation ring | 256 × 64-byte records plus four 32-bit scalars: 16,400 bytes |
| Submission metadata | 16 × (40-byte context plus 4-byte captured flag): 704 bytes |
| Fixed buffer subtotal | 557,792 bytes |

This subtotal excludes other collector contexts/scalars, stack use and the C
library's shutdown-export buffers. The startup log and export header report
the core's compiled buffer subtotal; neither claims the complete process
footprint. No allocation-accounting implementation or measured memory high-water
mark is implied by this trace.

Full storage drops new observations instead of overwriting retained records or
blocking producers. Retain drop/overflow counts, missing pairs and pending final
work. An incomplete interval is not evidence of zero stalls. The default
120-loop capture is a short sample; it does not cover the proposed
90–120-second reference route. A longer loop limit cannot guarantee a longer
retained history once event storage is full.

If queue-token identity is lost, the trace stops associating subsequent GPU
observations instead of guessing an originating submission. It does not add a
wait to recover identity. The header records that loss. A missing final callback
or scanout tail is always possible, even when pending counts are zero.

Collection uses stage/job boundaries, never per-sample or per-vertex clocks.
Callbacks and worker code do not call the main-owner collector. Their bounded
observations transfer through the existing ownership/completion boundaries.
The callback producer assumption is the pinned libctru
`36fe1ada5b7ebe53ba4decda36d764a55f8fefb6` singleton GSP event thread. An
unexpected queue/callback association invalidates further GPU attribution.
Instrumentation must not introduce GPU waits, readbacks, synchronous per-event
logging, scheduling changes or alternative resource admission decisions.

## Identity and observation limits

Trace context distinguishes frontend loops, simulation ticks and restore epochs,
render attempts/views, regular/midpoint work, map/BSP generations and actual
submissions. A simulation tick is not a unique key because checkpoint restore
can rewind it. A render attempt may be canceled without submission. Delayed
observations retain originating context instead of taking the current loop's
identity. Events with no applicable identity keep that distinction explicit.
Submission IDs identify actual observed Citro3D queue runs, including safe
transfer queues; they are not the aggregate profiler's independently numbered
submission IDs and do not each imply a top-screen presentation. A frame may
have no queue run, or additional transfer work. Match records within this trace
using their originating attempt/view and queue IDs.

The current stage coverage includes frontend services, actual simulation ticks
and frame updates, thread collection, texture service/fences, per-eye frusta
and visibility, object discovery/rendering, worker batches and GPU boundaries.
Checkpoint polling, input/director/observer/audio work outside a traced worker,
and top-level camera/history setup are not individually timed by this slice.
Object-render scopes include encoding and are not pure CPU preparation. A
service span can legitimately cross map generations or restore epochs; match
that span by loop/event family while preserving both endpoint contexts.

Queue callback time, owner-observed retirement, swap posting, observed swap
acknowledgement and VBlank are separate observations:

- Owner retirement is an upper bound on completion observation, not the instant
  PICA finished its last command. Queue work may include display transfers.
- A callback timestamp records callback execution; scheduling/interrupt latency
  and timer resolution remain part of its uncertainty.
- `!gspIsPresentPending(GFX_TOP)` after a known post is a sampled acknowledgement.
  It does not provide an exact latch or scanout timestamp.
- A VBlank notification alone does not prove a new image was displayed. Repeated
  images, pending swaps and missing observations must remain visible in analysis.

The nonblocking acknowledgement sample checks the callback's published version
around its observation. If completion changes that version or is in progress,
the sample makes no acknowledgement claim; it does not retry or wait.

Keep CPU, worker, I/O and GPU intervals separate where they overlap. Do not add
them into a synthetic frame time or convert queue/loop counts into displayed
FPS. The event trace complements the
[aggregate profiler](ALGORITHM-PROFILING.md); the profiler's reporting windows
are not interchangeable event samples.

## Shutdown export and schema 1

Collection stops before frontend teardown; export occurs later in normal
shutdown, with an `atexit` fallback. There is no
streaming writer during gameplay. A forced kill, crash, reset or power loss may
prevent export. The capture limit is not a process timeout: later automated
runs need a bounded external harness with graceful exit before forced cleanup.
The existing `--profile-frames` limit remains separate and requires aggregate
performance profiling; it is not implicitly selected by trace.

The output is `sdmc:/halo-trace-%016llx.bin`, using the run ID formatted as
16 hexadecimal digits. No file is written if gameplay capture never starts.
The run ID is the capture-start system tick, not a globally unique experiment
identity. A failed/partial export is reported in the existing log. A file is
complete only if its exact length, header, final trailer and checksum agree.

All integers are explicitly little-endian. Schema 1 contains a 128-byte header,
at most 8,192 64-byte records in retention order, then a 16-byte trailer. Maximum
file size is 524,432 bytes. Callback drains and joined worker timestamps mean
retention order is not chronological order.

| Header byte offset | Type | Field |
| --- | --- | --- |
| 0 | 8 bytes | `H2DSTRC1` |
| 8, 12, 16, 20 | u32 each | Schema 1, header bytes 128, record bytes 64, event capacity 8,192 |
| 24, 28 | u32 each | Retained record count; configured completed-loop limit |
| 32, 40 | u64 each | Run ID; timer frequency (`SYSCLOCK_ARM11`) |
| 48, 56 | u64 each | Capture-start and capture-stop system ticks |
| 64, 68, 72 | u32 each | Owner-journal, GPU-ring and VBlank-ring dropped counts |
| 76 | u32 | Queue identity lost: 0 or 1 |
| 80, 84, 88 | u32 each | Pending queues at start/stop; pending top-swap sequence |
| 92, 96 | u32 each | Stop reason (1 loop limit, 2 shutdown); completed captured loops |
| 100, 104 | u32 each | Posted-swap and VBlank sequences observed at stop |
| 108 | u32 | Fixed buffer subtotal: 557,792 bytes |
| 112 | u32 | Unconfirmed tail: always 1 in schema 1 |
| 116 | u32 | Midpoint presentation compiled: 0 or 1 |
| 120, 124 | u32 each | Latest queue and job IDs at export; not counts of captured events |

The stop snapshots and final export are different moments. Queue IDs can advance
after collection freezes. An in-progress callback may also remain unretained.
The bounded final drain discards callback records timestamped after the
capture-stop tick instead of attributing post-capture work to the interval.
The always-set unconfirmed-tail field prevents zero pending counters from being
mistaken for a fully drained GPU or displayed final image.

| Record byte offset | Type | Field |
| --- | --- | --- |
| 0 | u64 | Observation system tick |
| 8, 12, 16 | u32 each | Producer sequence; frontend loop; restore epoch |
| 20 | i32 | Simulation tick |
| 24, 28 | u32, i32 | Map/BSP generation; BSP index |
| 32, 36, 40, 44 | u32 each | Render attempt, view, submission and job identities |
| 48, 50, 51 | u16, u8, u8 | Event ID, render kind, producer |
| 52, 56, 60 | u32 each | Flags (currently zero), event argument 0 and argument 1 |

Event IDs are declared in `engine_dependency_trace.h`. Kind is 0 for frontend
context, 1 for regular rendering and 2 for midpoint rendering. Producer IDs are
0 owner, 1 GPU callback, 2 VBlank callback and 3 joined-job observation. Owner
and joined-job records share one sequence allocator; each callback ring has its
own sequence. VBlank records have no game context. Sequence gaps identify
missing observations within the retained range, not a known complete prefix or
tail. Event arguments remain specific to the recording seam.

The trailer contains `H2DEND01` at offset 0, the retained count as u32 at offset 8
and FNV-1a32 at offset 12. The checksum covers the encoded header and records,
excluding the trailer. Its initial value is 2,166,136,261 and its multiplier is
16,777,619 with 32-bit wrap. It detects accidental damage; it is not an
authenticity guarantee.

## Offline reader and future validation

[`tools/dependency_trace.py`](../tools/dependency_trace.py) is an unexecuted,
untested, standard-library reader. It bounds input to the schema-1 maximum,
checks the exact header/record/trailer sizes and checksum, and rejects unsupported
fields. It prints JSON containing raw clock labels, event counts, drops,
producer-sequence gaps and unmatched queue/swap identities. Examples of unmatched
identities are capped at 32; CPU stage and worker-pair analysis is not implemented.
The reader does not infer GPU completion instants, scanout times or FPS, and a
successful file-integrity result is not a correctness or performance gate.

A future offline invocation, not executed for this source slice:

```text
python3 tools/dependency_trace.py /private/path/halo-trace-RUN_ID.bin > /private/path/trace-summary.json
```

Retain the exported trace and its schema/clock/storage receipt together with
the exact executable/build manifest, source/dependency receipt, map/sidecar and
private save identities, console model, memory mode, settings and route. The
binary format is owned by `engine_dependency_trace.c`; parsers must reject
unsupported schema/record sizes and disclose incomplete captures. This slice
does not supply an offline replay tool or claim the trace is self-sufficient
for reproducing gameplay.

Before using timings, build and validate the trace separately, confirm event
pairing/context across menu entry, gameplay, pause, checkpoint restore, BSP
transition and shutdown, then measure its overhead against the same untraced
configuration. Do correctness captures separately from uncaptured physical
New 2DS XL timing. Emulator timings cannot establish hardware bottlenecks.

Tick-indexed deterministic replay, canonical state comparison and complete
ordinary/linear/staging allocation accounting remain proposed work. No
optimisation should be promoted from these source changes alone.
