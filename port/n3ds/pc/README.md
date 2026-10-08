# Halo CE PC retail cache integration

`cache_file_formats.c` and `.h` are the standalone, bounded PC v7 cache reader
from the local native rebuild branch. They parse the owner supplied map and
resource files, verify tag and BSP extents, convert selected PC tag values,
and relocate the pointer fields covered by their checked layouts. The code
does not contain game data. The source comments describe the format evidence
and the limits of each conversion.

The official 3DS build now compiles this reader for ARM. The opt-in
`build.py --probe-pc-map ui` or `--probe-pc-map bloodgulch` mode loads the
matching file from `sdmc:/halo-source/pc-retail/`, uses any available
`bitmaps.map`, `sounds.map`, and `loc.map` there, and logs conversion and
known pointer rebase results before normal Xbox startup. It reads and
rebases the scenario BSP in its reserved cache slot. It releases all probe
memory and file handles before the normal startup path. Probe builds are
diagnostics and must not be packaged as PC runtime releases.

On the New 3DS Azahar profile with private retail maps, `ui.map` (v7)
loaded 1,409 tags and rebased 10,274 known pointers. `bloodgulch.map` (v7)
loaded 2,408 tags, rebased 20,587 known pointers, and rebased 466 BSP
pointers across 16 lightmaps and 79 materials. Both probes reported no
unknown tag groups and passed. These are measured parser results. They do
not prove every pointer field in a known group was covered; runtime behavior
is reported below separately.

The normal launcher still accepts Xbox v5 caches with its existing `.nrl`
relocations. An opt-in `--diagnostic-pc-ui` build binds PC retail `ui.map` to
the original engine. `--diagnostic-pc-play timberland` or `bloodgulch` also
enters a solo Slayer match on that PC map at frame 90. This handoff translates
known tag pointers, gbxmodel geometry, bitmap offsets and pixels, and BSP
vertex and weather blocks. It is separate from the normal launcher because
the checked layouts do not yet cover every PC runtime field.

On the New 3DS Azahar OpenGL profile, the PC UI menu rendered through frame
300. PC Blood Gulch ran past frame 450 and PC Timberland past frame 360,
with player, weapon, world, and HUD visible. These are emulator observations;
several world surfaces have incorrect colors or materials, other maps and
weapons are unverified, and physical New 3DS behavior is untested. The PC bot
count was zero in both PC match runs. The regular Xbox UI still booted after
the PC adapter changes. No owner supplied maps, resource archives, or screen
captures are committed to this repository.

`--diagnostic-pc-campaign a10` requests the PC retail campaign map after
the PC UI has booted. The campaign's 626 gbxmodel parts need about 9.1 MB of
converted vertex and index storage; they cannot fit between the tag data
and the first reserved BSP slot. The experimental loader now owns that
storage separately, validates its bounds through the native cache resolver,
and releases it with the map. In Azahar, `a10.map` loaded 3,724 tags,
converted 76 models, and reached `MAP READY`; the first mission render
submitted 178 BSP batches and 40,934 triangles. Its first Chicago shader
uses a cube map (`type=1`). The later diagnostic build evaluates that cube
and the two-dimensional maps through the authored combiner at posed
vertices. After the GPU driver update, Azahar decoded the first cube image
and ran PC retail `a10` past frame 450 with a player unit, a BSP transition,
and no shader assertion. The frame-300 capture shows the opening Halo-ring
cinematic. This verifies that scene and engine progression, not the full
campaign, on-foot combat, or exact material color. The campaign diagnostic
still records and skips unsupported Chicago layouts that the candidate
path cannot handle; that skip is not a release behavior.

`setup/PC/Package-Halo3DS-PC.ps1` continues to require an independently
verified PC runtime binary and matching manifest. The diagnostic binary is
not that release. No PC runtime package or SD installation is produced yet.

`build.py --experimental-pc-runtime` builds a separate, source-only trial
launcher with ordinary menu flow. It prefers a valid PC retail v7 map under
`halo-source/pc-retail/` for each requested map name and uses the Xbox v5
map under `halo-source/` when the PC map is absent. This option forces no
map and writes no diagnostic screen capture. Its compatibility is still
being tested; it does not qualify for the installer manifest. Azahar booted
its PC retail UI through frame 511 without a forced map. The updated PC
Timberland diagnostic ran beyond frame 690 with a player unit; its frame-300
image matched the earlier PC Timberland capture after the model storage
change.

Further opt-in multiplayer diagnostics leave the original engine's placement,
match, weapon and vehicle code in charge. Timberland solo Slayer spawned 33
weapon objects and four Warthogs. Timberland CTF with two local bots spawned
two flags, four Warthogs and three bipeds and stayed active beyond frame 750.
Blood Gulch Oddball with two bots spawned the ball; the bots selected it,
routed and recorded combat hits. In Blood Gulch Race, the original engine
spawned three Warthogs and a bot entered a driver seat and moved the vehicle.
A separate Blood Gulch Slayer diagnostic held the New 3DS primary trigger for
30 frames; the map owned plasma pistol's original `game_time_last_fired`
changed from zero to 199. This verifies that firing path in the emulator,
not every PC weapon or a complete scored match.

The PC BSP converter now checks and rebases the byte pathfinding surface and
edge blocks along with collision geometry. Timberland's path flags were
present, but some cross-map CTF searches exhausted the original 1,024 node
list. The optional bot controller now takes partial waypoints from the
original path builder in that case and re-searches after advancing. No
original pathfinding or multiplayer rule was replaced. Long route completion,
scoring and physical New 3DS XL behavior remain unverified.
