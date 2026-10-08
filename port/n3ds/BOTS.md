# Local solo multiplayer bots

The New 3DS runtime can add 0 to 15 local AI players when the original menu starts a solo multiplayer match. The local player uses one of the engine's 16 player slots. Put a single decimal number from `0` to `15` in `sdmc:/halo-source/bots.txt`, or pass `-BotCount` to the PC ISO installer. Missing or invalid settings disable bots. The selected map and game variant still come from the game's multiplayer menu.

The behavior is adapted from the local Halo CE recomp combat bots. Bots follow BSP routes, steer around short obstacles, seek enemies, weapons, equipment and vehicles, and use weapon definitions and line of sight for combat. The mode objectives cover Slayer combat, CTF flags, Oddball, King hills and Race checkpoints. Bots are registered as local match players before the game clock starts. Their actions enter the normal player action queue; no network service or external bot process is launched.

The implementation is in `src/engine_bots.c`. `main_new_map` creates bots only for solo multiplayer, and `update_server_next_update` asks the bot controller for an action only for a registered bot. Normal builds omit per action diagnostic logs. The opt-in `build.py --probe-solo-bots N --probe-solo-mode MODE` build queues Blood Gulch for emulator checks.

Azahar OpenGL probes with private Xbox-format Blood Gulch and two bots reached game ticks past 2,900 in Slayer, selected CTF flag objectives, Oddball objectives, King hills and Race goals. A 15 bot Slayer probe registered all bots and reached tick 300. These checks establish match entry and AI decisions in the emulator; scoring, full matches, every map and weapon, physical New 3DS XL performance, and full PC retail compatibility still need validation. The existing PC retail runtime path is under integration and the PC installer cannot yet be packaged with a verified PC-capable 3DSX.

Experimental PC retail probes now cover two bot CTF on Timberland, and two bot
Oddball and Race on Blood Gulch. The original engine placed flags and four
Warthogs in Timberland CTF; the bots spawned and selected flag objectives.
The original path search filled its 1,024 node list on long routes in that BSP.
The bot controller now accepts the original path builder's closest explored
point as a partial waypoint when the node list fills, then searches again.
In the CTF probe, both bots received four step routes and closed their
remaining goal distances over successive searches, though they did not
complete a flag capture in the observed interval. Blood Gulch Oddball bots
selected the ball, routed and exchanged hits; a Race bot entered and drove a
map owned Warthog. These are emulator diagnostics, not a claim that every PC
map, weapon, vehicle, mode objective or full match has been verified.

The project does not include or distribute game maps, an ISO, Xbox game files, or bot executables.
