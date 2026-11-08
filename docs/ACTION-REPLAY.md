# Campaign action replay

Use a private copy of the SD data for each run. A replay build uses the ordinary campaign checkpoint flow, including writes to the copied save directory. Restore the same starting save and settings before every run. Record the executable, map, sidecar and initial save hashes alongside the action file.

1. Build native 400×240 with `--action-replay record` and reference algorithm settings. Load the route in a local single-player campaign. Capture starts on the first campaign simulation tick and stops after 3,600 ticks or a clean Start+Select exit. The game publishes `sdmc:/halo-action-replay.bin` after a successful recording. Move any previous file out of that slot before recording again.
2. Use `--action-replay replay` with the same reference algorithm settings. Restore the starting save and settings, put the recorded file in the same SD slot, and enter the same campaign. The replay build checks the file and starting state before injecting actions. Once replay begins, the exit chord remains available. Gameplay inputs come from the journal.
3. Require `ACTION REPLAY PASS: matched` and the intended tick count before comparing a candidate build. Repeat with the same file, start state and route. A mismatch stops the run and identifies the first failed sample and state group in `halo-source-engine.log`.

Each file covers one map and a contiguous range of simulation ticks. Normal in-map BSP transitions can be represented. The starting save can load before the route begins. A later checkpoint restore or map replacement fails the route, even if the restored tick matches the expected one. On failure the frontend stops before the next simulation, rendering or post-tick save service. Work already executed in the failed tick remains in memory through teardown. Menu choices, pause timing, cinematic skip requests and system input events are outside the action stream. Use the same menu choices to reach the start and avoid those events during the captured route.

The action stream checks the authoritative player-action consumer. For visual comparisons, record camera and presentation-time inputs separately. Frontend camera and input-processing state can depend on events outside the action stream.

The journal contains the fields the authoritative player action consumer reads after dequeue, with their exact numeric bits. The replay build keeps normal queue advancement and supplies the recorded actions at consumption. Each sample checks its tick and pre-tick BSP. The initial and post-tick comparisons include the global and local RNG seeds, BSP, and stable digests of named fields in player, object, unit, weapon and script state. Those fields cover identity, transforms, velocities, vitality, ammunition and selected timers, animation states, script continuations, typed scenario globals and ordered members of global script object lists. The comparison excludes pointers, structure padding, script locals, string globals and game state outside the listed coverage.

The local RNG starts from wall time and is used before campaign entry as well as during presentation and certain effect decisions. The journal reports local RNG differences explicitly and leaves initialization and RNG consumption to the engine. Matching the starting save alone does not guarantee a matching local seed. Reference repeatability still requires control of startup and presentation history.

The versioned, bounded binary journal validates its size, checksum, tick sequence and action fields before use. The initial check also covers scenario name, map header checksum and length, source tag CRC, difficulty, BSP and state digests. File validation does not replace the external asset and save receipts. Replay builds are correctness fixtures and are excluded from timing runs.

The standalone ARM fixture exercises the production journal and Xbox path translator using a synthetic campaign and in-memory SD I/O. Run it with the compiler paths and build directory for an existing action replay build.

```sh
uv run --isolated --with unicorn==2.1.4 --with pyelftools==0.32 \
  python tools/verify_action_replay.py --build PATH_TO_ACTION_BUILD \
  --clang PATH_TO_CLANG --gcc PATH_TO_ARM_NONE_EABI_GCC
```

`--fixed-world-width 400 --action-replay-fixture-ticks 100` can be added to both
action builds to launch an idle `a10` campaign from a fresh private profile.
The fixture sets a fixed local RNG starting seed before campaign creation and
feeds neutral input. Each build requires the full requested tick count.
Stage the recorded journal with the emulator harness's `--sd-file` option and
require the exact recorded or matched count in the guest log. This route
passed 100 ticks in Azahar with reference algorithms and again when replayed
with the combined verification configuration. The evidence covers the opening
campaign state and fields in this journal. Wider routes and physical hardware
remain to be checked.
