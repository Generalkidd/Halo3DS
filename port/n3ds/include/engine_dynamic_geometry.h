#ifndef HALO_N3DS_DYNAMIC_GEOMETRY_H
#define HALO_N3DS_DYNAMIC_GEOMETRY_H
/* Original-engine ABI; never included by platform/GCC translation units. */
struct native_triangle_view { const unsigned short *indices; unsigned int count; };
struct native_vertex_view { const void *data; unsigned int count; short type; };
boolean rasterizer_dynamic_geometry_initialize(void);
void rasterizer_dynamic_geometry_begin(void);
void rasterizer_dynamic_geometry_end(void);
void rasterizer_dynamic_geometry_dispose(void);
const struct native_triangle_view *n3ds_dynamic_triangles_get(long handle);
const struct native_vertex_view *n3ds_dynamic_vertices_get(long handle);
unsigned int n3ds_dynamic_geometry_bytes(void);
int halo_engine_dynamic_geometry_tests(void);
#endif
