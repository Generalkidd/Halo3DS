#ifndef N3DS_ENGINE_HUD_H
#define N3DS_ENGINE_HUD_H
#include "engine_text.h"
/* Fixed-width ILP32 packet shared by the engine and native GPU compiler. */
struct native_hud_meter {
    unsigned int minimum,maximum,background,flash;
    unsigned char negative,mode2,pad[2];
    unsigned int tint;
    float gradient;
};
/* Original menu plasma request, with already transformed per-map UVs. */
struct native_menu_plasma {
    void *textures[3];
    float uv[3][4][2],tint[3][4],fade[4];
    int point,wrap[3],blend;
};
void n3ds_hud_screen(int bottom);
void n3ds_gpu_hud_layout(int group);
void n3ds_gpu_hud_sensor_origin(float x,float y,float radius);
int n3ds_gpu_touch_panel(const unsigned int *pixels,unsigned int revision);
int n3ds_gpu_hud_icon_bounds(void *texture,float bounds[4]);
int n3ds_gpu_menu_plasma(const struct native_menu_plasma *,const struct native_text_vertex [4],float,float);
int n3ds_gpu_screen_multiply(const struct native_menu_plasma *,const struct native_text_vertex [4],float,float);
void n3ds_menu_plasma_evaluate(const float [3][4],const float [3][4],const float [4],const float [4],float [4]);
float n3ds_menu_plasma_intensity(const float [2][4],const float [3][4]);
int n3ds_menu_plasma_tests(void);
int n3ds_gpu_menu_plasma_tests(void);
void n3ds_gpu_hud_dispose(void);
void n3ds_gpu_hud_frame_begin(void);
/* Non-overlapping native CPU stages during the original HUD pass. */
void n3ds_gpu_hud_profile_reset(void);
void n3ds_gpu_hud_profile_add(unsigned int stage,long long ticks);
void n3ds_gpu_hud_profile_read(long long ticks[4]);
int n3ds_gpu_hud_meter(const struct native_hud_meter *,const struct native_text_vertex [4],float,float,int,int);
void n3ds_gpu_sensor_begin(void);
int n3ds_gpu_sensor_blip(void *,float,float,float,const float [3]);
int n3ds_gpu_sensor_end(void *,void *,float,float,float,float,float,float);
#endif
