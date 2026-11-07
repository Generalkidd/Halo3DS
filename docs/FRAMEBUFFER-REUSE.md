# Native framebuffer binding reuse

The New 2DS XL workload probe completed all five phases on A30 (Halo),
BSP 0, at simulation tick 1222. The console reported 804 MHz and L2 enabled.
Each phase has 12 completed queue samples and no missing samples.

| Phase | Median queue time | Renderer draws | Renderer elements |
| --- | ---: | ---: | ---: |
| Normal | 79.759 ms | 170 | 105,222 |
| Quarter-pixel scissor | 28.143 ms | 170 | 105,222 |
| Normal | 79.778 ms | 170 | 105,222 |
| Transparent shader effects skipped | 79.320 ms | 165 | 103,410 |
| Normal | 79.793 ms | 170 | 105,222 |

The scissor saved 51.63 ms against its bracketing medians with identical
draw counts and indices, while the skipped effect path saved 0.47 ms.
That points to pixel work or framebuffer traffic in this view. The probe
does not identify the cost of any individual material, and its queue time
includes transfers and callback latency.

Halo frequently restores render state to the same top target. In the
pinned Citro3D, `C3D_FrameDrawOn` unconditionally dirties framebuffer
state; after an intervening draw, reapplying that state flushes the
framebuffer, clears early-depth state and invalidates the framebuffer
cache. The candidate skips only that redundant bind for the direct
native 400×240 top target in a mono mission. It still marks the target
used and issues the same viewport/scissor reset. Draws, materials,
ordering, texture sampling and framebuffer masks stay the same.

Frame begin and end, clears, a changed target, and the radar and world
resolve split paths invalidate the binding. Offscreen, stereo, scaled
and menu target binds keep the original behavior. The top target is
not sampled as a texture. APT restoration dirties Citro3D state and the
next renderer frame begins with an unknown target.

Enable the candidate with `--fixed-world-width 400 --framebuffer-reuse`.
It is disabled by default. Adding `--gpu-probe` compares reference →
reuse → reference → reuse → reference from one stationary pause. All
phases render at full resolution with every effect enabled. The counter
reports actual world-target bind calls and same-target rebinds skipped;
some reference calls would already have coalesced before the next draw.
It does not count avoided GPU flushes directly.

For the card launcher, reach the same slow scene, press START, wait for
the camera to settle, press and release L+R, and leave it paused for 30
seconds. Hold START+SELECT to exit cleanly. The report is
`halo-framebuffer-reuse.log` at the card root. Compare the medians of
the two reuse phases to the surrounding reference phases and check
that the draw counts match. A timing win and matching screen output
are separate checks. Keep the candidate off by default until verified
on hardware.
