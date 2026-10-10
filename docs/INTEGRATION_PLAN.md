# Proposed New 3DS integration

This is a review guide for the original Halo3DS author. The work is on the
`local-integration` branch and has **not** been submitted upstream. It adds one
experimental New 3DS launcher that can run the existing Xbox campaign and
local multiplayer, plus Halo CE PC retail v7 local multiplayer. Original 3DS
and 2DS support in the upstream release should remain a separate supported
path until the new code is tested there.

## What players receive

The [unified installer guide](../port/n3ds/setup/Unified/README.md) is the
player-facing starting point. The Windows PowerShell installer takes the
player's own supported Xbox XISO and either their own PC ISO or extracted PC
`maps` folder. It checks both sources before installation, supports `-WhatIf`,
copies verified files to the SD card, and keeps a backup of the previous
launcher. The distributable contains the launcher, setup tools, source, and
license notices. It contains no game maps, disc images, XBE, HUD, audio,
firmware, or private SDK files.

The Xbox menu retains campaign and multiplayer. SELECT opens PC local
multiplayer. Its lower screen shows map and equipment previews, and the gear
page selects PC map weapons and vehicles. The campaign difficulty screen has
a saved switch for optional PC additions. These are runtime objects: Xbox map
files and their original placements are never changed.

## Where the changes live

| Area | Main files | Purpose |
| --- | --- | --- |
| PC cache loading | `port/n3ds/pc/cache_file_formats.c`, `engine_pc_runtime.c` | Validate and read owner supplied retail v7 maps. |
| Menus and previews | `port/n3ds/src/engine_frontend_boot.c`, `port/n3ds/preview/` | Choose a map and show its geometry and equipment. |
| Gameplay | `port/n3ds/src/engine_bots.c`, `source/game/`, `source/items/`, `source/units/` | Solo local matches, bots, and PC weapon and vehicle rules. |
| Campaign additions | `port/n3ds/src/engine_campaign_options.c`, `engine_campaign_extras.c`, `engine_campaign_overlay.c` | Save the switch, place optional pickups, and import the Rocket Warthog dependencies for b30. |
| Setup | `port/n3ds/setup/Unified/`, `port/n3ds/setup/PC/`, `port/n3ds/setup/Core/` | Validate sources, install a single launcher, and package source without game data. |

The [developer notes](DEVELOPMENT.md) contain build commands and test details;
the [modding guide](../port/n3ds/MODDING.md) points to code for common edits.
The repository's `AGENTS.md` records attribution requirements for the New 3DS
integration. Existing third party and upstream license notices remain intact.

## Review and merge recommendation

1. Review the data boundary first: `tools/check_repository_contents.py`,
   the fixed file list in `Package-Halo3DS-Unified.ps1`, and an extracted package.
   Build outputs, toolchains, private maps, and emulator SD folders stay out of Git.
2. Review Xbox regression and menu navigation on original 3DS, New 3DS, and
   emulator. Keep the default Xbox release route intact while introducing the
   unified launcher as an experimental choice.
3. Review PC cache conversion and one local multiplayer map at a time. Exercise
   weapon and vehicle pickups, bot matches, return to lobby, saves, sound, and
   frame time under combat on physical hardware.
4. Review campaign additions separately. The Xbox caches already contain the
   hidden Flamethrower and Fuel Rod definitions used in c20, c40, and d20.
   Runtime placement waits for the player to approach and confirms the active
   BSP contains the point. The b30 Rocket Warthog instead loads 16 missing
   dependencies from the player's PC Blood Gulch cache; a missing or rejected
   source leaves the Xbox mission playable. Test mission progression,
   checkpoints, HUD, driving, and the switch's off state on hardware.
5. Rebuild from the reviewed tree and test the **same 3DSX hash** that would be
   distributed. Keep its ELF and linker map for crash diagnosis. Merge only
   after the author is satisfied with hardware behavior and distribution terms.

## Evidence and open checks

On 2026-10-10, the current diagnostic build completed two PC menu and gear
navigation replays and the c20, c40, and d20 campaign replays in Azahar 2126.2
without a fatal error. Its logs recorded all four optional weapon pickups in
valid mission clusters: one in c20, one in c40, and two in d20. An extracted
installer package passed a PC map `-WhatIf` check. An invalid Xbox source was
rejected before any destination write. The b30 Rocket Warthog appeared beside
an original Warthog in an earlier emulator run; emulator
replays entered the vehicle and fired its mounted gun through the normal input
path. A previous New 3DS XL build completed a six-bot Timberland match. Those
checks show that the paths work, but they do not establish complete campaign
playthroughs or a steady 30 FPS. The current launcher still needs physical
New 3DS testing for campaign progression, visual quality, effects, busy bot
combat, and save and restore behavior. Emulator results should be reported as
emulator results.

The PC map selector now keeps every BSP triangle in the 19 supported retail
multiplayer maps; the previous 2,048-triangle limit left visible gaps. Death
Island scales its oversized decorative water only for the preview and uses dry
terrain to set the initial zoom. The lower screen gives the map most of its
space, with mode, bots, difficulty, and texture preference on the top screen.
Emulator replays checked map selection, gear/back navigation, option changes,
Death Island, and the optional NAV overlay without a fatal error. A New 3DS XL
Death Island match with seven bots crashed in an earlier launcher while a
layered shader requested a texture at the 8 MiB cache limit. The current code
reuses its base texture for that layer when allocation fails; that visual
fallback still needs a physical replay of the same match and tank firing.

The 16-player limit also had a focused emulator stress pass. A diagnostic
build replayed the PC Local menu on Death Island, selected CTF, raised the bot
count to 15, and selected all PC vehicles. The engine registered all 15 bots
beside the local player, created both CTF flags, and reached frame 1,800
(1,040 gameplay ticks) without a fatal error. The gameplay capture showed the
world and HUD. The CTF variant enables teams and the local player plus bots
are assigned to alternating teams in source, yielding eight players per side.
The game reported 16 active bipeds and 46 vehicle objects during this run.
After gameplay tick 800, the emulator's sampled frame time had a 37.6 ms
median, above the 33.3 ms target for steady 30 FPS; one sample reached
56.5 ms. These are emulator timings, not New 3DS XL measurements. A longer
physical match with moving players, combat, vehicles, and flag captures is
still needed before calling full-roster performance or stability proven.

A second emulator pass started the same Death Island CTF roster with high PC
textures and ran past frame 2,400, or about 77 seconds of gameplay. It
confirmed 16 players, bot pathing, weapon pickups, hits, grenades, melee,
vehicle entries, and a player death and respawn, with no fatal error. Its
sampled frame time after tick 800 had a 30.6 ms median and a 65.2 ms 95th
percentile as combat intensified. These varied emulator timings reinforce the
need for a measured New 3DS XL run before promising steady frame rate. The
player-fire diagnostic originally missed the saved shoulder-button swap;
the fixture now follows that setting and requires an actual shot to pass. A
follow-up replay passed: the assault rifle's loaded rounds fell from 60 to
45, and the engine recorded a nonzero firing time without a fatal error.

For a reproducible review, run `python port/n3ds/tools/check_attribution.py`
and `python tools/check_repository_contents.py --working-tree`,
build with the command in [developer notes](DEVELOPMENT.md), and run the
installer from a freshly extracted package with `-WhatIf` before any SD write.
Do not attach retail files, SD logs that contain private paths, or SDK files to
an upstream pull request.
