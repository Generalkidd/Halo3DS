/* Native filtered base/cube sampling, reflection vectors and facing tint.
 * BSP wave coefficients are cached and animated in the vertex program.
 * Model posing/waves and the bounded-cache fallback still use the CPU; Xbox ripple
 * composition, dependent bump reflection and water fog remain approximated.
 * Extended-range tints/cache failures preserve the legacy vertex fallback. */
#include "cseries.h"
#include "engine_models.h"
#include "engine_transparent.h"
#include "rasterizer/rasterizer_geometry.h"
#include "engine_glass.h"
#include "engine_chicago.h"
#include "engine_generic.h"
#include "engine_textures.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "rasterizer/rasterizer.h"
#include "shaders/shader_definitions.h"
struct water_shader {
    struct shader shader;word flags;short type;byte reserved[32];
    struct tag_reference base_map;byte reserved2[16];
    real_argb_color perpendicular,parallel;byte reserved3[16];
    struct tag_reference reflection_map;byte reserved4[16];
    real angle,velocity,scale;struct tag_reference ripple_map;
    short mipmaps,pad;real fade,bias;byte reserved5[64];struct tag_block ripples;
};
_Static_assert(offsetof(struct water_shader,base_map)==0x4c,"Water base ABI");
_Static_assert(offsetof(struct water_shader,perpendicular)==0x6c,"Water tint ABI");
_Static_assert(offsetof(struct water_shader,reflection_map)==0x9c,"Water cube ABI");
_Static_assert(offsetof(struct water_shader,ripples)==0x124,"Water ripple ABI");
extern struct rasterizer_window_begin_parameters global_window_parameters;
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
boolean rasterizer_set_texture_direct(short,long,short);
void n3ds_log(const char *);
static int water_native(const struct water_shader *w,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,unsigned int count,
    short type,const unsigned short *indices,unsigned int index_count)
{
    struct native_water_material m={.flags=w->flags,.sky=!!(p->geometry_flags&(1<<4))};
    /* Vertex-color clamping precedes texture modulation on PICA. Preserve
     * the CPU path for extended-range tints whose saturation order differs. */
    for(int c=0;c<4;++c) {
        m.perpendicular[c]=c==3?w->perpendicular.alpha:w->perpendicular.rgb.n[c];
        m.parallel[c]=c==3?w->parallel.alpha:w->parallel.rgb.n[c];
        if(!(m.perpendicular[c]>=0 && m.perpendicular[c]<=1 && m.parallel[c]>=0 && m.parallel[c]<=1)) return 0;
    }
    if(w->reflection_map.index!=NONE && !(m.cube=n3ds_engine_bitmap_resource(w->reflection_map.index,permutation))) return 0;
    if(w->base_map.index!=NONE && !(m.base=n3ds_engine_bitmap_resource(w->base_map.index,permutation))) return 0;
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    const real_vector2d unit={1,1};
    const struct native_render_vertex *pose=n3ds_model_skin_stream(cached,count,&p->skinning,&unit,p->unique_identifier);
    struct native_render_vertex *out=n3ds_gpu_model_vertices_allocate(count);assert(out);
    float t=global_frame_parameters.game_time_sec*w->velocity;
    float offset_u=t*cosf(w->angle),offset_v=t*sinf(w->angle);
    for(unsigned int i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
        const struct model_vertex_uncompressed *v=cached?cached+i:&decoded;
        if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded));}
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!pose) {assert(n3ds_model_skin_vertex(v,&p->skinning,&unit,&temporary));}
        memcpy(out[i].position,pose?pose[i].position:temporary.position,12);
        real_vector3d normal={0};
        for(int k=0;k<2;++k) if(v->node_weights[k]) {
            assert(v->nodes[k]>=0 && v->nodes[k]<p->skinning.node_matrix_count);
            real_vector3d n;matrix4x3_transform_vector(p->skinning.node_matrices+v->nodes[k],&v->normal,&n);
            for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[k];
        }
        float n2=dot_product3d(&normal,&normal);assert(n2>1e-12f);
        float inverse=1/sqrtf(n2);for(int c=0;c<3;++c) normal.n[c]*=inverse;
        normal.i+=.035f*sinf((v->texcoord.x*w->scale+offset_u)*6.2831853f);
        normal.j+=.035f*cosf((v->texcoord.y*w->scale+offset_v)*6.2831853f);
        memcpy(out[i].color,normal.n,12);
        out[i].uv[0]=v->texcoord.x*p->base_map_scale.i;out[i].uv[1]=v->texcoord.y*p->base_map_scale.j;
    }
    n3ds_gpu_geometry_flush(out,count*sizeof(*out));
    assert(n3ds_gpu_water_draw(out,indices,index_count,&m));
    static unsigned int generation,seen_count;static const void *seen[32];
    if(generation!=n3ds_engine_cache_generation()) {generation=n3ds_engine_cache_generation();seen_count=0;}
    unsigned int i;for(i=0;i<seen_count && seen[i]!=w;++i) {}
    if(i==seen_count && seen_count<32) {
        seen[seen_count++]=w;char text[192];
        snprintf(text,sizeof(text),"NATIVE WATER GPU: flags=%u sky=%d vertices=%u triangles=%u cube=%d passes=%u; native cube/mask/reflection/tint, CPU pose/waves",w->flags,m.sky,count,index_count/3,!!m.cube,1+!!(w->flags&2));n3ds_log(text);
    }
    return 1;
}

void n3ds_water_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,
    unsigned int count,short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct water_shader *w=(const void *)shader;
    assert(n3ds_cache_resolve(view,w,sizeof(*w))==w && w->type==0 && !(w->flags&~15));
    assert(isfinite(w->scale) && isfinite(w->angle) && isfinite(w->velocity));
    if(water_native(w,permutation,p,data,count,type,indices,index_count)) return;
    const byte *cube=w->reflection_map.index!=NONE?n3ds_cube_load(w->reflection_map.index,permutation):NULL;
    struct native_generic_material sample={.maps=1};
    sample.transform[0][0][0]=sample.transform[0][1][1]=1;
    sample.clamp_u[0]=sample.clamp_v[0]=1;
    if(w->base_map.index!=NONE) {
        assert(rasterizer_set_texture_direct(0,w->base_map.index,permutation));
        sample.textures[0]=n3ds_gpu_texture_bound(0);
    }
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    const real_vector2d unit={1,1};
    const struct native_render_vertex *pose=n3ds_model_skin_stream(cached,count,&p->skinning,&unit,p->unique_identifier);
    float t=global_frame_parameters.game_time_sec*w->velocity;
    for(int pass=(w->flags&2)?0:1;pass<2;++pass) {
        struct native_chicago_material material={.maps=1,.blend=pass?0:1,.two_sided=1,.fade=1,.vertex_color=1};
        material.sky=!!(p->geometry_flags&(1<<4));
        struct native_chicago_vertex *out=n3ds_gpu_chicago_allocate(count);assert(out);
        for(unsigned int i=0;i<count;++i) {
            struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
            const struct model_vertex_uncompressed *v=cached?cached+i:&decoded;
            if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded));}
            else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
            if(!pose) {assert(n3ds_model_skin_vertex(v,&p->skinning,&unit,&temporary));}
            const struct native_render_vertex *posed=pose?pose+i:&temporary;
            memcpy(out[i].position,posed->position,12);memset(out[i].uv,0,sizeof(out[i].uv));
            float base[4]={1,1,1,1};
            if(sample.textures[0]) n3ds_gpu_generic_sample(&sample,0,v->texcoord.x*p->base_map_scale.i,v->texcoord.y*p->base_map_scale.j,base);
            if(!pass) {memcpy(out[i].color,base,16);continue;}
            real_vector3d normal={0};float eye[3],reflection[3],n2=0,e2=0,dot=0;
            for(int k=0;k<2;++k) if(v->node_weights[k]) {
                assert(v->nodes[k]>=0 && v->nodes[k]<p->skinning.node_matrix_count);
                real_vector3d n;matrix4x3_transform_vector(p->skinning.node_matrices+v->nodes[k],&v->normal,&n);
                for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[k];
            }
            for(int c=0;c<3;++c) n2+=normal.n[c]*normal.n[c];
            assert(n2>1e-12f);for(int c=0;c<3;++c) normal.n[c]/=sqrtf(n2);
            /* Authored ripple direction/speed/scale drive bounded analytic
             * waves. No extra render targets, texture reads or scene copy. */
            float u=v->texcoord.x*w->scale+t*cosf(w->angle),q=v->texcoord.y*w->scale+t*sinf(w->angle);
            normal.i+=.035f*sinf(u*6.2831853f);normal.j+=.035f*cosf(q*6.2831853f);
            n2=dot_product3d(&normal,&normal);
            for(int c=0;c<3;++c) {
                normal.n[c]/=sqrtf(n2);
                eye[c]=global_window_parameters.camera.position.n[c]-posed->position[c];e2+=eye[c]*eye[c];
            }
            if(e2<1e-12f) for(int c=0;c<3;++c) eye[c]=-global_window_parameters.camera.forward.n[c];
            else for(int c=0;c<3;++c) eye[c]/=sqrtf(e2);
            for(int c=0;c<3;++c) dot+=normal.n[c]*eye[c];
            for(int c=0;c<3;++c) reflection[c]=2*dot*normal.n[c]-eye[c];
            float rgb[4]={1,1,1,1},facing=PIN(fabsf(dot),0.f,1.f);
            if(cube) n3ds_cube_sample(cube,reflection,rgb);
            for(int c=0;c<3;++c) out[i].color[c]=PIN(rgb[c]*(w->parallel.rgb.n[c]+facing*(w->perpendicular.rgb.n[c]-w->parallel.rgb.n[c])),0.f,1.f);
            out[i].color[3]=PIN(w->parallel.alpha+facing*(w->perpendicular.alpha-w->parallel.alpha),0.f,1.f)*((w->flags&1)?base[3]:1);
        }
        assert(n3ds_gpu_chicago_draw(&material,out,count,indices,index_count));
    }
    static unsigned int generation;static const void *seen[32];static unsigned int seen_count;
    if(generation!=n3ds_engine_cache_generation()) {generation=n3ds_engine_cache_generation();seen_count=0;}
    unsigned int i;for(i=0;i<seen_count && seen[i]!=shader;++i) {}
    if(i==seen_count && seen_count<32) {
        seen[seen_count++]=shader;char text[180];
        snprintf(text,sizeof(text),"NATIVE WATER: flags=%u sky=%d vertices=%u triangles=%u cube=%d waves=2",w->flags,!!(p->geometry_flags&(1<<4)),count,index_count/3,!!cube);n3ds_log(text);
    }
}

/* Map surfaces use BSP vertices, not skinned model vertices. Cache only the
 * immutable CPU decode; animated GPU vertices/indices belong to the frame
 * arena and may be reused by the second eye after a serial check. */
#define WATER_BSP_SLOTS 32
#define WATER_BSP_BYTES (128u*1024u)
static struct water_bsp_entry {
    const struct vertex_buffer *source;
    struct native_water_wave_vertex *base;
    struct native_render_vertex *animated;
    float wave_scale;
    const struct shader *animated_shader;
    const unsigned short *indices,*frame_indices;
    unsigned int index_count;
    unsigned long long frame;
} water_bsp[WATER_BSP_SLOTS];
static unsigned int water_bsp_bytes,water_bsp_generation;
static unsigned int water_bsp_draws,water_bsp_reuses,water_bsp_uncached;
void n3ds_water_bsp_release(void)
{
    if(water_bsp_draws || water_bsp_uncached) {
        char text[180];snprintf(text,sizeof(text),"BSP WATER CACHE: draws=%u stereo_reuses=%u uncached_draws=%u decode_bytes=%u",water_bsp_draws,water_bsp_reuses,water_bsp_uncached,water_bsp_bytes);n3ds_log(text);
    }
    for(unsigned int i=0;i<WATER_BSP_SLOTS;++i) free(water_bsp[i].base);
    memset(water_bsp,0,sizeof(water_bsp));water_bsp_bytes=water_bsp_generation=0;
    water_bsp_draws=water_bsp_reuses=water_bsp_uncached=0;
}
static void water_bsp_decode(const struct vertex_buffer *source,unsigned int i,struct native_render_vertex *v)
{
    const byte *p=(const byte *)source->base_address+i*(source->type==1?32:56);
    memcpy(v->position,p,12);memcpy(v->uv,p+(source->type==1?24:48),8);
    real_vector3d n;
    if(source->type==1) {unsigned long packed;memcpy(&packed,p+12,4);n=uncompress_int32_to_real_vector3d(packed);}
    else memcpy(n.n,p+12,12);
    float length=dot_product3d(&n,&n);assert(isfinite(length) && length>1e-12f);
    float inverse=1/sqrtf(length);
    for(int c=0;c<3;++c) {assert(isfinite(v->position[c]));v->color[c]=n.n[c]*inverse;}
    assert(isfinite(v->uv[0]) && isfinite(v->uv[1]));
}
void n3ds_water_bsp_draw(const struct transparent_geometry_group *group)
{
    const struct vertex_buffer *source=group->vertex_buffer;
    const struct triangle_buffer *triangles=group->triangle_buffer;
    const struct n3ds_cache_view *bsp=n3ds_engine_cache_view(1);
    const struct water_shader *w=(const void *)group->shader;
    assert(n3ds_cache_resolve(n3ds_engine_cache_view(0),w,sizeof(*w))==w && w->type==0 && !(w->flags&~15));
    assert(isfinite(w->scale) && isfinite(w->angle) && isfinite(w->velocity));
    assert(source && n3ds_cache_resolve(bsp,source,sizeof(*source))==source && source->type>=0 && source->type<=1);
    assert(source->count>0 && source->count<=65535 && source->base_address);
    unsigned int count=source->count,bytes=count*(source->type==1?32:56);
    assert(n3ds_cache_resolve(bsp,source->base_address,bytes)==source->base_address);
    assert(triangles && triangles->type==0 && triangles->count>0 && triangles->count<=0x7fffffff/6);
    unsigned int index_count=triangles->count*3;const unsigned short *indices=triangles->base_address;
    assert(indices && n3ds_cache_resolve(bsp,indices,index_count*2)==indices);
    if(water_bsp_generation!=n3ds_engine_cache_generation()) {n3ds_water_bsp_release();water_bsp_generation=n3ds_engine_cache_generation();}
    struct native_water_material material={.flags=w->flags};
    for(int c=0;c<4;++c) {
        material.perpendicular[c]=c==3?w->perpendicular.alpha:w->perpendicular.rgb.n[c];
        material.parallel[c]=c==3?w->parallel.alpha:w->parallel.rgb.n[c];
        assert(material.perpendicular[c]>=0 && material.perpendicular[c]<=1 && material.parallel[c]>=0 && material.parallel[c]<=1);
    }
    if(w->reflection_map.index!=NONE && !(material.cube=n3ds_engine_bitmap_resource(w->reflection_map.index,group->shader_permutation_index))) return;
    if(w->base_map.index!=NONE && !(material.base=n3ds_engine_bitmap_resource(w->base_map.index,group->shader_permutation_index))) return;
    struct water_bsp_entry *entry=NULL,*empty=NULL;
    for(unsigned int i=0;i<WATER_BSP_SLOTS;++i) {
        if(water_bsp[i].source==source) {entry=water_bsp+i;break;}
        if(!water_bsp[i].source && !empty) empty=water_bsp+i;
    }
    if(!entry) {
        for(unsigned int i=0;i<index_count;++i) assert(indices[i]<count);
        if(empty && count*sizeof(struct native_water_wave_vertex)<=WATER_BSP_BYTES-water_bsp_bytes) {
            empty->base=malloc(count*sizeof(*empty->base));
            if(empty->base) {
                for(unsigned int i=0;i<count;++i) water_bsp_decode(source,i,&empty->base[i].vertex);
                empty->wave_scale=NAN;empty->source=source;entry=empty;water_bsp_bytes+=count*sizeof(*entry->base);
            }
        }
        if(entry || !water_bsp_uncached) {
            char text[192];snprintf(text,sizeof(text),"BSP WATER GPU: vertices=%u triangles=%u flags=%u passes=%u gpu_waves=%d bytes=%u",count,index_count/3,w->flags,1+!!(w->flags&2),!!entry,water_bsp_bytes);n3ds_log(text);
        }
    }
    float phase[4];
    if(entry) {
        if(entry->wave_scale!=w->scale) {
            for(unsigned int i=0;i<count;++i) {
                float u=entry->base[i].vertex.uv[0]*w->scale*6.2831853f;
                float v=entry->base[i].vertex.uv[1]*w->scale*6.2831853f;
                entry->base[i].wave[0]=.035f*sinf(u);entry->base[i].wave[1]=.035f*cosf(u);
                entry->base[i].wave[2]=.035f*cosf(v);entry->base[i].wave[3]=.035f*sinf(v);
            }
            entry->wave_scale=w->scale;entry->frame=0;
        }
        float t=global_frame_parameters.game_time_sec*w->velocity;
        float u=t*cosf(w->angle)*6.2831853f,v=t*sinf(w->angle)*6.2831853f;
        phase[0]=cosf(u);phase[1]=sinf(u);phase[2]=cosf(v);phase[3]=-sinf(v);
        material.wave_phase=phase;
    }
    unsigned long long frame=n3ds_gpu_model_frame_serial();
    struct native_render_vertex *out;
    const unsigned short *gpu_indices=indices;
    if(entry && entry->frame==frame && entry->animated_shader==group->shader && entry->indices==indices && entry->index_count==index_count) {
        out=entry->animated;gpu_indices=entry->frame_indices;++water_bsp_reuses;
    } else {
        unsigned int bytes=count*(entry?sizeof(*entry->base):sizeof(*out));
        out=n3ds_gpu_model_vertices_allocate((bytes+sizeof(*out)-1)/sizeof(*out));assert(out);
        if(entry) memcpy(out,entry->base,bytes);
        else {
            float t=global_frame_parameters.game_time_sec*w->velocity;
            float offset_u=t*cosf(w->angle),offset_v=t*sinf(w->angle);
            for(unsigned int i=0;i<count;++i) {
                water_bsp_decode(source,i,out+i);
                out[i].color[0]+=.035f*sinf((out[i].uv[0]*w->scale+offset_u)*6.2831853f);
                out[i].color[1]+=.035f*cosf((out[i].uv[1]*w->scale+offset_v)*6.2831853f);
            }
        }
        n3ds_gpu_geometry_flush(out,bytes);
        gpu_indices=n3ds_gpu_indices_upload(indices,index_count);assert(gpu_indices);
        if(entry) {
            entry->frame=frame;entry->animated=out;entry->animated_shader=group->shader;
            entry->frame_indices=gpu_indices;entry->indices=indices;entry->index_count=index_count;
        } else ++water_bsp_uncached;
    }
    assert(n3ds_gpu_water_draw(out,gpu_indices,index_count,&material));++water_bsp_draws;
}
