# Optional RGBA8 texture archive

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
