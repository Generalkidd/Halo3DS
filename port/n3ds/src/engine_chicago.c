/* Native 2D Chicago material path: up to four maps (four-map materials use a bounded UV bake), original UV animation,
 * current/next/multiply/double-multiply/add combiners and framebuffer blending.
 * Additive angular fade uses the original camera-forward/posed-normal term. */
#include "cseries.h"
#include "engine_chicago.h"
#include "engine_generic.h"
#include "engine_models.h"
#include "engine_textures.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "rasterizer/rasterizer.h"
#include "bitmaps/bitmap_group.h"
struct chicago_map {
    word flags; byte reserved0[42]; short color_function,alpha_function;
    byte reserved1[36]; real u_scale,v_scale,u_offset,v_offset,rotation,mipmap_bias;
    struct tag_reference bitmap; byte reserved2[40]; struct shader_texture_animation animation;
};
struct chicago_shader {
    struct shader shader; byte counter,flags; short type,blend,fade_mode,fade_source,pad;
    real flare_spacing; struct tag_reference flare; struct tag_block extra_layers,maps;
    word extra_flags,pad2;
};
struct generic_map {
    word flags,pad; real u_scale,v_scale,u_offset,v_offset,rotation,mipmap_bias;
    struct tag_reference bitmap; struct shader_texture_animation animation;
};
struct generic_shader { byte common[96]; struct tag_block stages; };
_Static_assert(sizeof(struct generic_map)==100,"Generic map ABI");
_Static_assert(offsetof(struct generic_map,animation)==44,"Generic animation ABI");
_Static_assert(sizeof(struct chicago_map)==220,"Chicago map ABI");
_Static_assert(offsetof(struct chicago_map,animation)==164,"Chicago animation ABI");
_Static_assert(offsetof(struct chicago_shader,maps)==84,"Chicago maps ABI");
_Static_assert(sizeof(struct chicago_shader)==100,"Chicago shader ABI");
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
extern struct rasterizer_window_begin_parameters global_window_parameters;
boolean rasterizer_set_texture_direct(short stage,long bitmap,short index);
point2d *rasterizer_set_texture(short stage,short type,short usage,long bitmap,short index);
void n3ds_log(const char *message);
/* Chicago selects the same generic Xbox vertex program: oD1.rgb is
 * abs(dot(posed normal, -camera forward)); oD1.a is its complement.
 * This is a directional view term, not the positional reflection vector. */
static float angular_fade(const struct model_vertex_uncompressed *v,
    const struct rasterizer_model_skinning *skin,const real_vector3d *forward,int mode)
{
    real_vector3d normal={0};
    for(int i=0;i<2;++i) if(v->node_weights[i]) {
        real_vector3d n;
        matrix4x3_transform_vector(skin->node_matrices+v->nodes[i],&v->normal,&n);
        for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[i];
    }
    float length=normalize3d(&normal);
    assert(length>0);
    float facing=PIN(fabsf(dot_product3d(&normal,forward)),0.f,1.f);
    return mode==2?facing:1.f-facing;
}
/* Original transparent-geometry counter: round the normalized animation,
 * clamp to the tag's counter limit, then select a base-frame_count digit. */
static int numeric_frame(int frames,int limit,int digit,float value)
{
    if(frames<=0 || limit<0 || limit>255 || digit<0 || !isfinite(value)) return -1;
    int counter=(int)floorf(PIN(value,0.f,1.f)*limit+.5f);
    for(int i=0;i<digit && counter;++i) counter/=frames;
    return counter%frames;
}
/* Translate the same sequential, saturated Chicago operations used by the
 * three-texture GPU path. The fourth texture is retained by a low-detail bake. */
static void chicago_bake_program(struct native_generic_material *b,int maps,const int color[3],
    const int alpha[3],const int replicate[3])
{
    assert(n3ds_chicago_program(b,maps,color,alpha,replicate));
}
static int chicago_four_map_tests(void)
{
    for(int maps=1;maps<=4;++maps) for(int f=0;f<13;++f) for(int a=0;a<13;++a) for(int replicate=0;replicate<2;++replicate) {
        int cf[3]={f,(f+1)%13,(f+2)%13},af[3]={a,(a+1)%13,(a+2)%13},ar[3]={replicate,1-replicate,replicate};
        struct native_generic_material b={0};chicago_bake_program(&b,maps,cf,af,ar);
        if(!n3ds_generic_validate(&b)) return 1;
        for(int k=0;k<32;++k) {
            float samples[4][4],actual[4],expected[4];
            for(int m=0;m<4;++m) for(int c=0;c<4;++c) samples[m][c]=((k*31+m*57+c*19)%256)/255.f;
            memcpy(expected,samples[0],sizeof(expected));
            for(int m=1;m<maps;++m) for(int c=0;c<4;++c) {
                int function=c==3?af[m-1]:cf[m-1];float x=expected[c],y=samples[m][c<3 && ar[m-1]?3:c];
                float ca=expected[3],na=samples[m][3];
                float value=function==0?x:function==1?y:function==2?x*y:function==3?2*x*y:function==4?x+y:
                    function==5?y+2*x-1:function==6?x+2*y-1:function==7?y-x:function==8?x-y:
                    function==9?y*ca+x*(1-ca):function==10?x*ca+y*(1-ca):function==11?y*na+x*(1-na):x*na+y*(1-na);
                expected[c]=PIN(value,0.f,1.f);
            }
            n3ds_generic_evaluate(&b,samples,actual);
            for(int c=0;c<4;++c) if(fabsf(actual[c]-expected[c])>1e-5f) return 1;
        }
    }
    n3ds_log("PASS: Chicago: 43264 vectors, 1-4 maps, all 13 combiners and alpha replicate");return 0;
}
int halo_engine_chicago_numeric_tests(void)
{
    if(chicago_four_map_tests()) return 1;
    real_matrix4x3 bones[2]={0};
    struct rasterizer_model_skinning skin={0};skin.node_matrices=bones;skin.node_matrix_count=2;
    struct model_vertex_uncompressed vertex={0};vertex.normal.i=1;
    vertex.nodes[1]=1;vertex.node_weights[0]=vertex.node_weights[1]=.5f;
    for(int i=0;i<2;++i) {bones[i].scale=1;bones[i].forward.i=bones[i].left.j=bones[i].up.k=1;}
    const real_vector3d directions[4]={{1,0,0},{-1,0,0},{0,1,0},{.6f,.8f,0}};
    const float facing[4]={1,1,0,.6f};
    for(int pose=0;pose<2;++pose) {
        /* Both identity and a blended, rotated/scaled pose: looking along the
         * posed normal stays bright; the perpendicular view must disappear. */
        if(pose) {bones[0].scale=bones[1].scale=2;vertex.normal.i=0;vertex.normal.j=1;
            for(int i=0;i<2;++i) {bones[i].left.i=1;bones[i].left.j=0;bones[i].forward.i=0;bones[i].forward.j=-1;}}
        for(int view=0;view<4;++view) for(int mode=1;mode<=2;++mode)
            if(fabsf(angular_fade(&vertex,&skin,directions+view,mode)-(mode==2?facing[view]:1-facing[view]))>1e-5f) return 1;
    }
    n3ds_log("PASS: Chicago angular fade: 16 posed-normal/directional-view CPU vectors");
    static const struct {int frames,limit,digit;float value;int expected;} cases[]={
        {8,7,0,0,0},{8,7,0,1,7},{8,7,0,.3f,2},
        {8,7,0,-1,0},{8,7,0,2,7},{10,60,0,.85f,1},
        {10,60,1,.85f,5},{10,60,2,1,0},{10,255,2,1,2},
        {10,10,0,.05f,1},{1,60,0,1,0},{0,60,0,1,-1},
        {8,7,-1,1,-1},{10,0,0,1,0}
    };
    for(unsigned int i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        if(numeric_frame(cases[i].frames,cases[i].limit,cases[i].digit,cases[i].value)!=cases[i].expected) return 1;
    n3ds_log("PASS: Chicago numeric compass/digit selection: 14 CPU vectors");return 0;
}
void n3ds_generic_model_draw(const struct shader *,short,const struct rasterizer_model_begin_parameters *,
    const void *,unsigned int,short,const unsigned short *,unsigned int);
void n3ds_chicago_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *parameters,const void *data,
    unsigned int count,short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct chicago_shader *material=(const void *)shader;
    struct native_chicago_material native={0};
    real_vector4d transforms[4][2]={0};
    struct native_generic_material bake={0};
    int color[3]={0},alpha[3]={0},replicate[3]={0};
    float bounds[4]={1e30f,1e30f,-1e30f,-1e30f};
    struct native_chicago_vertex *vertices;
    unsigned int i; long m;short bitmap_index=permutation;
    assert(n3ds_cache_resolve(view,material,sizeof(*material))==material);
    boolean generic=shader->base.type==5;
    if(generic) {
        const struct generic_shader *g=(const void *)shader;
        assert(n3ds_cache_resolve(view,g,sizeof(*g))==g);
        if(g->stages.count) {
            n3ds_generic_model_draw(shader,permutation,parameters,data,count,type,indices,index_count);
            return;
        }
        /* Original generic preprocessor explicitly outputs texture0 when no
         * stages exist. This is a defined path, not a fallback for other stages. */
        assert(!g->stages.count && material->maps.count==1);
    }
    assert(material->type==0 && material->maps.count>=1 && material->maps.count<=4 && !material->extra_layers.count);
    if((material->flags&((1<<3)|(1<<6))) || (material->fade_mode && material->blend!=3)) {
        char message[240];
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            snprintf(message,sizeof(message),"CHICAGO FLAGS: tag=%08lx flags=%02x fade_mode=%d blend=%d maps=%ld name=%.140s",view->instances[t].tag_index,material->flags,material->fade_mode,material->blend,material->maps.count,view->instances[t].name);
            n3ds_log(message);break;
        }
    }
    assert(!(material->flags&((1<<3)|(1<<6))));
    assert(material->fade_mode>=0 && material->fade_mode<=2);
    /* Angular fading also occurs on alpha-blended level light volumes. */
    assert(material->fade_source>=0 && material->fade_source<=4);
    boolean four=material->maps.count==4;
    /* Subtraction and alpha interpolation fit one native TEV stage each.
     * Biased adds and four-map materials retain the bounded bake. */
    if(!generic) for(m=0;m<material->maps.count-1;++m) {
        const struct chicago_map *map=TAG_BLOCK_GET_ELEMENT(&material->maps,m,struct chicago_map);
        assert(n3ds_cache_resolve(view,map,sizeof(*map))==map);
        if(!n3ds_chicago_native_function(map->color_function) || !n3ds_chicago_native_function(map->alpha_function)) four=TRUE;
    }
    native.maps=four?1:material->maps.count; native.blend=material->blend;
    native.alpha_test=!!(material->flags&1); native.two_sided=!!(material->flags&4);
    native.decal=!!(material->flags&2); native.fade=1.f;
    native.vertex_fade=material->fade_mode?2:0;
    native.sky=!!(parameters->geometry_flags&(1u<<4));
    if(material->fade_source) {
        assert(parameters->animation.values);
        native.fade=PIN(parameters->animation.values[material->fade_source-1],0.f,1.f);
    }
    if(material->flags&(1<<7)) {
        long bitmap=generic?TAG_BLOCK_GET_ELEMENT(&material->maps,0,struct generic_map)->bitmap.index:
            TAG_BLOCK_GET_ELEMENT(&material->maps,0,struct chicago_map)->bitmap.index;
        const struct bitmap_group *group=bitmap_group_get(bitmap);
        assert(group && group->bitmaps.count>0 && parameters->animation.values);
        if(material->extra_flags&2) bitmap_index=numeric_countdown_timer_get(permutation);
        else bitmap_index=numeric_frame(group->bitmaps.count,material->counter,permutation,
            parameters->animation.values[group->bitmaps.count==8?3:0]);
        assert(bitmap_index>=0);
        static int reported;
        if(!reported) {
            char text[160];snprintf(text,sizeof(text),"NATIVE NUMERIC: original animated bitmap selection; frames=%ld limit=%u digit=%d bitmap=%d",group->bitmaps.count,material->counter,permutation,bitmap_index);
            n3ds_log(text);reported=1;
        }
    }
    for(m=0;m<material->maps.count;++m) {
        struct chicago_map adapted={0};
        const struct chicago_map *map;
        if(generic) {
            const struct generic_map *g=TAG_BLOCK_GET_ELEMENT(&material->maps,m,struct generic_map);
            assert(n3ds_cache_resolve(view,g,sizeof(*g))==g);
            adapted.flags=(g->flags&1)|((g->flags&6)<<1);
            adapted.u_scale=g->u_scale;adapted.v_scale=g->v_scale;adapted.u_offset=g->u_offset;adapted.v_offset=g->v_offset;
            adapted.rotation=g->rotation;adapted.mipmap_bias=g->mipmap_bias;adapted.bitmap=g->bitmap;adapted.animation=g->animation;
            map=&adapted;
        } else {
            map=TAG_BLOCK_GET_ELEMENT(&material->maps,m,struct chicago_map);
            assert(n3ds_cache_resolve(view,map,sizeof(*map))==map);
        }
        /* Xbox Chicago uses the usage-0 default for an absent optional map. */
        boolean bound=map->bitmap.index==NONE ?
            rasterizer_set_texture(four?0:(short)m,0,0,NONE,bitmap_index)!=NULL :
            rasterizer_set_texture_direct(four?0:(short)m,map->bitmap.index,bitmap_index);
        if(!bound) {
            char text[192]; snprintf(text,sizeof(text),"CHICAGO BITMAP: stage=%ld tag=%08lx permutation=%d cache_count=%u cache_bytes=%u",m,map->bitmap.index,permutation,n3ds_engine_texture_count(),n3ds_engine_texture_bytes()); n3ds_log(text);
        }
        assert(bound);
        if(four) {
            bake.textures[m]=n3ds_gpu_texture_bound(0);
            bake.point[m]=!!(map->flags&1);bake.clamp_u[m]=!!(map->flags&4);bake.clamp_v[m]=!!(map->flags&8);
            if(m<3) {color[m]=map->color_function;alpha[m]=map->alpha_function;replicate[m]=!!(map->flags&2);}
        } else {
            native.point[m]=!!(map->flags&1); native.clamp_u[m]=!!(map->flags&4); native.clamp_v[m]=!!(map->flags&8);
            if(m<material->maps.count-1) {
                native.color_function[m]=map->color_function; native.alpha_function[m]=map->alpha_function;
                native.alpha_replicate[m]=!!(map->flags&2);
            }
        }
        shader_texture_animation_evaluate(&map->animation,&parameters->animation,
            map->u_scale*parameters->base_map_scale.i,map->v_scale*parameters->base_map_scale.j,
            map->u_offset,map->v_offset,map->rotation,global_frame_parameters.game_time_sec,
            &transforms[m][0],&transforms[m][1]);
    }
    if(four) {
        memcpy(bake.transform,transforms,sizeof(transforms));
        chicago_bake_program(&bake,material->maps.count,color,alpha,replicate);
        native.clamp_u[0]=native.clamp_v[0]=1;
    }
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    if(!four && !native.vertex_fade) {
        extern int n3ds_model_chicago_gpu_draw(const struct model_vertex_uncompressed *,unsigned int,const unsigned short *,unsigned int,const struct native_chicago_material *,const float *);
        if(n3ds_model_chicago_gpu_draw(cached,count,indices,index_count,&native,transforms[0][0].n)) return;
    }
    vertices=n3ds_gpu_chicago_allocate(count); assert(vertices);
    const real_vector2d unit_scale={1.f,1.f};
    const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&parameters->skinning,&unit_scale,parameters->unique_identifier);
    float relative_origin[3]={0};if(!native.sky)n3ds_gpu_camera_origin(relative_origin);
    for(i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;
        struct native_render_vertex temporary;
        const struct native_render_vertex *skinned=posed?posed+i:&temporary;
        const struct model_vertex_uncompressed *vertex=cached?cached+i:&decoded;
        if(!cached && type==5) { assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded)); }
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!posed) assert(n3ds_model_skin_vertex(vertex,&parameters->skinning,&unit_scale,&temporary));
        for(int c=0;c<3;++c)vertices[i].position[c]=skinned->position[c]-relative_origin[c];
        memset(vertices[i].uv,0,sizeof(vertices[i].uv));
        if(four) for(int axis=0;axis<2;++axis) {
            float uv=vertices[i].uv[0][axis]=vertex->texcoord.n[axis];
            bounds[axis]=MIN(bounds[axis],uv);bounds[axis+2]=MAX(bounds[axis+2],uv);
        }
        else for(m=0;m<material->maps.count;++m) for(int axis=0;axis<2;++axis)
            vertices[i].uv[m][axis]=transforms[m][axis].n[0]*vertex->texcoord.x+
                transforms[m][axis].n[1]*vertex->texcoord.y+transforms[m][axis].n[3];
        if(native.vertex_fade) {
            float fade=angular_fade(vertex,&parameters->skinning,&global_window_parameters.camera.forward,material->fade_mode);
            for(int c=0;c<4;++c) vertices[i].color[c]=fade;
        }
    }
    if(four) {
        for(int axis=0;axis<2;++axis) if(bounds[axis+2]-bounds[axis]<.0001f) bounds[axis+2]=bounds[axis]+.0001f;
        void *texture=n3ds_gpu_generic_bake(&bake,bounds);assert(texture);
        for(i=0;i<count;++i) for(int axis=0;axis<2;++axis)
            vertices[i].uv[0][axis]=(vertices[i].uv[0][axis]-bounds[axis])/(bounds[axis+2]-bounds[axis]);
        assert(n3ds_gpu_texture_bind(0,texture));
    }
    assert(n3ds_gpu_chicago_relative_draw(&native,vertices,count,indices,index_count));
    static const void *seen[32]; static unsigned int seen_count;
    for(i=0;i<seen_count && seen[i]!=shader;++i) {}
    if(i==seen_count && seen_count<32) {
        char text[240]; seen[seen_count++]=shader;
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            snprintf(text,sizeof(text),"NATIVE %s: tag=%08lx maps=%d blend=%d triangles=%u name=%.140s",
                generic?"GENERIC BASE":"CHICAGO",view->instances[t].tag_index,(int)material->maps.count,native.blend,index_count/3,view->instances[t].name);
            n3ds_log(text); break;
        }
    }
}
