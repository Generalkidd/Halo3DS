/* New3DS integration additions by Kevin Boggs. */
/* Original director/observer camera through the established opaque BSP backend.
 * Original object/weapon submission is connected; HUD and full shading remain. */
#include "cseries.h"
#include "scenario/scenario.h"
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
#include "engine_widgets.h"
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
#include "cache_file_format.h"
#include "effects/particles.h"
#include "bitmaps/bitmap_group.h"
#include "objects/objects.h"
#include "game/players.h"
#include "game/game.h"
#include "items/weapons.h"
#include "physics/collisions.h"
void set_window_camera_values(struct render_window *window,const struct observer_result *observer);
void n3ds_log(const char *message);
void texture_cache_flush(void);
void render_sky(void);
extern real render_sky_globals[8];
void build_sprite_prepare_for_window(void);
void render_particles(void);
void particle_systems_render(void);
void render_contrails_normal(void);
void n3ds_decals_draw_visible(void);
boolean rasterizer_set_texture_direct(short stage,long group_index,short bitmap_index);
void n3ds_hud_draw_screen(void);
void n3ds_menu_draw_screen(void);
void n3ds_cinematic_draw_screen(void);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
void n3ds_engine_frame_service_ticks(long long result[3]);
void n3ds_engine_texture_idle_ticks(long long result[6]);
void n3ds_model_profile_read(long long result[3]);
#ifdef HALO_N3DS_RENDERER_TESTS
void n3ds_model_material_profile_read(long long result[12]);
#endif
static int native_visibility_verify;
void n3ds_visibility_verify(int enabled) {native_visibility_verify=enabled;}
static long long object_profile[6];
static long long hud_profile[4];
static long long effect_profile[4];
void n3ds_object_profile(unsigned int stage,long long ticks)
{ if(stage<6) object_profile[stage]+=ticks; }
struct mission_batch {
    struct structure_material *material; const struct shader *shader;short lightmap_index;
    real_point3d center;real_vector3d extent;
    struct triangle_buffer water_triangles;
    unsigned long clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE/32];int clusters_valid;
};
static unsigned int batch_visible,batch_culled;
/* PC flamethrower projectiles have neither model nor attachment. Its authored
 * jet effect only flashes at the muzzle, leaving the simulated stream invisible.
 * Place its map-owned flame sprite along the live projectile path. No extra
 * simulation objects or bitmap allocations are created. */
static struct native_widget_vertex flame_vertices[64*6];
static unsigned int flame_tag_generation;
static long flame_bitmap=NONE;
struct flame_sprite {short image;real_rectangle2d uv;};
static struct flame_sprite flame_sprites[4];
static unsigned int flame_sprite_count;
enum { FLAME_EMISSIONS=20 };
struct flame_emission {
    real_point3d origin;
    real_vector3d direction;
    float distance;
    unsigned int frame;
    int valid;
};
static struct flame_emission flame_emissions[FLAME_EMISSIONS];
static unsigned int flame_last_emitted=~0u;
/* Kevin Boggs: flame_stream_flush - New3DS integration. */
static void flame_stream_flush(unsigned int count,short image)
{
    if(count && rasterizer_set_texture_direct(0,flame_bitmap,image))
        n3ds_gpu_effect_draw(flame_vertices,count,NATIVE_BLEND_ADD,0,6,0);
}
/* Kevin Boggs: resolve the owner's map bitmap after each tag generation change. */
/* Kevin Boggs: flame_stream_lookup - New3DS integration. */
static void flame_stream_lookup(void)
{
    unsigned int generation=n3ds_engine_tag_generation();
    if(flame_tag_generation==generation) return;
    flame_tag_generation=generation;flame_bitmap=NONE;flame_sprite_count=0;
    memset(flame_emissions,0,sizeof(flame_emissions));flame_last_emitted=~0u;
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    if(!view || !view->tag_header || !view->instances) return;
    for(long i=0;i<view->tag_header->tag_count;++i) {
        const struct cache_file_tag_instance *tag=view->instances+i;
        if(tag->group_tag!=PARTICLE_TAG || !tag->name ||
           strcmp(tag->name,"weapons\\flamethrower\\effects\\flash h flame")) continue;
        const struct particle_definition *particle=particle_definition_get(tag->tag_index);
        if(particle) {
            flame_bitmap=particle->bitmap.index;
            const struct bitmap_group *group=bitmap_group_get(flame_bitmap);
            if(group && particle->first_sequence_index>=0 &&
               particle->initial_sequence_count>0) {
                for(int sequence=particle->first_sequence_index;
                    sequence<particle->first_sequence_index+particle->initial_sequence_count &&
                    sequence<group->sequences.count && flame_sprite_count<NUMBEROF(flame_sprites);
                    ++sequence) {
                    const struct bitmap_group_sequence *entry=TAG_BLOCK_GET_ELEMENT(
                        &group->sequences,sequence,struct bitmap_group_sequence);
                    if(!entry || entry->sprites.count<=0) continue;
                    const struct bitmap_group_sprite *sprite=TAG_BLOCK_GET_ELEMENT(
                        &entry->sprites,0,struct bitmap_group_sprite);
                    if(!sprite || sprite->bitmap_index<0 ||
                       sprite->bitmap_index>=group->bitmaps.count) continue;
                    flame_sprites[flame_sprite_count].image=sprite->bitmap_index;
                    flame_sprites[flame_sprite_count].uv=sprite->bounds;
                    ++flame_sprite_count;
                }
            }
        }
        break;
    }
}
/* Kevin Boggs: flame_owner_fire_age - New3DS integration. */
static long flame_owner_fire_age(long unit_index)
{
    const struct unit_datum *unit=unit_index==NONE?NULL:unit_try_and_get(unit_index);
    if(!unit || unit->unit.current_weapon_index<0 ||
       unit->unit.current_weapon_index>=MAXIMUM_WEAPONS_PER_UNIT) return NONE;
    long weapon_index=unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
    const struct weapon_datum *weapon=weapon_index==NONE?NULL:weapon_try_and_get(weapon_index);
    const char *name=weapon?tag_get_name(weapon->definition_index):NULL;
    return name && !strcmp(name,"weapons\\flamethrower\\flamethrower")?
        game_time_get()-weapon->weapon.game_time_last_fired:NONE;
}
/* Keep near-plane billboards and old turn samples from splashing across the
 * opposite edge of the first-person view. Samples within the view still keep
 * their original world position, so the jet bends naturally on a turn. */
static int flame_sprite_in_view(const real_point3d *center,float radius,
    const real_vector3d *right)
{
    real_vector3d delta;
    vector_from_points3d(&render.camera.position,center,&delta);
    float depth=dot_product3d(&delta,&render.camera.forward);
    float side=dot_product3d(&delta,right);
    float height=dot_product3d(&delta,&render.camera.up);
    if(depth<radius+0.15f) return 0;
    /* Halo's top display is wider than it is high. Leave a narrow guard band
     * for the sprite radius while avoiding out-of-frustum additive blooms. */
    return fabsf(side)<depth*.90f+radius*.25f &&
        fabsf(height)<depth*.55f+radius*.25f;
}
/* Kevin Boggs: display the original simulated flamethrower projectiles as a visible jet. */
/* Kevin Boggs: flame_stream_draw - New3DS integration. */
static void flame_stream_draw(void)
{
    flame_stream_lookup();
    if(flame_bitmap==NONE || !flame_sprite_count) return;
    real_vector3d right;
    cross_product3d(&render.camera.forward,&render.camera.up,&right);
    if(normalize3d(&right)<=0) return;
    struct object_iterator iterator;
    object_iterator_new(&iterator,_object_mask_projectile,0);
    unsigned int used=0;
    short image=NONE;
    unsigned int flame_count=0,local_projectiles=0,remote_projectiles=0;
    long player=local_player_get_player_index(0);
    long local_unit=player==NONE?NONE:player_get(player)->unit_index;
    long local_fire_age=flame_owner_fire_age(local_unit);
    int local_firing=local_fire_age!=NONE && local_fire_age>=0 && local_fire_age<=2;
    static const unsigned char order[6]={0,1,2,0,2,3};
    while(object_iterator_next(&iterator)) {
        const struct object_datum *projectile=object_get(iterator.index);
        const char *name=tag_get_name(projectile->definition_index);
        if(!name || strcmp(name,"weapons\\flamethrower\\flame")) continue;
        ++flame_count;
        long owner_index=projectile->object.owner_object_index;
        const struct unit_datum *owner=owner_index==NONE?NULL:unit_try_and_get(owner_index);
        long fire_age=flame_owner_fire_age(owner_index);
        if(!owner || fire_age==NONE || fire_age<0 || fire_age>18) continue;
        real_vector3d travel;
        vector_from_points3d(&owner->object.position,&projectile->object.position,&travel);
        /* The gameplay projectile may survive for seconds and travel up to
         * 16 m. Its visible flame belongs to the short, active firing jet. */
        if(dot_product3d(&travel,&travel)>25.f) continue;
        if(owner_index==local_unit) {
            ++local_projectiles;
            /* The simulated projectile starts at the world weapon origin,
             * which is displaced from the first-person nozzle on screen. */
            continue;
        }
        const float radius=.14f;
        if(!flame_sprite_in_view(&projectile->object.position,radius,&right))continue;
        ++remote_projectiles;
        const struct flame_sprite *sprite=&flame_sprites[(unsigned int)iterator.index%flame_sprite_count];
        if(used && (image!=sprite->image || used+6>NUMBEROF(flame_vertices))) {
            flame_stream_flush(used,image);used=0;
        }
        image=sprite->image;
        for(unsigned int corner=0;corner<6;++corner) {
            struct native_widget_vertex *v=flame_vertices+used+corner;
            int index=order[corner];
            float x=(index==0 || index==3)?-radius:radius;
            float y=(index<2)?-radius:radius;
            for(int axis=0;axis<3;++axis)
                v->position[axis]=projectile->object.position.n[axis]+right.n[axis]*x+render.camera.up.n[axis]*y;
            v->color[0]=1.f;v->color[1]=.70f;v->color[2]=.08f;
            v->color[3]=owner_index==local_unit?.25f:.14f;
            /* Rotate the authored flame sprite counterclockwise in texture
             * space: its round ignition edge is on the atlas's left. */
            v->uv[0]=index<2?sprite->uv.x0:sprite->uv.x1;
            v->uv[1]=(index==0 || index==3)?sprite->uv.y0:sprite->uv.y1;
        }
        used+=6;
    }
    flame_stream_flush(used,image);
    /* Emit at the animated first-person "spawn fire" marker on the nozzle.
     * The world weapon's "primary trigger" belongs to the simulated shot.
     * The aim point comes from the camera ray, so the visible jet converges on the same
     * impact point as the reticle while preserving the real projectile logic. */
    if(local_firing && local_projectiles && flame_last_emitted!=render.frame_index) {
        const struct unit_datum *unit=unit_try_and_get(local_unit);
        long weapon_index=unit && unit->unit.current_weapon_index>=0 &&
            unit->unit.current_weapon_index<MAXIMUM_WEAPONS_PER_UNIT?
            unit->unit.weapon_object_indices[unit->unit.current_weapon_index]:NONE;
        struct object_marker marker;
        short marker_count=weapon_index==NONE?0:first_person_weapon_get_marker_by_name_render(
            weapon_index,"spawn fire",&marker,1);
        real_point3d muzzle;
        if(marker_count>0) muzzle=marker.matrix.position;
        else {
            /* A few map variants omit the marker; keep their effect aligned
             * with the visible weapon until a marker is available. */
            muzzle=render.camera.position;
            for(int axis=0;axis<3;++axis)
                muzzle.n[axis]+=render.camera.forward.n[axis]*.65f+
                    right.n[axis]*.22f-render.camera.up.n[axis]*.16f;
        }
        real_vector3d aim=render.camera.forward;
        for(int axis=0;axis<3;++axis)aim.n[axis]*=16.f;
        struct collision_result hit;
        real_point3d target;
        if(collision_test_vector(_collision_test_for_line_of_sight_flags,
            &render.camera.position,&aim,local_unit,&hit)) target=hit.point;
        else {target=render.camera.position;
            for(int axis=0;axis<3;++axis)target.n[axis]+=aim.n[axis];}
        real_vector3d direction;
        vector_from_points3d(&muzzle,&target,&direction);
        float distance=normalize3d(&direction);
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
        if(render.frame_index%30==0){char message[192];
            snprintf(message,sizeof(message),
                "FLAME NOZZLE: marker=%d muzzle=%.2f,%.2f,%.2f target=%.2f,%.2f,%.2f distance=%.2f",
                marker_count,muzzle.x,muzzle.y,muzzle.z,target.x,target.y,target.z,distance);
            n3ds_log(message);}
#endif
        if(distance>0) {
            struct flame_emission *sample=flame_emissions+render.frame_index%FLAME_EMISSIONS;
            sample->origin=muzzle;
            sample->direction=direction;sample->distance=distance;
            sample->frame=render.frame_index;sample->valid=1;
            flame_last_emitted=render.frame_index;
        }
    }
    used=0;image=NONE;
    for(unsigned int slot=0;slot<FLAME_EMISSIONS;++slot) {
        const struct flame_emission *sample=flame_emissions+slot;
        unsigned int age=render.frame_index-sample->frame;
        if(!sample->valid || age>=FLAME_EMISSIONS)continue;
        const struct flame_sprite *sprite=&flame_sprites[slot%flame_sprite_count];
        if(used && (image!=sprite->image || used+6>NUMBEROF(flame_vertices))) {
            flame_stream_flush(used,image);used=0;
        }
        image=sprite->image;
        real_point3d center=sample->origin;
        float travel=fminf(sample->distance,.4f+.18f*age);
        for(int axis=0;axis<3;++axis)
            center.n[axis]+=sample->direction.n[axis]*travel;
        /* Twenty 30 Hz frames is a brief jet, not an endless beam. A modest
         * downward acceleration makes spent flame visibly fall before fade. */
        float seconds=(float)age/30.f;
        center.z-=.5f*4.5f*seconds*seconds;
        float fraction=(float)age/FLAME_EMISSIONS;
        float radius=.25f+.15f*fraction;
        float opacity=.40f*(1.f-fraction);
        if(!flame_sprite_in_view(&center,radius,&right))continue;
        for(unsigned int corner=0;corner<6;++corner) {
            struct native_widget_vertex *v=flame_vertices+used+corner;
            int index=order[corner];
            float x=(index==0 || index==3)?-radius:radius;
            float y=(index<2)?-radius:radius;
            for(int axis=0;axis<3;++axis)
                v->position[axis]=center.n[axis]+right.n[axis]*x+render.camera.up.n[axis]*y;
            v->color[0]=1.f;v->color[1]=.70f;v->color[2]=.08f;v->color[3]=opacity;
            v->uv[0]=index<2?sprite->uv.x0:sprite->uv.x1;
            v->uv[1]=(index==0 || index==3)?sprite->uv.y0:sprite->uv.y1;
        }
        used+=6;
    }
    flame_stream_flush(used,image);
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
    if(flame_count && render.frame_index%15==0) {
        char message[128];
        snprintf(message,sizeof(message),"FLAME RAY: live=%u local_fire=%d local=%u remote=%u time=%ld",
            flame_count,local_firing,local_projectiles,remote_projectiles,game_time_get());
        n3ds_log(message);
    }
#endif
}
/* A box is outside only when its nearest point is beyond one side plane.
 * The per-eye rasterizer frustum already includes the stereo projection shift.
 * Large boxes spanning the frustum are retained; no centroid-only rejection. */
static int batch_outside_plane(const struct mission_batch *batch,const struct render_frustum *frustum)
{
    for(int plane=0;plane<4;++plane) {
        const real_plane3d *p=frustum->world_planes+plane;
        real distance=dot_product3d(&p->n,(const real_vector3d *)&batch->center)-p->d;
        real radius=fabsf(p->n.i)*batch->extent.i+fabsf(p->n.j)*batch->extent.j+fabsf(p->n.k)*batch->extent.k;
        if(distance-radius>.01f) return plane;
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
#include "engine_batch_visibility.inl"
/* These CPU descriptors belong to the loaded map. Submitted draws keep their
 * own GPU buffers; retaining descriptors here would pin freed map-sized holes. */
void n3ds_engine_mission_batches_release(void)
{
    surface_clusters_release();
    free(batches);batches=NULL;batch_capacity=count=0;
    loaded_bsp=NULL;loaded_generation=0;
}
#ifdef HALO_N3DS_FRONTEND
void n3ds_frontend_trace(const char *);
#define FRONTEND_STAGE(s) n3ds_frontend_trace(s)
#else
#define FRONTEND_STAGE(s) ((void)0)
#endif
#define CHECK(e) do { if(!(e)) { n3ds_log("MISSION RENDER FAIL: " #e); return 1; } } while(0)
/* Populate terrain depth after the held weapon, before world models. This
 * permits early rejection of scenery/characters behind terrain without an
 * extra depth pass. Transparent groups still use the common sorted queue. */
static long long mission_environment_ticks;
static void draw_mission_environment(const void *context)
{
    const struct rasterizer_window_begin_parameters *window=context;
    long long start=n3ds_engine_ticks();
    rasterizer_environment_diffuse_textures_begin();
    for(long b=0;b<count;++b) {
        struct structure_material *m=batches[b].material;
        if(!batch_cluster_visible(batches+b)) {++batch_culled;continue;}
        int outside=batch_outside_plane(batches+b,&window->frustum);
        if(outside!=NONE) {
            ++batch_culled;
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
        ++batch_visible;
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
        n3ds_engine_bsp_draw(batches[b].shader,m->permutation_index,
            (const unsigned short *)((struct structure_surface *)loaded_bsp->surfaces.address+m->first_surface_index),
            m->surface_count*3,&m->vertices,loaded_bsp->lightmap_group.index,batches[b].lightmap_index,&m->lightmap_vertices);
    }
    rasterizer_environment_diffuse_textures_end();
    mission_environment_ticks=n3ds_engine_ticks()-start;
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
    /* Cinematic-only BSPs (including the Maw finale) legitimately have no
     * opaque world surfaces. Objects, skies and effects still render below. */
    loaded_bsp=bsp; loaded_generation=n3ds_engine_cache_generation();
    snprintf(message,sizeof(message),"MISSION RENDER: batches=%ld triangles=%ld unsupported=%ld; opaque BSP plus original object pass",count,triangles,excluded);
    n3ds_log(message);
    snprintf(message,sizeof(message),"BSP WATER PREPARED: materials=%ld triangles=%ld; original transparent queue",water_batches,water_triangles);n3ds_log(message);
    return 0;
}
int n3ds_engine_mission_render(long unit_index,long time)
{
    if(time==1) {
        /* Cache stress tests belong before map loading in bounded developer
         * runs, never in a live map's first frame with most memory occupied. */
        n3ds_log("NATIVE LIVE PACING: original30Hz loop cap; GPU queue wait retained, extra VBlank wait disabled");
    }
    long long stamp[7];static long long profile[6],service_profile[9],wait_profile;static unsigned int profile_frames;
#ifdef HALO_N3DS_AUDIO_TRACE
    static long long prep_detail[3];
#endif
    stamp[0]=n3ds_engine_ticks();
    struct rasterizer_frame_begin_parameters frame={0};
    struct rasterizer_window_begin_parameters window={0};
    struct render_window original_window={0};
    struct unit_datum *unit=unit_index==NONE?NULL:unit_get(unit_index);
    struct observer_result const *observer=observer_get_camera(0);
    real_point3d unit_camera;
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
    window.camera=original_window.rasterizer_camera;
    if(unit) unit_get_camera_position(unit_index,&unit_camera);
    else unit_camera=window.camera.position;
    CHECK(valid_real_point3d(&window.camera.position) && valid_real_normal3d(&window.camera.forward));
    CHECK(valid_real_normal3d(&window.camera.up) && window.camera.vertical_field_of_view>0.f);
    frame.game_time_sec=time/30.f; frame.dt=1.f/30.f;
    ++render.frame_index;
    render.time_delta_since_tick_sec=frame.dt;
    float slider=n3ds_platform_3d_slider();
    if(!isfinite(slider) || slider<0) slider=0;
    if(slider>1) slider=1;
    int stereo=slider>0;
    n3ds_gpu_stereo_level(slider);
    static float previous_slider=-1;
    if(slider!=previous_slider) {
        char message[96];snprintf(message,sizeof(message),"STEREO: slider=%.3f views=%d frame=%ld",slider,stereo?2:1,render.frame_index);
        n3ds_log(message);previous_slider=slider;
    }
    long long view_cost[3]={0,0,0};
    struct render_camera center_render=original_window.render_camera;
    struct render_camera center_raster=original_window.rasterizer_camera;
    n3ds_gpu_flashlight_set(unit && !cinematic_in_progress()?unit->unit.integrated_light_power:0,
        center_render.position.n,center_render.forward.n);
    render.local_player_index=0; render.window_index=0;
#ifdef HALO_N3DS_AUDIO_TRACE
    long long prep_gpu_start=n3ds_engine_ticks();
#endif
    n3ds_gpu_frame_sync(0);
    FRONTEND_STAGE("frame-begin");
    n3ds_gpu_world_budget(!main_menu_is_active());
    render.camera=center_render;
    extern void interface_prepare_screen_effect(void);
    if(main_menu_is_active()) n3ds_gpu_screen_effect(NULL);
    else interface_prepare_screen_effect();
    rasterizer_frame_begin(&frame);
    n3ds_gpu_frame_sync(1);
    rasterizer_windows_begin();
#ifdef HALO_N3DS_AUDIO_TRACE
    prep_detail[0]+=n3ds_engine_ticks()-prep_gpu_start;
#endif
    long long services[9];n3ds_engine_frame_service_ticks(services);n3ds_engine_texture_idle_ticks(services+3);
    for(int i=0;i<9;++i) service_profile[i]+=services[i];
    wait_profile+=n3ds_gpu_frame_wait_ticks();
    /* Right view first; left last leaves the ordinary HUD camera authoritative.
     * Simulation, frame service, and the original HUD execute only once. */
    for(int eye=stereo?1:0;eye>=0;--eye) {
        float offset=stereo?(eye?1.f:-1.f)*.0105f*slider:0;
        real_vector3d right;
        render.camera=center_render;window.camera=center_raster;
        cross_product3d(&center_render.forward,&center_render.up,&right);normalize3d(&right);
        for(int axis=0;axis<3;++axis) render.camera.position.n[axis]+=right.n[axis]*offset;
        cross_product3d(&center_raster.forward,&center_raster.up,&right);normalize3d(&right);
        for(int axis=0;axis<3;++axis) window.camera.position.n[axis]+=right.n[axis]*offset;
        ++render.scene_index;
        n3ds_gpu_stereo_eye(eye,offset);
        /* Original visibility/projection must agree with the off-axis GPU view. */
        real_rectangle2d bounds;
        bounds.y0=-1;bounds.y1=1;
        float shift=-offset/(1.5f*tanf(render.camera.vertical_field_of_view*.5f)*(400.f/240.f));
        bounds.x0=-1+shift;bounds.x1=1+shift;
        render_camera_build_frustum(&render.camera,stereo?&bounds:NULL,&render.frustum,TRUE);
        shift=-offset/(1.5f*tanf(window.camera.vertical_field_of_view*.5f)*(400.f/240.f));
        bounds.x0=-1+shift;bounds.x1=1+shift;
        render_camera_build_frustum(&window.camera,stereo?&bounds:NULL,&window.frustum,TRUE);
#ifdef HALO_N3DS_AUDIO_TRACE
        long long prep_visibility_start=n3ds_engine_ticks();
#endif
        structure_visibility_find_camera(&render.camera);
#ifdef HALO_N3DS_AUDIO_TRACE
        prep_detail[1]+=n3ds_engine_ticks()-prep_visibility_start;
        prep_visibility_start=n3ds_engine_ticks();
#endif
        n3ds_structure_visibility_compute();
#ifdef HALO_N3DS_AUDIO_TRACE
        prep_detail[2]+=n3ds_engine_ticks()-prep_visibility_start;
#endif
#ifdef HALO_N3DS_ARCHIVE_VERIFY
        if(render.frame_index%30==0) CHECK(batch_visibility_test());
#endif
        if(native_visibility_verify && render.frame_index%300==0) {
            CHECK(n3ds_structure_visibility_test());
            n3ds_log("PASS: native cluster visibility matches full Xbox traversal; unused surface list omitted");
        }
        n3ds_gpu_world_view_begin();
        rasterizer_window_begin(&window);
        stamp[1]=n3ds_engine_ticks();
    build_sprite_prepare_for_window();
    FRONTEND_STAGE("objects");
    long long object_stamp=n3ds_engine_ticks();
    /* Render-side animation advances once, including sky overlay phases and
     * glow movement. The second eye reuses the first eye's updated state. */
    render.time_delta_since_tick_sec=eye==(stereo?1:0)?frame.dt:0.f;
    render_sky();
    n3ds_object_profile(0,n3ds_engine_ticks()-object_stamp);object_stamp=n3ds_engine_ticks();
    if(eye==(stereo?1:0)) {
        struct render_camera eye_camera=render.camera;
        render.camera=center_render;first_person_weapon_render_update();render.camera=eye_camera;
    }
    n3ds_object_profile(1,n3ds_engine_ticks()-object_stamp);object_stamp=n3ds_engine_ticks();
    extern void rasterizer_lights_begin_for_new_frame(void);
    rasterizer_lights_begin_for_new_frame();
    lights_preprocess_scene();
    n3ds_object_profile(2,n3ds_engine_ticks()-object_stamp);
    extern void n3ds_render_objects_with_environment(void (*)(const void *),const void *);
    mission_environment_ticks=0;
    n3ds_render_objects_with_environment(draw_mission_environment,&window);
    stamp[3]=n3ds_engine_ticks();stamp[2]=stamp[3]-mission_environment_ticks;
    long long effect_stamp=stamp[3],effect_next;
    n3ds_decals_draw_visible();
    render_particles();
    effect_next=n3ds_engine_ticks();effect_profile[0]+=effect_next-effect_stamp;effect_stamp=effect_next;
    particle_systems_render();
    effect_next=n3ds_engine_ticks();effect_profile[1]+=effect_next-effect_stamp;effect_stamp=effect_next;
    flame_stream_draw();
    render_contrails_normal();
    effect_next=n3ds_engine_ticks();effect_profile[2]+=effect_next-effect_stamp;effect_stamp=effect_next;
    n3ds_transparent_draw_queued();
    extern void rasterizer_lens_flares_draw(void);
    rasterizer_lens_flares_draw();
    stamp[4]=n3ds_engine_ticks();
    effect_profile[3]+=stamp[4]-effect_stamp;
    view_cost[0]+=stamp[2]-stamp[1];view_cost[1]+=stamp[3]-stamp[2];view_cost[2]+=stamp[4]-stamp[3];
    n3ds_gpu_world_resolve();
    if(eye) {rasterizer_window_end();continue;}
    n3ds_gpu_stereo_overlay(stereo);
    n3ds_gpu_hud_profile_reset();
    FRONTEND_STAGE("screen-widgets");
    if(!main_menu_is_active()) n3ds_hud_draw_screen();
    n3ds_cinematic_draw_screen();
    n3ds_menu_draw_screen();
    n3ds_gpu_stereo_overlay(0);
    stamp[5]=n3ds_engine_ticks();
    long long hud[4];n3ds_gpu_hud_profile_read(hud);
    for(int i=0;i<4;++i) hud_profile[i]+=hud[i];
    FRONTEND_STAGE("present");
    rasterizer_window_end(); rasterizer_windows_end(); rasterizer_frame_end(); rasterizer_present(NULL,NULL);
    stamp[6]=n3ds_engine_ticks();
    } /* eye views */
    render.time_delta_since_tick_sec=frame.dt;
    profile[1]+=view_cost[0];profile[2]+=view_cost[1];profile[3]+=view_cost[2];
    profile[4]+=stamp[5]-stamp[4];profile[5]+=stamp[6]-stamp[5];
    profile[0]+=stamp[4]-stamp[0]-view_cost[0]-view_cost[1]-view_cost[2];
    ++profile_frames;
    if(time%30==0) {
        extern void n3ds_collision_cache_report(void);n3ds_collision_cache_report();
        extern void n3ds_model_offload_report(void);n3ds_model_offload_report();
        char message[256];double scale=1000.0/((double)n3ds_engine_tick_frequency()*profile_frames);
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
        long long model_stages[3];n3ds_model_profile_read(model_stages);
        snprintf(message,sizeof(message),"MODEL PROFILE: time=%ld decode_ms=%.3f skin_ms=%.3f indices_ms=%.3f",time,model_stages[0]*scale,model_stages[1]*scale,model_stages[2]*scale);n3ds_log(message);
        snprintf(message,sizeof(message),"RENDER PROFILE: time=%ld frames=%u prepare_ms=%.3f objects_ms=%.3f bsp_ms=%.3f effects_ms=%.3f hud_ms=%.3f present_ms=%.3f",time,profile_frames,profile[0]*scale,profile[1]*scale,profile[2]*scale,profile[3]*scale,profile[4]*scale,profile[5]*scale);
        n3ds_log(message);
        snprintf(message,sizeof(message),"OBJECT PROFILE: time=%ld sky_ms=%.3f weapon_update_ms=%.3f lights_ms=%.3f find_ms=%.3f weapon_draw_ms=%.3f world_draw_ms=%.3f",time,object_profile[0]*scale,object_profile[1]*scale,object_profile[2]*scale,object_profile[3]*scale,object_profile[4]*scale,object_profile[5]*scale);
        n3ds_log(message);memset(object_profile,0,sizeof(object_profile));
        snprintf(message,sizeof(message),"HUD PROFILE: time=%ld meter_prepare_ms=%.3f sensor_prepare_ms=%.3f text_state_ms=%.3f text_draw_ms=%.3f",time,hud_profile[0]*scale,hud_profile[1]*scale,hud_profile[2]*scale,hud_profile[3]*scale);
        n3ds_log(message);memset(hud_profile,0,sizeof(hud_profile));
        snprintf(message,sizeof(message),"EFFECT PROFILE: time=%ld particles_ms=%.3f systems_ms=%.3f contrails_ms=%.3f queue_ms=%.3f",time,effect_profile[0]*scale,effect_profile[1]*scale,effect_profile[2]*scale,effect_profile[3]*scale);
        n3ds_log(message);memset(effect_profile,0,sizeof(effect_profile));
        snprintf(message,sizeof(message),"FRAME SERVICE PROFILE: time=%ld threads_ms=%.3f texture_ms=%.3f gpu_begin_ms=%.3f texture_collect_ms=%.3f texture_load_ms=%.3f",time,service_profile[0]*scale,service_profile[1]*scale,service_profile[2]*scale,service_profile[3]*scale,service_profile[4]*scale);
        n3ds_log(message);
        snprintf(message,sizeof(message),"TEXTURE PIPELINE PROFILE: time=%ld read_ms=%.3f decode_ms=%.3f tile_ms=%.3f upload_ms=%.3f",time,service_profile[5]*scale,service_profile[6]*scale,service_profile[7]*scale,service_profile[8]*scale);
        n3ds_log(message);memset(service_profile,0,sizeof(service_profile));
        snprintf(message,sizeof(message),"FRAME BEGIN PROFILE: time=%ld wait_ms=%.3f other_prepare_ms=%.3f",time,wait_profile*scale,(profile[0]-wait_profile)*scale);
        n3ds_log(message);
#ifdef HALO_N3DS_AUDIO_TRACE
        snprintf(message,sizeof(message),"FRAME PREP DETAIL: time=%ld gpu_begin_ms=%.3f find_camera_ms=%.3f visibility_ms=%.3f remainder_ms=%.3f",
            time,prep_detail[0]*scale,prep_detail[1]*scale,prep_detail[2]*scale,
            (profile[0]-prep_detail[0]-prep_detail[1]-prep_detail[2])*scale);
        n3ds_log(message);memset(prep_detail,0,sizeof(prep_detail));
#endif
        memset(profile,0,sizeof(profile));profile_frames=0;wait_profile=0;
    }
    if(time==1 || time%30==0) {
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
