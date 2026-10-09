# Local solo multiplayer bots

The New 3DS runtime can add 0 to 15 local AI players to a solo multiplayer match. The local player uses one of the engine's 16 player slots. Put a decimal number from `0` to `15` in `sdmc:/halo-source/bots.txt`, or pass `-BotCount` to the PC installer. Invalid settings disable bots. The multiplayer menu selects the map and variant.

Bots are local players in the original engine's player and action queues. There is no network bot process. The controller in `src/engine_bots.c` looks for enemies, weapons, grenades, vehicles, and mode objectives; uses BSP routes and short detours; and submits movement, aim, melee, grenade, and weapon actions. The game's pickup, damage, and scoring code remains in charge. Team bots leave a flag or ball held by an ally alone and select the nearest eligible one. A disappearing ground weapon target is checked before use.

Each match assigns ordinary bots distinct random names from the forty built-in Xbox guest names in the owner's `ui.map`, including Donut. The 11-character profile limit accommodates every name in that list. In free for all, ordinary bots also receive distinct randomized player profile colors. ChaoticSPARTAN1 uses bright green. Team games render every bot in the assigned red or blue team color. The PC Local Multiplayer menu sets Easy, Normal or Hard before the match. Ordinary bots fire about every 20, 15 or 11 ticks with 70%, 85% or 93% aim rolls respectively. The separate named bot keeps its own combat tuning and appears less often on Easy (one chance in twelve matches), compared with Normal (one in four) and Hard (one in three).

Weapon firing still uses the original engine's muzzle, projectile, contrail, impact, shield, grenade and decal systems. Native particle budgets now prioritize combat effects, including distant impact and weapon particles, while visible contrails and decals continue through their original render paths. Sniper bots can fire across large maps when they have line of sight. Camera frustum and geometry visibility still apply.

Mode goals:

- Slayer: seek and fight an enemy, with item and vehicle choices.
- CTF: seek an enemy flag, bring a carried flag to the home base, or defend and return a displaced home flag.
- Oddball: seek a free or enemy-held ball, then evade nearby enemies while carrying it.
- King: move toward the active hill and fight nearby enemies.
- Race: pursue the next eligible checkpoint, preferring a vehicle.

The PC and Xbox launchers use the same bot controller. The PC runtime lets the menu select the bot count; `bots.txt` supplies a default. Bots are created only for a solo multiplayer match. Test builds can queue a match directly with `build.py --diagnostic-pc-play MAP --diagnostic-pc-mode MODE --diagnostic-pc-bots N` or `--probe-solo-bots N --probe-solo-mode MODE` for Xbox format Blood Gulch.

## Checks and limits

Azahar 2126.2 reached PC Timberland CTF frame 691 with two registered bots, 24 route decisions, both bots selecting enemy flags, decreasing goal distances, and no fatal error after the ally-target and stale-weapon fixes. In a longer run, both bots reached flag-carry mode and continued to frame 5,371 after the native renderer learned to handle the flag cloth's nested model pass. Earlier emulator probes entered Slayer, CTF, Oddball, King, and Race matches; a 15-bot Slayer probe reached tick 300. The PC mode cross section reached active frames on Gephyrophobia CTF, Death Island Oddball, Ice Fields King, Infinity Race, and Timberland Slayer. A Race bot entered and drove a Warthog in Blood Gulch. These checks establish match entry, decisions and flag carrying, not full match outcomes.

Full flag captures, Oddball and hill scoring, Race lap completion, reliable combat on every map and weapon, and physical New 3DS XL performance still need device validation. Path building can return a partial route on long Timberland paths and the bot retries from there. Some combat damage is applied directly for a bot's confirmed line-of-sight weapon hit because the local fake client cannot report projectile impact. This can differ from human projectile behavior. The project contains no game maps, ISO, Xbox game files, or bot executables.
