#ifndef HALO_N3DS_ENGINE_GPU_MODEL_LAYOUT_H
#define HALO_N3DS_ENGINE_GPU_MODEL_LAYOUT_H

#include <citro3d.h>
#include "engine_model_vertex.h"

#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
#define N3DS_SKIN_ATTRIBUTES 5
#define N3DS_SKIN_PERMUTATION 0x43210
#else
#define N3DS_SKIN_ATTRIBUTES 4
#define N3DS_SKIN_PERMUTATION 0x3210
#endif

static inline void n3ds_gpu_model_attributes_initialize(C3D_AttrInfo *attributes,int skinned)
{
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);
    AttrInfo_AddLoader(attributes,1,GPU_FLOAT,3);
    AttrInfo_AddLoader(attributes,2,GPU_FLOAT,2);
    if(skinned) {
#if N3DS_VERTEX_LAYOUT_PACKED_ENABLED
        AttrInfo_AddLoader(attributes,3,GPU_SHORT,2);
        AttrInfo_AddLoader(attributes,4,GPU_FLOAT,2);
#else
        AttrInfo_AddLoader(attributes,3,GPU_FLOAT,4);
#endif
    }
}
#endif
