#ifndef HALO_N3DS_ENGINE_MODEL_VERTEX_H
#define HALO_N3DS_ENGINE_MODEL_VERTEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "engine_renderer_features.h"

struct native_skin_vertex {
    float position[3],normal[3],uv[2];
#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
    int16_t bone_rows[2];
    float weights[2];
#else
    float bones[4];
#endif
};

/* The shared 32-byte prefix can still be copied from a rigid vertex.
 * PICA reads the row offsets as non-normalized signed shorts in v3.xy,
 * and the original float weights in v4.xy. */
#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
typedef char n3ds_skin_vertex_row_offset[(offsetof(struct native_skin_vertex,bone_rows)==32)?1:-1];
typedef char n3ds_skin_vertex_weight_offset[(offsetof(struct native_skin_vertex,weights)==36)?1:-1];
typedef char n3ds_skin_vertex_stride[(sizeof(struct native_skin_vertex)==44)?1:-1];
#else
typedef char n3ds_skin_vertex_row_offset[(offsetof(struct native_skin_vertex,bones)==32)?1:-1];
typedef char n3ds_skin_vertex_stride[(sizeof(struct native_skin_vertex)==48)?1:-1];
#endif

static inline void n3ds_skin_vertex_read_bones(const struct native_skin_vertex *vertex,float bones[4])
{
#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
    bones[0]=vertex->bone_rows[0];
    bones[1]=vertex->bone_rows[1];
    memcpy(bones+2,vertex->weights,sizeof(vertex->weights));
#else
    memcpy(bones,vertex->bones,sizeof(vertex->bones));
#endif
}

static inline void n3ds_skin_vertex_set_bones(struct native_skin_vertex *vertex,
    unsigned int first_row,unsigned int second_row,float first_weight,float second_weight)
{
#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
    vertex->bone_rows[0]=first_row;
    vertex->bone_rows[1]=second_row;
    vertex->weights[0]=first_weight;
    vertex->weights[1]=second_weight;
#if HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_PACKED_VERIFY
    float actual[4];
    const float expected[]={first_row,second_row,first_weight,second_weight};
    n3ds_skin_vertex_read_bones(vertex,actual);
    if(memcmp(expected,actual,sizeof(actual)))abort();
#endif
#else
    vertex->bones[0]=first_row;
    vertex->bones[1]=second_row;
    vertex->bones[2]=first_weight;
    vertex->bones[3]=second_weight;
#endif
}
#endif
