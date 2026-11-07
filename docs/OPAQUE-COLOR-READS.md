# Destination color reads on opaque draws

The framebuffer reuse test on New 2DS XL completed all five phases on
A30 (Halo), BSP 0, tick 1066. The run reported 804 MHz and L2 enabled.
Each phase used 12 complete queues. Every phase submitted the same
208 renderer draws and 126,834 elements.

| Phase | Median queue | World target binds | Rebinds skipped |
| --- | ---: | ---: | ---: |
| Reference | 105.582 ms | 123 | 0 |
| Framebuffer reuse | 105.526 ms | 25 | 98 |
| Reference | 105.581 ms | 123 | 0 |
| Framebuffer reuse | 105.518 ms | 25 | 98 |
| Reference | 105.610 ms | 123 | 0 |

Relative to the mean of each pair of surrounding reference medians,
reuse saved 0.0555 and 0.0775 ms. The mean saving was 0.0665 ms, or
0.063%. The ranges overlap. This is a real reduction in API calls with
no useful queue-time improvement demonstrated in the tested scene.
Keep framebuffer reuse disabled for the next comparison.

The earlier crop test found a much larger dependence on pixel coverage.
This candidate removes destination color reads only from indexed draws
into the direct native mono top target whose installed GPU state meets
all of these conditions:

- Ordinary fragment mode, full RGBA8 output, and the usual
  greater-or-equal depth test with color and depth writes.
- Normal additive blending with source ONE and destination ZERO for
  both RGB and alpha. The output at an accepted sample is the source
  in all four channels, independent of the previous destination color.
- Full existing depth and stencil access, with no gas pass.

Alpha, depth and stencil tests, their updates, geometry, sampling,
ordering, and framebuffer write masks are unchanged. Real blends and
partial color writes keep the stock read permission. A framebuffer
bind restores the stock permission before any candidate override.
When access changes without a bind, the adapter orders a framebuffer
flush and invalidation before changing the read mask. Consecutive
eligible draws retain the mask. The adapter hooks pinned Citro3D
command emission, so the decision follows installed state rather than
the material's name. See the
[PICA register reference](https://www.3dbrew.org/wiki/GPU/Internal_Registers).

Build with `--fixed-world-width 400 --skip-opaque-color-reads`; the
option is disabled by default. Add `--gpu-probe` for the stationary
reference → candidate → reference → candidate → reference comparison.
Every phase draws at full resolution with all effects. The log
records eligible draws, elided reads by draw, custom mask writes, and
extra flushes. Counts describe commands, not bytes saved or visible
frames. Extra barriers or hardware that already bypasses destination
reads can offset the expected benefit.

A focused Azahar fixture compared all 96,000 RGBA8 pixels at 400×240
and found no differences. It included opaque, cutout and depth-tested
overlap followed by a destination-dependent blend without a target
rebind. The recorded command stream used read masks `F/F/F/F` on the
reference and `0/0/0/F` on the candidate, with matching write masks
and depth, alpha and blend settings. This checks command selection and
fixture output. It does not measure physical hardware performance.

Reach a slow view, press START, wait for the camera to settle, then
press and release L+R on the regular pause screen. Leave the console
paused and untouched for 30 seconds. Hold START+SELECT to exit cleanly.
The report is `halo-opaque-color-reads.log` at the card root.
Keep the default renderer until the candidate shows a useful gain and
matches the reference image on the console.
