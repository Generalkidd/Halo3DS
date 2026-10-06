/* Native sorted queue consumption: model materials, original widget callbacks
 * and primary-map dynamic unlit effects. Unsupported variants fail explicitly. */
#include "cseries.h"
#include "engine_transparent.h"
#include "engine_renderer.h"
#include "engine_text.h"
#include "engine_widgets.h"
#include "engine_chicago.h"
#include "engine_models.h"
#include "shaders/shaders.h"
#include "engine_dynamic_geometry.h"
#include "shaders/shader_definitions.h"
#include "real_math.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_transparent_geometry.h"
#include "rasterizer/rasterizer_memory_pool.h"
#include "rasterizer/rasterizer_debug_options.h"
static boolean initialized, drawing;
static boolean saved_zsprites, flat_particle_reported;
extern struct rasterizer_window_begin_parameters global_window_parameters;
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
boolean rasterizer_set_texture_direct(short,long,short);
boolean rasterizer_set_texture_bitmap_data(short,const struct bitmap_data *);
void n3ds_log(const char *);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
static long long material_ticks[12];
static unsigned int material_calls[12], material_frames;
enum { EFFECT_BATCH_VERTICES=384 };
/* Groups are consumed synchronously; the GPU copies this bounded staging area
 * into its frame-owned arena before it returns. Never reorder sorted groups. */
static struct native_widget_vertex effect_vertices[EFFECT_BATCH_VERTICES];
static struct native_packed_effect_vertex effect_packed[EFFECT_BATCH_VERTICES];
static float pending_rows[12];
static unsigned short packed_indices[EFFECT_BATCH_VERTICES*3/2];
static unsigned int pending_index_count,packed_vertices_saved;
static unsigned short effect_indices[EFFECT_BATCH_VERTICES];
static unsigned int effect_groups,effect_batches,effect_triangles;
static unsigned int effect_converted,effect_reused,effect_index_reused;
static int effect_primary_key_tests(void);
struct unlit_vertex {real_point3d point;real_point2d uv;unsigned long color;};
typedef char unlit_size[sizeof(struct unlit_vertex)==24?1:-1];
/* Original negative handles encode sequential primitives, not buffer IDs.
 * Lightning uses -vertex_count for one triangle strip; sprites use -4. */
static int effect_sequential_indices(long handle,unsigned int vertices,unsigned int triangles,
    unsigned int first,unsigned int count,unsigned short *indices)
{
    if(!indices || !triangles || !count || count%3 || count>EFFECT_BATCH_VERTICES ||
       first>triangles || count/3>triangles-first || handle>=-2 || handle< -65536) return 0;
    unsigned int primitive=(unsigned int)-handle;
    if(primitive==3) {if(triangles>vertices/3 || triangles>65536/3) return 0;}
    else if(primitive<=4 || primitive>vertices || triangles!=primitive-2) return 0;
    for(unsigned int i=0;i<count;i+=3) {
        unsigned int triangle=first+i/3;
        if(primitive==3) for(unsigned int j=0;j<3;++j) indices[i+j]=triangle*3+j;
        else {
            indices[i]=triangle+(triangle&1);indices[i+1]=triangle+!(triangle&1);indices[i+2]=triangle+2;
        }
    }
    return 1;
}
static int effect_sequential_tests(void)
{
    unsigned short indices[EFFECT_BATCH_VERTICES+2];
    const unsigned short strip[]={0,1,2,2,1,3,2,3,4,4,3,5};
    const unsigned short boundary[]={128,127,129,128,129,130};
    const unsigned short list[]={3,4,5,6,7,8};
    memset(indices,0x5a,sizeof(indices));
    if(!effect_sequential_indices(-6,6,4,0,12,indices+1) || memcmp(indices+1,strip,sizeof(strip)) ||
       indices[0]!=0x5a5a || indices[13]!=0x5a5a) return 0;
    if(!effect_sequential_indices(-132,132,130,127,6,indices) || memcmp(indices,boundary,sizeof(boundary))) return 0;
    if(!effect_sequential_indices(-18,18,16,0,48,indices) || indices[45]!=16 || indices[46]!=15 || indices[47]!=17) return 0;
    if(!effect_sequential_indices(-3,9,3,1,6,indices) || memcmp(indices,list,sizeof(list))) return 0;
    if(effect_sequential_indices(-18,17,16,0,48,indices) || effect_sequential_indices(-18,18,15,0,45,indices) ||
       effect_sequential_indices(-18,18,16,15,6,indices) || effect_sequential_indices(-3,8,3,0,9,indices) ||
       effect_sequential_indices(-2,18,16,0,3,indices) || effect_sequential_indices(-4,18,16,0,3,indices) ||
       effect_sequential_indices(-18,18,16,0,2,indices) || effect_sequential_indices(-18,18,16,0,0,indices)) return 0;
    n3ds_log("PASS: sequential effects: lightning strip winding, batch boundaries, triangle lists and invalid bounds");
    return 1;
}
static int effect_convert(const struct unlit_vertex *source,unsigned int source_count,
    const unsigned short *indices,unsigned int first_triangle,unsigned int count,
    const real_matrix4x3 *transform,struct native_widget_vertex *output)
{
    static const unsigned int quad[6]={0,1,2,0,2,3};unsigned int refs[6];
    /* Beam strips repeatedly reference the preceding two vertices. This
     * bounded cache also benefits indexed effect meshes, with exact colors
     * and transforms. Tags are local to this conversion, never reused across
     * sources or stereo views. */
    static struct native_widget_vertex indexed_cache[64];
    unsigned int indexed_tags[64];
    if(!source || !output || !count || count%3 || count>EFFECT_BATCH_VERTICES) return 0;
    if(!indices && ((first_triangle&1) || count%6 || first_triangle/2>source_count/4 || count/6>source_count/4-first_triangle/2)) return 0;
    if(indices) memset(indexed_tags,0,sizeof(indexed_tags));
    for(unsigned int i=0;i<count;++i) {
        unsigned int slot=i%6,index=indices?indices[i]:(first_triangle/2+i/6)*4+quad[slot];
        if(index>=source_count) return 0;
        refs[slot]=index;
        /* Two triangles share corners0/2. Indexed groups use the shortcut
         * only when their actual indices match, including arbitrary meshes. */
        if(slot==3 && index==refs[0]) {output[i]=output[i-3];++effect_reused;continue;}
        if(slot==4 && index==refs[2]) {output[i]=output[i-2];++effect_reused;continue;}
        unsigned int cache_slot=index&63;
        if(indices && indexed_tags[cache_slot]==index+1) {
            output[i]=indexed_cache[cache_slot];++effect_index_reused;continue;
        }
        real_point3d position=source[index].point;
        if(transform) matrix4x3_transform_point(transform,&position,&position);
        memcpy(output[i].position,position.n,12);memcpy(output[i].uv,source[index].uv.n,8);
        unsigned long color=source[index].color;
        output[i].color[0]=((color>>16)&255)/255.f;output[i].color[1]=((color>>8)&255)/255.f;
        output[i].color[2]=(color&255)/255.f;output[i].color[3]=(color>>24)/255.f;
        if(indices) {indexed_cache[cache_slot]=output[i];indexed_tags[cache_slot]=index+1;}
        ++effect_converted;
    }
    return 1;
}
int n3ds_effect_vertex_reuse_tests(void)
{
    struct unlit_vertex source[260];struct native_widget_vertex actual[386],expected[384];
    unsigned short indices[384];unsigned int compared=0,cases=0,state=0x91bda23u;
    real_matrix4x3 transform=*global_identity4x3;
    transform.scale=1.5f;transform.position.x=.2f;transform.position.y=-.4f;transform.position.z=2;
    transform.forward.i=0;transform.forward.j=1;transform.left.i=-1;transform.left.j=0;
    for(unsigned int i=0;i<260;++i) {
        for(int j=0;j<3;++j) source[i].point.n[j]=((int)(i*13+j*73)%101-50)/17.f;
        source[i].uv.x=i/31.f;source[i].uv.y=-(real)i/71.f;
        state=state*1664525u+1013904223u;source[i].color=state;
    }
    for(unsigned int mode=0;mode<4;++mode) for(unsigned int rotated=0;rotated<2;++rotated) for(unsigned int test=0;test<4;++test) {
        static const unsigned int counts[4]={6,384,6,12},starts[4]={0,0,128,126},quad[6]={0,1,2,0,2,3};
        unsigned int count=counts[test],first=starts[test],before=effect_reused;
        for(unsigned int i=0;i<count;++i) {
            unsigned int triangle=first+i/3,corner=i%3;
            indices[i]=mode==2?(i*37+11)%260:mode==3?
                triangle+(corner==2?2:corner==0?(triangle&1):!(triangle&1)):
                (first/2+i/6)*4+quad[i%6];
        }
        /* Deliberately arbitrary indexed triangles cannot reuse these corners. */
        memset(actual,0x5a,sizeof(actual));
        if(!effect_convert(source,260,mode?indices:NULL,first,count,rotated?&transform:NULL,actual+1)) return 0;
        if(mode<3 && effect_reused-before!=(mode==2?0:count/3)) return 0;
        for(unsigned int i=0;i<count;++i) {
            unsigned int index=mode?indices[i]:(first/2+i/6)*4+quad[i%6];
            real_point3d position=source[index].point;
            if(rotated) matrix4x3_transform_point(&transform,&position,&position);
            memcpy(expected[i].position,position.n,12);memcpy(expected[i].uv,source[index].uv.n,8);
            unsigned long color=source[index].color;
            expected[i].color[0]=((color>>16)&255)/255.f;expected[i].color[1]=((color>>8)&255)/255.f;
            expected[i].color[2]=(color&255)/255.f;expected[i].color[3]=(color>>24)/255.f;
        }
        if(memcmp(actual+1,expected,count*sizeof(*expected))) return 0;
        for(unsigned int i=0;i<sizeof(*actual);++i) if(((byte *)actual)[i]!=0x5a || ((byte *)(actual+count+1))[i]!=0x5a) return 0;
        compared+=count;++cases;
    }
    if(effect_convert(NULL,260,NULL,0,6,NULL,actual) || effect_convert(source,260,NULL,0,6,NULL,NULL) ||
       effect_convert(source,260,NULL,0,0,NULL,actual) || effect_convert(source,260,NULL,0,390,NULL,actual) ||
       effect_convert(source,260,NULL,1,6,NULL,actual) || effect_convert(source,260,NULL,0,3,NULL,actual) ||
       effect_convert(source,3,NULL,0,6,NULL,actual)) return 0;
    indices[0]=260;if(effect_convert(source,260,indices,0,6,NULL,actual)) return 0;
    char message[192];snprintf(message,sizeof(message),"PASS: effect vertex reuse: %u exact vertices in %u indexed/quad/transform/batch-boundary cases, 8 rejected inputs",compared,cases);n3ds_log(message);
    effect_converted=effect_reused=effect_index_reused=0;return effect_sequential_tests() && effect_primary_key_tests();
}
/* Coalesce only consecutive, already sorted primary-map sprite groups.
 * Copy their vertices immediately; queued dynamic buffers can then retire.
 * No sorting, shader approximation or cross-material reordering is allowed. */
static const struct bitmap_data *pending_bitmap;
static unsigned int pending_count, pending_shader, pending_sampler, pending_geometry;
static int primary_state_active;
static int pending_blend;
static unsigned int effect_merged;
static void effect_flush(void)
{
    if(!pending_count) return;
    assert(rasterizer_set_texture_bitmap_data(0,pending_bitmap));
    assert(n3ds_gpu_effect_packed_indexed_draw(effect_packed,pending_count,packed_indices,pending_index_count,pending_blend,
        pending_shader,pending_sampler,pending_geometry,(pending_geometry&(1u<<5))?pending_rows:NULL));
    ++effect_batches;pending_count=pending_index_count=0;primary_state_active=1;
}
static void effect_close(void)
{
    effect_flush();
    if(primary_state_active) {n3ds_gpu_world_state_restore();primary_state_active=0;}
}
static int effect_primary_compatible(const struct transparent_geometry_group *group)
{
    const struct shader_effect_definition *s=(const void *)group->shader;
    return pending_bitmap==group->lightmap && pending_blend==s->framebuffer_blend_function &&
        pending_shader==s->flags && pending_sampler==s->primary_map_flags &&
        pending_geometry==group->geometry_flags;
}
static int effect_primary_key_tests(void)
{
    struct shader_effect_definition shader={0};
    struct transparent_geometry_group group={0};
    group.shader=(struct shader *)&shader;
    pending_bitmap=group.lightmap=NULL;pending_blend=0;
    pending_shader=pending_sampler=pending_geometry=0;
    if(!effect_primary_compatible(&group)) return 0;
    group.geometry_flags=128;if(effect_primary_compatible(&group)) return 0;group.geometry_flags=0;
    shader.flags=2;if(effect_primary_compatible(&group)) return 0;shader.flags=0;
    shader.primary_map_flags=1;if(effect_primary_compatible(&group)) return 0;shader.primary_map_flags=0;
    shader.framebuffer_blend_function=3;if(effect_primary_compatible(&group)) return 0;shader.framebuffer_blend_function=0;
    group.lightmap=(void *)&shader;if(effect_primary_compatible(&group)) return 0;group.lightmap=NULL;
    /* Fade/radius are already baked into vertices or inactive depth shaping. */
    shader.framebuffer_fade_mode=1;shader.secondary_map_radius=10;
    if(!effect_primary_compatible(&group)) return 0;
    n3ds_log("PASS: adjacent effect keys: texture/blend/tint/sampler/first-person boundaries retained");
    return 1;
}
static void effect_primary_append(const struct transparent_geometry_group *group,
    const struct native_vertex_view *view)
{
    const struct shader_effect_definition *s=(const void *)group->shader;
    assert(group->triangle_count>0 && group->triangle_count<=131072);
    int quad=group->dynamic_triangle_buffer_index==-4;
    int sequential=group->dynamic_triangle_buffer_index<0 && !quad;
    const struct native_triangle_view *triangles=NULL;
    if(quad) {assert(!(group->triangle_count&1) && (unsigned long)group->triangle_count*2<=view->count);}
    else if(!sequential) {triangles=n3ds_dynamic_triangles_get(group->dynamic_triangle_buffer_index);assert(triangles && (unsigned long)group->triangle_count<=triangles->count);}
    if(pending_count && !effect_primary_compatible(group)) effect_flush();
    else if(pending_count) ++effect_merged;
    pending_bitmap=group->lightmap;pending_blend=s->framebuffer_blend_function;
    pending_shader=s->flags;pending_sampler=s->primary_map_flags;pending_geometry=group->geometry_flags;
    if(group->geometry_flags&(1u<<5)) {
        const real_matrix4x3 *m=&global_window_parameters.frustum.view_to_world;
        for(int r=0;r<3;++r) {
            pending_rows[r*4]=m->scale*m->forward.n[r];pending_rows[r*4+1]=m->scale*m->left.n[r];
            pending_rows[r*4+2]=m->scale*m->up.n[r];pending_rows[r*4+3]=m->position.n[r];
        }
    }
    ++effect_groups;
    for(unsigned int triangle=0;triangle<(unsigned long)group->triangle_count;) {
        unsigned int count=quad?MIN((EFFECT_BATCH_VERTICES-pending_count)/4,
            ((unsigned long)group->triangle_count-triangle)/2)*6:
            MIN((EFFECT_BATCH_VERTICES-pending_count)/3,(unsigned long)group->triangle_count-triangle)*3;
        count=MIN(count,(unsigned int)(EFFECT_BATCH_VERTICES*3/2-pending_index_count));
        count-=count%(quad?6:3);
        if(!count) {effect_flush();continue;}
        const struct unlit_vertex *source=view->data;
        static const unsigned int corners[6]={0,1,2,0,2,3};
        unsigned int n=quad?count/6*4:count;
        if(quad) {
            unsigned int first=triangle/2*4;
            assert(first<=view->count && n<=view->count-first);
            memcpy(effect_packed+pending_count,source+first,n*sizeof(*effect_packed));
            for(unsigned int i=0;i<count;++i) packed_indices[pending_index_count+i]=pending_count+i/6*4+corners[i%6];
            packed_vertices_saved+=count-n;
        } else {
            if(sequential) assert(effect_sequential_indices(group->dynamic_triangle_buffer_index,view->count,group->triangle_count,triangle,count,effect_indices));
            const unsigned short *idx=triangles?triangles->indices+triangle*3:effect_indices;
            for(unsigned int i=0;i<count;++i) {
                assert(idx[i]<view->count);memcpy(effect_packed+pending_count+i,source+idx[i],sizeof(*effect_packed));
                packed_indices[pending_index_count+i]=pending_count+i;
            }
        }
        pending_count+=n;pending_index_count+=count;triangle+=count/3;effect_triangles+=count/3;
        if(pending_count==EFFECT_BATCH_VERTICES) effect_flush();
    }
}
static void effect_draw(const struct transparent_geometry_group *group)
{
    extern int n3ds_gpu_diagnostic_mode(void);if(n3ds_gpu_diagnostic_mode()==2) return;
    const struct shader_effect_definition *shader=(const void *)group->shader;
    const struct native_vertex_view *view=n3ds_dynamic_vertices_get(group->dynamic_vertex_buffer_index);
    const struct native_triangle_view *triangles=NULL;
    struct native_widget_vertex *output=effect_vertices;
    static unsigned int seen;
    unsigned int signature=(shader->flags&7)|((shader->framebuffer_blend_function&7)<<3);
    if(!(seen&(1u<<(signature%32)))) {
        char message[192];snprintf(message,sizeof(message),"NATIVE EFFECT: blend=%d flags=%04x sampler=%04x geometry=%08lx secondary=%08lx triangles=%ld",
            shader->framebuffer_blend_function,shader->flags,shader->primary_map_flags,group->geometry_flags,shader->secondary_map.index,group->triangle_count);
        n3ds_log(message);seen|=1u<<(signature%32);
    }
    assert(group->effect.type==0 && !group->first_triangle_index && view && view->type==6);
    assert(!(group->geometry_flags&((1u<<4)|(1u<<8))));
    /* Anchor 2 is Xbox depth shaping, not an ordinary secondary UV layer.
     * Original rendering leaves it inactive for first-person effects or when
     * zsprites is disabled. Use that original flat-particle fidelity option;
     * still draw every primary particle and retain depth/weapon-mask tests. */
    boolean inactive_depth_map=shader->secondary_map_anchor==2 &&
        (!rasterizer_debug_options.zsprites || (group->geometry_flags&(1u<<7)));
    boolean secondary=shader->secondary_map.index!=NONE && !inactive_depth_map;
    boolean supported_secondary_blend=shader->framebuffer_blend_function==NATIVE_BLEND_ADD ||
        shader->framebuffer_blend_function==NATIVE_BLEND_SUBTRACT || shader->framebuffer_blend_function==NATIVE_BLEND_MAX;
    if(secondary && (shader->secondary_map_anchor<0 || shader->secondary_map_anchor>1 || !supported_secondary_blend || (shader->flags&2))) {
        char message[192];snprintf(message,sizeof(message),"NATIVE EFFECT SECONDARY: bitmap=%08lx anchor=%d sampler=%04x blend=%d shader_flags=%04x geometry=%08lx",
            shader->secondary_map.index,shader->secondary_map_anchor,shader->secondary_map_flags,shader->framebuffer_blend_function,shader->flags,group->geometry_flags);n3ds_log(message);
    }
    assert(!secondary || (shader->secondary_map_anchor>=0 && shader->secondary_map_anchor<=1 && supported_secondary_blend && !(shader->flags&2)));
    if(inactive_depth_map && shader->secondary_map.index!=NONE && !flat_particle_reported) {
        n3ds_log("NATIVE EFFECT: original flat-particle profile; anchor2 depth shaping inactive, primary particle/depth/weapon overlap retained");
        flat_particle_reported=TRUE;
    }
    if(!secondary &&
       shader->framebuffer_blend_function!=NATIVE_BLEND_MIN && shader->framebuffer_blend_function!=NATIVE_BLEND_MAX) {
        effect_primary_append(group,view);return;
    }
    effect_close();
    assert(group->triangle_count>0 && group->triangle_count<=131072);
    assert(rasterizer_set_texture_bitmap_data(0,group->lightmap));
    real_vector4d secondary_transform[2];
    if(secondary) {
        assert(rasterizer_set_texture_direct(1,shader->secondary_map.index,group->shader_permutation_index));
        shader_texture_animation_evaluate(&shader->secondary_map_animation,group->animation,
            group->model_base_map_scale.i,group->model_base_map_scale.j,0,0,0,
            global_frame_parameters.game_time_sec,secondary_transform,secondary_transform+1);
    }
    boolean sequential=group->dynamic_triangle_buffer_index<0 && group->dynamic_triangle_buffer_index!=-4;
    if(group->dynamic_triangle_buffer_index>=0) {
        triangles=n3ds_dynamic_triangles_get(group->dynamic_triangle_buffer_index);
        assert(triangles && (unsigned long)group->triangle_count<=triangles->count);
    } else if(!sequential) assert(!(group->triangle_count&1) && (unsigned long)group->triangle_count*2<=view->count);
    const struct unlit_vertex *source=view->data;
    ++effect_groups;
    for(unsigned int triangle=0;triangle<(unsigned long)group->triangle_count;) {
        unsigned int count=MIN(EFFECT_BATCH_VERTICES/3,(unsigned long)group->triangle_count-triangle)*3;
        if(sequential) {
            assert(effect_sequential_indices(group->dynamic_triangle_buffer_index,view->count,group->triangle_count,triangle,count,effect_indices));
            static boolean reported;
            if(!reported) {char message[128];snprintf(message,sizeof(message),"NATIVE SEQUENTIAL EFFECT: handle=%ld vertices=%u triangles=%ld",group->dynamic_triangle_buffer_index,view->count,group->triangle_count);n3ds_log(message);reported=TRUE;}
        }
        assert(effect_convert(source,view->count,triangles?triangles->indices+triangle*3:sequential?effect_indices:NULL,triangle,count,
            (group->geometry_flags&(1u<<5))?&global_window_parameters.frustum.view_to_world:NULL,output));
        if(secondary) {
            struct native_chicago_vertex *layer=n3ds_gpu_chicago_allocate(count);assert(layer);
            for(unsigned int i=0;i<count;++i) {
                float coordinates[4]={output[i].uv[0],output[i].uv[1],0,1};
                if(shader->secondary_map_anchor==1) {
                    real_point3d world,viewpoint;memcpy(world.n,output[i].position,sizeof(world));
                    matrix4x3_transform_point(&global_window_parameters.frustum.world_to_view,&world,&viewpoint);
                    const real (*projection)[4]=global_window_parameters.frustum.projection_matrix;
                    for(int c=0;c<4;++c) coordinates[c]=viewpoint.x*projection[0][c]+viewpoint.y*projection[1][c]+viewpoint.z*projection[2][c]+projection[3][c];
                    /* Xbox effect program12 explicitly copies clip.w to z. */
                    coordinates[2]=coordinates[3];
                }
                memcpy(layer[i].position,output[i].position,sizeof(layer[i].position));
                memcpy(layer[i].color,output[i].color,sizeof(layer[i].color));
                memcpy(layer[i].uv[1],output[i].uv,sizeof(output[i].uv));
                for(int a=0;a<2;++a) {
                    layer[i].uv[0][a]=0;
                    for(int c=0;c<4;++c) layer[i].uv[0][a]+=coordinates[c]*secondary_transform[a].n[c];
                }
                layer[i].uv[2][0]=coordinates[3];layer[i].uv[2][1]=0;
            }
            assert(n3ds_gpu_effect_layer_draw(layer,count,shader->framebuffer_blend_function,shader->primary_map_flags,shader->secondary_map_flags,group->geometry_flags));
        } else assert(n3ds_gpu_effect_draw(output,count,shader->framebuffer_blend_function,shader->flags,shader->primary_map_flags,group->geometry_flags));
        ++effect_batches;effect_triangles+=count/3;triangle+=count/3;
    }
    n3ds_gpu_world_state_restore();
}
boolean rasterizer_transparent_geometry_initialize_aux_buffer(void)
{
    assert(!initialized);
    /* PICA does not use Xbox's synthesized texcoord stream. The native queue
     * instead owns the original pose/lighting copy pool used by submissions. */
    initialized=rasterizer_memory_pool_initialize();
    if(initialized) {
        effect_groups=effect_batches=effect_triangles=effect_merged=pending_count=0;
        effect_converted=effect_reused=effect_index_reused=packed_vertices_saved=pending_index_count=0;
        saved_zsprites=rasterizer_debug_options.zsprites;
        rasterizer_debug_options.zsprites=FALSE;
        flat_particle_reported=FALSE;
    }
    return initialized;
}
void rasterizer_transparent_geometry_dispose_aux_buffer(void)
{
    assert(!drawing);
    if(initialized) {
        char message[160];snprintf(message,sizeof(message),"EFFECT BATCHES: groups=%u draws=%u triangles=%u capacity=%u merged=%u",effect_groups,effect_batches,effect_triangles,EFFECT_BATCH_VERTICES,effect_merged);
        n3ds_log(message);
        snprintf(message,sizeof(message),"EFFECT VERTEX REUSE: converted=%u reused=%u indexed=%u",effect_converted,effect_reused,effect_index_reused);n3ds_log(message);
        snprintf(message,sizeof(message),"EFFECT GPU OFFLOAD: duplicate_vertices_avoided=%u",packed_vertices_saved);n3ds_log(message);
        rasterizer_memory_pool_dispose();
        rasterizer_debug_options.zsprites=saved_zsprites;
    }
    initialized=FALSE;
}
void n3ds_transparent_begin_view(void)
{
    assert(initialized && !drawing);
    rasterizer_memory_pool_begin();
    rasterizer_transparent_geometry_begin();
}
void rasterizer_transparent_geometry_groups_begin(void)
{
    assert(initialized && !drawing && n3ds_engine_window_active());
    n3ds_gpu_world_state_restore();
    primary_state_active=0;
    drawing=TRUE;
}
void rasterizer_transparent_geometry_groups_end(void)
{
    assert(drawing);
    effect_flush();
    n3ds_gpu_world_state_restore();
    primary_state_active=0;
    drawing=FALSE;
}
void rasterizer_transparent_geometry_group_draw(struct transparent_geometry_group *group, boolean dirty)
{
    (void)dirty; /* The native path reinstalls complete base-material state. */
    assert(drawing && group);
    if(!rasterizer_transparent_geometry_get_group_pending_status(group)) return;
    unsigned int bucket=group->shader?(unsigned int)group->shader->base.type:0;
    if(bucket>=12) bucket=11;
    long long begin=n3ds_engine_ticks();
    if(!group->shader || group->shader->base.type!=1) effect_close();
    if(!group->shader) {
        /* Original widget submissions intentionally have no material. Their
         * callback aliases the triangle-buffer field, with the object/widget
         * arguments in the two triangle-count fields. Preserve sorted order. */
        assert(group->render_proc && group->dynamic_triangle_buffer_index==NONE);
        group->render_proc(group->first_triangle_index,group->triangle_count);
        n3ds_gpu_world_state_restore();
    } else if(group->shader->base.type==1) effect_draw(group);
    else if(group->shader->base.type==7 && group->vertex_buffer && group->vertex_buffer->type<=1)
        n3ds_water_bsp_draw(group);
    else n3ds_model_draw_group(group);
    rasterizer_transparent_geometry_set_group_pending_status(group,FALSE);
    material_ticks[bucket]+=n3ds_engine_ticks()-begin;
    ++material_calls[bucket];
}
void n3ds_transparent_draw_queued(void)
{
    struct transparent_geometry_group *group;
    long last_source=0;short last_effect=0;
    rasterizer_sort_external();
    rasterizer_transparent_geometry_groups_begin();
    group=rasterizer_transparent_geometry_first_group();
    for(;group;group=rasterizer_transparent_geometry_next_group(group)) {
        if(group->effect.type==2 && (last_source!=group->source_object_index || last_effect!=2)) {
            /* Original Cortana/self-occluding transparency: establish the
             * nearest posed surface of this contiguous object group first,
             * then shade its original transparent materials against that Z. */
            effect_flush();
            for(struct transparent_geometry_group *part=group;part && part->effect.type==2 && part->source_object_index==group->source_object_index;part=rasterizer_transparent_geometry_next_group(part))
                if(!shader_ignores_effect(part->shader)) n3ds_model_depth_group(part);
            static int reported;if(!reported) {n3ds_log("NATIVE MODEL: original self-occluding transparent depth prepass");reported=1;}
        }
        rasterizer_transparent_geometry_group_draw(group,FALSE);
        last_source=group->source_object_index;last_effect=group->effect.type;
    }
    long long flush_stamp=n3ds_engine_ticks();
    rasterizer_transparent_geometry_groups_end();
    material_ticks[1]+=n3ds_engine_ticks()-flush_stamp;
    if(++material_frames%30==0) {
        double scale=1000.0/(30.0*n3ds_engine_tick_frequency());
        for(unsigned int i=0;i<12;++i) if(material_calls[i]) {
            char text[160];
            snprintf(text,sizeof(text),"MATERIAL PROFILE: frame=%u type=%u calls=%u ms=%.3f",material_frames,i,material_calls[i],material_ticks[i]*scale);
            n3ds_log(text);
        }
        memset(material_ticks,0,sizeof(material_ticks));
        memset(material_calls,0,sizeof(material_calls));
    }
}
