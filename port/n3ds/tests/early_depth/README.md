# Native early-depth characterization

This separate diagnostic launcher leaves gameplay source and defaults unchanged. It tests the separate PICA early-depth facility, whose native semantics remain uncertain. It is **not a speed benchmark**, and finite image equality cannot prove a generally safe gameplay setting. Enabled results must come from a physical console; Azahar reference-only runs validate the harness, not early-depth.

Build from an explicitly selected successful frozen mirror configuration:

```sh
python3 port/n3ds/tests/early_depth/build.py \
  --base build/performance-goal-vram-r3 \
  --configuration mirrors-probe \
  --output build/performance-early-depth
```

The builder reads the frozen SDK/compiler paths, objects and response files, recompiles private renderer/frontend/main overlays sequentially, then links `early-depth.3dsx`. Commands have individual deadlines and process-group cleanup on interruption/timeout. `build.json` identifies the fixture, builder, source inventory, base binary, checked transfer adapter and produced binary. Production files are not rewritten. This is a separately runnable full-engine-linked diagnostic, not an independent minimal libctru example. It inherits the bootstrap, shaders and native render target. Use the existing working Halo3DS SD setup, including prepared `sdmc:/halo-source` data and `sdmc:/3ds/dspfirm.cdc`; minimal asset-free bootstrap has not been verified. Synthetic geometry/textures are created by the fixture and no mission is loaded. The bootstrap requests New-model CPU speed/L2; results record queried model, clock and L2 (unavailable values are explicitly marked).

Copy the launcher to a **separate** SD app path; do not replace the game. Allow at least 31 MB free for captures. It runs automatically, writes `sdmc:/early-depth/results.csv` plus raw captures, and exits after ten cases × four phases. Its watchdog terminates the process after 120 seconds if work stalls; it never frees outstanding DMA buffers. Startup errors also exit instead of waiting indefinitely for START. Capture the existing `halo-texture-vram-mirrors.log` bootstrap log alongside the results. A current `complete,1` row and all current records are mandatory: old files may remain after an interrupted rerun. Archive the whole results directory before rerunning.

Every case first renders early-depth off, then repeats off and requires exact full-buffer equality. Two enabled configurations follow: `GPU_EARLYDEPTH_GEQUAL` with reference/data zero, and `GPU_EARLYDEPTH_GREATER` with zero. This intentionally small **zero-reference-only** sweep makes no assumption that either is equivalent to ordinary depth testing. It does not cover LEQUAL/LESS, other references or general early-depth semantics.

Cases cover far/near and near/far order, equal depth, adjacent input floating-point Z, alpha cutout, stencil pass/stencil-fail, target roundtrip, command-list split, disabled depth writes, and stencil depth-fail. Adjacent input Z is **not** guaranteed to mean adjacent quantized D24 values. Command-list split is `C3D_FrameSplit` within one final GX submission, not two separately retired queues. Target roundtrip and split explicitly reinstall and clear early-depth before the second draw; these cases characterize reset transitions, not persistence without a clear. Weapon projection transitions and a complete reference/function sweep remain future work.

The fixture uses the production projection, depth clear zero, GEQUAL and D24S8 target. It emits pending context state **before** explicit `EARLYDEPTH_CLEAR`, logs the relevant emitted registers and draws, and checks actual enable/function/data values and expected draw counts. SDK automatic clears are also logged. Mirrors are forced off and admission frozen immediately after each frame begin, so texture location cannot switch across test phases.

The off/off controls require covered depth and unchanged background; known near/far/equality winners; both cutout colors and distinct depth values; and stencil value two after the explicit replace-then-increment scenarios. Readback flushes destination CPU cache, uses the pinned checked GX enqueue plus exact queue/callback retirement, then invalidates CPU cache before comparisons. `.rgba` and `.d24s8` are **native tiled raw memory**, not linear screenshots. All 96,000 32-bit words per plane are compared. Enabled differences are recorded as observations, not a diagnostic failure or silently accepted approximation.

Validate copied SD results:

```sh
python3 port/n3ds/tests/early_depth/verify_results.py /path/to/early-depth \
  --output /path/to/verification.json
```

For emulator harness validation only, build with `--reference-only` into another output directory and use the archived bounded `build/performance-vram-validation/emulator_smoke.py` harness, a fresh private profile and a 90-second deadline. It omits every enabled phase. Neither its elapsed time nor its equality results are hardware optimisation evidence.

Native promotion would require a conservative state/geometry invariant justified by hardware behavior, exact visual validation across that restricted scope, and paired console timing. This fixture alone authorizes none of those conclusions.
