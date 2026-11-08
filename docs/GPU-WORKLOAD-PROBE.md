# Paused-view GPU workload probe

The 7 November New Nintendo 2DS XL runs on A30 (Halo) reached about
99 ms per GPU queue in regular gameplay and 109 ms with controller
settings open. The latter occurred with simulation and audio paused.
The whole-queue time includes display transfers and callback latency.
It neither identifies an expensive pass nor measures display cadence.

Build a separate launcher with `port/n3ds/build.py --fixed-world-width 400
--gpu-probe` and the same dependencies as the regular 400×240 build.
The probe is disabled in ordinary builds. Its launcher uses the regular
renderer until the user explicitly starts a comparison.

Go to a slow view in gameplay. Press START to open the regular pause
menu, then press and release L and R together. Keep the view paused and stationary
for about 30 seconds. The run allows up to 30 seconds, with three warmup
queues and 12 timed queues per phase. Hold START and SELECT together to exit
cleanly before removing the card; preserve `halo-gpu-probe.log` from
the card root before launching again. Look for the `GPU PROBE` lines.
The probe cancels on other input, leaving the pause menu, a changing camera,
or a missing queue identity. Up to three attempts can start per launch.
Use a spare profile for measurements if you want to keep a campaign
checkpoint at a particular spot.

The comparison is normal → quarter-pixel scissor → normal → effect
draws skipped → normal. Every phase retains the native target and
projection. The scissor clips much of the image temporarily, so it
tests sensitivity to screen coverage without changing LOD or simulation.
The effect phase suppresses the transparent shader-effect draw path.
It does not remove water, glass, Chicago materials, or all particles.
Neither diagnostic mode represents a proposed gameplay setting.
`GPU PROBE COUNTS` reports actual renderer draw and element ranges, with
a separate count for opaque BSP geometry after filtering and batching.
The total does not include the HUD draw sites outside the main renderer.

Compare each diagnostic with both surrounding normal samples. If those
normal samples drift, repeat in a more stable view. Pause freezes game
time but some renderer-only animation continues. A large scissor delta
points to fill and overdraw; an effect delta includes all work removed
by that path, including any associated transfers or texture use.
Stable high timings in both probes point toward vertex/submission cost,
the remaining materials, or queue transfers. Those possibilities need
additional attribution before deciding on a fix.

Use this run for diagnosis only. Validate a selected optimization against
the regular launcher, with both launchers rendering the same full-quality
view, before making a performance claim.
