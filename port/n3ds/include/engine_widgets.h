#ifndef HALO_N3DS_ENGINE_WIDGETS_H
#define HALO_N3DS_ENGINE_WIDGETS_H
struct native_widget_vertex {float position[3],color[4],uv[2];};
struct native_packed_effect_vertex {float position[3],uv[2];unsigned int argb;};
/* Optional affine rows transform original view-space positions. NULL is world. */
int n3ds_gpu_effect_packed_draw(const struct native_packed_effect_vertex *,unsigned int,
    int,unsigned int,unsigned int,unsigned int,const float *rows);
int n3ds_gpu_effect_packed_tests(void);
void n3ds_gpu_effect_state_invalidate(void);
int n3ds_gpu_effect_packed_indexed_draw(const struct native_packed_effect_vertex *,unsigned int,
    const unsigned short *,unsigned int,int,unsigned int,unsigned int,unsigned int,const float *);
int n3ds_gpu_widgets_initialize(void);
void n3ds_gpu_widgets_dispose(void);
void n3ds_gpu_widgets_frame_begin(void);
void n3ds_gpu_widgets_flush(void);
int n3ds_gpu_widget_draw(const struct native_widget_vertex vertices[4],int world,
    float width,float height,float tint,unsigned int flags);
int n3ds_gpu_world_projection(float matrix[16]);
int n3ds_engine_widgets_active(void);
int n3ds_gpu_widget_tests(void);
/* Effect triangles use original ARGB vertex tint and primary-map UVs. */
int n3ds_gpu_effect_draw(const struct native_widget_vertex *vertices,unsigned int count,
    int blend,unsigned int shader_flags,unsigned int sampler_flags,unsigned int geometry_flags);
#endif
