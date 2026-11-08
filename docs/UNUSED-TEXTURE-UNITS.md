# Unused terrain texture units

The native texture binding API uses a real white texture for an absent map.
This is necessary when a material consumes that map as a neutral input. It
also leaves all three PICA texture units enabled when a material uses fewer
maps. The pinned Citro3D enables units from non-null descriptors, independently
of the texture combiner's sources.

The opt-in `--unused-texture-units` candidate disables unused samplers only
during `n3ds_gpu_environment_draw`, the plain/detail terrain path. The mask is
unit 0 for base, unit 1 when lightmapping is enabled, and unit 2 when detail
mapping is enabled. All eight recipes retain every consumed sampler. The
flashlight combines texture 0 with fragment lighting and needs no extra map.
Bump, plasma, emission, water, models, effects and HUD retain stock enables.

The adapter changes only enable bits in the texture configuration register.
It keeps Citro3D's descriptors and texture cache identities intact, preserves
the remaining register bits, and follows a change with texture-cache
invalidation. The next context update restores stock enables even when the
next material does not rebind any texture. Stock texture writes, including
APT restoration, reset the adapter's tracked hardware state. There are no
new allocations, transfers, texture conversions or changes to draw ordering.

Build with the usual dependencies and:

```text
--fixed-world-width 400 --unused-texture-units --gpu-probe
```

The paused L+R comparison runs reference → candidate → reference → candidate
→ reference. Every phase keeps the same full picture. Each has three warmup
queues and twelve timed queues; the entire attempt is bounded to 30 seconds.
Hold START+SELECT to exit cleanly. Preserve `halo-unused-texture-units.log`
from the SD root. `GPU PROBE TEXTURE UNITS` reports reduced draws, the sum of
disabled units across those draws, and enable-mask writes. These are command
counts, not measured texture fetches or bytes saved. Check matching draw and
element counts and compare each candidate median to both surrounding references.

The candidate requires native 400×240 regular rendering, and runtime scope
excludes scaled, stereo, bottom-screen and screen-effect paths. Midpoint and
other diagnostic builds are rejected. A paired probe cannot combine this
candidate with framebuffer reuse or omitted opaque color reads. Standalone
combination with opaque color reads shares one context wrapper.

Current validation:

- The production adapter passes host AddressSanitizer/UndefinedBehaviorSanitizer
  checks via `python3 tools/verify_texture_units.py`: all terrain masks,
  unchanged-descriptor restoration, repeated draws, stock rebind/resume/discard
  state, unrelated register bits, cache invalidation and absent bindings. An
  exhaustive 384-case matrix covers every consecutive pair of terrain masks,
  every subset of stock bindings, and clean/TexAll/TexStatus updates, checking
  exact write and invalidation counts. This includes mask 5 but does not prove
  emissive-material image parity or enable that runtime path.
- Current-source reference, paired-probe and combined opaque-read/texture-unit
  launchers compile and link. Private commands, source inventory, hashes and
  configuration checks are under `build/performance-goal`.
- Build controls and existing Python/shader tests pass, including real Picasso
  assembly checks. BSP packet parity tests pass.
- A bounded Azahar/Vulkan fixture passed all sixteen plain/detail recipe and
  cutout combinations. Each reference/candidate pair matched all 96,000 raw
  RGBA words and 96,000 D24S8 words, with command masks checked at draw time
  and a following texture-2 draw restored without rebinding. A separate
  reference/reference run passed the same checks. This is synthetic coverage,
  not campaign coverage or PICA hardware proof. Private inputs, scripts and
  captures are under `build/performance-goal/texture-validation`.

The first fixture's display-transfer readback failed even against itself.
Raw texture-copy readback passed the reference-repeat control and was used
for the candidate comparison. The failed runs remain in the private evidence;
they do not count as candidate rendering failures or successful validation.
All four emulator processes exited and were cleaned up.

Physical image/depth parity and useful queue-time improvement remain required.
Validate a saved five-phase native run with
`python3 tools/analyze_texture_units_probe.py /path/to/halo-unused-texture-units.log --run 1`.
The checker requires a complete ordered run, no missing timing samples, stable
draw/element counts across phases, and matching active candidate counters.
It reports each candidate queue median against the mean of its two adjacent
reference medians, alongside reference drift. This conservative screening can
reject noisy but otherwise valid runs; repeat those measurements. Passing only
establishes consistency of logged counters, not identical scenes, statistical
significance, visual parity, or displayed FPS. Synthetic protocol tests run with
`python3 -m unittest discover -s tools -p test_texture_units_probe_analysis.py`.

The 7 November crop probe established strong pixel-work sensitivity, not that
unused sampler work explains it. PICA may already avoid some unused fetches,
and added configuration/cache writes may offset savings. This candidate stays
disabled by default. No 60 FPS result is established.
