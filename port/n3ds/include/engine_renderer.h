#ifndef HALO_N3DS_ENGINE_RENDERER_H
#define HALO_N3DS_ENGINE_RENDERER_H
/* Matches skin.v.pica: 87 bone vectors leave room for camera and constants. */
enum { NATIVE_GPU_MODEL_BONES = 29 };
struct native_render_vertex;
void n3ds_gpu_world_budget(int enabled);
struct native_screen_effect {float enhancement,desaturation,tint[3],noise;int additive,video,video_overbright;};
void n3ds_gpu_screen_effect(const struct native_screen_effect *);
void n3ds_gpu_frame_work(float milliseconds);
void n3ds_gpu_world_view_begin(void);
void n3ds_gpu_world_resolve(void);
int n3ds_gpu_world_pressure(void);
float n3ds_gpu_world_pixel_scale(void);
int n3ds_gpu_stereo_view(void);
int n3ds_gpu_fragment_tests(void);
void n3ds_gpu_flashlight_set(float power,const float origin[3],const float forward[3]);
int n3ds_gpu_flashlight_tests(void);
int n3ds_gpu_model_depth_draw(const struct native_render_vertex *,const unsigned short *,unsigned int);
int n3ds_gpu_model_depth_relative_draw(const struct native_render_vertex *,const unsigned short *,unsigned int);
void n3ds_gpu_camera_origin(float origin[3]);
/* Rebase only disposable upload copies, never the shared world-space pose cache. */
void n3ds_gpu_model_rebase_vertices(struct native_render_vertex *,unsigned int,int sky);
int n3ds_gpu_model_depth_transform_draw(const void *,const unsigned short *,unsigned int,const float *,unsigned int,int);
int n3ds_gpu_model_cloak_transform_draw(const void *,const unsigned short *,unsigned int,const float *,unsigned int,int,const float *,float,int,int);
/* Fixed-width ABI boundary: original engine uses long32, platform uses int32. */
struct native_render_vertex { float position[3], color[3], uv[2]; };
/* All live buffer owners use this binding path. Reuse concerns descriptors,
 * never mutable vertex contents; callers still flush written bytes as before. */
int n3ds_gpu_vertex_buffer_bind(const void *data,unsigned int stride,unsigned int attributes,unsigned int permutation);
/* Constant colors use PICA RGBA8 byte order (R in the low byte). Map lifetime
 * owns the optional immutable multipurpose texture through queued draws. */
struct native_model_lighting {
    float ambient[3];
    struct { float color[3],direction[3]; } distant[2];
    unsigned int count;
};
/* Borrowed lighting is copied to renderer-owned state before this call returns. */
struct native_model_material { void *multipurpose; unsigned int emission,tint; int alpha_test;
    const struct native_model_lighting *lighting; float translucency;
    void *reflection,*base; float reflection_delta[4],reflection_parallel[4];
    void *detail; float detail_scale[2]; int detail_function,detail_mask,detail_after; };
/* Water input has posed world position, animated normal in color, and base UV.
 * Resource ownership remains with the bounded map texture cache. */
struct native_water_wave_vertex {struct native_render_vertex vertex;float wave[4];};
/* Non-null phase selects the extended immutable wave stream. Four products
 * apply angle-addition identities without per-vertex CPU trigonometry. */
struct native_water_material {void *base,*cube;float perpendicular[4],parallel[4];unsigned int flags;int sky;const float *wave_phase;};
int n3ds_gpu_water_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,const struct native_water_material *);
int n3ds_gpu_water_tests(void);
int n3ds_gpu_model_detail_tests(void);
int n3ds_gpu_model_reflection_tests(void);
/* Immutable stream revisions stay GPU-resident; NULL requests frame fallback.
 * Caller supplies a stable0..255 cache slot and a new revision on every decode. */
void *n3ds_gpu_model_resident_upload(unsigned int slot,unsigned long long revision,const void *source,unsigned int bytes);
int n3ds_gpu_model_resident_tests(void);
int n3ds_gpu_model_lighting_tests(void);
/* Rigid input stores a local normal in color; rows are scaled bone XYZ rows. */
int n3ds_gpu_model_rigid_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,
    const float rows[12],const float uv[2],int sky,int decal,int two_sided,const struct native_model_material *);
struct native_skin_vertex { float position[3],normal[3],uv[2],bones[4]; };
int n3ds_gpu_model_skin_draw(const struct native_skin_vertex *,const unsigned short *,unsigned int,
    const float *rows,unsigned int nodes,const float uv[2],int sky,int decal,int two_sided,const struct native_model_material *);
int n3ds_gpu_model_cpu_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,
    int sky,int decal,int two_sided,const struct native_model_material *);
int n3ds_gpu_model_cpu_relative_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,
    int sky,int decal,int two_sided,const struct native_model_material *);
int n3ds_gpu_model_material_tests(void);
int n3ds_gpu_model_transform_tests(void);
/* Compatible opaque parts share GPU state only inside a model submission scope.
 * Flush before any CPU-shaded/transparent path or external render-state change. */
void n3ds_gpu_model_batch_begin(void);
void n3ds_gpu_model_batch_flush(void);
void n3ds_gpu_model_batch_end(void);
int n3ds_gpu_model_batch_tests(void);
int n3ds_gpu_environment_draw(const struct native_render_vertex *,const float *light_uv,
    const unsigned short *,unsigned int count,int lightmap,int detail,float detail_u_scale,float detail_v_scale,unsigned int material,int alpha_test);
/* Original primary/secondary emission plus constant plasma-off channel.
 * RGB map channels are masks; alpha-driven plasma remains a separate path. */
struct native_environment_emission {unsigned int color[3];float uv[4];};
int n3ds_gpu_environment_emission_draw(const struct native_render_vertex *,const float *light_uv,
    const unsigned short *,unsigned int count,int lightmap,unsigned int material,int alpha_test,
    const struct native_environment_emission *);
int n3ds_gpu_environment_plasma_draw(const struct native_render_vertex *,const float *light_uv,
    const unsigned short *,unsigned int count,int lightmap,unsigned int material,int alpha_test,
    const struct native_environment_emission *,unsigned int on,unsigned int phase);
void n3ds_gpu_plasma_stats(unsigned int values[3]);
int n3ds_gpu_plasma_lut_tests(void);
int n3ds_gpu_environment_tests(void);
struct native_environment_bump_vertex {float light_uv[2],quaternion[4],weight;};
int n3ds_gpu_environment_bump_draw(const struct native_render_vertex *,const struct native_environment_bump_vertex *,
    const unsigned short *,unsigned int count,float u_scale,float v_scale,unsigned int material,int alpha_test);
int n3ds_gpu_environment_bump_tests(void);
struct native_render_camera {
    float position[3], forward[3], up[3], vertical_fov, near_clip, far_clip;
    unsigned int clear_color;
    int suppress_clear;
};
int n3ds_gpu_renderer_initialize(void);
int n3ds_gpu_programs_initialize(void);
void n3ds_gpu_program_park(void);
void n3ds_gpu_programs_dispose(void);
void n3ds_gpu_renderer_dispose(void);
int n3ds_gpu_frame_begin(void);
int n3ds_gpu_frame_active(void);
int n3ds_gpu_bottom_owned(void);
void n3ds_gpu_hud_screen(int bottom);
void n3ds_gpu_hud_screen_restore(void);
float n3ds_platform_3d_slider(void);
void n3ds_gpu_stereo_level(float level);
void n3ds_gpu_stereo_eye(int right,float offset);
void n3ds_gpu_stereo_overlay(int enabled);
int n3ds_gpu_overlay_other_begin(void);
void n3ds_gpu_overlay_other_end(void);
/* Current command segment; Citro3D's usage statistic updates only at submit. */
float n3ds_gpu_command_usage(void);
long long n3ds_gpu_frame_wait_ticks(void);
void n3ds_gpu_frame_sync(int enabled);
/* Original shader framebuffer blend-function order, rasterizer_xbox.c. */
enum { NATIVE_BLEND_ALPHA, NATIVE_BLEND_MULTIPLY, NATIVE_BLEND_DOUBLE_MULTIPLY,
    NATIVE_BLEND_ADD, NATIVE_BLEND_SUBTRACT, NATIVE_BLEND_MIN, NATIVE_BLEND_MAX,
    NATIVE_BLEND_PREMULTIPLIED_ALPHA, NATIVE_BLEND_COUNT };
int n3ds_gpu_framebuffer_blend(int mode);
int n3ds_gpu_blend_tests(void);
void n3ds_gpu_window_begin(const struct native_render_camera *camera);
void n3ds_gpu_present(void);
int n3ds_gpu_screen_test(void);
int n3ds_gpu_readback(unsigned int *pixels,unsigned int count);
int n3ds_gpu_sky_projection(float matrix[16]);
int n3ds_gpu_world_projection(float matrix[16]);
/* Model palettes subtract this origin before PICA's reduced-precision math. */
int n3ds_gpu_model_projection(float matrix[16],float origin[3]);
void n3ds_gpu_world_state_restore(void);
void n3ds_gpu_opaque_sky_begin(void);
int n3ds_gpu_first_person_begin(float near_clip,float far_clip);
void n3ds_gpu_first_person_end(void);
void n3ds_gpu_model_stencil_apply(void);
int n3ds_gpu_first_person_tests(void);
int n3ds_gpu_loading_ready(void);
int n3ds_gpu_loading_draw(float progress);
void n3ds_gpu_loading_pulse(float progress,int force);
void n3ds_gpu_loading_enable(int enabled);
unsigned int n3ds_gpu_loading_submissions(void);
int n3ds_gpu_loading_capture(unsigned int stage);
int n3ds_gpu_screen_quad_capture(void);
void *n3ds_gpu_geometry_allocate(unsigned int bytes);
/* Optional visual data leaves 2 MiB of linear headroom for gameplay. */
void *n3ds_gpu_geometry_optional_allocate(unsigned int bytes);
void n3ds_gpu_geometry_free(void *memory);
void n3ds_gpu_geometry_flush(void *memory, unsigned int bytes);
int n3ds_gpu_geometry_draw(const struct native_render_vertex *vertices,
                          const unsigned short *indices, unsigned int count);
/* Immutable linear-memory indices, validated and flushed by their BSP owner. */
int n3ds_gpu_geometry_draw_resident(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count);
struct shader;
struct vertex_buffer;
void n3ds_engine_bsp_draw(const struct shader *shader,short bitmap_index,
    const unsigned short *indices,unsigned int count,const struct vertex_buffer *source,
    int lightmap_group,short lightmap_index,const struct vertex_buffer *lightmap_source);
int n3ds_gpu_model_decal_draw(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count,int sky,int two_sided);
int n3ds_gpu_model_decal_tests(void);
int n3ds_gpu_indexed_draw(const void *vertices,const unsigned short *indices,
    unsigned int count,unsigned int stride,unsigned int attributes,unsigned int permutation);
/* Frame-owned immutable copy; remains valid through both eye views and is
 * retired only by the next frame's GPU wait. NULL outside an active frame. */
const unsigned short *n3ds_gpu_indices_upload(const unsigned short *indices,unsigned int count);
struct native_render_vertex *n3ds_gpu_model_vertices_allocate(unsigned int count);
unsigned long long n3ds_gpu_model_frame_serial(void);
void n3ds_gpu_model_cache_release(void);
int n3ds_engine_window_active(void);
int n3ds_engine_models_active(void);
int halo_engine_model_tests(void);
int halo_engine_model_fixture_prepare(void *scenario, const float *position);
void halo_engine_model_fixture_draw(void);
void n3ds_engine_geometry_flush(void);
unsigned int n3ds_engine_geometry_count(void);
int halo_engine_render_tests(void);
#endif
