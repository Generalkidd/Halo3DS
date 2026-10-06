/* Reduced-resolution UV-domain shading for vertex-independent generic stages.
 * Preserve all maps, animated transforms and stage arithmetic. This trades fine
 * texture detail for a bounded low-resolution bake, not material or object visibility. */
#include "cseries.h"
#include "engine_generic.h"
#include "engine_glass.h"
#include "engine_chicago.h"
#include "engine_models.h"
#include "engine_textures.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "math/periodic_functions.h"
#include "rasterizer/rasterizer.h"
#include "cutscene/cinematics.h"
struct generic_map {
    word flags,pad;real u,v,x,y,rotation,bias;struct tag_reference bitmap;
    struct shader_texture_animation animation;
};
struct generic_stage {
    word flags,pad;short source,wave;real period;real_argb_color lower,upper,constant;
    short op[26];
};
struct generic_shader {
    struct shader shader;byte counter,flags;short type,blend,fade_mode,fade_source,pad;
    real flare_spacing;struct tag_reference flare;struct tag_block layers,maps,stages;
};
_Static_assert(sizeof(struct generic_stage)==112,"Generic stage ABI");
_Static_assert(sizeof(struct generic_map)==100,"Generic map ABI");
_Static_assert(offsetof(struct generic_shader,stages)==96,"Generic shader ABI");
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
extern struct rasterizer_window_begin_parameters global_window_parameters;
boolean rasterizer_set_texture_direct(short stage,long bitmap,short index);
point2d *rasterizer_set_texture(short stage,short type,short usage,long bitmap,short index);
void n3ds_log(const char *message);
static float angular_fade(const struct model_vertex_uncompressed *v,
    const struct rasterizer_model_skinning *skin,const float position[3],int mode)
{
    real_vector3d normal={0},eye;real n2=0,e2=0,dot=0;
    for(int i=0;i<2;++i) if(v->node_weights[i]) {
        real_vector3d n;
        matrix4x3_transform_vector(skin->node_matrices+v->nodes[i],&v->normal,&n);
        for(int j=0;j<3;++j) normal.n[j]+=n.n[j]*v->node_weights[i];
    }
    for(int j=0;j<3;++j) {
        eye.n[j]=global_window_parameters.camera.position.n[j]-position[j];
        n2+=normal.n[j]*normal.n[j];e2+=eye.n[j]*eye.n[j];dot+=normal.n[j]*eye.n[j];
    }
    float facing=n2*e2>1e-12f?PIN(fabsf(dot)/sqrtf(n2*e2),0.f,1.f):0;
    return mode==2?facing:1-facing;
}
/* Original generic vertex programs47/57: oD1.rgb=abs(dot(normal,-forward))
 * times fog visibility; oD1.a=(1-facing)*visibility. Reflection uses the
 * positional eye vector instead. These are intentionally different vectors. */
static void generic_view(const struct model_vertex_uncompressed *v,
    const struct rasterizer_model_skinning *skin,const float position[3],
    float vertex[2][4],float reflected[3],int sky)
{
    real_vector3d normal={0};float n2=0,dot=0,facing=0,depth=0,plane=0;
    const struct render_fog *fog=&global_window_parameters.fog;
    for(int k=0;k<2;++k) if(v->node_weights[k]) {
        real_vector3d n;matrix4x3_transform_vector(skin->node_matrices+v->nodes[k],&v->normal,&n);
        for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[k];
    }
    for(int c=0;c<3;++c) n2+=normal.n[c]*normal.n[c];
    assert(n2>1e-12f);
    for(int c=0;c<3;++c) {
        normal.n[c]/=sqrtf(n2);
        reflected[c]=global_window_parameters.camera.position.n[c]-position[c];
        dot+=normal.n[c]*reflected[c];
        facing-=normal.n[c]*global_window_parameters.camera.forward.n[c];
        depth-=reflected[c]*global_window_parameters.camera.forward.n[c];
        plane+=fog->plane.n.n[c]*position[c];
    }
    for(int c=0;c<3;++c) reflected[c]=2*dot*normal.n[c]-reflected[c];
    float visible=1;
    if(!sky && fog->atmospheric_maximum_density>0) {
        float range=fog->atmospheric_maximum_distance-fog->atmospheric_minimum_distance;
        assert(range>0);
        visible*=1-PIN((depth-fog->atmospheric_minimum_distance)/range,0.f,1.f)*PIN(fog->atmospheric_maximum_density,0.f,1.f);
    }
    if(!sky && fog->planar_mode && fog->planar_maximum_density>0) {
        assert(fog->planar_maximum_distance>0 && fog->planar_maximum_depth>0);
        float x=PIN(1+(plane-fog->plane.d)/fog->planar_maximum_depth,0.f,1.f);
        float y=PIN(1-depth/fog->planar_maximum_distance,0.f,1.f);
        float camera_plane=plane3d_distance_to_point(&fog->plane,&global_window_parameters.camera.position);
        float q=1-MIN(1.f,x*x+y*y),r=1-y*y;
        float density=q*q+(r*r-q*q)*PIN(-camera_plane/fog->planar_maximum_depth,0.f,1.f);
        visible*=1-density*PIN(fog->planar_maximum_density,0.f,1.f);
    }
    facing=PIN(fabsf(facing),0.f,1.f);
    memset(vertex,0,2*4*sizeof(float));vertex[0][3]=visible;
    for(int c=0;c<3;++c) vertex[1][c]=facing*visible;
    vertex[1][3]=(1-facing)*visible;
}
#include "engine_hologram_model.inl"
void n3ds_generic_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *parameters,const void *data,
    unsigned int count,short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct generic_shader *g=(const void *)shader;
    struct native_generic_material bake={0};
    struct native_chicago_material native={.maps=1,.fade=1};
    struct native_chicago_vertex *vertices;
    const byte *cube=NULL;
    float bounds[4]={1e30f,1e30f,-1e30f,-1e30f};
    assert(n3ds_cache_resolve(view,g,sizeof(*g))==g);
    assert(g->maps.count>=0 && g->maps.count<=4 && g->stages.count>=1 && g->stages.count<=8);
    if(g->type>1 || (g->flags&(1<<7)) || (g->type==1 && (g->flags&((1<<3)|(1<<6))))) {
        char message[256];
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            snprintf(message,sizeof(message),"GENERIC GEOMETRY REJECT: tag=%08lx type=%d flags=%02x layers=%ld maps=%ld stages=%ld name=%.140s",view->instances[t].tag_index,g->type,g->flags,g->layers.count,g->maps.count,g->stages.count,view->instances[t].name);n3ds_log(message);break;
        }
        snprintf(message,sizeof(message),"GENERIC VIEW: position=%.6f,%.6f,%.6f forward=%.6f,%.6f,%.6f",global_window_parameters.camera.position.x,global_window_parameters.camera.position.y,global_window_parameters.camera.position.z,global_window_parameters.camera.forward.i,global_window_parameters.camera.forward.j,global_window_parameters.camera.forward.k);n3ds_log(message);
    }
    assert(g->type>=0 && g->type<=1 && !(g->flags&(1<<7)));
    assert(g->type==0 || !(g->flags&((1<<3)|(1<<6))));
    assert(g->layers.count>=0 && g->layers.count<=4);
    static unsigned int layer_depth;
    assert(layer_depth<8);++layer_depth;
    for(long i=0;i<g->layers.count;++i) {
        const struct tag_reference *ref=TAG_BLOCK_GET_ELEMENT(&g->layers,i,struct tag_reference);
        assert(n3ds_cache_resolve(view,ref,sizeof(*ref))==ref);
        const struct shader *layer=shader_definition_get(ref->index);
        assert(layer && layer->base.type==5);
        n3ds_generic_model_draw(layer,permutation,parameters,data,count,type,indices,index_count);
    }
    --layer_depth;
    assert(g->fade_mode>=0 && g->fade_mode<=2 && g->fade_source>=0 && g->fade_source<=4);
    /* All framebuffer modes use their own neutral fade colour. */
    bake.maps=g->maps.count;bake.stages=g->stages.count;
    native.blend=g->blend;native.alpha_test=!!(g->flags&1);native.two_sided=!!(g->flags&4);
    native.decal=!!(g->flags&2);native.sky=!!(parameters->geometry_flags&(1<<4));
    native.vertex_fade=g->fade_mode!=0;
    native.clamp_u[0]=native.clamp_v[0]=1;
    if(g->fade_source && parameters->animation.values)
        native.fade=PIN(parameters->animation.values[g->fade_source-1],0.f,1.f);
    for(int i=0;i<bake.maps;++i) {
        const struct generic_map *m=TAG_BLOCK_GET_ELEMENT(&g->maps,i,struct generic_map);
        real_vector4d transform[2];
        assert(n3ds_cache_resolve(view,m,sizeof(*m))==m);
        if(!i && g->type==1) {
            assert(!(m->flags&1)); /* Cube path currently preserves linear sampling only. */
            cube=n3ds_cube_load(m->bitmap.index,permutation);assert(cube);continue;
        }
        /* Generic shaders, like Chicago shaders, permit an absent 2D map.
         * Match the Xbox renderer's type0/usage0 default in that case. */
        boolean bound=m->bitmap.index==NONE ? rasterizer_set_texture(0,0,0,NONE,permutation)!=NULL :
            rasterizer_set_texture_direct(0,m->bitmap.index,permutation);
        if(!bound) {
            char message[224];
            for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
                snprintf(message,sizeof(message),"GENERIC MAP BIND: bitmap=%08lx map=%d permutation=%d name=%.130s",m->bitmap.index,i,permutation,view->instances[t].name);n3ds_log(message);break;
            }
        }
        assert(bound);
        bake.textures[i]=n3ds_gpu_texture_bound(0);
        bake.point[i]=!!(m->flags&1);bake.clamp_u[i]=!!(m->flags&2);bake.clamp_v[i]=!!(m->flags&4);
        float u=m->u,v=m->v;
        if(i || !(g->flags&8)) {u*=parameters->base_map_scale.i;v*=parameters->base_map_scale.j;}
        if(!i && (g->flags&64)) {
            real_vector3d relative;vector_from_points3d(&global_window_parameters.camera.position,&parameters->centroid,&relative);
            float distance=dot_product3d(&relative,&global_window_parameters.camera.forward);u*=distance;v*=distance;
        }
        shader_texture_animation_evaluate(&m->animation,&parameters->animation,
            u,v,m->x,m->y,m->rotation,
            global_frame_parameters.game_time_sec,&transform[0],&transform[1]);
        memcpy(bake.transform[i],transform,sizeof(transform));
    }
    for(int i=0;i<bake.stages;++i) {
        const struct generic_stage *s=TAG_BLOCK_GET_ELEMENT(&g->stages,i,struct generic_stage);
        struct native_generic_stage *out=&bake.stage[i];
        assert(n3ds_cache_resolve(view,s,sizeof(*s))==s && s->period!=0);
        out->flags=s->flags;
        float f=(s->flags&4) && parameters->animation.values?parameters->animation.values[0]:
            periodic_function_evaluate(s->wave,global_frame_parameters.game_time_sec/s->period);
        for(int c=0;c<4;++c) {
            int argb=c==3?0:c+1;
            float value=s->lower.n[argb]+f*(s->upper.n[argb]-s->lower.n[argb]);
            if(c<3 && s->source>0 && s->source<5 && parameters->animation.colors)
                value*=parameters->animation.colors[s->source-1].n[c];
            out->constant[0][c]=floorf(PIN(value,0.f,1.f)*255.f+.5f)/255.f;
            out->constant[1][c]=floorf(PIN(s->constant.n[argb],0.f,1.f)*255.f+.5f)/255.f;
        }
        for(int j=0;j<26;++j) out->op[j]=s->op[j];
    }
    boolean per_vertex=g->type==1 || (g->flags&8) || !n3ds_generic_validate(&bake);
    unsigned int outputs=(g->blend==0 || g->blend==7 || (g->flags&1))?15:7;
    if(!n3ds_generic_validate_inputs(&bake,per_vertex?3:0,outputs)) {
        char text[320];
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            snprintf(text,sizeof(text),"GENERIC REJECT: tag=%08lx maps=%d stages=%d flags=%02x fade=%d name=%.140s",view->instances[t].tag_index,bake.maps,bake.stages,g->flags,g->fade_mode,view->instances[t].name);n3ds_log(text);break;
        }
        for(int s=0;s<bake.stages;++s) {
            int *p=bake.stage[s].op;
            snprintf(text,sizeof(text),"GENERIC STAGE: index=%d flags=%u ops=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",s,bake.stage[s].flags,p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],p[16],p[17],p[18],p[19],p[20],p[21],p[22],p[23],p[24],p[25]);n3ds_log(text);
        }
    }
    assert(n3ds_generic_validate_inputs(&bake,per_vertex?3:0,outputs));
    int native_lerp=g->type==0 && !(g->flags&8) && g->blend==0 && n3ds_generic_lit_lerp(&bake);
    int shield=n3ds_generic_shield(&bake);
    /* Chicago's sky projection already preserves the no-depth sky frustum.
     * Evaluate cube/view inputs in that posed world space, without world fog. */
    if(per_vertex) { native.vertex_color=1;native.vertex_fade=0; }
    if(native_lerp) {
        native.maps=2;native.vertex_color=0;native.vertex_fade=0;native.generic_lit_lerp=1;
        for(int i=0;i<2;++i) {
            native.point[i]=bake.point[i];native.clamp_u[i]=bake.clamp_u[i];native.clamp_v[i]=bake.clamp_v[i];
        }
    }
    vertices=n3ds_gpu_chicago_allocate(count);assert(vertices);
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    const real_vector2d scale={1,1};
    const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&parameters->skinning,&scale,parameters->unique_identifier);
    int hologram=g->type==0 && (g->flags&8) && !(g->flags&1) && g->blend==3 && !g->fade_mode?
        n3ds_generic_hologram(&bake):0;
    if(hologram_model_draw(hologram,&bake,native,vertices,posed,count,indices,index_count)) return;
    float relative_origin[3]={0};if(!native.sky)n3ds_gpu_camera_origin(relative_origin);
    /* Cinematic screen-mapped holograms: keep every posed vertex and triangle,
     * but evaluate the expensive four-map combiner once per 8x8 UV cell each
     * draw. All animation uniforms and the camera are resampled every frame.
     * Cube materials and ordinary gameplay retain full per-vertex evaluation. */
    int coarse=per_vertex && !cube && (g->flags&8) && cached && count>=128 && cinematic_in_progress();
    float shade_bounds[4]={1e30f,1e30f,-1e30f,-1e30f},shade_colors[64][4];byte shade_valid[64]={0};
    unsigned int shade_evaluations=0;
    if(coarse) {
        for(unsigned int i=0;i<count;++i) for(int axis=0;axis<2;++axis) {
            float value=cached[i].texcoord.n[axis];shade_bounds[axis]=MIN(shade_bounds[axis],value);shade_bounds[axis+2]=MAX(shade_bounds[axis+2],value);
        }
        for(int axis=0;axis<2;++axis) shade_bounds[axis+2]=8.f/MAX(shade_bounds[axis+2]-shade_bounds[axis],.0001f);
    }
    for(unsigned int i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
        const struct native_render_vertex *skinned=posed?posed+i:&temporary;
        const struct model_vertex_uncompressed *vertex=cached?cached+i:&decoded;
        if(!cached && type==5) { assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded)); }
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!posed) assert(n3ds_model_skin_vertex(vertex,&parameters->skinning,&scale,&temporary));
        for(int c=0;c<3;++c) vertices[i].position[c]=skinned->position[c]-relative_origin[c];
        memset(vertices[i].uv,0,sizeof(vertices[i].uv));
        for(int axis=0;axis<2;++axis) {
            float uv=vertices[i].uv[0][axis]=skinned->uv[axis];
            bounds[axis]=MIN(bounds[axis],uv);bounds[axis+2]=MAX(bounds[axis+2],uv);
        }
        vertices[i].uv[2][0]=g->fade_mode?angular_fade(vertex,&parameters->skinning,skinned->position,g->fade_mode):1;
        if(per_vertex) {
            unsigned int shade_cell=0;
            if(coarse) {
                int x=PIN((int)((skinned->uv[0]-shade_bounds[0])*shade_bounds[2]),0,7);
                int y=PIN((int)((skinned->uv[1]-shade_bounds[1])*shade_bounds[3]),0,7);
                shade_cell=y*8+x;
                if(shade_valid[shade_cell]) {memcpy(vertices[i].color,shade_colors[shade_cell],sizeof(vertices[i].color));continue;}
            }
            float inputs[2][4],reflected[3],samples[4][4]={{0}};
            generic_view(vertex,&parameters->skinning,skinned->position,inputs,reflected,native.sky);
            /* Generic vertex0 is diffuse model lighting. Use the same bounded
             * lighting approximation as opaque models; keep its alpha as the
             * original fog visibility, rather than inventing a white input. */
            memcpy(inputs[0],skinned->color,3*sizeof(float));
            if(native.sky) inputs[0][0]=inputs[0][1]=inputs[0][2]=1;
            if(native_lerp) {
                memcpy(vertices[i].color,inputs[0],sizeof(vertices[i].color));
                vertices[i].color[3]*=vertices[i].uv[2][0];
                for(int m=0;m<2;++m) for(int axis=0;axis<2;++axis) {
                    const float *row=bake.transform[m][axis];
                    vertices[i].uv[m][axis]=row[0]*skinned->uv[0]+row[1]*skinned->uv[1]+row[3];
                }
                continue;
            }
            for(int m=0;m<bake.maps;++m) {
                if(!m && cube) n3ds_cube_sample(cube,reflected,samples[m]);
                else if(!m && (g->flags&8)) {
                    /* Screen-space generic programs project map0; evaluate
                     * that coordinate at posed vertices, within the existing
                     * low-detail vertex-shading profile used for view inputs. */
                    real_point3d world,point;memcpy(world.n,skinned->position,sizeof(world));
                    matrix4x3_transform_point(&global_window_parameters.frustum.world_to_view,&world,&point);
                    const real (*p)[4]=global_window_parameters.frustum.projection_matrix;
                    float q=point.x*p[0][3]+point.y*p[1][3]+point.z*p[2][3]+p[3][3];
                    if(fabsf(q)<1e-6f) q=q<0?-1e-6f:1e-6f;
                    float u=(point.x*p[0][0]+point.y*p[1][0]+point.z*p[2][0]+p[3][0])/q;
                    float v=(point.x*p[0][1]+point.y*p[1][1]+point.z*p[2][1]+p[3][1])/q;
                    n3ds_gpu_generic_sample(&bake,m,u,v,samples[m]);
                }
                else n3ds_gpu_generic_sample(&bake,m,skinned->uv[0],skinned->uv[1],samples[m]);
            }
            if(shield) n3ds_generic_shield_evaluate(&bake,samples,vertices[i].color);
            else n3ds_generic_evaluate_inputs(&bake,samples,inputs,vertices[i].color);
            /* Original final fog/fade combiner. Animation fade is applied by
             * Chicago's blend-specific TEV stage below, exactly once. */
            float visibility=inputs[0][3]*vertices[i].uv[2][0];
            for(int c=0;c<4;++c) {
                if(g->blend==0) {if(c==3) vertices[i].color[c]*=visibility;}
                else if(g->blend==7) vertices[i].color[c]*=visibility;
                else if(c<3) {
                    float neutral=g->blend==1 || g->blend==5?1:g->blend==2?.5f:0;
                    vertices[i].color[c]=vertices[i].color[c]*visibility+neutral*(1-visibility);
                }
            }
            if(outputs==7) vertices[i].color[3]=1; /* Multiply does not consume source alpha. */
            if(coarse) {memcpy(shade_colors[shade_cell],vertices[i].color,sizeof(vertices[i].color));shade_valid[shade_cell]=1;++shade_evaluations;}
        }
    }
    if(per_vertex) {
        if(native_lerp) for(int m=0;m<2;++m) assert(n3ds_gpu_texture_bind(m,bake.textures[m]));
        if(coarse) {
            static const void *reported[16];static unsigned int reports;
            unsigned int n;for(n=0;n<reports && reported[n]!=shader;++n) {}
            if(n==reports && reports<16) {reported[reports++]=shader;char text[160];snprintf(text,sizeof(text),"NATIVE CINEMATIC HOLOGRAM: grid=8 vertices=%u evaluations=%u full_pose=1",count,shade_evaluations);n3ds_log(text);}
        }
        assert(n3ds_gpu_chicago_relative_draw(&native,vertices,count,indices,index_count));
        static const void *seen_view[32];static int view_count;
        int i;for(i=0;i<view_count && seen_view[i]!=shader;++i) {}
        if(i==view_count && view_count<32) {
            seen_view[view_count++]=shader;
            char text[160];snprintf(text,sizeof(text),"NATIVE GENERIC %s: cube=%d maps=%d stages=%d blend=%d layers=%ld vertices=%u triangles=%u",native_lerp?"TEV LIT LERP":"VIEW",!!cube,bake.maps,bake.stages,g->blend,g->layers.count,count,index_count/3);n3ds_log(text);
        }
        return;
    }
    for(int axis=0;axis<2;++axis) if(bounds[axis+2]-bounds[axis]<.0001f) bounds[axis+2]=bounds[axis]+.0001f;
    void *texture=shield?n3ds_gpu_shield_bake(&bake,bounds):n3ds_gpu_generic_bake(&bake,bounds);assert(texture);
    if(shield) {
        native.maps=2;native.shield_mask=1;
        native.point[1]=bake.point[3];native.clamp_u[1]=bake.clamp_u[3];native.clamp_v[1]=bake.clamp_v[3];
        const float *c=bake.stage[3].constant[0],*other=bake.stage[3].constant[1];
        for(int channel=0;channel<3;++channel) {
            float tint=(1-c[3])*c[channel]+c[3]*other[channel];
            native.shield_tint|=(unsigned int)(PIN(tint,0.f,1.f)*255.f+.5f)<<(8*channel);
        }
        for(unsigned int i=0;i<count;++i) for(int axis=0;axis<2;++axis) {
            const float *row=bake.transform[3][axis];
            vertices[i].uv[1][axis]=row[0]*vertices[i].uv[0][0]+row[1]*vertices[i].uv[0][1]+row[3];
        }
        assert(n3ds_gpu_texture_bind(1,bake.textures[3]));
    }
    for(unsigned int i=0;i<count;++i) for(int axis=0;axis<2;++axis)
        vertices[i].uv[0][axis]=(vertices[i].uv[0][axis]-bounds[axis])/(bounds[axis+2]-bounds[axis]);
    assert(n3ds_gpu_texture_bind(0,texture));
    assert(n3ds_gpu_chicago_relative_draw(&native,vertices,count,indices,index_count));
    static const void *seen[32];static int seen_count;
    int i;for(i=0;i<seen_count && seen[i]!=shader;++i) {}
    if(i==seen_count && seen_count<32) {
        seen[seen_count++]=shader;
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            char text[240];snprintf(text,sizeof(text),"NATIVE GENERIC %s: maps=%d stages=%d size=%d fade=%d triangles=%u name=%.130s",
                shield?"SHIELD MASK":"BAKE",bake.maps,bake.stages,NATIVE_GENERIC_BAKE_SIZE,g->fade_mode,index_count/3,view->instances[t].name);n3ds_log(text);break;
        }
    }
}
