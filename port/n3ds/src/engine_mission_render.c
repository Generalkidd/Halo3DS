/* Original director/observer camera through the established opaque BSP backend.
 * Original object/weapon submission is connected; HUD and full shading remain. */
#include "cseries.h"
#include "scenario/scenario.h"
#ifdef HALO_N3DS_TEXTURE_PREFETCH
#include "scenario/scenario_definitions.h"
#endif
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_transparent_geometry.h"
#include "shaders/shader_definitions.h"
#include "units.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_diagnostics.h"
#include "engine_input.h"
#include "engine_models.h"
#include "engine_hud.h"
#include "engine_transparent.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "render/render.h"
#include "render/render_objects.h"
#include "structures/structure_visibility.h"
#include "objects/object_lights_rendering.h"
#include "interface/first_person_weapons.h"
#include "interface/ui_widget.h"
#include "cache/texture_cache.h"
#include "engine_presentation.h"
#include "engine_algorithm_profile.h"
#include "engine_dependency_trace.h"
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
#include "engine_prepared_view.h"
#endif
#if defined(HALO_N3DS_PRESENT_INTERPOLATION) || defined(HALO_N3DS_GPU_PROBE)
#include "engine_controls.h"
#include "game/game.h"
#endif
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
#include "bink/bink_playback.h"
#include "game/player_control.h"
#include "main/console.h"
#include "main/main.h"
#include "rasterizer/rasterizer_debug_options.h"
#endif
void set_window_camera_values(struct render_window *window,const struct observer_result *observer);
void n3ds_log(const char *message);
void texture_cache_flush(void);
void render_sky(void);
extern real render_sky_globals[8];
void build_sprite_prepare_for_window(void);
void render_particles(void);
void particle_systems_render(void);
void render_contrails_normal(void);
void n3ds_hud_draw_screen(void);
void n3ds_menu_draw_screen(void);
void n3ds_cinematic_draw_screen(void);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
void n3ds_engine_frame_service_ticks(long long result[3]);
void n3ds_engine_texture_idle_ticks(long long result[6]);
#ifdef HALO_N3DS_RENDERER_TESTS
void n3ds_model_material_profile_read(long long result[12]);
#endif
static int native_visibility_verify;
void n3ds_visibility_verify(int enabled) {native_visibility_verify=enabled;}
static long long object_profile[6];
#ifdef HALO_N3DS_HUD_PROFILE
static long long hud_profile[4];
#endif
static long long effect_profile[4];
void n3ds_object_profile(unsigned int stage,long long ticks)
{
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active()) return;
#endif
    if(stage<6) object_profile[stage]+=ticks;
}
struct mission_batch {
    struct structure_material *material; const struct shader *shader;short lightmap_index;
    real_point3d center;real_vector3d extent;
    struct triangle_buffer water_triangles;
    unsigned long clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE/32];int clusters_valid;
    unsigned int single_owner_clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE/32];
    const struct native_bsp_index_run *index_runs;
    unsigned int index_run_count;
    unsigned int rejecting_plane;
};
static unsigned int batch_visible,batch_culled;
struct batch_frustum {
    const real_plane3d *planes;
    real_vector3d absolute_normals[4];
};
static void batch_frustum_prepare(struct batch_frustum *out,const struct render_frustum *frustum)
{
    out->planes=frustum->world_planes;
    for(int plane=0;plane<4;++plane)
        for(int axis=0;axis<3;++axis)
            out->absolute_normals[plane].n[axis]=fabsf(out->planes[plane].n.n[axis]);
}
/* A box is outside only when its nearest point is beyond one side plane.
 * The per-eye rasterizer frustum already includes the stereo projection shift.
 * Large boxes spanning the frustum are retained; no centroid-only rejection. */
static int batch_outside_plane(struct mission_batch *batch,const struct batch_frustum *frustum)
{
    /* The previous rejecting plane is only a search-order hint. Every view
     * still evaluates its own planes; a turn, portal change or stereo eye
     * cannot reuse an old rejection or omit a newly visible material. */
    for(unsigned int test=0;test<4;++test) {
        unsigned int plane=(batch->rejecting_plane+test)&3;
        const real_plane3d *p=frustum->planes+plane;
        const real_vector3d *normal=frustum->absolute_normals+plane;
        real distance=dot_product3d(&p->n,(const real_vector3d *)&batch->center)-p->d;
        real radius=normal->i*batch->extent.i+normal->j*batch->extent.j+normal->k*batch->extent.k;
        if(distance-radius>.01f) {batch->rejecting_plane=plane;return plane;}
    }
    return NONE;
}
/* Reuse the largest BSP's small pointer table for this process. Later A10
 * sections exceed the original bring-up limit of 256 materials. */
static struct mission_batch *batches;
static long batch_capacity;
static struct structure_bsp *loaded_bsp;
static unsigned int loaded_generation;
static long count;
#ifdef HALO_N3DS_TEXTURE_PREFETCH
static long prefetch_batch_cursor;
#endif
#include "engine_batch_visibility.inl"
/* These CPU descriptors belong to the loaded map. Submitted draws keep their
 * own GPU buffers; retaining descriptors here would pin freed map-sized holes. */
void n3ds_engine_mission_batches_release(void)
{
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    n3ds_presentation_reset();
#endif
    surface_clusters_release();
    free(batches);batches=NULL;batch_capacity=count=0;
    loaded_bsp=NULL;loaded_generation=0;
#ifdef HALO_N3DS_TEXTURE_PREFETCH
    prefetch_batch_cursor=0;
#endif
}
#ifdef HALO_N3DS_FRONTEND
void n3ds_frontend_trace(const char *);
#define FRONTEND_STAGE(s) n3ds_frontend_trace(s)
#else
#define FRONTEND_STAGE(s) ((void)0)
#endif
#define CHECK(e) do { if(!(e)) { n3ds_log("MISSION RENDER FAIL: " #e); return 1; } } while(0)
/* Frame checks occur within an open view. Preserve the existing failure path
 * while closing only its trace scopes; do not submit or retire failed work. */
#define FRAME_CHECK(e) do { if(!(e)) { n3ds_log("MISSION RENDER FAIL: " #e); \
    n3ds_trace_view_end();n3ds_trace_attempt_end(N3DS_TRACE_ATTEMPT_PREPARATION_REJECT);return 1; } } while(0)
/* Populate terrain depth after the held weapon, before world models. This
 * permits early rejection of scenery/characters behind terrain without an
 * extra depth pass. Transparent groups still use the common sorted queue. */
static long long mission_environment_ticks;
static void draw_mission_environment(const void *context)
{
    const struct rasterizer_window_begin_parameters *window=context;
    long long start=N3DS_MISSION_PROFILE?n3ds_engine_ticks():0;
#ifdef HALO_N3DS_GPU_PROBE
    n3ds_gpu_probe_terrain(1);
#endif
    rasterizer_environment_diffuse_textures_begin();
    struct batch_frustum bounds_view;
    batch_frustum_prepare(&bounds_view,&window->frustum);
    const unsigned int *candidates=batch_view_candidates();
    if(N3DS_MISSION_PROFILE && candidates) {
        batch_culled+=count;
        for(unsigned int word=0;word<batch_candidate_words;++word)
            batch_culled-=__builtin_popcount(candidates[word]);
    }
    for(long b=batch_next_candidate(candidates,0);b<count && !n3ds_presentation_midpoint_rejected();
        b=batch_next_candidate(candidates,b+1)) {
        struct structure_material *m=batches[b].material;
        if(!candidates && !batch_cluster_visible(batches+b)) {if(N3DS_MISSION_PROFILE) ++batch_culled;continue;}
        int outside=batch_outside_plane(batches+b,&bounds_view);
        if(outside!=NONE) {
            if(N3DS_MISSION_PROFILE) ++batch_culled;
            if(native_visibility_verify && render.frame_index%300==0) {
                const real_plane3d *p=window->frustum.world_planes+outside;
                unsigned int stride=m->vertices.type==1?32:56;
                for(long v=0;v<m->vertices.count;++v) {
                    real_point3d position;memcpy(&position,(const byte *)m->vertices.base_address+v*stride,sizeof(position));
                    assert(dot_product3d(&p->n,(const real_vector3d *)&position)-p->d>0);
                }
            }
            continue;
        }
        if(N3DS_MISSION_PROFILE) ++batch_visible;
        if(batches[b].shader->base.type==7) {
            struct transparent_geometry_group *group=rasterizer_transparent_geometry_new_group();
            if(!group) {n3ds_log("BSP WATER: transparent queue exhausted; surface deferred");continue;}
            long sorted=group->sorted_index;memset(group,0,sizeof(*group));group->sorted_index=sorted;
            group->object_index=group->source_object_index=NONE;
            group->shader=(struct shader *)batches[b].shader;group->shader_permutation_index=m->permutation_index;
            group->triangle_buffer=&batches[b].water_triangles;group->triangle_count=m->surface_count;
            group->vertex_buffer=&m->vertices;
            group->dynamic_triangle_buffer_index=group->dynamic_vertex_buffer_index=NONE;
            group->model_base_map_scale.i=group->model_base_map_scale.j=1;
            group->centroid=m->centroid;group->plane=m->plane;
            real_vector3d relative;vector_from_points3d(&window->camera.position,&group->centroid,&relative);
            group->z_sort=-dot_product3d(&window->camera.forward,&relative);
            group->previous_group_presorted_index=group->next_group_presorted_index=NONE;
            group->active_camouflage_transparent_source_object_index=NONE;
            continue;
        }
        const struct native_bsp_material_visibility visibility={
            .single_owner_clusters=batches[b].single_owner_clusters,
            .runs=batches[b].index_runs,.run_count=batches[b].index_run_count
        };
        n3ds_engine_bsp_draw(batches[b].shader,m->permutation_index,
            (const unsigned short *)((struct structure_surface *)loaded_bsp->surfaces.address+m->first_surface_index),
            m->surface_count*3,&m->vertices,loaded_bsp->lightmap_group.index,batches[b].lightmap_index,&m->lightmap_vertices,
            surface_cluster_owners?&visibility:NULL);
    }
    rasterizer_environment_diffuse_textures_end();
#ifdef HALO_N3DS_GPU_PROBE
    n3ds_gpu_probe_terrain(0);
#endif
    if(N3DS_MISSION_PROFILE) mission_environment_ticks=n3ds_engine_ticks()-start;
}
static int prepare(void)
{
    struct structure_bsp *bsp=global_structure_bsp_get();
    long i,j,triangles=0,excluded=0,water_batches=0,water_triangles=0;
    char message[192];
    /* A BSP transition changes the working set. Reclaim only between submitted
     * frames; texture_cache_flush waits for the GPU and clears bitmap handles. */
    /* Section unload now retains map-owned textures on New models and
     * reclaims original-model textures before the next BSP allocation. */
    count=0;
    long required=0;
    for(i=0;i<bsp->lightmaps.count;++i) {
        struct structure_lightmap *lightmap=TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps,i,struct structure_lightmap);
        CHECK(lightmap->materials.count>=0 && lightmap->materials.count<=0x7fffffff/(long)sizeof(*batches)-required);
        required+=lightmap->materials.count;
    }
    if(required>batch_capacity) {
        struct mission_batch *resized=realloc(batches,required*sizeof(*batches));
        CHECK(resized);batches=resized;batch_capacity=required;
    }
    for(i=0;i<bsp->lightmaps.count;++i) {
        struct structure_lightmap *lightmap=TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps,i,struct structure_lightmap);
        for(j=0;j<lightmap->materials.count;++j) {
            struct structure_material *m=TAG_BLOCK_GET_ELEMENT(&lightmap->materials,j,struct structure_material);
            if(!m->surface_count) continue;
            unsigned long group=n3ds_cache_tag(n3ds_engine_cache_view(0),m->shader.index,'senv')?'senv':'swat';
            if(group=='swat' && !n3ds_cache_tag(n3ds_engine_cache_view(0),m->shader.index,'swat')) { ++excluded; continue; }
            CHECK(count<batch_capacity && m->surface_count>0 && m->first_surface_index>=0);
            CHECK(m->surface_count<=bsp->surfaces.count && m->first_surface_index<=bsp->surfaces.count-m->surface_count);
            struct mission_batch *batch=batches+count++;
            batch->material=m;batch->shader=tag_get(group,m->shader.index);batch->lightmap_index=lightmap->bitmap_index;
            batch->rejecting_plane=0;
            memset(&batch->water_triangles,0,sizeof(batch->water_triangles));
            if(group=='swat') {
                batch->water_triangles.count=m->surface_count;
                batch->water_triangles.base_address=(struct structure_surface *)bsp->surfaces.address+m->first_surface_index;
                ++water_batches;water_triangles+=m->surface_count;
            }
            const struct vertex_buffer *vb=&m->vertices;
            CHECK(vb->count>0 && vb->count<=65535 && (vb->type==0 || vb->type==1));
            unsigned int stride=vb->type==1?32:56;
            const byte *vertices=vb->base_address;
            CHECK(vertices && n3ds_cache_resolve(n3ds_engine_cache_view(1),vertices,vb->count*stride)==vertices);
            real_rectangle3d bounds;
            for(unsigned int v=0;v<(unsigned int)vb->count;++v) {
                real_point3d position;memcpy(&position,vertices+v*stride,sizeof(position));
                for(int axis=0;axis<3;++axis) {
                    CHECK(isfinite(position.n[axis]));
                    if(!v) bounds.n[axis*2]=bounds.n[axis*2+1]=position.n[axis];
                    else {bounds.n[axis*2]=MIN(bounds.n[axis*2],position.n[axis]);bounds.n[axis*2+1]=MAX(bounds.n[axis*2+1],position.n[axis]);}
                }
            }
            for(int axis=0;axis<3;++axis) {
                batch->center.n[axis]=(bounds.n[axis*2]+bounds.n[axis*2+1])*.5f;
                batch->extent.n[axis]=(bounds.n[axis*2+1]-bounds.n[axis*2])*.5f+.001f*(1+fabsf(batch->center.n[axis]));
            }
            triangles+=m->surface_count;
        }
    }
    batch_clusters_prepare(bsp);
#ifdef HALO_N3DS_TEXTURE_PREFETCH
    prefetch_batch_cursor=0;
#endif
    /* Cinematic-only BSPs (including the Maw finale) legitimately have no
     * opaque world surfaces. Objects, skies and effects still render below. */
    loaded_bsp=bsp; loaded_generation=n3ds_engine_cache_generation();
    snprintf(message,sizeof(message),"MISSION RENDER: batches=%ld triangles=%ld unsupported=%ld; opaque BSP plus original object pass",count,triangles,excluded);
    n3ds_log(message);
    snprintf(message,sizeof(message),"BSP WATER PREPARED: materials=%ld triangles=%ld; original transparent queue",water_batches,water_triangles);n3ds_log(message);
    return 0;
}

#ifdef HALO_N3DS_TEXTURE_PREFETCH
static real distance_squared_to_batch(const struct mission_batch *batch, const real_point3d *position)
{
    real distance = 0.f;
    for (unsigned int axis = 0; axis < 3; ++axis) {
        real gap = fabsf(position->n[axis] - batch->center.n[axis]) - batch->extent.n[axis];
        if (gap > 0.f)
            distance += gap * gap;
    }
    return distance;
}

static int batch_texture_hints(const struct mission_batch *batch,
    int (*load)(const struct bitmap_data *), int remaining, long long deadline)
{
    int accepted = 0;
    short permutation = batch->material->permutation_index;
    const struct bitmap_data *base = n3ds_engine_bsp_prefetch_bitmap(batch->shader, permutation, 0);
    if (base)
        accepted = load(base);
    if (accepted < remaining && (!deadline || n3ds_engine_ticks() < deadline)) {
        const struct bitmap_data *lightmap =
            n3ds_engine_bitmap_metadata(loaded_bsp->lightmap_group.index, batch->lightmap_index);
        if (lightmap)
            accepted += load(lightmap);
    }
    if (accepted < remaining && (!deadline || n3ds_engine_ticks() < deadline)) {
        const struct bitmap_data *extra = n3ds_engine_bsp_prefetch_bitmap(batch->shader, permutation, 1);
        if (extra)
            accepted += load(extra);
    }
    return accepted;
}

static int mission_prewarm_position(const struct structure_bsp *bsp, real_point3d *position, int *cluster)
{
    const struct observer_result *camera = observer_get_camera(0);
    if (camera && camera->location.cluster_index >= 0 &&
        camera->location.cluster_index < bsp->clusters.count && valid_real_point3d(&camera->position)) {
        *position = camera->position;
        *cluster = camera->location.cluster_index;
        return 1;
    }

    /* A new map may not have run the observer yet. Use a campaign start in
     * the active BSP instead of treating the default observer origin as a view. */
    const struct scenario *scenario = global_scenario_try_and_get();
    if (!scenario || scenario->type != _scenario_type_solo || scenario->players.count <= 0)
        return 0;
    long candidates = MIN(scenario->players.count, 32);
    const struct player_starting_location *starts = n3ds_cache_resolve(n3ds_engine_cache_view(0),
        scenario->players.address, candidates * sizeof(*starts));
    if (!starts)
        return 0;
    short bsp_index = global_structure_bsp_index_get();
    for (long i = 0; i < candidates; ++i) {
        const struct player_starting_location *start = starts + i;
        if ((start->structure_bsp_reference_index != NONE && start->structure_bsp_reference_index != bsp_index) ||
            !valid_real_point3d(&start->position))
            continue;
        struct location location;
        scenario_location_from_point(&location, &start->position);
        if (location.cluster_index < 0 || location.cluster_index >= bsp->clusters.count)
            continue;
        *position = start->position;
        *cluster = location.cluster_index;
        return 1;
    }
    return 0;
}

void n3ds_engine_mission_texture_prewarm(void)
{
    enum { CANDIDATES = 6, MAX_UPLOADS = 3 };
    if (n3ds_input_platform_original_model() || global_structure_bsp_index_get() == NONE || n3ds_gpu_frame_active())
        return;
    struct structure_bsp *bsp = global_structure_bsp_get();
    real_point3d position;
    int cluster;
    if (!mission_prewarm_position(bsp, &position, &cluster))
        return;
    long long setup_start = n3ds_engine_ticks();
    if ((loaded_bsp != bsp || loaded_generation != n3ds_engine_cache_generation()) && prepare())
        return;

    long long start = n3ds_engine_ticks();
    long long deadline = start + n3ds_engine_tick_frequency() * 8 / 1000;
    const struct mission_batch *nearest[CANDIDATES] = {0};
    real scores[CANDIDATES];
    for (long i = 0; i < count; ++i) {
        if ((i & 15) == 0 && n3ds_engine_ticks() >= deadline)
            break;
        const struct mission_batch *batch = batches + i;
        if (batch->shader->base.type != 3)
            continue;
        real score = distance_squared_to_batch(batch, &position);
        if (cluster >= 0 && cluster < MAXIMUM_CLUSTERS_PER_STRUCTURE &&
            batch->clusters_valid && !(batch->clusters[cluster / 32] & (1u << (cluster % 32))))
            score += 24.f * 24.f;
        for (unsigned int slot = 0; slot < CANDIDATES; ++slot) {
            if (nearest[slot] && scores[slot] <= score)
                continue;
            for (unsigned int move = CANDIDATES - 1; move > slot; --move) {
                nearest[move] = nearest[move - 1];
                if (nearest[move])
                    scores[move] = scores[move - 1];
            }
            nearest[slot] = batch;
            scores[slot] = score;
            break;
        }
    }

    /* Preload a small entry working set while the loading screen is still up.
     * The time limit is checked between loads, so a single SD read can exceed it. */
    int uploads = 0;
    for (unsigned int i = 0; i < CANDIDATES && nearest[i] && uploads < MAX_UPLOADS &&
        n3ds_engine_ticks() < deadline; ++i)
        uploads += batch_texture_hints(nearest[i], n3ds_engine_texture_prewarm, MAX_UPLOADS - uploads, deadline);
    char message[128];
    double ms_per_tick = 1000.0 / n3ds_engine_tick_frequency();
    snprintf(message, sizeof(message), "TEXTURE PREWARM: uploads=%d selection_and_load_ms=%.2f bsp_prepare_ms=%.2f",
        uploads, (n3ds_engine_ticks() - start) * ms_per_tick, (start - setup_start) * ms_per_tick);
    n3ds_log(message);
}

static void prefetch_upcoming_textures(const struct rasterizer_window_begin_parameters *window)
{
    if (n3ds_input_platform_original_model() || main_menu_is_active() ||
        (render.frame_index & 3) || !count)
        return;

    /* Scan a few nearby surfaces outside the current view. A rotating cursor
     * keeps large BSPs from turning lookahead into a second visibility pass. */
    int hints = 0;
    struct batch_frustum bounds_view;
    batch_frustum_prepare(&bounds_view,&window->frustum);
    for (long i = 0; i < MIN(count, 32) && hints < 2; ++i) {
        struct mission_batch *batch = batches + prefetch_batch_cursor;
        if (++prefetch_batch_cursor == count)
            prefetch_batch_cursor = 0;
        if (batch->shader->base.type != 3 ||
            distance_squared_to_batch(batch, &window->camera.position) > 16.f * 16.f ||
            (batch_cluster_visible(batch) && batch_outside_plane(batch, &bounds_view) == NONE))
            continue;
        hints += batch_texture_hints(batch, n3ds_engine_texture_prefetch, 2 - hints, 0);
    }
}
#endif

#ifdef HALO_N3DS_PERFORMANCE_PROFILE
#include "engine_texture_profile_report.inl"
#endif
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
#include "engine_presentation.inl"
#endif

static void mission_build_frusta(const struct render_camera *camera, struct render_frustum *frustum,
    struct rasterizer_window_begin_parameters *window, int stereo, float offset)
{
    if(stereo) {
        /* Original visibility/projection must agree with the off-axis GPU view. */
        real_rectangle2d bounds;
        bounds.y0=-1;bounds.y1=1;
        float shift=-offset/(1.5f*tanf(camera->vertical_field_of_view*.5f)*(400.f/240.f));
        bounds.x0=-1+shift;bounds.x1=1+shift;
        render_camera_build_frustum(camera,&bounds,frustum,TRUE);
        /* The same off-axis camera needs the same projection and planes.
         * Separate debug/culling cameras retain independent frusta. */
        if(!memcmp(camera,&window->camera,sizeof(*camera))) window->frustum=*frustum;
        else {
            shift=-offset/(1.5f*tanf(window->camera.vertical_field_of_view*.5f)*(400.f/240.f));
            bounds.x0=-1+shift;bounds.x1=1+shift;
            render_camera_build_frustum(&window->camera,&bounds,&window->frustum,TRUE);
        }
    } else {
        render_camera_build_frustum(camera,NULL,frustum,TRUE);
        /* Debug camera modes can separate the culling and rasterizer views. */
        if(!memcmp(camera,&window->camera,sizeof(*camera))) window->frustum=*frustum;
        else render_camera_build_frustum(&window->camera,NULL,&window->frustum,TRUE);
    }
}

#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
struct mission_prepared_view {
    struct render_camera render_camera, rasterizer_camera;
    struct render_frustum render_frustum, rasterizer_frustum;
    struct n3ds_prepared_visibility visibility;
    struct n3ds_prepared_objects objects;
    unsigned int generation;
    int bsp;
    long time, frame_index, previous_leaf_index;
};

/* Single main-owner record. It is read only within the current regular frame
 * and is never read by a worker or passed to the GPU. */
static struct mission_prepared_view prepared_regular_view;
#endif

static int mission_render_frame(long unit_index, long time, const struct render_window *original_window,
    float slider, int midpoint, int weapon_prepared, int submitted_midpoint, long long begin);

#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
static int midpoint_capture_complete;
#endif

int n3ds_engine_mission_render(long unit_index,long time,long long tick_start)
{
    n3ds_algorithm_profile_context(time,n3ds_engine_cache_generation(),global_structure_bsp_index_get(),1);
    long long begin=n3ds_engine_ticks();
    if(time==1) {
        /* Cache stress tests belong before map loading in bounded developer
         * runs, never in a live map's first frame with most memory occupied. */
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
        if(n3ds_platform_is_new_2ds_xl())
            n3ds_log("NATIVE LIVE PACING: 30 Hz game loop, optional New 2DS XL midpoints");
        else
#endif
            n3ds_log("NATIVE LIVE PACING: original30Hz loop cap; GPU queue wait retained, extra VBlank wait disabled");
    }
    struct render_window original_window={0};
    struct observer_result const *observer=observer_get_camera(0);
    if(loaded_bsp!=global_structure_bsp_get() || loaded_generation!=n3ds_engine_cache_generation()) CHECK(!prepare());
    CHECK(observer);
    original_window.local_player_index=0;
    original_window.rasterizer_camera.viewport_bounds.x1=400;
    original_window.rasterizer_camera.viewport_bounds.y1=240;
    original_window.rasterizer_camera.window_bounds=original_window.rasterizer_camera.viewport_bounds;
    /* Script-authored close-up clipping was previously left at the gameplay
     * default. Set it before copying both culling and rasterizer cameras. */
    extern real rasterizer_get_near_clip_distance(void);
    rasterizer_globals.near_clip_distance=rasterizer_get_near_clip_distance();
    /* PICA clips the miniature hologram in the authored cinematic close-ups
     * at the normal 1/16-world-unit near plane. Keep a close cinematic plane
     * for both visibility and rasterization, and restore normal gameplay depth
     * precision as soon as the cinematic ends. Smaller scripted values win. */
    extern boolean cinematic_in_progress(void);
    if(cinematic_in_progress()) rasterizer_globals.near_clip_distance=MIN(rasterizer_globals.near_clip_distance,.005f);
#ifdef HALO_N3DS_RENDERER_TESTS
    static real reported_near=-1;
    if(reported_near!=rasterizer_globals.near_clip_distance) {
        char text[120];reported_near=rasterizer_globals.near_clip_distance;
        snprintf(text,sizeof(text),"CINEMATIC CLIP: time=%ld near=%.7f",time,reported_near);n3ds_log(text);
    }
#endif
    set_window_camera_values(&original_window,observer);
    #ifdef HALO_N3DS_BSP_PROBE
    if(HALO_N3DS_BSP_PROBE==4 && global_structure_bsp_index_get()==4) {
        /* Probe-only camera just beyond the recorded BSP 3 -> 4 crossing.
         * The player is still in the starting room; its camera would see no
         * geometry here and invalidate the final pixel check. */
        original_window.render_camera.position.x=-78.f;
        original_window.render_camera.position.y=27.f;
        original_window.render_camera.position.z=1.02f;
        original_window.render_camera.forward.i=-1.f;
        original_window.render_camera.forward.j=original_window.render_camera.forward.k=0;
        original_window.render_camera.up.i=original_window.render_camera.up.j=0;
        original_window.render_camera.up.k=1.f;
        original_window.rasterizer_camera=original_window.render_camera;
    }
    #endif
    const struct render_camera *camera=&original_window.rasterizer_camera;
    CHECK(valid_real_point3d(&camera->position) && valid_real_normal3d(&camera->forward));
    CHECK(valid_real_normal3d(&camera->up) && camera->vertical_field_of_view>0.f);
    float slider=n3ds_platform_3d_slider();
    if(!isfinite(slider) || slider<0) slider=0;
    if(slider>1) slider=1;
    n3ds_gpu_stereo_level(slider);
    render.local_player_index=0; render.window_index=0;
    render.camera=original_window.render_camera;
    n3ds_gpu_world_budget(!main_menu_is_active());
    extern void interface_prepare_screen_effect(void);
    if(main_menu_is_active()) n3ds_gpu_screen_effect(NULL);
    else interface_prepare_screen_effect();

#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_platform_is_new_2ds_xl()) {
        int weapon_prepared=0, submitted=0, rejection=0;
        unsigned int textures=n3ds_engine_texture_presentation_revision();
        int eligible=unit_index!=NONE && game_in_progress() && !main_menu_is_active() &&
            !n3ds_main_is_solo_multiplayer() && !game_time_get_paused() && game_time_get_speed()==1.f &&
            !cinematic_in_progress() && !bink_playback_in_progress() && !ui_widgets_active() &&
            !n3ds_controls_active() && !console_is_active() && !debug_render_freeze &&
            !native_visibility_verify && slider==0.f && n3ds_gpu_midpoint_compatible();
        if(!eligible) {
            ++presentation.context_skips;
            n3ds_presentation_reset();
        } else if(!presentation_budget_fits(tick_start)) {
            ++presentation.budget_skips;
            presentation_forget_history();
        } else {
            struct presentation_snapshot current={
                .window=original_window,
                .tick=(unsigned int)time,
                .tick_start=tick_start,
                .generation=n3ds_engine_cache_generation(),
                .unit_index=unit_index,
                .zoom=player_control_get_zoom_level(0),
                .perspective=director_get_perspective(0)
            };
            struct render_window midpoint_window;
            int adjacent=presentation_window_midpoint(&current,&midpoint_window);
            if(!adjacent)
                presentation_forget_history();
            render.time_delta_since_tick_sec=1.f/30.f;
            first_person_weapon_render_update();
            weapon_prepared=1;
            n3ds_presentation_weapon_capture();
            n3ds_presentation_poses_capture();
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
            n3ds_presentation_particles_capture();
#endif
            presentation.previous=current;
            presentation.has_previous=1;
            if(!adjacent || !n3ds_presentation_weapon_midpoint(&midpoint_window.render_camera)) {
                ++presentation.context_skips;
            } else if(!presentation_budget_fits(tick_start)) {
                ++presentation.budget_skips;
            } else {
                long long midpoint_begin=n3ds_engine_ticks();
                n3ds_algorithm_profile_context(time,n3ds_engine_cache_generation(),global_structure_bsp_index_get(),2);
                n3ds_trace_attempt_begin(2,time,n3ds_engine_cache_generation(),global_structure_bsp_index_get());
                n3ds_gpu_frame_sync(0);
                if(n3ds_gpu_midpoint_try_begin()) {
                    presentation.midpoint=1;
                    rejection=mission_render_frame(unit_index,time,&midpoint_window,0.f,1,1,0,midpoint_begin);
                    presentation.midpoint=0;
                    n3ds_presentation_reject_reason=0;
                    long long cost=n3ds_engine_ticks()-midpoint_begin;
                    submitted=!rejection;
                    presentation.submitted+=submitted;
                    presentation.texture_skips+=rejection==NATIVE_MIDPOINT_TEXTURE;
                    presentation.deadline_skips+=rejection==NATIVE_MIDPOINT_DEADLINE;
                    if(rejection) presentation.rejected_ticks+=cost;
                    else presentation.midpoint_prepare_ticks=MAX(cost,
                        presentation.midpoint_prepare_ticks-presentation.midpoint_prepare_ticks/8);
                } else {
                    n3ds_trace_attempt_end(N3DS_TRACE_ATTEMPT_GPU_BUSY);
                    ++presentation.busy_skips;
                }
            }
        }
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
        int capture=0;
        if(!midpoint_capture_complete && time>=HALO_N3DS_MIDPOINT_CAPTURE_TICK) {
            if(time-HALO_N3DS_MIDPOINT_CAPTURE_TICK>=120) {
                midpoint_capture_complete=1;
                n3ds_log("MIDPOINT CAPTURE: no eligible pair in 120-tick window");
            } else if(submitted) {
                capture=n3ds_gpu_capture_midpoint_image(time,1);
                if(capture<0) {n3ds_log("MIDPOINT CAPTURE FAIL: midpoint target");return 1;}
            }
        }
#endif
        long long key_begin=n3ds_engine_ticks();
        int result=mission_render_frame(unit_index,time,&original_window,slider,0,weapon_prepared,submitted,key_begin);
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
        if(capture==1 && !result) {
            if(n3ds_gpu_capture_midpoint_image(time,0)!=1) {n3ds_log("MIDPOINT CAPTURE FAIL: regular target");return 1;}
            midpoint_capture_complete=1;
            n3ds_log("MIDPOINT CAPTURE: paired color, depth and camera committed; timing invalid");
        }
#endif
        if(eligible && !result)
            presentation_observe(tick_start,key_begin,textures,submitted,rejection);
        presentation_report(time, eligible);
        return result;
    }
#endif
    (void)tick_start;
    return mission_render_frame(unit_index,time,&original_window,slider,0,0,0,begin);
}

#ifdef HALO_N3DS_GPU_PROBE
static void mission_gpu_probe(const struct render_window *window, long time, int eligible)
{
    unsigned int shoulders = NATIVE_KEY_L | NATIVE_KEY_R;
    unsigned int held = n3ds_input_last_held();
    int chord = (held & shoulders) == shoulders;
    /* Eligibility excludes the main menu, so the shared shortcut predicate
     * admits only the same pause-menu roots used by the original probe. */
    if (!eligible || (held & ~shoulders) || !n3ds_ui_controls_shortcut_active() || n3ds_controls_active() ||
        window->render_camera.mirrored || window->rasterizer_camera.mirrored) {
        n3ds_gpu_probe_view(NULL, chord);
        return;
    }
    struct native_gpu_probe_view view = {
        .generation = n3ds_engine_cache_generation(),
        .tick = time,
        .bsp = global_structure_bsp_index_get()
    };
    const struct render_camera *camera[] = {&window->render_camera, &window->rasterizer_camera};
    for (unsigned int i = 0; i < 2; ++i) {
        memcpy(view.camera[i], camera[i]->position.n, 3 * sizeof(float));
        memcpy(view.camera[i] + 3, camera[i]->forward.n, 3 * sizeof(float));
        memcpy(view.camera[i] + 6, camera[i]->up.n, 3 * sizeof(float));
        view.camera[i][9] = camera[i]->vertical_field_of_view;
        view.camera[i][10] = camera[i]->z_near;
        view.camera[i][11] = camera[i]->z_far;
        memcpy(view.bounds[i], camera[i]->viewport_bounds.v, 4 * sizeof(short));
        memcpy(view.bounds[i] + 4, camera[i]->window_bounds.v, 4 * sizeof(short));
    }
    n3ds_gpu_probe_view(&view, chord);
}
#endif

static int mission_render_frame(long unit_index, long time, const struct render_window *original_window,
    float slider, int midpoint, int weapon_prepared, int submitted_midpoint, long long begin)
{
    n3ds_algorithm_profile_context(time,n3ds_engine_cache_generation(),global_structure_bsp_index_get(),midpoint?2:1);
    /* Midpoints begin their attempt before the nonblocking GPU reservation. */
    if(!midpoint) n3ds_trace_attempt_begin(1,time,n3ds_engine_cache_generation(),global_structure_bsp_index_get());
    extern boolean cinematic_in_progress(void);
    long long stamp[7];static long long profile[6],service_profile[9],wait_profile;static unsigned int profile_frames;
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
    static long long visibility_ticks;
#endif
    stamp[0]=begin;
    struct rasterizer_frame_begin_parameters frame={0};
    struct rasterizer_window_begin_parameters window={0};
    struct unit_datum *unit=unit_index==NONE?NULL:unit_get(unit_index);
    frame.game_time_sec=(time-(midpoint?.5f:0.f))/30.f;
    frame.dt=midpoint?0.f:1.f/30.f;
    if(!midpoint) ++render.frame_index;
    render.time_delta_since_tick_sec=frame.dt;
    int stereo=slider>0;
    static float previous_slider=-1;
    if(!midpoint && slider!=previous_slider) {
        char message[96];snprintf(message,sizeof(message),"STEREO: slider=%.3f views=%d frame=%ld",slider,stereo?2:1,render.frame_index);
        n3ds_log(message);previous_slider=slider;
    }
    long long view_cost[3]={0,0,0};
    struct render_camera center_render=original_window->render_camera;
    struct render_camera center_raster=original_window->rasterizer_camera;
    n3ds_gpu_flashlight_set(unit && !cinematic_in_progress()?unit->unit.integrated_light_power:0,
        center_render.position.n,center_render.forward.n);
    render.local_player_index=0; render.window_index=0;
    n3ds_gpu_frame_sync(0);
    if(!midpoint) FRONTEND_STAGE("frame-begin");
    render.camera=center_render;
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
    struct mission_prepared_view *prepared = NULL;
    boolean early_view = FALSE;
    boolean verification_ok = TRUE;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    long long preparation_ticks = 0;
#endif
    if(submitted_midpoint && !stereo && weapon_prepared && n3ds_prepare_visibility_allowed()) {
        struct mission_prepared_view *record = &prepared_regular_view;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        long long preparation_start = n3ds_engine_ticks();
#endif
        n3ds_trace_view_begin(0);
        early_view = TRUE;
        record->generation = n3ds_engine_cache_generation();
        record->bsp = global_structure_bsp_index_get();
        record->time = time;
        record->frame_index = render.frame_index;
        record->previous_leaf_index = render.leaf_index;
        record->render_camera = center_render;
        record->rasterizer_camera = window.camera = center_raster;

        n3ds_trace_event(N3DS_TRACE_FRUSTUM_BEGIN,0,0);
        mission_build_frusta(&record->render_camera,&record->render_frustum,&window,0,0.f);
        record->rasterizer_frustum = window.frustum;
        n3ds_trace_event(N3DS_TRACE_FRUSTUM_END,0,0);
        n3ds_trace_event(N3DS_TRACE_VISIBILITY_BEGIN,0,0);
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
        long long visibility_start = n3ds_engine_ticks();
#endif
        n3ds_prepare_visibility(&record->render_camera,&record->render_frustum,&record->visibility);
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
        visibility_ticks += n3ds_engine_ticks() - visibility_start;
#endif
        n3ds_trace_event(N3DS_TRACE_VISIBILITY_END,(unsigned int)record->visibility.cluster_count,0);
        if(n3ds_prepare_render_objects(&record->visibility,&record->objects))
            prepared = record;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        preparation_ticks = n3ds_engine_ticks() - preparation_start;
#endif
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_ATTEMPT, 1);
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_TICKS, preparation_ticks);
        n3ds_algorithm_profile_max(N3DS_ALG_CPU_PREPARED_VIEW_PEAK, sizeof(*record));
    }
#else
    (void)submitted_midpoint;
#endif
#ifdef HALO_N3DS_GPU_PROBE
    mission_gpu_probe(original_window, time, !midpoint && !stereo && game_in_progress() &&
        !main_menu_is_active() && game_time_get_paused() && !cinematic_in_progress());
#endif
    rasterizer_frame_begin(&frame);
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
    if(early_view) {
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_WAIT, n3ds_gpu_frame_wait_ticks());
        /* Frame acquisition can handle an applet boundary. Only this frame's
         * view of the same BSP and simulation tick may consume the record. */
        if(!prepared || !presentation.has_previous || prepared->generation != n3ds_engine_cache_generation() ||
            prepared->bsp != global_structure_bsp_index_get() || prepared->time != game_time_get() ||
            prepared->frame_index != render.frame_index || prepared->previous_leaf_index != render.leaf_index ||
            !n3ds_prepare_visibility_allowed()) {
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_DISCARD, 1);
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_DISCARD_TICKS, preparation_ticks);
            prepared = NULL;
        }
    }
#endif
    n3ds_gpu_frame_sync(1);
    rasterizer_windows_begin();
    if(N3DS_MISSION_PROFILE && !midpoint) {
        long long services[9];n3ds_engine_frame_service_ticks(services);n3ds_engine_texture_idle_ticks(services+3);
        for(int i=0;i<9;++i) service_profile[i]+=services[i];
        wait_profile+=n3ds_gpu_frame_wait_ticks();
    }
    /* Right view first; left last leaves the ordinary HUD camera authoritative.
     * Simulation, frame service, and the original HUD execute only once. */
    for(int eye=stereo?1:0;eye>=0;--eye) {
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
        if(!early_view)
#endif
            n3ds_trace_view_begin((unsigned int)eye);
        float offset=stereo?(eye?1.f:-1.f)*.0105f*slider:0.f;
        render.camera=center_render;window.camera=center_raster;
        if(stereo) {
            real_vector3d right;
            cross_product3d(&center_render.forward,&center_render.up,&right);normalize3d(&right);
            for(int axis=0;axis<3;++axis) render.camera.position.n[axis]+=right.n[axis]*offset;
            cross_product3d(&center_raster.forward,&center_raster.up,&right);normalize3d(&right);
            for(int axis=0;axis<3;++axis) window.camera.position.n[axis]+=right.n[axis]*offset;
        }
        ++render.scene_index;
        n3ds_gpu_stereo_eye(eye,offset);
#if HALO_N3DS_ALGO_PREPARED_VIEW == N3DS_ALGORITHM_CANDIDATE
        if(prepared) {
            render.camera = prepared->render_camera;
            render.frustum = prepared->render_frustum;
            window.camera = prepared->rasterizer_camera;
            window.frustum = prepared->rasterizer_frustum;
            n3ds_publish_visibility(&prepared->visibility);
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_VIEW_USED, 1);
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_PREPARED_COPY,
                prepared->visibility.cluster_count * sizeof(prepared->visibility.clusters[0]) +
                BIT_VECTOR_SIZE_IN_BYTES(global_structure_bsp_get()->clusters.count));
        } else
#endif
        {
            n3ds_trace_event(N3DS_TRACE_FRUSTUM_BEGIN,0,0);
            mission_build_frusta(&render.camera,&render.frustum,&window,stereo,offset);
            n3ds_trace_event(N3DS_TRACE_FRUSTUM_END,0,0);
            n3ds_trace_event(N3DS_TRACE_VISIBILITY_BEGIN,0,0);
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
            long long visibility_start = n3ds_engine_ticks();
#endif
            structure_visibility_find_camera(&render.camera);
            n3ds_structure_visibility_compute();
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
            if(!midpoint) visibility_ticks += n3ds_engine_ticks() - visibility_start;
#endif
            n3ds_trace_event(N3DS_TRACE_VISIBILITY_END,(unsigned int)render.rendered_cluster_count,0);
        }
#if HALO_N3DS_ALGO_PREPARED_VIEW == N3DS_ALGORITHM_VERIFY
        if(prepared && (!n3ds_frusta_match(&prepared->render_frustum,&render.frustum) ||
            !n3ds_frusta_match(&prepared->rasterizer_frustum,&window.frustum) ||
            !n3ds_prepared_visibility_matches(&prepared->visibility))) {
            n3ds_log("PREPARED VIEW VERIFY FAIL: frusta or ordered cluster results differ");
            verification_ok = FALSE;
            prepared = NULL;
        }
#endif
#ifdef HALO_N3DS_ARCHIVE_VERIFY
        if(!midpoint && render.frame_index%30==0) FRAME_CHECK(batch_visibility_test());
#endif
        if(!midpoint && native_visibility_verify && render.frame_index%300==0) {
            FRAME_CHECK(n3ds_structure_visibility_test());
            n3ds_log("PASS: native cluster visibility matches full Xbox traversal; unused surface list omitted");
        }
        n3ds_gpu_world_view_begin();
        rasterizer_window_begin(&window);
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
        if(!eye && !midpoint_capture_complete && time>=HALO_N3DS_MIDPOINT_CAPTURE_TICK &&
           time-HALO_N3DS_MIDPOINT_CAPTURE_TICK<120 && (midpoint || submitted_midpoint)) {
            struct native_render_camera culling={
                .vertical_fov=render.camera.vertical_field_of_view,
                .near_clip=render.camera.z_near,
                .far_clip=render.camera.z_far,
            };
            memcpy(culling.position,render.camera.position.n,sizeof(culling.position));
            memcpy(culling.forward,render.camera.forward.n,sizeof(culling.forward));
            memcpy(culling.up,render.camera.up.n,sizeof(culling.up));
            n3ds_gpu_capture_midpoint_view(time,midpoint,&culling,frame.game_time_sec);
        }
#endif
        if(N3DS_MISSION_PROFILE) stamp[1]=n3ds_engine_ticks();
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    build_sprite_prepare_for_window();
    if(!midpoint) FRONTEND_STAGE("objects");
    long long object_stamp=N3DS_MISSION_PROFILE?n3ds_engine_ticks():0;
    /* Render-side animation advances once, including sky overlay phases and
     * glow movement. The second eye reuses the first eye's updated state. */
    render.time_delta_since_tick_sec=eye==(stereo?1:0)?frame.dt:0.f;
    render_sky();
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    if(N3DS_MISSION_PROFILE) {n3ds_object_profile(0,n3ds_engine_ticks()-object_stamp);object_stamp=n3ds_engine_ticks();}
    if(!midpoint && !weapon_prepared && eye==(stereo?1:0)) {
        struct render_camera eye_camera=render.camera;
        render.camera=center_render;first_person_weapon_render_update();render.camera=eye_camera;
    }
    if(N3DS_MISSION_PROFILE) {n3ds_object_profile(1,n3ds_engine_ticks()-object_stamp);object_stamp=n3ds_engine_ticks();}
    extern void rasterizer_lights_begin_for_new_frame(void);
    rasterizer_lights_begin_for_new_frame();
    lights_preprocess_scene();
    if(N3DS_MISSION_PROFILE) n3ds_object_profile(2,n3ds_engine_ticks()-object_stamp);
    extern void n3ds_render_objects_with_environment(void (*)(const void *),const void *);
    if(N3DS_MISSION_PROFILE) mission_environment_ticks=0;
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
    if(prepared) {
        if(!n3ds_render_prepared_objects_with_environment(draw_mission_environment,&window,&prepared->objects))
            verification_ok = FALSE;
    } else
#endif
    n3ds_render_objects_with_environment(draw_mission_environment,&window);
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    long long effect_stamp=0,effect_next;
    if(N3DS_MISSION_PROFILE) {
        effect_stamp=stamp[3]=n3ds_engine_ticks();stamp[2]=stamp[3]-mission_environment_ticks;
    }
    render_particles();
    if(N3DS_MISSION_PROFILE && !midpoint) {
        effect_next=n3ds_engine_ticks();effect_profile[0]+=effect_next-effect_stamp;effect_stamp=effect_next;
    }
    particle_systems_render();
    if(N3DS_MISSION_PROFILE && !midpoint) {
        effect_next=n3ds_engine_ticks();effect_profile[1]+=effect_next-effect_stamp;effect_stamp=effect_next;
    }
    render_contrails_normal();
    if(N3DS_MISSION_PROFILE && !midpoint) {
        effect_next=n3ds_engine_ticks();effect_profile[2]+=effect_next-effect_stamp;effect_stamp=effect_next;
    }
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    n3ds_transparent_draw_queued();
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    extern void rasterizer_lens_flares_draw(void);
    rasterizer_lens_flares_draw();
    if(n3ds_presentation_check_midpoint()) goto finish_view;
    if(N3DS_MISSION_PROFILE && !midpoint) {
        stamp[4]=n3ds_engine_ticks();effect_profile[3]+=stamp[4]-effect_stamp;
        view_cost[0]+=stamp[2]-stamp[1];view_cost[1]+=stamp[3]-stamp[2];view_cost[2]+=stamp[4]-stamp[3];
    }
    n3ds_gpu_world_resolve();
    if(eye) {rasterizer_window_end();n3ds_trace_view_end();continue;}
    n3ds_gpu_stereo_overlay(stereo);
#ifdef HALO_N3DS_HUD_PROFILE
    n3ds_gpu_hud_profile_reset();
#endif
    if(!midpoint) FRONTEND_STAGE("screen-widgets");
    if(!main_menu_is_active()) n3ds_hud_draw_screen();
    if(!n3ds_presentation_check_midpoint()) n3ds_cinematic_draw_screen();
    if(!midpoint) n3ds_menu_draw_screen();
    n3ds_gpu_stereo_overlay(0);
    if(N3DS_MISSION_PROFILE) stamp[5]=n3ds_engine_ticks();
    if(!midpoint) {
#ifdef HALO_N3DS_HUD_PROFILE
        long long hud[4];n3ds_gpu_hud_profile_read(hud);
        for(int i=0;i<4;++i) hud_profile[i]+=hud[i];
#endif
        FRONTEND_STAGE("present");
    }
finish_view:
    rasterizer_window_end(); rasterizer_windows_end(); rasterizer_frame_end(); rasterizer_present(NULL,NULL);
    n3ds_trace_view_end();
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(midpoint) {
        /* Trace reason values 1/2 deliberately match texture/deadline rejects. */
        n3ds_trace_attempt_end((unsigned int)n3ds_presentation_reject_reason);
        return n3ds_presentation_reject_reason;
    }
#endif
#ifdef HALO_N3DS_TEXTURE_PREFETCH
    if (!cinematic_in_progress())
        prefetch_upcoming_textures(&window);
#endif
    if(N3DS_MISSION_PROFILE) stamp[6]=n3ds_engine_ticks();
    } /* eye views */
    render.time_delta_since_tick_sec=frame.dt;
#if HALO_N3DS_ALGO_PREPARED_VIEW != N3DS_ALGORITHM_REFERENCE
    if(!verification_ok) {
        n3ds_trace_attempt_end(N3DS_TRACE_ATTEMPT_PREPARATION_REJECT);
        return 1;
    }
#endif
    n3ds_trace_attempt_end(N3DS_TRACE_ATTEMPT_COMPLETE);
    if(!N3DS_MISSION_PROFILE) return 0;
    profile[1]+=view_cost[0];profile[2]+=view_cost[1];profile[3]+=view_cost[2];
    profile[4]+=stamp[5]-stamp[4];profile[5]+=stamp[6]-stamp[5];
    profile[0]+=stamp[4]-stamp[0]-view_cost[0]-view_cost[1]-view_cost[2];
    ++profile_frames;
    const int report_frame = profile_frames >= 30;
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
    texture_profile_observe(time, report_frame, profile_frames);
#endif
    /* Game time can be frozen at a multiple of 30 while menus keep drawing. */
    if(report_frame) {
        extern void n3ds_collision_cache_report(void);n3ds_collision_cache_report();
        extern void n3ds_model_offload_report(void);n3ds_model_offload_report();
        char message[256];double scale=1000.0/((double)n3ds_engine_tick_frequency()*profile_frames);
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
        snprintf(message, sizeof(message), "VISIBILITY PROFILE: time=%ld frames=%u camera_and_traversal_ms=%.3f",
            time, profile_frames, visibility_ticks * scale);
        n3ds_log(message);
        visibility_ticks = 0;
#endif
        snprintf(message,sizeof(message),"BSP VISIBILITY: time=%ld submitted=%u culled=%u frames=%u",time,batch_visible,batch_culled,profile_frames);n3ds_log(message);
        batch_visible=batch_culled=0;
#ifdef HALO_N3DS_RENDERER_TESTS
        unsigned int marker_hits,marker_misses;extern void n3ds_marker_cache_counters(unsigned int *,unsigned int *);
        n3ds_marker_cache_counters(&marker_hits,&marker_misses);
        snprintf(message,sizeof(message),"MARKER REUSE: hits=%u misses=%u",marker_hits,marker_misses);n3ds_log(message);
#ifdef HALO_N3DS_CROWD_PROFILE
        extern unsigned int n3ds_transform_calls[2];
        snprintf(message,sizeof(message),"ARM TRANSFORM CALLS: point=%u vector=%u frames=%u",n3ds_transform_calls[0],n3ds_transform_calls[1],profile_frames);n3ds_log(message);
        n3ds_transform_calls[0]=n3ds_transform_calls[1]=0;
#endif
        long long material_stages[2][6];n3ds_model_material_profile_read(&material_stages[0][0]);
        for(int k=0;k<2;++k) {
            long long *v=material_stages[k];double us=v[1]?1000000.0/((double)n3ds_engine_tick_frequency()*v[1]):0;
            snprintf(message,sizeof(message),"MODEL MATERIAL PROFILE: time=%ld reflective=%d draws=%lld samples=%lld prepare_us=%.3f rows_us=%.3f bind_us=%.3f submit_us=%.3f",time,k,v[0],v[1],v[2]*us,v[3]*us,v[4]*us,v[5]*us);n3ds_log(message);
        }
#endif
#ifdef HALO_N3DS_MODEL_STAGE_PROFILE
        long long model_stages[3];n3ds_model_profile_read(model_stages);
        snprintf(message,sizeof(message),"MODEL PROFILE: time=%ld decode_ms=%.3f skin_ms=%.3f indices_ms=%.3f",time,model_stages[0]*scale,model_stages[1]*scale,model_stages[2]*scale);n3ds_log(message);
#endif
        snprintf(message,sizeof(message),"RENDER PROFILE: time=%ld frames=%u prepare_ms=%.3f objects_ms=%.3f bsp_ms=%.3f effects_ms=%.3f hud_ms=%.3f present_ms=%.3f",time,profile_frames,profile[0]*scale,profile[1]*scale,profile[2]*scale,profile[3]*scale,profile[4]*scale,profile[5]*scale);
        n3ds_log(message);
        snprintf(message,sizeof(message),"OBJECT PROFILE: time=%ld sky_ms=%.3f weapon_update_ms=%.3f lights_ms=%.3f find_ms=%.3f weapon_draw_ms=%.3f world_draw_ms=%.3f",time,object_profile[0]*scale,object_profile[1]*scale,object_profile[2]*scale,object_profile[3]*scale,object_profile[4]*scale,object_profile[5]*scale);
        n3ds_log(message);memset(object_profile,0,sizeof(object_profile));
#ifdef HALO_N3DS_HUD_PROFILE
        snprintf(message,sizeof(message),"HUD PROFILE: time=%ld meter_prepare_ms=%.3f sensor_prepare_ms=%.3f text_state_ms=%.3f text_draw_ms=%.3f",time,hud_profile[0]*scale,hud_profile[1]*scale,hud_profile[2]*scale,hud_profile[3]*scale);
        n3ds_log(message);memset(hud_profile,0,sizeof(hud_profile));
#endif
        snprintf(message,sizeof(message),"EFFECT PROFILE: time=%ld particles_ms=%.3f systems_ms=%.3f contrails_ms=%.3f queue_ms=%.3f",time,effect_profile[0]*scale,effect_profile[1]*scale,effect_profile[2]*scale,effect_profile[3]*scale);
        n3ds_log(message);memset(effect_profile,0,sizeof(effect_profile));
        snprintf(message,sizeof(message),"FRAME SERVICE PROFILE: time=%ld threads_ms=%.3f texture_ms=%.3f gpu_begin_ms=%.3f texture_collect_ms=%.3f texture_load_ms=%.3f",time,service_profile[0]*scale,service_profile[1]*scale,service_profile[2]*scale,service_profile[3]*scale,service_profile[4]*scale);
        n3ds_log(message);
        snprintf(message,sizeof(message),"TEXTURE PIPELINE PROFILE: time=%ld read_ms=%.3f decode_ms=%.3f tile_ms=%.3f upload_ms=%.3f",time,service_profile[5]*scale,service_profile[6]*scale,service_profile[7]*scale,service_profile[8]*scale);
        n3ds_log(message);memset(service_profile,0,sizeof(service_profile));
        snprintf(message,sizeof(message),"FRAME BEGIN PROFILE: time=%ld wait_ms=%.3f other_prepare_ms=%.3f",time,wait_profile*scale,(profile[0]-wait_profile)*scale);
        n3ds_log(message);memset(profile,0,sizeof(profile));profile_frames=0;wait_profile=0;
    }
    if(report_frame || (time==1 && render.frame_index==1)) {
        real_point3d unit_camera=window.camera.position;
        if(unit) unit_get_camera_position(unit_index,&unit_camera);
        char message[256];
        snprintf(message,sizeof(message),"SKY ANIMATION: time=%ld sky=%d visible=%d phase0=%.7f dt=%.7f views=%d",time,render.visible_sky_index,render.visible_sky_model,render_sky_globals[0],frame.dt,stereo?2:1);
        n3ds_log(message);
        struct native_model_counters counters;
        n3ds_engine_model_counters(&counters);
        snprintf(message,sizeof(message),"MISSION MODELS: time=%ld models=%u parts=%u triangles=%u max_nodes=%u clusters=%d",
            time,counters.models,counters.parts,counters.triangles,counters.max_nodes,render.rendered_cluster_count);
        n3ds_log(message);
        snprintf(message,sizeof(message),"MISSION CAMERA: time=%ld unit=%08lx position=%.7f,%.7f,%.7f forward=%.7f,%.7f,%.7f",
            time,unit_index,unit_camera.x,unit_camera.y,unit_camera.z,
            unit?unit->unit.aiming_vector.i:window.camera.forward.i,unit?unit->unit.aiming_vector.j:window.camera.forward.j,unit?unit->unit.aiming_vector.k:window.camera.forward.k);
        n3ds_log(message);
        snprintf(message,sizeof(message),"MISSION VIEW: time=%ld perspective=%d position=%.7f,%.7f,%.7f forward=%.7f,%.7f,%.7f up=%.7f,%.7f,%.7f fov=%.7f",
            time,director_get_perspective(0),window.camera.position.x,window.camera.position.y,window.camera.position.z,
            window.camera.forward.i,window.camera.forward.j,window.camera.forward.k,
            window.camera.up.i,window.camera.up.j,window.camera.up.k,window.camera.vertical_field_of_view);
        n3ds_log(message);
    }
    return 0;
}
