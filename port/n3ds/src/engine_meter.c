/* Original tint-mode1 meter program with native UV baking. Its value, gradient,
 * flash and brightness read the live original shader-animation packet. */
#include "cseries.h"
#include "engine_generic.h"
#include "engine_chicago.h"
#include "engine_models.h"
#include "engine_textures.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "shaders/shader_definitions.h"
#include "rasterizer/rasterizer.h"
struct meter_shader {
    struct shader shader;word flags,pad;byte reserved[32];struct tag_reference map;
    byte reserved2[32];real_rgb_color minimum,maximum,background,flash,tint;
    real meter_transparency,background_transparency;byte reserved3[24];short source[5],pad2;
    byte reserved4[32];
};
_Static_assert(offsetof(struct meter_shader,minimum)==124,"Meter colors ABI");
_Static_assert(offsetof(struct meter_shader,source)==216,"Meter animation ABI");
boolean rasterizer_set_texture_direct(short,long,short);
void n3ds_log(const char *);
void n3ds_meter_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,unsigned int count,
    short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct meter_shader *g=(const void *)shader;
    struct native_generic_material bake={0};
    struct native_chicago_material native={.maps=1,.blend=0,.fade=1,.meter=1};
    float values[5]={1,1,1,1,1},constants[5][4],bounds[4]={1e30f,1e30f,-1e30f,-1e30f};
    assert(n3ds_cache_resolve(view,g,sizeof(*g))==g);
    /* Mode2 has a different destination factor and requires a separate mask
     * strategy. Neither Wizard plasma-weapon gauge uses that mode. */
    assert(!(g->flags&8) && !(p->geometry_flags&(1<<4)));
    for(int i=0;i<5;++i) if(g->source[i]>=1 && g->source[i]<=4 && p->animation.values)
        values[i]=PIN(p->animation.values[g->source[i]-1],0.f,1.f);
    for(int c=0;c<3;++c) {
        constants[0][c]=g->minimum.n[c];constants[1][c]=g->maximum.n[c];
        constants[2][c]=g->flash.n[c]*values[1];constants[3][c]=g->background.n[c];constants[4][c]=g->tint.n[c];
    }
    constants[0][3]=values[2];constants[1][3]=1.f/MAX(values[3]*8.f,1.f);
    constants[2][3]=values[4];constants[3][3]=0;constants[4][3]=values[0];
    for(int i=0;i<5;++i) for(int c=0;c<4;++c)
        constants[i][c]=floorf(PIN(constants[i][c],0.f,1.f)*255.f+.5f)/255.f;
    n3ds_meter_program(&bake,constants,!!(g->flags&4));assert(n3ds_generic_validate(&bake));
    assert(rasterizer_set_texture_direct(0,g->map.index,permutation));bake.textures[0]=n3ds_gpu_texture_bound(0);
    bake.point[0]=!!(g->flags&16);
    bake.transform[0][0][0]=p->base_map_scale.i;bake.transform[0][1][1]=p->base_map_scale.j;
    native.two_sided=!!(g->flags&2);native.decal=!!(g->flags&1);
    native.clamp_u[0]=native.clamp_v[0]=1;native.point[0]=bake.point[0];
    for(int c=0;c<4;++c) native.meter_tint|=(unsigned int)(constants[4][c]*255.f+.5f)<<(c*8);
    struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(count);assert(v);
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    const real_vector2d scale={1,1};
    const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&p->skinning,&scale,p->unique_identifier);
    float relative_origin[3]={0};if(!native.sky)n3ds_gpu_camera_origin(relative_origin);
    for(unsigned int i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
        const struct native_render_vertex *skinned=posed?posed+i:&temporary;
        const struct model_vertex_uncompressed *vertex=cached?cached+i:&decoded;
        if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded));}
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!posed) assert(n3ds_model_skin_vertex(vertex,&p->skinning,&scale,&temporary));
        for(int c=0;c<3;++c)v[i].position[c]=skinned->position[c]-relative_origin[c];
        memset(v[i].uv,0,sizeof(v[i].uv));
        for(int axis=0;axis<2;++axis) {
            float uv=v[i].uv[0][axis]=skinned->uv[axis];bounds[axis]=MIN(bounds[axis],uv);bounds[axis+2]=MAX(bounds[axis+2],uv);
        }
    }
    for(int axis=0;axis<2;++axis) if(bounds[axis+2]-bounds[axis]<.0001f) bounds[axis+2]=bounds[axis]+.0001f;
    void *texture=n3ds_gpu_meter_bake(&bake,bounds);assert(texture);
    for(unsigned int i=0;i<count;++i) for(int axis=0;axis<2;++axis)
        v[i].uv[0][axis]=(v[i].uv[0][axis]-bounds[axis])/(bounds[axis+2]-bounds[axis]);
    assert(n3ds_gpu_texture_bind(0,texture));assert(n3ds_gpu_chicago_relative_draw(&native,v,count,indices,index_count));
    static const void *seen[16];static int seen_count;int i;
    for(i=0;i<seen_count && seen[i]!=shader;++i) {}
    if(i==seen_count && seen_count<16) {
        seen[seen_count++]=shader;
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            char text[224];snprintf(text,sizeof(text),"NATIVE METER: value_source=%d live_value=%.6f triangles=%u name=%.140s",g->source[2],values[2],index_count/3,view->instances[t].name);n3ds_log(text);break;
        }
    }
}
