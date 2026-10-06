/* Native backend for the original frame and opaque-environment entrypoints.
 * Additional Halo material passes are deliberately not supplied by this unit. */
#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "shaders/shader_definitions.h"
#include "cache/texture_cache.h"
#include "tag_files/tag_groups.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "engine_renderer.h"
#include "engine_input.h"
#include "engine_textures.h"
#include "engine_bitmaps.h"
#include "shaders/shaders.h"
#include "render/render.h"
#include "math/periodic_functions.h"
#include "engine_text.h"
#include "engine_widgets.h"
#include "engine_threads.h"
#include "engine_transparent.h"
#include "rasterizer/rasterizer_transparent_geometry.h"
#include "rasterizer/rasterizer_text.h"
#include "engine_dynamic_geometry.h"
#include "rasterizer/xbox/rasterizer_xbox_draw_primitives.h"
/* SDK declarations only, required by the original backend's public header. */
#include "cseries/cseries_windows.h"
#include "rasterizer/xbox/rasterizer_xbox.h"

struct rasterizer_window_begin_parameters global_window_parameters;
struct rasterizer_frame_begin_parameters global_frame_parameters;
_Static_assert(sizeof(global_window_parameters) == 600, "Original window ABI");
_Static_assert(sizeof(struct native_render_vertex) == 32, "GPU vertex ABI");

enum { GEOMETRY_SLOTS=512 };
struct native_geometry {
    const struct vertex_buffer *source;
    struct native_render_vertex *vertices;
    const unsigned short *index_source;
    unsigned short *indices;
    unsigned int index_count;
    unsigned short *filtered[2];
    unsigned int filter_count[2],filter_key[2][MAXIMUM_CLUSTERS_PER_STRUCTURE/32],filter_frame[2];
    int filter_ready[2];
    float *light_uv;
    int lightmap_valid;
    const struct shader *detail_shader;
    int detail_bitmap_index,detail_scale_valid;
    float detail_scale[2];
    struct native_environment_bump_vertex *bump;
    int bump_tried;
    const struct shader *material_shader;
    unsigned int material_color;
    const struct shader *bitmap_shader;
    int bitmap_permutation,lightmap_group,lightmap_index,bitmap_metadata_valid;
    const struct bitmap_data *bitmap_metadata[3],*emission_bitmap;
    struct native_environment_emission emission;
    float emission_time;
    int emission_allowed,emission_ready,emission_animated,plasma;
    unsigned int plasma_on,plasma_phase;
};
static unsigned int bsp_index_bytes,bsp_index_hits,bsp_index_misses;
static unsigned int bsp_tinted_draws,bsp_emission_draws,bsp_emission_updates;
static unsigned int bsp_bump_bytes,bsp_bump_draws,bsp_bump_fallbacks;
static unsigned int bsp_lightmap_draws,bsp_detail_draws,bsp_base_draws,bsp_light_uv_bytes,bsp_detail_add_draws;
static struct native_geometry geometry[GEOMETRY_SLOTS];
/* The original immutable owners remain authoritative. A compact index avoids
 * scanning the same BSP geometry twice for every material, every eye. */
static unsigned short geometry_lookup[1024];
static unsigned int geometry_count;
static int frame_state, diffuse_active;
#include "engine_bsp_index_visibility.inl"
long long n3ds_engine_ticks(void);
static long long frame_service_ticks[3];
void n3ds_engine_frame_service_ticks(long long result[3]) { memcpy(result,frame_service_ticks,sizeof(frame_service_ticks)); }
/* states: 0 idle, 1 frame, 2 windows, 3 window, 4 windows ended, 5 ready to present */

boolean rasterizer_set_texture_direct(short stage, long group_index, short index);
boolean rasterizer_set_texture_bitmap_data(short stage,const struct bitmap_data *bitmap);
void rasterizer_text_cache_dispose(void);

void rasterizer_set_framebuffer_blend_function(short function)
{
    assert(function>=0 && function<NATIVE_BLEND_COUNT);
    assert(n3ds_gpu_framebuffer_blend(function));
}

unsigned int n3ds_engine_geometry_count(void) { return geometry_count; }
int n3ds_engine_window_active(void) { return frame_state==3 && !diffuse_active && !n3ds_engine_widgets_active(); }
void n3ds_engine_geometry_flush(void)
{
    unsigned int i;
    /* Water retains CPU-only immutable decode data, never queued GPU memory. */
    extern void n3ds_water_bsp_release(void);n3ds_water_bsp_release();
    if (!geometry_count) return;
    assert(frame_state == 0 && n3ds_gpu_texture_barrier());
    {
        extern void n3ds_log(const char *message);
        char message[160];snprintf(message,sizeof(message),"NATIVE ENVIRONMENT: lightmap_draws=%u detail_draws=%u base_fallback_draws=%u light_uv_bytes=%u",bsp_lightmap_draws,bsp_detail_draws,bsp_base_draws,bsp_light_uv_bytes);n3ds_log(message);
        bsp_lightmap_draws=bsp_detail_draws=bsp_base_draws=bsp_light_uv_bytes=0;
        snprintf(message,sizeof(message),"NATIVE ENVIRONMENT EMISSION: draws=%u animation_updates=%u; single pass",bsp_emission_draws,bsp_emission_updates);n3ds_log(message);bsp_emission_draws=bsp_emission_updates=0;
        unsigned int stats[3];n3ds_gpu_plasma_stats(stats);
        if(stats[0]) {snprintf(message,sizeof(message),"NATIVE PLASMA: draws=%u lut_builds=%u lut_hits=%u texture_bytes=%u; cumulative,one pass",stats[0],stats[1],stats[2],n3ds_gpu_texture_variant_bytes());n3ds_log(message);}
        snprintf(message,sizeof(message),"NATIVE ENVIRONMENT COLOR: plain_tinted_draws=%u; single pass",bsp_tinted_draws);n3ds_log(message);bsp_tinted_draws=0;
        snprintf(message,sizeof(message),"NATIVE ENVIRONMENT BIASED ADD: draws=%u; base/detail/light in one pass",bsp_detail_add_draws);n3ds_log(message);bsp_detail_add_draws=0;
        snprintf(message,sizeof(message),"NATIVE ENVIRONMENT BUMP: draws=%u geometry_bytes=%u allocation_fallbacks=%u; single pass",bsp_bump_draws,bsp_bump_bytes,bsp_bump_fallbacks);n3ds_log(message);
        bsp_bump_draws=bsp_bump_bytes=bsp_bump_fallbacks=0;
    }
    for (i=0; i<geometry_count; ++i) {
        n3ds_gpu_geometry_free(geometry[i].vertices);
        n3ds_gpu_geometry_free(geometry[i].indices);
        for(int eye=0;eye<2;++eye)n3ds_gpu_geometry_free(geometry[i].filtered[eye]);
        n3ds_gpu_geometry_free(geometry[i].light_uv);
        n3ds_gpu_geometry_free(geometry[i].bump);
    }
    extern void n3ds_log(const char *);
    char message[128];snprintf(message,sizeof(message),"BSP INDEX CACHE: hits=%u misses=%u bytes=%u released=%u",
        bsp_index_hits,bsp_index_misses,bsp_index_bytes,bsp_index_bytes);n3ds_log(message);
    snprintf(message,sizeof(message),"BSP TRIANGLE FILTER: saved=%u rebuilds=%u reused=%u bytes=%u",bsp_filter_saved,bsp_filter_builds,bsp_filter_hits,bsp_filter_bytes);n3ds_log(message);
    bsp_filter_bytes=bsp_filter_builds=bsp_filter_hits=bsp_filter_saved=0;
    bsp_index_bytes=bsp_index_hits=bsp_index_misses=0;
    memset(geometry, 0, sizeof(geometry));memset(geometry_lookup,0,sizeof(geometry_lookup));geometry_count=0;
}
boolean _rasterizer_initialize(void)
{
    assert(!rasterizer_globals.initialized && !frame_state);
    if (!n3ds_gpu_renderer_initialize()) return FALSE;
    if (!rasterizer_dynamic_geometry_initialize()) { n3ds_gpu_renderer_dispose(); return FALSE; }
    texture_cache_new(); texture_cache_open();
    if(!rasterizer_text_cache_initialize()) {
        texture_cache_close(); texture_cache_delete();
        rasterizer_dynamic_geometry_dispose(); n3ds_gpu_renderer_dispose();
        return FALSE;
    }
    if(!rasterizer_transparent_geometry_initialize()) {
        rasterizer_transparent_geometry_dispose();
        rasterizer_text_cache_dispose();
        texture_cache_close(); texture_cache_delete();
        rasterizer_dynamic_geometry_dispose(); n3ds_gpu_renderer_dispose();
        return FALSE;
    }
    rasterizer_globals.reserved04.screen_bounds.x0=0;
    rasterizer_globals.reserved04.screen_bounds.y0=0;
    rasterizer_globals.reserved04.screen_bounds.x1=400;
    rasterizer_globals.reserved04.screen_bounds.y1=240;
    rasterizer_globals.reserved04.frame_bounds=rasterizer_globals.reserved04.screen_bounds;
    rasterizer_globals.initialized=TRUE;
    extern void rasterizer_screen_effects_initialize(void);
    rasterizer_screen_effects_initialize();
    return TRUE;
}
void _rasterizer_reset_state(void)
{
    assert(!frame_state);
    n3ds_gpu_texture_bind(0, NULL); n3ds_gpu_texture_bind(1, NULL); n3ds_gpu_texture_bind(2, NULL);
    memset(&global_window_parameters, 0, sizeof(global_window_parameters));
}
void _rasterizer_frame_begin(const struct rasterizer_frame_begin_parameters *parameters)
{
    assert(rasterizer_globals.initialized && !frame_state && parameters);
    long long stamp=n3ds_engine_ticks(),next;
    n3ds_threads_collect();
    next=n3ds_engine_ticks();frame_service_ticks[0]=next-stamp;stamp=next;
    texture_cache_idle();
    next=n3ds_engine_ticks();frame_service_ticks[1]=next-stamp;stamp=next;
    global_frame_parameters=*parameters;
    assert(n3ds_gpu_frame_begin()); frame_state=1;++bsp_filter_frame;
    frame_service_ticks[2]=n3ds_engine_ticks()-stamp;
}
void _rasterizer_windows_begin(void) { assert(frame_state==1); frame_state=2; }
void _rasterizer_window_begin(const struct rasterizer_window_begin_parameters *parameters)
{
    struct native_render_camera camera;
    const struct render_camera *input;
    assert(frame_state==2 && parameters);
    /* This backend currently has one primary, non-mirrored screen target. */
    assert(parameters->rasterizer_target==0 && parameters->window_index==0 && !parameters->has_mirror);
    input=&parameters->camera;
    assert(!input->mirrored && input->z_near>0 && input->z_far>input->z_near);
    assert(input->vertical_field_of_view>0 && input->vertical_field_of_view<3.14f);
    global_window_parameters=*parameters;
    rasterizer_dynamic_geometry_begin();
    n3ds_transparent_begin_view();
    memcpy(camera.position, input->position.n, sizeof(camera.position));
    memcpy(camera.forward, input->forward.n, sizeof(camera.forward));
    memcpy(camera.up, input->up.n, sizeof(camera.up));
    camera.vertical_fov=input->vertical_field_of_view;
    camera.near_clip=input->z_near; camera.far_clip=input->z_far;
    camera.clear_color=0x101822ff; /* Opaque pass; atmospheric fog is not implemented yet. */
    camera.suppress_clear=parameters->suppress_clear;
    n3ds_gpu_window_begin(&camera); frame_state=3;
}
void _rasterizer_window_get_fog(struct render_fog *fog) { assert(fog); *fog=global_window_parameters.fog; }
void _rasterizer_window_set_fog(const struct render_fog *fog) { assert(fog); global_window_parameters.fog=*fog; }
void _rasterizer_window_end(void) { assert(frame_state==3 && !diffuse_active && !n3ds_engine_models_active() && !n3ds_engine_text_active() && !n3ds_engine_widgets_active()); rasterizer_dynamic_geometry_end(); frame_state=2; }
void _rasterizer_windows_end(void) { assert(frame_state==2); frame_state=4; }
void _rasterizer_frame_end(void) { assert(frame_state==4); frame_state=5; }
void _rasterizer_present(struct bitmap_data *screenshot_bitmap, const point2d *screenshot_index)
{
    assert(frame_state==5 && !screenshot_bitmap && !screenshot_index);
    n3ds_gpu_present(); frame_state=0;
    ++rasterizer_globals.frame_index; ++rasterizer_globals.d3d_flip_count;
}
void _rasterizer_dispose(void)
{
    assert(!frame_state);
    n3ds_engine_geometry_flush();
    rasterizer_transparent_geometry_dispose();
    rasterizer_dynamic_geometry_dispose();
    rasterizer_text_cache_dispose();
    assert(n3ds_dynamic_bitmaps_collect());
    texture_cache_close(); texture_cache_delete();
    n3ds_gpu_renderer_dispose(); rasterizer_globals.initialized=FALSE;
}

static struct native_geometry *get_geometry(const struct vertex_buffer *source)
{
    unsigned int i;
    const struct n3ds_cache_view *bsp=n3ds_engine_cache_view(1);
    real *decoded;
    struct native_render_vertex *vertices;
    assert(source && n3ds_cache_resolve(bsp, source, sizeof(*source))==source);
    unsigned int slot=((unsigned int)(uintptr_t)source>>4)*2654435761u;slot>>=22;
    while(geometry_lookup[slot]) {
        struct native_geometry *entry=geometry+geometry_lookup[slot]-1;
        if(entry->source==source) return entry;
        slot=(slot+1)&1023;
    }
    assert(geometry_count<GEOMETRY_SLOTS && source->count>0 && source->count<=65535);
    assert(source->type==_rasterizer_vertex_type_environment_compressed || source->type==_rasterizer_vertex_type_environment_uncompressed);
    i=source->type==_rasterizer_vertex_type_environment_compressed ? 32 : 56;
    assert(source->base_address && n3ds_cache_resolve(bsp, source->base_address, source->count*i)==source->base_address);
    /* Decode in a small stack window. A full temporary uncompressed mesh can
     * require hundreds of KiB in a fragmented original-model CPU heap, even
     * though its final GPU vertices fit comfortably in linear memory. */
    unsigned int stride=i;
    real scratch[64*14];
    vertices=n3ds_gpu_geometry_allocate(source->count*sizeof(*vertices)); assert(vertices);
    for(unsigned int base=0;base<(unsigned long)source->count;base+=64) {
        unsigned int chunk=MIN(64,(unsigned long)source->count-base);
        if(stride==32) {
            rasterizer_geometry_uncompress_vertices(source->type,chunk,scratch,chunk*56,(byte *)source->base_address+base*32,chunk*32);
            decoded=scratch;
        } else decoded=(real *)source->base_address+base*14;
        for(unsigned int j=0;j<chunk;++j) {
            real *v=decoded+j*14;real light;i=base+j;
            assert(isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]) && isfinite(v[12]) && isfinite(v[13]));
            memcpy(vertices[i].position,v,12);memcpy(vertices[i].uv,v+12,8);
            light=.3f+.7f*fabsf(v[3]*.3f+v[4]*.4f+v[5]*.8660254f);
            vertices[i].color[0]=light*.72f;vertices[i].color[1]=light*.82f;vertices[i].color[2]=light*.9f;
        }
    }
    n3ds_gpu_geometry_flush(vertices, source->count*sizeof(*vertices));
    struct native_geometry *entry=geometry+geometry_count;
    entry->source=source;entry->vertices=vertices;geometry_lookup[slot]=++geometry_count;
    return entry;
}
/* Native implementation of the original indexed-draw entrypoint. The caller
 * owns material state; this routine validates staging and copies indices into
 * the GPU submission arena before CPU staging can be reused. */
void rasterizer_draw_dynamic_triangles_static_vertices(long handle, long first,
    long count, const struct vertex_buffer *source)
{
    const struct native_triangle_view *triangles=n3ds_dynamic_triangles_get(handle);
    struct native_render_vertex *vertices;
    unsigned int i;
    assert(frame_state==3 && triangles && source);
    assert(first>=0 && count>=0 && (unsigned long)first<=triangles->count &&
        (unsigned long)count<=triangles->count-first);
    if (!count) return;
    vertices=get_geometry(source)->vertices;
    for (i=0;i<(unsigned long)count*3;++i)
        assert(triangles->indices[first*3+i]<source->count);
    assert(n3ds_gpu_geometry_draw(vertices,triangles->indices+first*3,count*3));
}
void _rasterizer_environment_diffuse_textures_begin(void) { assert(frame_state==3 && !diffuse_active); diffuse_active=1; }
void _rasterizer_environment_diffuse_textures_end(void) { assert(diffuse_active); n3ds_gpu_world_state_restore();diffuse_active=0; }
void _rasterizer_environment_diffuse_texture_draw(const struct shader *shader, short bitmap_index,
    long handle, long first, long count, const struct vertex_buffer *source)
{
    /* Xbox environment shader base bitmap: verified tag-layout offset 0x88. */
    struct environment_animation {byte reserved[0x18];real on[3],off[3];short function,pad;real period,phase;};
    struct environment_prefix { byte preceding_fields[0x88]; struct tag_reference base_map; };
    const struct environment_prefix *environment=(const void *)shader;
    const struct native_triangle_view *t=n3ds_dynamic_triangles_get(handle);
    const struct n3ds_cache_view *tags=n3ds_engine_cache_view(0);
    assert(frame_state==3 && diffuse_active && t && source);
    assert(first>=0 && count>=0 && (unsigned long)first<=t->count && (unsigned long)count<=t->count-first);
    if (!count) return;
    assert(environment && n3ds_cache_resolve(tags, environment, sizeof(*environment))==environment);
    assert(shader->base.type==3); /* Original _shader_type_environment. */
    if (!rasterizer_set_texture_direct(0, environment->base_map.index, bitmap_index)) n3ds_gpu_texture_bind(0, NULL);
    rasterizer_draw_dynamic_triangles_static_vertices(handle,first,count,source);
}

/* BSP materials and surface arrays are immutable until BSP unload, whose idle
 * geometry flush waits for the GPU before releasing this owner. Dynamic draw
 * APIs retain their original validation and submission-copy semantics. */
static void environment_bump_prepare(struct native_geometry *entry,const struct vertex_buffer *source,
    const struct vertex_buffer *lightmap_source)
{
    if(entry->bump_tried) return;
    entry->bump_tried=1;
    unsigned int bytes=(unsigned int)source->count*sizeof(struct native_environment_bump_vertex);
    unsigned int budget=n3ds_input_platform_original_model()?512*1024:1024*1024;
    if(!entry->lightmap_valid || bsp_bump_bytes>budget || bytes>budget-bsp_bump_bytes) {++bsp_bump_fallbacks;return;}
    struct native_environment_bump_vertex *out=n3ds_gpu_geometry_optional_allocate(bytes);
    if(!out) {++bsp_bump_fallbacks;return;}
    /* Program16 consumes v7=incident radiosity, projects it onto tangent,
     * binormal, normal, and uses length(v7)^2 as the directional weight.
     * Geometry is immutable until the existing BSP flush/fence. */
    for(unsigned int i=0;i<(unsigned int)source->count;++i) {
        real_vector3d axes[3],incident;float local[3],magnitude=0,length=0;
        if(source->type==_rasterizer_vertex_type_environment_compressed) {
            const byte *p=(const byte *)source->base_address+i*32+12;
            for(int a=0;a<3;++a) {unsigned int packed;memcpy(&packed,p+a*4,4);axes[a]=uncompress_int32_to_real_vector3d(packed);}
        } else {
            const byte *p=(const byte *)source->base_address+i*56+12;
            for(int a=0;a<3;++a) memcpy(axes[a].n,p+a*12,12);
        }
        if(lightmap_source->type==3) {
            unsigned int packed;memcpy(&packed,(const byte *)lightmap_source->base_address+i*8,4);
            incident=uncompress_int32_to_real_vector3d(packed);
        } else memcpy(incident.n,(const byte *)lightmap_source->base_address+i*20,12);
        for(int a=0;a<3;++a) {
            magnitude+=incident.n[a]*incident.n[a];local[a]=0;
            for(int c=0;c<3;++c) local[a]+=incident.n[c]*axes[2-a].n[c];
            length+=local[a]*local[a];
        }
        if(!isfinite(magnitude) || !isfinite(length)) {n3ds_gpu_geometry_free(out);++bsp_bump_fallbacks;return;}
        out[i].weight=PIN(magnitude,0.f,1.f);
        memcpy(out[i].light_uv,entry->light_uv+i*2,2*sizeof(float));
        out[i].quaternion[0]=out[i].quaternion[1]=out[i].quaternion[3]=0;out[i].quaternion[2]=1;
        if(length>1e-12f) {
            float inverse=1/sqrtf(length);for(int a=0;a<3;++a) local[a]*=inverse;
            local[2]+=1;length=local[0]*local[0]+local[1]*local[1]+local[2]*local[2];
            if(length>1e-8f) {inverse=1/sqrtf(length);for(int a=0;a<3;++a) out[i].quaternion[a]=local[a]*inverse;}
            else {out[i].quaternion[0]=1;out[i].quaternion[2]=0;}
        }
    }
    n3ds_gpu_geometry_flush(out,bytes);entry->bump=out;bsp_bump_bytes+=bytes;
}

void n3ds_engine_bsp_draw(const struct shader *shader,short bitmap_index,
    const unsigned short *indices,unsigned int count,const struct vertex_buffer *source,
    int lightmap_group,short lightmap_index,const struct vertex_buffer *lightmap_source)
{
    struct environment_animation {byte reserved[0x18];real on[3],off[3];short function,pad;real period,phase;};
    struct environment_prefix {
        byte header[0x28];word flags;byte reserved[0x42];word diffuse_flags;byte reserved_diffuse[0x1a];struct tag_reference base_map;
        byte unused[0x18];short detail_function,pad;real detail_scale;struct tag_reference detail_map;
        byte reserved_detail[0x44];real material_color[3];byte reserved_color[0xc];
        real bump_scale;struct tag_reference bump_map;real runtime_bump_scale[2];
        byte reserved_bump[0x10];short u_function,u_pad;real u_period,u_scale;
        short v_function,v_pad;real v_period,v_scale;byte reserved_uv[0x18];
        word emission_flags;short emission_pad;struct environment_animation animation[3];
        byte reserved_emission[0x18];real emission_scale;struct tag_reference emission_map;
    };
    _Static_assert(offsetof(struct environment_prefix,u_function)==0x150,"Xbox environment UV animation ABI");
    _Static_assert(offsetof(struct environment_prefix,emission_flags)==0x180,"Xbox environment illumination ABI");
    _Static_assert(offsetof(struct environment_prefix,animation[0].on)==0x19c,"Xbox environment primary color ABI");
    _Static_assert(offsetof(struct environment_prefix,animation[2].on)==0x214,"Xbox environment plasma color ABI");
    _Static_assert(offsetof(struct environment_prefix,emission_map)==0x254,"Xbox environment illumination map ABI");
    _Static_assert(offsetof(struct environment_prefix,bump_map)==0x128,"Xbox environment bump map ABI");
    _Static_assert(offsetof(struct environment_prefix,runtime_bump_scale)==0x138,"Xbox environment bump scale ABI");
    _Static_assert(offsetof(struct environment_prefix,material_color)==0x10c,"Xbox environment material color ABI");
    _Static_assert(offsetof(struct environment_prefix,diffuse_flags)==0x6c,"Xbox environment diffuse flags ABI");
    _Static_assert(offsetof(struct environment_prefix,detail_map)==0xb8,"Xbox environment primary detail ABI");
    const struct environment_prefix *environment=(const void *)shader;
    const struct n3ds_cache_view *bsp=n3ds_engine_cache_view(1),*tags=n3ds_engine_cache_view(0);
    assert(frame_state==3 && diffuse_active && count && count%3==0 && count<=131072*3);
    assert(environment && n3ds_cache_resolve(tags,environment,sizeof(*environment))==environment && shader->base.type==3);
    struct native_geometry *entry=get_geometry(source);
    struct native_render_vertex *vertices=entry->vertices;
    if(!entry->indices) {
        assert(indices && n3ds_cache_resolve(bsp,indices,count*sizeof(*indices))==indices);
        for(unsigned int i=0;i<count;++i) assert(indices[i]<source->count);
        entry->indices=n3ds_gpu_geometry_allocate(count*sizeof(*indices));assert(entry->indices);
        memcpy(entry->indices,indices,count*sizeof(*indices));
        n3ds_gpu_geometry_flush(entry->indices,count*sizeof(*indices));
        entry->index_source=indices;entry->index_count=count;
        bsp_index_bytes+=count*sizeof(*indices);++bsp_index_misses;
    } else {
        assert(entry->index_source==indices && entry->index_count==count);++bsp_index_hits;
    }
    const unsigned short *visible_indices=bsp_visible_index_buffer(entry,&count);
    if(!count)return;
    if(!entry->light_uv) {
        entry->light_uv=n3ds_gpu_geometry_allocate(source->count*2*sizeof(float));
        if(entry->light_uv) {
            bsp_light_uv_bytes+=source->count*2*sizeof(float);
            unsigned int stride=lightmap_source->type==3?8:20;
            int valid=lightmap_source->count==source->count && (lightmap_source->type==2 || lightmap_source->type==3) &&
                lightmap_source->base_address && n3ds_cache_resolve(bsp,lightmap_source->base_address,source->count*stride)==lightmap_source->base_address;
            for(unsigned int v=0;v<(unsigned int)source->count;++v) {
                float uv[2]={0,0};
                if(valid) {
                    const byte *at=(const byte *)lightmap_source->base_address+v*stride;
                    if(stride==8) {short packed[2];memcpy(packed,at+4,4);uv[0]=(packed[0]*2.f+1.f)/65535.f;uv[1]=(packed[1]*2.f+1.f)/65535.f;}
                    else memcpy(uv,at+12,8);
                    assert(isfinite(uv[0]) && isfinite(uv[1]));
                }
                memcpy(entry->light_uv+v*2,uv,sizeof(uv));
            }
            entry->lightmap_valid=valid;
            n3ds_gpu_geometry_flush(entry->light_uv,source->count*2*sizeof(float));
        }
    }
    if(!entry->bitmap_metadata_valid || entry->bitmap_shader!=shader || entry->bitmap_permutation!=bitmap_index ||
       entry->lightmap_group!=lightmap_group || entry->lightmap_index!=lightmap_index) {
        /* BSP ownership ends before tags unload. Cache tag pointers only;
         * bitmap->hardware_format/slot remain validated by normal binding. */
        entry->bitmap_metadata[0]=n3ds_engine_bitmap_metadata(environment->base_map.index,bitmap_index);
        entry->bitmap_metadata[1]=entry->lightmap_valid?n3ds_engine_bitmap_metadata(lightmap_group,lightmap_index):NULL;
        int extra_bitmap=NONE;
        if(environment->detail_map.index!=NONE && environment->detail_function>=0 && environment->detail_function<=2 && isfinite(environment->detail_scale))
            extra_bitmap=environment->detail_map.index;
        else if(environment->detail_map.index==NONE && environment->bump_map.index!=NONE && !(environment->flags&2) &&
                isfinite(environment->runtime_bump_scale[0]) && isfinite(environment->runtime_bump_scale[1]) &&
                isfinite(environment->material_color[0]) && isfinite(environment->material_color[1]) && isfinite(environment->material_color[2]))
            extra_bitmap=environment->bump_map.index;
        entry->bitmap_metadata[2]=n3ds_engine_bitmap_metadata(extra_bitmap,bitmap_index);
        entry->emission_ready=0;entry->emission_animated=0;entry->emission_bitmap=NULL;entry->plasma=0;entry->plasma_on=0xff000000u;
        /* Preserve detail/bump paths when all three samplers are occupied.
         * Plasma uses signed fragment lighting and all six combiners; retain
         * authored detail/true bump when those already occupy the third unit. */
        entry->emission_allowed=environment->emission_map.index!=NONE && environment->detail_map.index==NONE &&
            (environment->bump_map.index==NONE || (environment->flags&2)) && isfinite(environment->emission_scale);
        for(int i=0;i<3;++i) {
            const struct environment_animation *a=&environment->animation[i];
            if(i<2 && a->function>=2 && memcmp(a->on,a->off,sizeof(a->on))) entry->emission_animated|=1<<i;
            for(int c=0;c<3;++c) if(!isfinite(a->on[c]) || !isfinite(a->off[c]) || a->on[c]<0 || a->off[c]<0 || a->on[c]>1 || a->off[c]>1)
                entry->emission_allowed=0;
            if(i<2 && (a->function<0 || a->function>=12 || !isfinite(a->period) || a->period==0 || !isfinite(a->phase))) entry->emission_allowed=0;
            if(environment->animation[2].on[i]!=0) entry->plasma=1;
            if(isfinite(environment->animation[2].on[i])) entry->plasma_on|=(unsigned int)(PIN(environment->animation[2].on[i],0.f,1.f)*255+.5f)<<(i*8);
        }
        if(entry->plasma) {
            const struct environment_animation *a=&environment->animation[2];
            if(a->function<0 || a->function>=12 || !isfinite(a->period) || a->period==0 || !isfinite(a->phase)) entry->emission_allowed=0;
            if(a->function>=2) entry->emission_animated|=8;
        }
        if(environment->u_function<0 || environment->u_function>=12 || !isfinite(environment->u_period) || environment->u_period==0 || !isfinite(environment->u_scale) ||
           environment->v_function<0 || environment->v_function>=12 || !isfinite(environment->v_period) || environment->v_period==0 || !isfinite(environment->v_scale)) entry->emission_allowed=0;
        if((environment->u_function>=2 && environment->u_scale!=0) || (environment->v_function>=2 && environment->v_scale!=0)) entry->emission_animated|=4;
        if(entry->emission_allowed) entry->emission_bitmap=n3ds_engine_bitmap_metadata(environment->emission_map.index,bitmap_index);
        entry->bitmap_shader=shader;entry->bitmap_permutation=bitmap_index;
        entry->lightmap_group=lightmap_group;entry->lightmap_index=lightmap_index;entry->bitmap_metadata_valid=1;
    }
    if(!rasterizer_set_texture_bitmap_data(0,entry->bitmap_metadata[0])) n3ds_gpu_texture_bind(0,NULL);
    if(!entry->light_uv) {n3ds_gpu_world_state_restore();assert(n3ds_gpu_geometry_draw_resident(vertices,visible_indices,count));return;}
    /* Lightmaps always clamp. Installing the default repeat sampler first
     * forced two descriptor changes per draw even when reusing the same map. */
    int lightmap=entry->lightmap_valid && lightmap_group!=NONE && lightmap_index!=NONE &&
        n3ds_engine_bitmap_bind_sampler(1,entry->bitmap_metadata[1],NATIVE_SAMPLER_CLAMP_U|NATIVE_SAMPLER_CLAMP_V);
    if(entry->material_shader!=shader) {
        entry->material_color=0xff000000;
        for(int c=0;c<3;++c) {
            float value=environment->material_color[c];
            if(!isfinite(value)) value=1.f;
            entry->material_color|=(unsigned int)(PIN(value,0.f,1.f)*255+.5f)<<(c*8);
        }
        entry->material_shader=shader;
    }
    if(entry->emission_allowed && entry->emission_bitmap && (entry->plasma?n3ds_engine_bitmap_bind_plasma:n3ds_engine_bitmap_bind_sampler)(2,entry->emission_bitmap,
        environment->emission_flags&1?NATIVE_SAMPLER_MAG_POINT|NATIVE_SAMPLER_MIN_POINT:0)) {
        if(!entry->emission_ready || (entry->emission_animated && entry->emission_time!=global_frame_parameters.game_time_sec)) {
            entry->emission_time=global_frame_parameters.game_time_sec;++bsp_emission_updates;
            for(int i=0;i<3;++i) {
                if(entry->emission_ready && (i==2 || !(entry->emission_animated&(1<<i)))) continue;
                const struct environment_animation *a=&environment->animation[i];
                real value=i==2 || a->function==1?0:a->function==0?1:periodic_function_evaluate(a->function,(entry->emission_time+a->phase)/a->period);
                entry->emission.color[i]=0xff000000u;
                for(int c=0;c<3;++c) entry->emission.color[i]|=(unsigned int)(PIN(a->off[c]*(1-value)+a->on[c]*value,0.f,1.f)*255+.5f)<<(c*8);
            }
            if(!entry->emission_ready || (entry->emission_animated&4)) {
                real u,v;shader_environment_texture_animation_evaluate(shader,entry->emission_time,&u,&v);
                entry->emission.uv[0]=entry->emission.uv[1]=environment->emission_scale;
                entry->emission.uv[2]=u*environment->emission_scale;entry->emission.uv[3]=v*environment->emission_scale;
            }
            if(entry->plasma && (!entry->emission_ready || (entry->emission_animated&8))) {
                const struct environment_animation *a=&environment->animation[2];
                real value=a->function==1?0:a->function==0?1:periodic_function_evaluate(a->function,(entry->emission_time+a->phase)/a->period);
                entry->plasma_phase=(unsigned int)__builtin_rint((double)PIN(value,0.f,1.f)*255.0);
            }
            entry->emission_ready=1;
        }
        if(entry->plasma) {assert(n3ds_gpu_environment_plasma_draw(vertices,entry->light_uv,visible_indices,count,lightmap,
            lightmap?entry->material_color:0xffffffffu,!!(environment->flags&1),&entry->emission,entry->plasma_on,entry->plasma_phase));}
        else {assert(n3ds_gpu_environment_emission_draw(vertices,entry->light_uv,visible_indices,count,lightmap,
            lightmap?entry->material_color:0xffffffffu,!!(environment->flags&1),&entry->emission));}
        ++bsp_emission_draws;if(lightmap) ++bsp_lightmap_draws;else ++bsp_base_draws;return;
    }
    /* Preserve authored detail where its third sampler is already used.
     * A bump map marked as a specular mask is not a normal map. */
    if(lightmap && environment->detail_map.index==NONE && environment->bump_map.index!=NONE && !(environment->flags&2) &&
       isfinite(environment->runtime_bump_scale[0]) && isfinite(environment->runtime_bump_scale[1]) &&
       isfinite(environment->material_color[0]) && isfinite(environment->material_color[1]) && isfinite(environment->material_color[2])) {
        environment_bump_prepare(entry,source,lightmap_source);
        if(entry->bump && rasterizer_set_texture_bitmap_data(2,entry->bitmap_metadata[2])) {
            assert(n3ds_gpu_environment_bump_draw(vertices,entry->bump,visible_indices,count,environment->runtime_bump_scale[0],environment->runtime_bump_scale[1],entry->material_color,!!(environment->flags&1)));
            ++bsp_bump_draws;++bsp_lightmap_draws;return;
        }
    }
    int detail=0;
    if(environment->detail_map.index!=NONE && environment->detail_function>=0 && environment->detail_function<=2 &&
       isfinite(environment->detail_scale) && rasterizer_set_texture_bitmap_data(2,entry->bitmap_metadata[2]))
        detail=environment->detail_function==2?3:environment->detail_function==0?2:1;
    if(detail && (entry->detail_shader!=shader || entry->detail_bitmap_index!=bitmap_index || !entry->detail_scale_valid)) {
        /* Tag metadata and BSP geometry share the map lifetime. Cache only the
         * authored UV scale, never a texture handle; uploads/eviction remain
         * owned by the existing cache. Re-evaluate on shader/permutation change. */
        entry->detail_scale[0]=entry->detail_scale[1]=environment->detail_scale;
        entry->detail_scale_valid=1;
        if(environment->diffuse_flags&1) {
            int base_size[2],detail_size[2];
            entry->detail_scale_valid=n3ds_engine_bitmap_dimensions(environment->base_map.index,bitmap_index,base_size) &&
                n3ds_engine_bitmap_dimensions(environment->detail_map.index,bitmap_index,detail_size);
            if(entry->detail_scale_valid) for(int axis=0;axis<2;++axis)
                entry->detail_scale[axis]*=(float)base_size[axis]/detail_size[axis];
        }
        entry->detail_shader=shader;entry->detail_bitmap_index=bitmap_index;
    }
    if(detail && (!entry->detail_scale_valid || !isfinite(entry->detail_scale[0]) || !isfinite(entry->detail_scale[1]))) detail=0;
    if(!lightmap) n3ds_gpu_texture_bind(1,NULL);
    if(!detail) n3ds_gpu_texture_bind(2,NULL);
    if(lightmap) ++bsp_lightmap_draws;else ++bsp_base_draws;
    if(detail) ++bsp_detail_draws;
    if(detail==3) ++bsp_detail_add_draws;
    if(lightmap && entry->material_color!=0xffffffffu) ++bsp_tinted_draws;
    assert(n3ds_gpu_environment_draw(vertices,entry->light_uv,visible_indices,count,lightmap,detail,detail?entry->detail_scale[0]:1.f,detail?entry->detail_scale[1]:1.f,lightmap?entry->material_color:0xffffffffu,!!(environment->flags&1)));
}
