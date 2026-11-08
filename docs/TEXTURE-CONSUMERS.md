# Texture consumer inventory for native formats

Source review only; no assets were scanned or rendered. This inventory is the
starting point for a map-specific consumer graph, not proof that any bitmap is
eligible for a smaller format. The implemented opt-in [format-aware archive](NTX4.md)
emits only RGBA8, with every descriptor conservatively marked CPU-RGBA8 and
unknown-use. See the [preservation contract](RENDERER-PRESERVATION.md).

The map cache owns one GPU resource per `bitmap_data`, with no consumer-role
metadata. [Texture selection](../port/n3ds/src/engine_rasterizer_textures.c)
wraps permutations and can substitute shared default textures. Eligibility
must union **all** reachable uses, including animated frames, indirect/default
references, low-memory fallbacks and CPU-only readers. Selecting a format from
the first draw that requests a texture is unsafe.

## Consumers and current assumptions

| Consumer or owner | Source and behavior | Initial native-format policy |
| --- | --- | --- |
| Chicago fallback, generic bakes and shields | [GPU Chicago](../port/n3ds/src/engine_gpu_chicago.c): `sample` indexes tiled `unsigned int` RGBA8; shield bake directly extracts low-byte alpha. | Require RGBA8 for every source that can reach these paths. |
| Generic per-vertex shading | [Generic models](../port/n3ds/src/engine_generic_model.c) uses the same CPU sampler, including projective coordinates. | Require RGBA8 even if a different material pass uses the same bitmap only on GPU. |
| Chicago material dispatch | [Chicago](../port/n3ds/src/engine_chicago.c): unsupported combiner operations can bake even with fewer than four maps; numeric animations can select different bitmap frames. | Inspect functions and all permutations; map count alone cannot establish eligibility. |
| Water fallback and model meters | [Water](../port/n3ds/src/engine_water.c) captures the base texture for CPU fallback; [meters](../port/n3ds/src/engine_meter.c) pass maps to the bake pipeline. | Require RGBA8 while these fallbacks remain possible. |
| Menu plasma and screen multiplication | [HUD](../port/n3ds/src/engine_gpu_hud.c): CPU sampler reads tiled RGBA8; menu maps 0/1 are CPU sampled, map 2 GPU sampled; multiply samples both maps. | Exclude CPU inputs. Map 2 still needs the complete sharing audit. |
| HUD meters | [HUD](../port/n3ds/src/engine_gpu_hud.c): compares/copies `width × height × 4` bytes and reads blue/alpha from packed words. | Require RGBA8; blue and alpha are numerical material inputs. |
| Radar and icon bounds | [HUD](../port/n3ds/src/engine_gpu_hud.c): radar fallback, mask samples and icon alpha scanning read tiled RGBA8. | Require RGBA8 for mask, blips, sweep and scanned icons, even when the radar GPU path is available. |
| Plasma environment derivative | [GPU textures](../port/n3ds/src/engine_gpu_textures.c): `n3ds_gpu_texture_plasma` requires RGBA8 and rotates each packed word into an alpha-first derivative. | Exclude both source and derivative initially; preserve separate allocation/lifetime accounting. |
| CPU plasma volumes | [Plasma](../port/n3ds/src/engine_plasma.c): reads original map formats into a separate red/alpha volume cache. It does not read C3D texture storage. | Separate consumer class; leave outside the initial single-level 2D archive. |
| CPU reflection cubes | [Glass](../port/n3ds/src/engine_glass.c): separate map-read RGBA cube cache serves glass/water/generic fallbacks. | Separate from native cubemap eligibility, payload and memory accounting. |
| GPU terrain, models, holograms, water, glass and supported Chicago | [Renderer](../port/n3ds/src/engine_gpu_renderer.c), [GPU Chicago](../port/n3ds/src/engine_gpu_chicago.c) bind descriptors. Channels also control masks, alpha tests, bump lighting and reflection. | Potential candidates only after all shared consumers and exact channel/sampling semantics are known. |
| GPU effects, sprites and ordinary text | [Widgets](../port/n3ds/src/engine_gpu_widgets.c), [text](../port/n3ds/src/engine_gpu_text.c) use GPU descriptors/samplers. | Preserve alpha/blend/border semantics; their draw routines alone do not prove global eligibility. |
| Generated HUD/generic images and render targets | [HUD](../port/n3ds/src/engine_gpu_hud.c), [GPU Chicago](../port/n3ds/src/engine_gpu_chicago.c): write RGBA8, flush and publish content revisions; radar has an RGBA8 render target. | Keep existing generated-resource wrappers and revisions; exclude mutable/derived outputs. |
| Dynamic bitmaps, font atlases and touch panel | [Hardware bitmaps](../port/n3ds/src/engine_hardware_bitmaps.c) replaces/fences atlas resources; Original-model format-9 fonts already have a dedicated RGBA4 path. [Touch panel](../port/n3ds/src/engine_gpu_text.c) writes RGBA8 in place. | Preserve specialized policies. Do not route these through generic map-format selection. |
| Built-in/procedural images and world-resolve targets | [Loading resources](../port/n3ds/src/engine_gpu_renderer.c) already use RGBA4 artwork and RGBA8 font data. [Controls](../port/n3ds/include/engine_controls_draw.inl) and [video/noise](../port/n3ds/include/engine_screen_effect.inl) own procedural RGBA8 images. [World resolve](../port/n3ds/include/engine_world_scale.inl) uses RGB565 on Original models and RGBA8 on New models. | Exclude these independent owners from map-format selection; preserve their existing formats and target/fence rules. |
| Unknown or unclassified use | A tag/reference type, debug path or future material outside the audited consumer set. | Require RGBA8; absence of a recorded CPU use is not proof of GPU-only use. |

## Constraints on the future descriptor and eligibility pass

1. Key the consumer union by stable map identity and bitmap descriptor offset,
   with all group permutations/default substitutions resolved. Runtime pointers
   and content revision IDs are not archive identities. Keep a reason for each
   RGBA8 requirement so coverage can be inspected without weakening fallback.
2. Record CPU-readable RGBA8, GPU-only, mutable/derived, volume/cube and unknown
   requirements separately. An unknown or CPU-readable use dominates any
   GPU-only use of the same bitmap. Do not discover a stricter requirement only
   after publishing a smaller allocation.
3. Judge **final baseline texels** after selected mip, decoding, resizing and
   lightmap treatment. [Lightmap averaging/dilation](../port/n3ds/include/engine_lightmap_filter.inl)
   can create eight-bit values not representable by source RGB565. DXT
   interpolation and P8 palette values also require exact reconstruction
   checks. Source format labels are insufficient.
4. Keep RGBA8 as default. RGB8 needs fully opaque alpha and matching implicit
   alpha behavior; RGB565/RGBA5551/RGBA4 need exact reconstruction of every final
   channel. Luminance/alpha formats additionally need verified implicit-channel
   and material behavior. Texel parity alone does not prove PICA sampling parity.
5. Keep ownership identity separate from format/capability. Current immutable
   and generated revision IDs in [GPU textures](../port/n3ds/src/engine_gpu_textures.c)
   represent content lifetime. Never reinterpret a published allocation or
   change bytes still referenced by queued GPU work.

## Allocation and loading boundary

The RGBA8 descriptor is selected **before** storage reservation in every lane.
[Bitmap plans](../port/n3ds/src/engine_textures.c),
[worker preparation](../port/n3ds/include/engine_texture_prefetch.inl),
[GPU reserve/finalize](../port/n3ds/src/engine_gpu_textures.c) and
[legacy archive validation](../port/n3ds/include/engine_texture_archive.inl)
retain the existing RGBA8 decoding while allocation and budget checks now use
explicit resident bytes and bounded per-face spans. Native format, faces/levels,
source mip, conversion policy and CPU requirements remain fixed by this first
representation. Selected worker lanes separately account for actual scratch;
archive workers need none. Smaller formats still require a complete consumer
graph and format-specific handling in every lane.

Retain the current zero-extra-image-copy preparation sequence: reserve private
final storage, bounded worker read/prepare, validate completion and generation,
render-owner flush, then publish. Keep slot limits and headroom unchanged.
Cancel and acknowledge before reclaiming worker storage; release failed native
storage before allocating the legacy/map fallback. Midpoints do not load.

`.ntx4` contains single-level 2D RGBA8 with explicit little-endian
fields, schema/conversion versions, installation/map identity, stable bitmap
and resolution keys, provenance, checked bounds and checksums. Candidate
identity validation uses a setup-verified catalog map identity, loaded header
and already-validated pre-relocation tag CRC. No whole-map hash is added during
demand. `.ntx` and original map decode remain independent fallbacks; the
installer estimates both pending archive queues while retaining installed data.

Descriptor and candidate archive source work is implemented but unvalidated.
The per-map eligibility graph and smaller-format allocation remain pending.
ETC1/ETC1A4, authored mip chains and CPU shadow copies remain separate future
work with separate quality and memory decisions.
