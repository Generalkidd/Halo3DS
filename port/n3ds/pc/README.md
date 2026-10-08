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
loaded 2,408 tags, rebased 20,587 known pointers, and rebased 465 BSP
pointers across 16 lightmaps and 79 materials. Both probes reported no
unknown tag groups and passed. These are measured parser results. They do
not prove every pointer field in a known group was covered, nor do they show
that a PC map can run in the official Xbox engine.

The normal launcher still accepts Xbox v5 caches with its existing `.nrl`
relocations. PC tags are deliberately not bound to that engine: the PC tag
index differs, and model geometry, bitmap/sound resource offsets, HUD and
script conversions, and rendering must be integrated and tested first.
`setup/PC/Package-Halo3DS-PC.ps1` continues to require an independently
verified PC runtime binary and matching manifest. No PC runtime release or
SD installation is produced by this probe.
