# Modifying the native 3DS build

The playable engine is built from `source/` plus the New 3DS platform code in
`port/n3ds/src/` and `port/n3ds/pc/`. `sources.json` lists the compiled units;
`build.py` records source hashes, link inputs, a map file, and the 3DSX hash in
the chosen output directory. The unified Xbox and PC retail build is selected
with `--experimental-pc-runtime --unified-ui`. See
[`docs/DEVELOPMENT.md`](../../docs/DEVELOPMENT.md) for toolchain requirements and
the full command.

Useful edit points:

| Goal | Source |
| --- | --- |
| Campaign pickup and Rocket Warthog placement | `src/engine_campaign_extras.c` |
| Campaign opt-in and saved preference | `src/engine_campaign_options.c` |
| PC Rocket Warthog tag dependencies in b30 | `src/engine_campaign_overlay.c` |
| PC retail v7 cache conversion and resource access | `pc/cache_file_formats.c`, `pc/engine_pc_runtime.c` |
| Native renderer and texture streaming | `src/engine_gpu_renderer.c`, `src/engine_textures.c` |
| Local bots and their objectives | `src/engine_bots.c`, [`BOTS.md`](BOTS.md) |
| Original weapon, vehicle, player and mode rules | `../../source/items/`, `../../source/units/`, `../../source/game/` |

Campaign extra coordinates are beside original Xbox placements. The runtime
tests that each point is in the active BSP before it creates an object. A new
weapon must already have a tag in that campaign cache, or it needs a validated
dependency import like the b30 Rocket Warthog. Keep the original map file
read-only and test the object under its actual mission BSP. The owner supplied
PC Blood Gulch cache provides the Rocket Warthog; the overlay is currently
limited to that known cache and b30. Matching tag names across PC and Xbox do
not guarantee identical definitions. The PC reticle bitmap, for example, has
two more sequences than the Xbox one and is explicitly supplied by the overlay.

The package and repository contain no Halo ISO, map, HUD, sound, or firmware
dump. The setup scripts accept the player's own supported retail sources and
verify copies. Keep private SDK headers, toolchains, game data, emulator SD
folders, and build output out of commits. Run
`python tools/check_repository_contents.py --working-tree` before sharing a
source tree, then check the staged index with the same command without the
flag. Review license terms and the notices before distributing a build.

For a change that affects gameplay, first compile a diagnostic 3DSX, use an
isolated emulator SD with owner supplied files, and inspect
`halo-source-engine.log` for map readiness, tag errors, and frame costs. Then
build again without diagnostic flags and test that exact hash on a New 3DS.
Keep its ELF and linker map beside the 3DSX to resolve any crash addresses.
Emulator success does not establish physical speed, audio output, or long
campaign and multiplayer stability.
