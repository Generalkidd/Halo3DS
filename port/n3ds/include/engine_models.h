#ifndef HALO_N3DS_ENGINE_MODELS_H
#define HALO_N3DS_ENGINE_MODELS_H
#include "rasterizer/rasterizer_model_types.h"
#include "engine_renderer.h"
struct native_model_counters { unsigned int models,parts,triangles,max_nodes; };
struct transparent_geometry_group;
struct shader;
void n3ds_plasma_model_draw(const struct shader *,short,const struct rasterizer_model_begin_parameters *,const void *,unsigned int,short,const unsigned short *,unsigned int);
void n3ds_water_model_draw(const struct shader *,short,const struct rasterizer_model_begin_parameters *,const void *,unsigned int,short,const unsigned short *,unsigned int);
void n3ds_model_depth_group(const struct transparent_geometry_group *);
void n3ds_engine_model_counters(struct native_model_counters *result);
int n3ds_model_decode_vertex(const struct model_vertex_compressed *input, struct model_vertex_uncompressed *output);
const struct model_vertex_uncompressed *n3ds_model_decode_stream(const void *data,unsigned int count);
void n3ds_model_decode_cache_flush(void);
int n3ds_model_decode_cache_tests(void);
const struct native_render_vertex *n3ds_model_skin_stream(const struct model_vertex_uncompressed *vertices,
    unsigned int count,const struct rasterizer_model_skinning *skin,const real_vector2d *uv_scale,unsigned int owner);
int n3ds_model_skin_cache_tests(void);
int n3ds_model_gpu_cache_tests(void);
/* Existing bounded cache, no new ownership. NULL preserves CPU posing. */
const struct native_render_vertex *n3ds_model_rigid_stream(
    const struct model_vertex_uncompressed *,unsigned int,
    const struct rasterizer_model_skinning *,float rows[12]);
/* Checked conversion helpers also exercised independently of GPU submission. */
int n3ds_model_skin_vertex(const struct model_vertex_uncompressed *vertex,
    const struct rasterizer_model_skinning *skinning, const real_vector2d *uv_scale,
    struct native_render_vertex *output);
long n3ds_model_triangle_list(short type, const unsigned short *source,
    unsigned int triangles, unsigned int vertices, unsigned short *output,
    unsigned int capacity);
#endif
