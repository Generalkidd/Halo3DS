# Optional RGBA8 texture archive

## Gameplay texture modes

On New systems, open **Graphics Options** from the bottom screen in the
main or pause menu. Press **Y** for the same shortcut. Choose a mode and
save. The new mode applies when the next campaign map loads. Returning to
the main menu and loading a mission also applies it.

| Mode | Campaign world texture cap | Detail sampling |
| --- | --- | --- |
| Performance (default) | 32 px | Authored mipmaps on eligible detail textures |
| Balanced | 64 px | Authored mipmaps on eligible detail textures |
| Classic | 64 px | Single level |

The cap applies to the longest dimension. HUD, menu art, readable displays,
and reflection cubes retain their separate resolution rules. Eligible detail
chains come from the retail map, including the authored fade at smaller
mips. Exact RGB8 is used for the audited opaque sand and cliff detail
textures in Silent Cartographer. Other detail textures retain an RGBA8 base,
and unsupported chains use their ordinary single-level texture. The
ordinary texture archives already cover the 32 and 64 px plans, so the
graphics settings work with existing installs.

## Native texture LOD fixture

Cook the three opaque retail Silent Cartographer detail textures into a
standalone development pack.

```text
halo3ds-setup texture-lod --map b30.map --output texture-lod.ntxm
```

The pack holds one tiled RGB8 chain per texture. Mode 0 uses its 64×64
base level. Mode 1 uses all four levels from 64×64 through 8×8. Mode 2 uses
the same chain from 32×32 through 8×8. The mip modes blend between levels.
The fixture renders the same 400×240 corridor and camera sweep in each mode.
Sand, cliff rock and brushed metal come from the retail map. The corridor is
a controlled scene for evaluating texture sampling.

Build with `--fixed-world-width 400 --texture-lod-fixture capture` for one
60-frame sweep with top-screen framebuffer readback. Use `--texture-lod-fixture timing`
with the same viewport for four sweeps without readback. Both builds load
`sdmc:/halo-texture-lod.ntxm` and select the mode from a one-byte ASCII
`0`, `1`, or `2` in `sdmc:/halo-texture-lod-mode.txt`.

`port/n3ds/tools/emulator_texture_lod.py` stages the private Azahar profiles,
checks the selected dimensions and allocation sizes, and assembles native PNG
stills and a frame-indexed 30 fps motion comparison. The timing runs measure
guest render work through GPU queue retirement and paced loop intervals.
Compare physical New 2DS XL timings separately.

## Opt-in texture comparison

The standalone setup command writes a matched schema-2 archive from an
expanded, catalogue-verified retail map. Use a new output path for each
variant. The ordinary setup installer continues to generate schema 1.

```text
halo3ds-setup texture-compare --map a10.map --output raw/a10.ntx4 --variant raw
halo3ds-setup texture-compare --map a10.map --output lz4hc/a10.ntx4 --variant lz4hc
halo3ds-setup texture-compare --map a10.map --output compact/a10.ntx4 --variant compact
```

Only `lz4hc` requires the optional host `liblz4` shared library. Pass
`--lz4-library PATH` if it is outside the loader search path. Each compressed
payload is independent and decodes into its original tiled RGBA8 allocation.
The compact mode considers only audited BSP lightmap descriptors in retail
`a10` and `b30`, after the normal lightmap filtering. Eligible grayscale with
alpha uses LA8 and eligible opaque color uses RGB8. All other payloads use raw
RGBA8. The cooker reads the archive back and compares reconstructed RGBA8
against the canonical conversion before publishing.

Build once per fixture with `--texture-storage candidate-archive
--texture-compare --fixed-world-width 400 --texture-cache-mib 4`. The archive
header selects the mode, so use the same executable for all three inputs.
The comparison build requires prefetch and midpoint presentation disabled.
The ordinary legacy archive and map decoder remain available on failure.

To qualify the Azahar route, build with `--texture-compare-fixture capture`.
Cook both `ui` and `a10` into each variant's directory. Run
`port/n3ds/tools/emulator_texture_compare.py --phase capture` with the retained
Azahar binary, private prepared assets, DSP firmware and a new output directory.
Pass `--case LABEL BINARY ARCHIVE_DIR` and `--expect-mode LABEL MODE` for
each input, using mode names `raw`, `lz4`, and `packed`. The harness runs
sequentially, checks actual mode and format publication, and compares native
RGBA8 captures. Its report identifies each emulator backend and input hash.

Use a separate build with `--texture-compare-fixture timing
--performance-profile` after capture parity passes. Run the same tool with
`--phase timing`. It compares the bounded title-to-campaign fixture and
records guest entry time, loop work and loop intervals. These are Azahar
observations. Physical microSD latency, ARM11 time and displayed stutter
require a separate console comparison.

Schema 2 retains the 128-byte header and 96-byte entry layout below. Header
offset 100 stores mode 1 (raw), 2 (LZ4 HC), or 3 (compact). Entry offset 88
stores codec 0 (raw) or 1 (LZ4), and offset 92 stores the canonical RGBA8 FNV.
Offsets 48 and 60 distinguish resident and stored length, and offset 64 checks
the resident bytes. Stable format IDs 2 and 3 represent RGB8 and LA8,
respectively. The stored LZ4 block is capped at 256 KiB and must be smaller
than its decoded length.

## Schema 1 reference

This is development source, not a validated release. No builds, tests, archive
generation, emulator runs or device comparisons have been performed for this
stage. Rendering parity, lifecycle behavior and performance remain unverified.

Two independent opt-ins are required:

- Game build: `--texture-storage candidate-archive` (default: `legacy`).
- Setup CLI `prepare`: `--candidate-textures` (default: omitted).

The setup core exposes the same option as `SetupOptions.CandidateTextures`.
Launcher-only updates reject combining it with candidate generation; normal
launcher-only behavior remains unchanged. Geometry and vertex-layout selectors
are independent and still reject their unimplemented candidate modes.

The new path preserves the existing selected source mip, output dimensions,
RGBA8 conversion, lightmap filtering/dilation, sampling and materials. It does
not add compression, mipmaps, smaller formats or channel quantization. Native
400×240 presentation, authoritative 30 Hz logic and regular-frame priority are
outside this change. The initial archive can cost more disk/index memory than
legacy `.ntx`; it is infrastructure for later separately evaluated formats,
not a measured performance improvement.

## Resource and loading contract

[The engine descriptor](../port/n3ds/include/engine_texture_descriptor.h) gives
RGBA8 its own stable ID `1`, explicitly translated to `GPU_RGBA8`. It records
dimensions, faces/levels, source mip, conversion policy, CPU requirements,
source/resident/scratch byte counts and checked face offsets/lengths. Every
descriptor currently requires CPU-readable RGBA8 plus unknown-use (bits 1|2).
No per-map consumer graph or smaller-format eligibility decision is implied.

`n3ds_gpu_texture_reserve_descriptor` validates the descriptor and actual C3D
allocation before returning byte spans. Existing RGBA8 creation/reservation
functions remain wrappers. Existing cube allocations retain six separate
images and their size limit; candidate archives themselves support only 2D.
The dedicated RGBA4 font and other procedural/mutable owners keep their
existing specialized paths.

Demand, queued and speculative loads reserve unpublished storage before
preparation. Archive reads go directly into its bounded RGBA8 span; map decode
writes the same final image. Worker jobs copy plans/ranges/hash/kind and never
retain archive-directory entries. Only the render owner checks completion and
generation, flushes and publishes. Cancellation keeps worker-owned storage
alive until acknowledgement. A failed async job is released before a later
fallback allocation; synchronous archive failure can safely overwrite the same
unpublished RGBA8 span without allocating a second image.

The descriptor's source count identifies the selected original mip. Resident
bytes account for all faces. Its initial scratch count bounds map fallback:
four-byte-aligned source size plus lightmap distance storage when needed.
Queued/prefetch selection replaces that count with the lane's actual scratch
allocation (zero for archives). Existing load counters record actual I/O bytes.
Slot limits, texture-cache byte caps and demand headroom stay unchanged.

The candidate directory opens once per tag generation at regular-frame idle,
outside an active GPU frame. Selection from an open directory does no I/O.
Midpoints retain the existing cold-texture rejection path and never open or
read an archive. A demand before candidate preparation uses legacy/map data.
Candidate lookup or validation failure keeps the `.ntx` path available; a bad
candidate payload disables only the candidate for that generation. Legacy
failure continues to map decoding. Map unload joins/cancels existing worker
preparation and closes both indexes through the existing texture flush path.
The application also flushes texture ownership before GPU teardown on return
from the frontend, including an early failure that bypassed normal map cleanup.
This joins preparation before releasing private buffers and archive handles;
resident textures still pass through the existing GPU fence. The same call is
safe after normal frontend teardown has already emptied the cache.
The archive-close log separates validated candidate and legacy payload hits;
these counters identify which storage path served data, not displayed FPS or
successful GPU publication. Existing lane profiles continue reporting actual
I/O and preparation work.

## Identity and setup ownership

Setup hashes the full expanded map and requires the catalog SHA-256 before
candidate generation or acceptance. It reuses the existing texture plans and
conversion functions. Candidates are generated in the installer's staging
directory, read back with metadata/bounds/payload checks, then committed with
the existing backup/rollback operation. Legacy `.ntx` is retained. Disk checks
account for both pending archive queues and staging; installed archives already
occupy filesystem space and remain in place until replacement is ready.

Runtime compares the candidate's recorded map SHA-256/name/size to the compiled
catalog, its map-header hash to the loaded header, and its tag CRC to the CRC
already verified before relocation. Each lookup also checks the relevant live
bitmap metadata and selected conversion plan. This deliberately does not
rehash the map on the console: arbitrary edits to pixel bytes after setup are
not detected by a whole-map runtime scan. Re-run setup verification after
changing installation files.

The new `source_tags_crc` member belongs to native cache-view metadata, outside
authoritative game state and checkpoint payloads. No runtime pointers, GPU
handles or generation counters are serialized into the archive.

The compiled catalog is derived from `setup/Resources/catalog.json`; regenerate
it with `python port/n3ds/tools/texture_catalog.py` when that source changes.
Unknown map identities fall back to existing paths. The converter is shared
by CLI, modern Windows setup and the existing Windows 7 source adapter; no new
encoder or package dependency is introduced. Those packaging paths still need
their later build/validation checks.

## On-disk schema 1

All integers are unsigned 32-bit little-endian values. SHA-256 is 32 raw bytes.
Checksums named FNV use existing FNV-1a, seed 2166136261, multiplier 16777619,
modulo 2³². Tag CRC is standard reflected CRC-32, matching relocation data.
The conversion-policy version is `1` and must change when canonical output or
source-selection interpretation changes. File size is bounded by `INT_MAX` for
the console's seek interface.

| Header byte offset | Field |
| ---: | --- |
| 0 | Magic `0x34585448` |
| 4 | Schema `1` |
| 8, 12 | Header bytes `128`, entry bytes `96` |
| 16, 20 | Conversion policy `1`, byte-order marker `0x01020304` |
| 24, 28 | Expanded map bytes, FNV of its 2048-byte header |
| 32, 36 | Original tag bytes, CRC of original unrelocated tag data |
| 40 | Entry count, 1…16384 |
| 44, 48 | Directory offset `128`, directory bytes `count × 96` |
| 52 | Payload start `128 + 16384 × 96` |
| 56, 60 | Archive bytes, FNV of the actual directory |
| 64 | Expanded map SHA-256 (32 bytes) |
| 96 | Header FNV, calculated with this word zero |
| 100…127 | Reserved; must be zero |

The directory is sorted strictly by `(bitmap_offset, resolution)`. Payload
space follows the bounded reserved directory, as with the existing legacy
writer. Runtime allocates only the actual `count × 96` directory bytes, at most
1.5 MiB, separately from the legacy index. Index allocation failure falls back.

| Entry byte offset | Field |
| ---: | --- |
| 0, 4 | Bitmap descriptor offset in original tags, effective resolution key |
| 8, 12 | Selected source mip file offset and byte count |
| 16, 20 | Target width and height |
| 24, 28, 32 | Stable native format `1` (RGBA8), faces `1`, levels `1` |
| 36, 40, 44 | Selected source mip, conversion policy `1`, CPU requirements `3` |
| 48, 52 | Resident bytes, bounded fallback scratch bytes |
| 56, 60, 64 | Payload offset, byte count, FNV |
| 68, 72, 76 | Original format ID, selected source mip width and height |
| 80 | FNV of the bitmap descriptor's first 32 source-metadata bytes |
| 84 | Lightmap processing flag (0 or 1) |
| 88, 92 | Reserved; must be zero |

Offsets/lengths are checked before reads. Payload is the same tiled RGBA8 byte
sequence produced by existing conversion, with one level and the same vertical
orientation. Exact aliases may share a payload when all conversion/provenance
inputs agree. Setup's verifier rejects conflicting aliases, partial overlaps,
unexplained padding between distinct payloads and trailing data. A future
format/layout/policy is unsupported until both allocator and consumers support
it; it must not be interpreted through RGBA8 assumptions.

## Deferred validation

Keep candidate selection off in normal builds until explicitly evaluated.
First establish the [reference and comparison records](RENDERER-PRESERVATION.md),
then check canonical texels, source mip/lightmaps and GPU sampling/materials
against legacy archives and original decoding. Include absent/stale/corrupt
archives, interrupted installation, worker cancellation, low memory, transitions
and checkpoint restore. The existing archive-verification diagnostic can compare
loaded candidate pixels against map decoding; diagnostic readback runs cannot
supply performance evidence. Do not combine this stage with geometry/packing or
promote defaults before the requested individual validation gates pass.
