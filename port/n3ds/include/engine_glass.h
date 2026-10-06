#ifndef N3DS_ENGINE_GLASS_H
#define N3DS_ENGINE_GLASS_H
#include "engine_renderer.h"
int n3ds_gpu_glass_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,int pass,int two_sided);
int n3ds_gpu_glass_sky_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,int,int,int);
int n3ds_gpu_glass_tests(void);
struct native_glass_material {
    void *cube,*diffuse;
    float perpendicular[4],parallel[4];
    unsigned int tint; /* Packed RGBA8 constant destination multiplier. */
    int two_sided;
    const float *pose; /* Optional 3x4 rigid transform; NULL means world vertices. */
    float uv_scale[2];
};
int n3ds_gpu_glass_native_draw(const struct native_render_vertex *,const unsigned short *,
    unsigned int,const struct native_glass_material *);

int n3ds_gpu_glass_native_sky_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,const struct native_glass_material *,int);
/* Cube direction uses D3D face order +X,-X,+Y,-Y,+Z,-Z. */
int n3ds_cube_coordinates(const float direction[3],int *face,float uv[2]);
const unsigned char *n3ds_cube_load(long tag,short permutation);
void n3ds_cube_sample(const unsigned char *pixels,const float direction[3],float rgba[4]);
#endif
