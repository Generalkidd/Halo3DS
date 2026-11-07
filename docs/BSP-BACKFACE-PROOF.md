# BSP backface block proof

The [host cooker](../tools/bsp_backface_proof.py) accepts the native 32-byte BSP
vertex records and their matching full little-endian uint16 triangle list.
Supply the bytes after runtime decode and vertex compaction, before portal
index filtering. The cooker hashes both byte streams, validates their lengths
and indices, and partitions the existing sequence into contiguous 32-triangle
blocks. It preserves the list and all source bytes. It emits a versioned JSON
report for inspection and verification. The current fixture requires those
geometry buffers as inputs. The game's midpoint capture supplies camera
records independently.

```text
python3 tools/bsp_backface_proof.py native-vertices.bin native-indices.bin --camera view.camera > blocks.json
python3 tools/bsp_backface_proof.py native-vertices.bin native-indices.bin --camera view.camera --verify blocks.json
python3 -m unittest discover -s tools -p test_bsp_backface_proof.py
```

For each triangle the cooker computes the winding normal
**n = (b − a) × (c − a)** using exact rational arithmetic on the binary32
inputs. It records the position and normal component bounds across the block.
The first normal defines an axis. For every nonzero normal with positive axis
dot product, the exact squared alignment is
**(n · axis)² / (‖n‖² ‖axis‖²)**. Its minimum describes a closed cone about
that axis. A zero normal or a nonpositive dot makes the block uncertain.

With a camera record, the rasterizer eye and position bounds enclose each
component of **eye − a**. The minimum and maximum products of the normal and
displacement interval endpoints enclose that component's contribution to
**n · (eye − a)**. Summing the bounds encloses the signed result for every
triangle. A strictly positive or negative interval establishes a common
camera side in ideal arithmetic. An interval containing zero stays uncertain.

The engine still submits its existing triangle lists. Promotion requires a
verified relation between the ideal side and the native PICA winding decision
after vertex conversion, DPH projection, clipping, viewport transform and
subpixel quantization. An initial hardware comparison can restrict the trial
to blocks whose vertices have proven margins inside every clip plane and whose
projected signed area stays strictly on the rejected side under a validated
error bound. Clipped triangles and ambiguous areas remain visible. The
physical New 2DS XL must establish that bound.

The opaque environment path generally uses `GPU_CULL_FRONT_CCW`. Missing
light-UV storage can take the generic draw path with culling disabled. A
future filter must validate the actual emitted pass, including that fallback,
before using block evidence. It also needs a camera-dependent filter identity
and must preserve original order when retaining spans alongside portal
filtering. Measure the impact on full-list batching separately.
