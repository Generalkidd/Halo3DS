/* Plasma shields: authored animated volume noise, runtime intensity/tint and
 * view-dependent colors. PICA has no Xbox volume-texture combiner equivalent;
 * sample the volumes at model vertices and interpolate the additive result.
 * Two bounded 64 KiB slots are shared across all shields, with no frame heap. */
#include "cseries.h"
#include "engine_models.h"
#include "engine_glass.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "engine_textures.h"
#include "engine_bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_runtime.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "shaders/shader_definitions.h"
struct plasma_shader {
    struct shader shader;byte reserved[4];short intensity_source,pad0;real intensity_exponent;
    short offset_source,pad1;real offset_amount,offset_exponent;byte reserved1[32];
    real_argb_color perpendicular,parallel;short color_source;byte reserved2[62];
    real primary_period;real_vector3d primary_direction;real primary_scale;struct tag_reference primary_map;
    byte reserved3[36];
    real secondary_period;real_vector3d secondary_direction;real secondary_scale;struct tag_reference secondary_map;
    byte reserved4[32];
};
_Static_assert(offsetof(struct plasma_shader,primary_period)==0xc0,"Plasma primary ABI");
_Static_assert(offsetof(struct plasma_shader,secondary_map.index)==0x128,"Plasma secondary ABI");
_Static_assert(sizeof(struct plasma_shader)==332,"Plasma ABI");
extern struct rasterizer_window_begin_parameters global_window_parameters;
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
void n3ds_log(const char *);
struct plasma_volume {
    const struct bitmap_data *bitmap;int size[3],stride;long address[3][32];byte pixels[32*32*32*2];
};
static struct plasma_volume volumes[2];
static unsigned int generation,draws;
static const struct shader *reported;
#ifdef HALO_N3DS_PLASMA_SHIELD_TEST
static unsigned int test_draws;
unsigned int n3ds_plasma_test_draws(void) {return test_draws;}
#endif
static const struct plasma_volume *volume_load(long tag,short permutation,const struct plasma_volume *keep)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct bitmap_group *group=bitmap_group_get(tag);
    assert(group && group->bitmaps.count>0 && permutation>=0);
    const struct bitmap_data *b=bitmap_group_try_and_get_bitmap(tag,permutation%group->bitmaps.count);
    for(int i=0;i<2;++i) if(volumes[i].bitmap==b) return volumes+i;
    struct plasma_volume *v=keep==volumes?volumes+1:volumes;
    assert(n3ds_cache_resolve(view,b,sizeof(*b))==b && b->signature=='bitm');
    assert(b->type==1 && (b->flags&0x88)==0x88 && b->format>=0 && b->format<=3);
    v->stride=b->format==3?2:1;unsigned int skip=0,bytes;int level=0;
    v->size[0]=b->width;v->size[1]=b->height;v->size[2]=b->depth;
    for(int c=0;c<3;++c) assert(v->size[c]>0 && v->size[c]<=256 && !(v->size[c]&(v->size[c]-1)));
    while(MAX(v->size[0],MAX(v->size[1],v->size[2]))>32) {
        assert(level++<b->mipmap_count);skip+=v->size[0]*v->size[1]*v->size[2]*v->stride;
        for(int c=0;c<3;++c) v->size[c]=MAX(1,v->size[c]/2);
    }
    bytes=v->size[0]*v->size[1]*v->size[2]*v->stride;
    assert(b->pixels_offset>=2048 && b->pixels_size>0 && skip+bytes<=(unsigned long)b->pixels_size);
    assert(n3ds_engine_cache_read(b->pixels_offset+skip,bytes,v->pixels));
    for(int c=0;c<3;++c) for(int i=0;i<v->size[c];++i) {
        long xyz[3]={0},address[3];xyz[c]=i;
        bitmap_swizzle_vector3d(v->size[0],v->size[1],v->size[2],xyz[0],xyz[1],xyz[2],address);
        v->address[c][i]=address[c];
    }
    v->bitmap=b;
    char message[160];snprintf(message,sizeof(message),"NATIVE PLASMA VOLUME: tag=%08lx size=%d,%d,%d bytes=%u mip=%d",tag,v->size[0],v->size[1],v->size[2],bytes,level);n3ds_log(message);
    return v;
}
static void volume_sample(const struct plasma_volume *v,const float xyz[3],float out[2])
{
    int ix[3];float f[3];
    for(int c=0;c<3;++c) {
        assert(isfinite(xyz[c]));float x=(xyz[c]-floorf(xyz[c]))*v->size[c]-.5f;
        ix[c]=(int)floorf(x);f[c]=x-ix[c];
    }
    out[0]=out[1]=0;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        unsigned int address=v->address[0][(ix[0]+x)&(v->size[0]-1)]|
            v->address[1][(ix[1]+y)&(v->size[1]-1)]|v->address[2][(ix[2]+z)&(v->size[2]-1)];
        unsigned int rgba=n3ds_bitmap_decode_pixel(v->pixels+address*v->stride,v->bitmap->format);
        float weight=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2])/255.f;
        out[0]+=(rgba>>24)*weight;out[1]+=(rgba&255)*weight;
    }
}
static float animation_value(const struct render_animation *a,int source,float exponent,float fallback)
{
    if(!a->values || source<1 || source>4) return fallback;
    float value=a->values[source-1];assert(isfinite(value) && isfinite(exponent));
    /* A depleted shield stays invisible, including malformed negative inputs. */
    if(value<=0) return 0;
    float result=powf(value,exponent);return isfinite(result)?PIN(result,0.f,1.f):0;
}
void n3ds_plasma_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,unsigned int count,
    short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct plasma_shader *s=(const void *)shader;
    assert(shader->base.type==10 && n3ds_cache_resolve(view,s,sizeof(*s))==s);
    if(generation!=n3ds_engine_cache_generation()) {
        volumes[0].bitmap=volumes[1].bitmap=NULL;generation=n3ds_engine_cache_generation();draws=0;reported=NULL;
    }
    float intensity=animation_value(&p->animation,s->intensity_source,s->intensity_exponent,1);
    if(reported!=shader) {
        reported=shader;
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            char message[224];snprintf(message,sizeof(message),"NATIVE PLASMA MATERIAL: intensity=%.3f name=%.160s",intensity,view->instances[t].name);n3ds_log(message);break;
        }
    }
    #ifdef HALO_N3DS_PLASMA_SHIELD_TEST
    /* Opt-in renderer coverage: illuminate the map's submitted shield mesh.
     * This never changes vitality/damage and is absent from release builds. */
    intensity=1;
    ++test_draws;
    #endif
    if(intensity<=0) return;
    float offset=animation_value(&p->animation,s->offset_source,s->offset_exponent,0)*s->offset_amount;
    if(offset<.0005f) offset=0;
    const real_rgb_color *tint=global_real_rgb_white;
    if(p->animation.colors && s->color_source>=1 && s->color_source<=4) tint=p->animation.colors+s->color_source-1;
    const struct plasma_volume *primary=volume_load(s->primary_map.index,permutation,NULL);
    const struct plasma_volume *secondary=volume_load(s->secondary_map.index,permutation,primary);
    assert(isfinite(s->primary_period) && s->primary_period!=0 && isfinite(s->secondary_period) && s->secondary_period!=0);
    float time[2]={global_frame_parameters.game_time_sec/s->primary_period,global_frame_parameters.game_time_sec/s->secondary_period};
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    const real_vector2d unit={1,1};
    const struct native_render_vertex *pose=n3ds_model_skin_stream(cached,count,&p->skinning,&unit,p->unique_identifier);
    struct native_render_vertex *out=n3ds_gpu_model_vertices_allocate(count);assert(out);
    if(pose) memcpy(out,pose,count*sizeof(*out));
    for(unsigned int i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;
        const struct model_vertex_uncompressed *v=cached?cached+i:&decoded;
        if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded));}
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!pose) assert(n3ds_model_skin_vertex(v,&p->skinning,&unit,out+i));
        real_vector3d normal={0};float eye[3],n2=0,e2=0,dot=0;
        for(int k=0;k<2;++k) if(v->node_weights[k]) {
            assert(v->nodes[k]>=0 && v->nodes[k]<p->skinning.node_matrix_count);
            real_vector3d n;matrix4x3_transform_vector(p->skinning.node_matrices+v->nodes[k],&v->normal,&n);
            for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[k];
        }
        float xyz[2][3],noise[2][2];
        for(int c=0;c<3;++c) {
            eye[c]=global_window_parameters.camera.position.n[c]-out[i].position[c];
            n2+=normal.n[c]*normal.n[c];e2+=eye[c]*eye[c];dot+=normal.n[c]*eye[c];
            xyz[0][c]=v->position.n[c]*s->primary_scale+time[0]*s->primary_direction.n[c];
            xyz[1][c]=v->position.n[c]*s->secondary_scale+time[1]*s->secondary_direction.n[c];
        }
        /* Retain the primary coordinate shear from the Xbox constant matrix. */
        xyz[0][0]+=offset*v->position.z;
        volume_sample(primary,xyz[0],noise[0]);volume_sample(secondary,xyz[1],noise[1]);
        float facing=n2>1e-12f && e2>1e-12f?PIN(fabsf(dot)/sqrtf(n2*e2),0.f,1.f):0;
        float a=PIN(.5f+noise[0][0]-noise[1][0],0.f,1.f);
        float b=PIN(.5f+noise[1][1]-noise[0][1],0.f,1.f);
        /* Low-frequency approximation of the original nonlinear noise blend. */
        float energy=PIN(2*(a*a+b*b),0.f,1.f);
        float alpha=PIN(s->parallel.alpha+(s->perpendicular.alpha-s->parallel.alpha)*facing,0.f,1.f)*intensity*energy;
        for(int c=0;c<3;++c) out[i].color[c]=PIN((s->parallel.rgb.n[c]+(s->perpendicular.rgb.n[c]-s->parallel.rgb.n[c])*facing)*tint->n[c]*alpha,0.f,1.f);
    }
    assert(n3ds_gpu_texture_bind(0,NULL));n3ds_gpu_geometry_flush(out,count*sizeof(*out));
    /* Premultiplied additive RGB, both faces, depth tested, no depth/alpha writes. */
    assert(n3ds_gpu_glass_draw(out,indices,index_count,1,1));
    if(++draws==1 || draws==60 || draws==300) {
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            char message[256];snprintf(message,sizeof(message),"NATIVE PLASMA: draw=%u triangles=%u intensity=%.3f time=%.3f name=%.140s",draws,index_count/3,intensity,global_frame_parameters.game_time_sec,view->instances[t].name);n3ds_log(message);break;
        }
    }
}
