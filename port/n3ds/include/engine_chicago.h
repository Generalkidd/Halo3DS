#ifndef N3DS_ENGINE_CHICAGO_H
#define N3DS_ENGINE_CHICAGO_H
/* Fixed-width native GPU boundary. */
/* Biased adds need additional stages; keep them on the bounded bake path. */
static inline int n3ds_chicago_native_function(int function)
{ return function>=0 && function<=12 && function!=5 && function!=6; }
struct native_chicago_vertex { float position[3],uv[3][2],color[4]; };
/* Linear add/subtract/component-max two-map effect. uv0.xy/uv2.x are homogeneous secondary
 * coordinates; uv1 is the primary sprite UV. Source textures remain slots0/1. */
int n3ds_gpu_effect_layer_draw(struct native_chicago_vertex *,unsigned int,int blend,
    unsigned int primary_sampler,unsigned int secondary_sampler,unsigned int geometry_flags);
struct native_chicago_material {
    int maps,blend,alpha_test,two_sided,decal,sky;
    int color_function[2],alpha_function[2],alpha_replicate[2];
    int point[3],clamp_u[3],clamp_v[3];
    float fade;
    int vertex_fade; /* 1: baked map fade in uv[2].x; 2: fade in color (all UVs retained). */
    int meter; /* Original tint-mode1 constant-alpha/constant-color blend. */
    unsigned int meter_tint; /* Native C3D RGBA packing. */
    int vertex_color; /* Already evaluated view-dependent generic RGBA. */
    int generic_lit_lerp; /* Two-map generic fragment combiner; color is lighting/fog. */
    int shield_mask; /* Scalar animation in map0 alpha; authored mask in map1. */
    unsigned int shield_tint; /* Native C3D RGBA packing. */
};
int n3ds_gpu_chicago_initialize(void);
int n3ds_gpu_hologram_draw(const struct native_chicago_material *,struct native_chicago_vertex *,
    unsigned int,const unsigned short *,unsigned int,const float screen_rows[3][4],const float tint[3],float noise);
int n3ds_gpu_chicago_tests(void);
int n3ds_gpu_chicago_transform_tests(void);
int n3ds_gpu_chicago_transform_draw(const struct native_chicago_material *,const void *,unsigned int,
    const unsigned short *,unsigned int,const float *,unsigned int,int,const float *);
void n3ds_gpu_chicago_dispose(void);
void n3ds_gpu_chicago_frame_begin(void);
void n3ds_gpu_chicago_flush(void);
struct native_chicago_vertex *n3ds_gpu_chicago_allocate(unsigned int count);
int n3ds_gpu_chicago_draw(const struct native_chicago_material *material,
    struct native_chicago_vertex *vertices,unsigned int vertex_count,
    const unsigned short *indices,unsigned int index_count);
int n3ds_gpu_chicago_relative_draw(const struct native_chicago_material *,
    struct native_chicago_vertex *,unsigned int,const unsigned short *,unsigned int);
#endif
