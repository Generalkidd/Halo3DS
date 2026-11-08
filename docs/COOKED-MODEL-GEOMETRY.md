# Cooked model geometry

`--geometry-source cooked` enables optional `.nmg` files for the runtime's existing opaque resident model compactor. The build still validates and decodes every admitted stream on ARM. The cooker supplies integer source representative and remapped index arrays, so host floating-point behavior cannot affect vertex attributes.

Run the cooker against a prepared Xbox map and its matching relocation sidecar. The following example writes a local archive. Install it next to the corresponding map as `halo-source/a10.nmg` when preparing a private test card or emulator profile.

```sh
python3 port/n3ds/tools/cook_model_geometry.py /path/to/a10.map \
    --output /path/to/a10.nmg --compact-mode exact
```

The default `--compact-mode reference` produces source-index order for `--algo-model-compact reference`. Use `exact` with the candidate or verification compactor. It preserves first-use order and merges complete packed tuples only when all bytes match. Weighted and rigid vertex layouts both consume the same integer plan. The runtime checks the current layout against its own resident memory limit.

The versioned archive binds to the map header and tag CRC and checks its payload CRC. Its records are keyed by source tag offsets, source vertex count and triangle descriptor. Each requested record revalidates every ordered corner, checking original packed and native ARM attributes before the existing GPU owner publishes the buffers. Missing or stale data, incompatible modes, malformed plans and allocation pressure use the normal runtime path. The archive occupies at most 256 KiB on New models and 64 KiB on Original models. Use `--max-bytes 65536` to target Original model memory.

The cooker handles static, compressed Xbox model parts with opaque model or environment shaders. Runtime material, LOD and visibility eligibility still applies. The scope excludes BSP terrain, merged runtime meshes, dynamic vertices and special material paths. Loading a cooked plan avoids building the runtime remap hash table. It still validates corners and copies the selected ARM vertices.

Across 24 prepared map and relocation pairs, no scanned part met the unchanged 4 KiB net GPU storage admission rule. For Pillar of Autumn, Silent Cartographer and 343 Guilty Spark, every scanned GPU-eligible source vertex was referenced. Exact duplicates numbered 54, 54 and 47 respectively, with best net per-part savings of −12, −6 and −6 bytes after including remapped indices. Sparse synthetic fixtures cover positive admission and invalid-plan fallback. Hardware timing and rendering evidence remain open.
