#ifndef HALO_N3DS_ENGINE_RENDERER_H
#define HALO_N3DS_ENGINE_RENDERER_H
#include "engine_renderer_features.h"
#include "engine_model_vertex.h"
/* Matches skin.v.pica: 87 bone vectors leave room for camera and constants. */
enum { NATIVE_GPU_MODEL_BONES = 29 };
#if defined(HALO_N3DS_PERFORMANCE_PROFILE) || defined(HALO_N3DS_PRESENT_INTERPOLATION)
/* Nominal top-screen interval for profiling and optional draw admission. */
#define N3DS_TOP_REFRESH_MS (1000.0 / 59.831)
#endif
struct native_render_vertex;
#ifdef HALO_N3DS_GPU_PROBE
struct native_gpu_probe_view {
    unsigned int generation;
    int tick, bsp;
    /* Render/rasterizer position, forward, up, vertical FOV and clip planes. */
    float camera[2][12];
    short bounds[2][8];
};
void n3ds_gpu_probe_view(const struct native_gpu_probe_view *view, int chord_held);
void n3ds_gpu_probe_terrain(int terrain);
#endif
void n3ds_gpu_world_budget(int enabled);
struct native_screen_effect {float enhancement,desaturation,tint[3],noise;int additive,video,video_overbright;};
void n3ds_gpu_screen_effect(const struct native_screen_effect *);
void n3ds_gpu_frame_work(float milliseconds);
void n3ds_gpu_world_view_begin(void);
void n3ds_gpu_world_resolve(void);
#ifdef HALO_N3DS_FRAMEBUFFER_REUSE
/* Before a render-target clear, command-list split or external target bind. */
void n3ds_gpu_framebuffer_invalidate(void);
#else
#define n3ds_gpu_framebuffer_invalidate() ((void)0)
#endif
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
/* Single-stream binding reuse concerns descriptors, never vertex contents.
 * Callers still flush written bytes as before. */
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
/* These paths take owner-validated, flushed linear indices that stay live
 * until the next GPU fence. Other callers keep the checked staging path. */
int n3ds_gpu_water_draw_resident(const struct native_render_vertex *,const unsigned short *,unsigned int,const struct native_water_material *);
int n3ds_gpu_water_tests(void);
/* Preserve order: only adjacent native water/glass draws may retain state.
 * Break before other transparent paths or depth prepasses; end at view end. */
void n3ds_gpu_surface_batch_begin(void);
void n3ds_gpu_surface_batch_break(void);
void n3ds_gpu_surface_batch_end(void);
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
int n3ds_gpu_model_skin_draw(const struct native_skin_vertex *,const unsigned short *,unsigned int,
    const float *rows,unsigned int nodes,const float uv[2],int sky,int decal,int two_sided,const struct native_model_material *);
int n3ds_gpu_model_scenery_draw_resident(const void *,const unsigned short *,unsigned int,
    const float *rows,unsigned int instances,const float uv[2],int two_sided,const struct native_model_material *);
int n3ds_gpu_model_compact_draw_resident(const void *,const unsigned short *,unsigned int,
    const float *rows,unsigned int nodes,const float uv[2],int two_sided,int skinned,const struct native_model_material *);
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
/* True only while an active frame uses mono scanout; stable through a text pass. */
int n3ds_gpu_frame_is_mono(void);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
/* On success, complete one rasterizer frame and present it before waiting.
 * Its ordinary frame begin consumes the native reservation. */
int n3ds_gpu_midpoint_try_begin(void);
/* Includes the reservation until its rasterizer frame is presented. */
int n3ds_gpu_midpoint_active(void);
/* Read after the following keyframe. The transfer path owns the bounded wait. */
int n3ds_gpu_midpoint_latched(void);
int n3ds_gpu_midpoint_compatible(void);
long long n3ds_gpu_present_cost_ticks(int midpoint);
unsigned int n3ds_gpu_keyframe_latches(void);
long long n3ds_gpu_next_top_blank(long long now);
long long n3ds_gpu_top_refresh_ticks(void);
#endif
int n3ds_gpu_bottom_owned(void);
void n3ds_gpu_hud_screen(int bottom);
void n3ds_gpu_hud_screen_restore(void);
int n3ds_platform_console_model(void); /* CFG_SystemModel, or -1 if unavailable. */
int n3ds_platform_stereo_supported(void);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
int n3ds_platform_is_new_2ds_xl(void);
#endif
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
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
void n3ds_gpu_capture_midpoint_view(unsigned int tick,int midpoint,
    const struct native_render_camera *culling,float presentation_time);
/* 1 captured (regular commits the pair), 0 midpoint lacked a top submission, -1 failure. */
int n3ds_gpu_capture_midpoint_image(unsigned int tick,int midpoint);
#endif
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
#ifdef HALO_N3DS_TEXTURE_PREFETCH
struct bitmap_data;
const struct bitmap_data *n3ds_engine_bsp_prefetch_bitmap(const struct shader *shader, short bitmap_index, int extra);
#endif
struct vertex_buffer;
struct native_bsp_index_run { unsigned int index_count; unsigned short cluster; };
struct native_bsp_material_visibility {
    const unsigned int *single_owner_clusters;
    const struct native_bsp_index_run *runs;
    unsigned int run_count;
};
void n3ds_engine_bsp_draw(const struct shader *shader,short bitmap_index,
    const unsigned short *indices,unsigned int count,const struct vertex_buffer *source,
    int lightmap_group,short lightmap_index,const struct vertex_buffer *lightmap_source,
    const struct native_bsp_material_visibility *visibility);
int n3ds_gpu_model_decal_draw(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count,int sky,int two_sided);
int n3ds_gpu_model_decal_tests(void);
int n3ds_gpu_indexed_draw(const void *vertices,const unsigned short *indices,
    unsigned int count,unsigned int stride,unsigned int attributes,unsigned int permutation);
/* Frame-owned immutable copy; remains valid through both eye views and is
 * retired only by the next frame's GPU wait. NULL outside an active frame. */
const unsigned short *n3ds_gpu_indices_upload(const unsigned short *indices,unsigned int count);
/* Reuse a complete slice in a live renderer arena, or stage external indices
 * once. Persistent callers must still honor the residency epoch below. */
const unsigned short *n3ds_gpu_indices_prepare(const unsigned short *indices,unsigned int count);
/* An immutable index upload survives until the residency epoch changes.
 * An epoch of zero disables residency. */
unsigned long long n3ds_gpu_index_residency_epoch(void);
const unsigned short *n3ds_gpu_indices_upload_resident(const unsigned short *indices,unsigned int count);
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
