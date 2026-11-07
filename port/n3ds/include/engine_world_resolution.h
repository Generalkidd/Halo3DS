#ifndef HALO_N3DS_ENGINE_WORLD_RESOLUTION_H
#define HALO_N3DS_ENGINE_WORLD_RESOLUTION_H

/* Stable values stored in the graphics preferences. */
enum n3ds_world_resolution {
    N3DS_WORLD_RESOLUTION_AUTO,
    N3DS_WORLD_RESOLUTION_400,
    N3DS_WORLD_RESOLUTION_320,
    N3DS_WORLD_RESOLUTION_280,
    N3DS_WORLD_RESOLUTION_200,
    N3DS_WORLD_RESOLUTION_COUNT
};

/* Settings request a mode; the renderer latches it before the next world frame. */
void n3ds_gpu_world_resolution_request(enum n3ds_world_resolution mode);
int n3ds_gpu_world_resolution_available(void);

#endif
