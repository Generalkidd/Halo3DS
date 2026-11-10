# PC and wireless integration status

The PC contribution and wireless multiplayer/co-op work are integrated on `dev`.
This document tracks follow-up work; it is not a release or hardware certification.

## Implemented

- Shared Xbox/PC map and gear browsing, with PC options hidden when their assets
  are absent. Campaign extras remain disabled in co-op.
- Return to Lobby restores the successfully launched local match's map format,
  map, full gametype, bot count/difficulty and gear choices. Cancelled edits do
  not overwrite those choices.
- System Link hosts can add up to three bots before countdown. Host-generated
  player actions use the normal network stream; bots never receive local viewports.
  Local matches still allow fifteen bots. Co-op removes bot profiles.
- The expanded touch scoreboard is available in wireless multiplayer.
- Six PC-exclusive maps have top-screen thumbnails read from the owner's PC
  UI assets. Asynchronous previews cancel obsolete selections and publish only
  current textures after GPU completion; no Halo artwork is distributed.
- Balanced, Quality and Performance presets are selectable and saved. Performance
  reduces model detail/reflection layers and cosmetic particles, without changing
  simulation or network rates. Balanced remains the default.
- Both Windows setup apps accept revised Xbox discs, warn about unverified
  releases, and offer optional PC maps in a collapsed section. Executable-only
  updates preserve maps and saves. The CLI retains compatibility warnings in
  quiet mode and distinguishes strict reference verification from preparation.

## Verification

Host regressions cover settings migration/persistence, format-qualified maps,
co-op gating, local lobby restoration, host-only bot roster ownership, bot removal
in co-op, asynchronous preview cancellation and bounded GPU publication.
The six PC-exclusive thumbnails and geometry previews completed emulator sweeps
under New and Original model limits. A campaign menu regression resumed a real
saved Halo checkpoint with the correct difficulty and state marker. A PC
Timberland match entered gameplay and returned to the shared lobby with its
bot settings preserved. Paired emulators ran two humans plus three bots through
2,460 network ticks: all 82 sampled seeds and object-state hashes matched.
Performance-mode beach combat completed on both emulated model types; the
runs do not establish a hardware speedup. Physical wireless tests remain open.
Installer tests cover malformed sources, destination safety, reuse, cancellation,
optional PC import and revised-disc handling. New setup packages are version 1.20.0.

## Still requires physical consoles

No consoles were available for this integration pass. Before promoting this test
build, check a mixed Original/New pair with matching executables and game data:

- Xbox and PC map identity, missing-map rejection and all PC-exclusive maps.
- Host bot controls, combat/damage, deaths/respawns, team modes, scores and
  prolonged synchronization; match completion, leave/disconnect and rematch.
- Co-op progression, save behavior and blocked campaign extras.
- Preview responsiveness, full-screen touch-aim capture and readable thumbnails.
- Balanced/Quality/Performance frame times and memory use on both generations.
- Full local bot rosters and the previously reported Death Island tank-firing path.

Emulator checks do not establish hardware frame rate or wireless reliability.

## Development and distribution

See [developer notes](DEVELOPMENT.md) for builds and implementation details,
[the main README](../README.md) for simple setup instructions, and
[licensing and credits](../LICENSING.md) for component licenses. Preserve the
Kevin Boggs attribution markers registered in `AGENTS.md` and the attribution
manifests. Keep retail maps, ISOs, XBE files, HUD data, firmware, private SDKs and
private test logs out of Git and setup packages. The installers bundle only the
launcher, setup code and required licenses/runtimes.
