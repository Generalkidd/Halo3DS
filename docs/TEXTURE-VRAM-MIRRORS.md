# Exact texture copies in VRAM

`--texture-vram-mirrors` is an independent, default-off experiment for the
native 400×240 renderer. The physical crop probe showed strong pixel-work
sensitivity, but did not distinguish framebuffer work from texture traffic.
This candidate tests sampling identical textures from VRAM instead of linear
main memory. It introduces no smaller textures, compression, quantization,
new mip levels, filtering changes or material changes.

The canonical texture remains in linear memory for CPU readers, checksums,
fallbacks and existing ownership. Only the GPU's retained sampler descriptor
selects the mirror address. All wrap, filtering, LOD and projective overrides
remain intact. Switching back to reference repairs retained descriptors even
if the next draw does not rebind its textures.

## Bounds and ownership

- At most 64 mirrors, one MiB of actual VRAM allocations, with at least one
  MiB free VRAM after each allocation. Allocator padding and retained tails
  count against the cap; upload length remains the exact texture byte length.
- Only immutable 2D RGBA8/RGB8 owners qualify. Existing mip chains are copied
  in full. Mutable/revision owners, cubes, oversized chains, non-linear data
  and unsupported formats use their original resources.
- Actual binds queue requests. Between regular frames, at most one image of
  at most 64 KiB is uploaded. No upload, allocation or fence occurs per bind.
  Rejected identities are not repeatedly retried, and admitted mirrors are
  not evicted to make room for other candidates.
- Retiring an owner immediately removes its mirror from lookup and restores
  any retained sampler. The VRAM storage is freed only after queued GPU work
  and its completion callback have retired. Canonical texture lifetime and
  the existing map-unload fences remain authoritative.
- The pinned queue adapter checks transfer enqueue success and waits for
  both DMA and callback completion. A generic PPF event or queue-idle flag
  alone is insufficient. Before dispatch, missing GPU ownership falls back;
  after dispatch, storage remains owned until retirement.

Uploads can add cold-frame stalls. Warm queue improvements would not establish
an overall benefit without checking those stalls, main-thread work and memory
pressure. The extra VRAM is duplicate storage, not a reduction in the existing
texture cache budget. A stuck GPU retains its source and destination rather
than recycling them while DMA may still use them.

## Paired console comparison

Build with normal dependencies and:

```text
--fixed-world-width 400 --texture-vram-mirrors --gpu-probe
```

Reach the stationary slow scene on a New model with stereo off, pause, and
allow several seconds for mirror preparation before pressing and releasing
L+R. The comparison is reference → VRAM → reference → VRAM → reference,
with three warmup and twelve measured queues per phase and a 30-second limit.
The mirror set is frozen for the entire comparison. No-ready-mirror and
changed-residency runs abort rather than report a misleading timing result.

`GPU PROBE VRAM` records retained images, actual duplicate bytes, completed
preparation uploads and failures. These counts do not measure texture bytes
read by the GPU. Match draw/element counts, compare both surrounding reference
phases, and inspect output. Hold START+SELECT to exit cleanly, then retain
`halo-texture-vram-mirrors.log` from the card root.

The paired comparison rejects other GPU candidates. Standalone combinations
remain separate builds, and need their own validation. Original models,
scaled or stereo rendering, and screen-effect/diagnostic scope retain the
canonical path. Midpoint presentation and other validation/profiling options
are rejected for this experiment.

## Evidence

`tools/verify_texture_vram.py` exercises the production mirror implementation
with ASan/UBSan and explicit DMA/allocator substitutes. Coverage includes exact
mip-chain bytes, bounded uploads, allocation padding, budget/headroom failure,
retained sampler overrides, mode restoration, owner invalidation, frozen
residency, failed allocations/cache flushes/enqueues and ownership loss after
a retired copy. `tools/verify_capture_staging.py` additionally exercises the
production queue/callback wait and checked-copy functions.
The host probe test covers all five comparison phases and rejection of empty
or changing residency, changed views and expired deadlines.

Five configurations compiled and linked from the corrected source snapshot:
native-400 reference, mirrors, mirrors with probe, mirrors with both sampler
and opaque-read candidates, and the existing midpoint reference. Their private
receipts are under `build/performance-goal-vram-r3`. The subsequent change to
the probe log label from `allocation_failures` to `preparation_failures` makes
clear that the counter includes cache and transfer failures too.

The bounded Azahar/Vulkan fixture passed nineteen reference/candidate pairs,
with 3,648,000 exact raw RGBA8/D24S8 word comparisons and no differences. It
checked emitted physical texture addresses, all plain/detail and cutout
combinations, an RGB8 two-level chain with genuine second-mip sampling,
retained sampler overrides, in-frame revision retirement and subsequent
re-publication. A separate reference/reference run passed. Actual allocation
was 1,792 bytes for 1,728 payload bytes, exercising allocator padding. Private
sources, hashes, captures and reproduction instructions are under
`build/performance-vram-validation`. All three owned emulator runs stopped.

The emulator cases use synthetic inputs and do not establish campaign coverage
or physical PICA parity. Build, host, emulator and physical timing evidence
must be assessed separately.
No native speedup or 60 FPS result is established, and the default stays off.
