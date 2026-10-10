# Halo CE PC retail cache integration

The local integration branch reads owner-supplied Halo: Combat Evolved PC retail
v7 caches on New 3DS. The source and packages contain no maps, ISO, firmware or
extracted game assets. `cache_file_formats.c` and `.h` provide a bounded cache
reader; the New 3DS adapter validates and rebases known tag, model, bitmap and
BSP data before passing a map to the original game engine.

## Runtime and installation

`build.py --experimental-pc-runtime --unified-ui` builds the combined launcher.
It keeps the original Xbox main menu and campaign, adds a SELECT Local
Multiplayer selector for PC maps, and falls back to installed Xbox caches when
a requested PC map is absent. The [unified installer](../setup/Unified/README.md)
accepts the owner's Xbox and PC disc images and verifies the launcher manifest
and destination copies. The [separate PC-only installer](../setup/PC/README.md)
remains available for that launcher. Parser probes and forced-map diagnostic
builds are not release packages.

The selector lists all 19 stock PC multiplayer maps and the original Slayer,
CTF, Oddball, King and Race variants. Matches run locally with zero to fifteen
optional bots. Weapon and vehicle sets can keep stock placements or select PC
items. The All PC vehicles choice adds two Banshees each to Danger Canyon,
Ice Fields and Timberland using assets already present in those retail caches.
PC multiplayer uses the original weapon, vehicle, damage, objective and scoring
systems. LAN and wireless multiplayer are not implemented by this integration.

## Parser and runtime evidence

Isolated Azahar probes loaded PC `ui.map` with 1,409 tags and 10,274 known
pointers rebased, and Blood Gulch with 2,408 tags, 20,587 known pointers and
466 BSP pointers rebased. Both had zero unknown tag groups. These counts
establish coverage of checked layouts; they do not prove every possible field
in a known group is converted.

Azahar entered active local matches on all 19 stock PC multiplayer maps.
Selected CTF, Oddball, King and Race fixtures placed their original objectives
and ran bots. A moving six-bot Timberland run reached 1,200 frames without a
fatal error. On a New 3DS XL, an earlier six-bot Timberland build reached frame
20,621 and the postgame state. A missing return path found in that run was
fixed and verified in isolated emulator menu replays; the newer device build
still needs a repeat hardware check.

The PC Blood Gulch arsenal fixture fired all 11 tested weapons or mounted gun
paths. Captures show map textures, vehicles, HUD, particles, tracers, the
world-space flamethrower trail, and fading Fuel Rod impact. A sniper shot on a
136.84 m map-authored clear sightline rendered a 36-point contrail spanning
136.28 m. The native two-map effect renderer also passed 50 independent GPU
pixel comparisons, including premultiplied secondary effects.

A separate PC `a10` diagnostic loaded 3,724 tags, converted 76 models and
rendered the opening ring cinematic past frame 450. This establishes only that
scene's startup. The combined launcher's campaign path uses the installed Xbox
maps; optional campaign PC weapons and a Rocket Warthog are described in the
[developer notes](../../../docs/DEVELOPMENT.md).

## Remaining verification

The current clean unified launcher has passed an isolated Azahar main-menu
smoke check. New 3DS XL checks are still needed for its postgame return, saved
controller layout, decals and effects, vehicle handling, sustained moving
combat frame rate, checkpoint resume and longer campaign progression. An older
hardware Timberland log showed combat windows slower than 30 FPS, so emulator
capture timing is not a device performance measurement.

For extension points and file ownership, see the [modding guide](../MODDING.md)
and [developer notes](../../../docs/DEVELOPMENT.md).
