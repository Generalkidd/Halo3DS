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
#if defined(HALO_N3DS_PERFORMANCE_PROFILE) || defined(HALO_N3DS_RENDERER_TESTS) || defined(HALO_N3DS_CROWD_PROFILE)
#define HALO_N3DS_MODEL_STAGE_PROFILE 1
void n3ds_model_profile_read(long long result[3]);
#endif
int n3ds_model_decode_vertex(const struct model_vertex_compressed *input, struct model_vertex_uncompressed *output);
const struct model_vertex_uncompressed *n3ds_model_decode_stream(const void *data,unsigned int count);
/* Exact raw UV extrema owned by the decoded revision. No extent adjustment,
 * allocation or eviction; zero leaves the caller's original reduction active. */
int n3ds_model_uv_bounds(const struct model_vertex_uncompressed *vertices,unsigned int count,float bounds[4]);
void n3ds_model_decode_cache_flush(void);
int n3ds_model_decode_cache_tests(void);
/* Consume posed CPU vertices before requesting another pose. Midpoint
 * scratch is reused, so draws must publish only their own GPU-arena copy. */
const struct native_render_vertex *n3ds_model_skin_stream(const struct model_vertex_uncompressed *vertices,
    unsigned int count,const struct rasterizer_model_skinning *skin,const real_vector2d *uv_scale,unsigned int owner);
/* Optional raw weighted normals, without normalization or material effects.
 * A request cannot evict a decoded/posed stream; returned CPU storage remains
 * valid until this GPU frame ends or the map cache is explicitly flushed.
 * NULL retains the caller's existing per-vertex calculation. */
const real_vector3d *n3ds_model_normal_stream(const struct model_vertex_uncompressed *vertices,
    unsigned int count,const struct rasterizer_model_skinning *skin,unsigned int owner);
/* Paired position/UV and raw normal request for consumers that replace color.
 * The returned vertex color is unspecified. Either stream may be unavailable. */
#if HALO_N3DS_ALGO_SHARED_SKINNING != N3DS_ALGORITHM_REFERENCE
const struct native_render_vertex *n3ds_model_unlit_skin_stream(const struct model_vertex_uncompressed *,
    unsigned int,const struct rasterizer_model_skinning *,const real_vector2d *,
    unsigned int,const real_vector3d **);
#else
static inline const struct native_render_vertex *n3ds_model_unlit_skin_stream(
    const struct model_vertex_uncompressed *v,unsigned int count,const struct rasterizer_model_skinning *skin,
    const real_vector2d *scale,unsigned int owner,const real_vector3d **normals)
{
    const struct native_render_vertex *pose=n3ds_model_skin_stream(v,count,skin,scale,owner);
    *normals=n3ds_model_normal_stream(v,count,skin,owner);
    return pose;
}
#endif
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
