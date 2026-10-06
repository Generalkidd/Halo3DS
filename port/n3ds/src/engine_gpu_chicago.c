#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
void n3ds_log(const char *message);
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_text.h"
#include "engine_widgets.h"
#include "engine_chicago.h"
#include "engine_generic.h"
#include "engine_chicago_shader.h"
#include "engine_rigid_chicago_shader.h"
#include "engine_skin_chicago_shader.h"
#include "engine_hologram_shader.h"
static DVLB_s *hologram_binary;
static shaderProgram_s hologram_program;
static int hologram_ready,hologram_projection,hologram_screen;
#define VERTICES 32768u
static DVLB_s *shader;
static shaderProgram_s program;
static int ready,location;
static struct {DVLB_s *binary;shaderProgram_s program;int ready,projection,bone,uv;} transform_programs[2];
static struct native_chicago_vertex *stream;
static unsigned int used;
#define BAKE_SIZE NATIVE_GENERIC_BAKE_SIZE
_Static_assert(NATIVE_GENERIC_SAMPLE_SIZE>0 && BAKE_SIZE%NATIVE_GENERIC_SAMPLE_SIZE==0,"Integral generic sample grid");
#define BAKE_SLOTS 32
static C3D_Tex baked[BAKE_SLOTS];
/* Outputs may remain resident between frames, but must never be rewritten
 * after a draw has referenced them in the current frame (including eye two).
 * A source is reusable only when its owner published immutable texels. */
static struct {
    struct native_generic_material material;
    float bounds[4];
    unsigned int identity[4];
    uint64_t last_frame;
    int valid,meter;
} bake_cache[BAKE_SLOTS];
static unsigned int baked_count;
static uint64_t bake_frame=1;
static unsigned int bake_requests,bake_hits,bake_uncached,bake_texels,bake_full,bake_shield_texels;
static void bake_report(void)
{
    if(!bake_requests) return;
    extern void n3ds_log(const char *);
    char message[192];
    snprintf(message,sizeof(message),"GENERIC BAKE CACHE: requests=%u hits=%u uncached=%u evaluated_texels=%u full=%u slots=%u shield_texels=%u",
        bake_requests,bake_hits,bake_uncached,bake_texels,bake_full,baked_count,bake_shield_texels);
    n3ds_log(message);
}
void n3ds_gpu_chicago_map_release(void)
{
    bake_report();
    for(unsigned int i=0;i<baked_count;++i) C3D_TexDelete(&baked[i]);
    memset(baked,0,sizeof(baked));memset(bake_cache,0,sizeof(bake_cache));
    baked_count=0;bake_frame=1;
    bake_requests=bake_hits=bake_uncached=bake_texels=bake_full=bake_shield_texels=0;
}
void n3ds_gpu_chicago_dispose(void)
{
    n3ds_gpu_program_park();
    if(hologram_ready) shaderProgramFree(&hologram_program);
    if(hologram_binary) DVLB_Free(hologram_binary);
    hologram_ready=0;hologram_binary=NULL;
    for(int i=0;i<2;++i) {
        if(transform_programs[i].ready) shaderProgramFree(&transform_programs[i].program);
        if(transform_programs[i].binary) DVLB_Free(transform_programs[i].binary);
    }
    memset(transform_programs,0,sizeof(transform_programs));
    if(ready) shaderProgramFree(&program);
    if(shader) DVLB_Free(shader);
    linearFree(stream); stream=NULL; shader=NULL; ready=0; used=0;
    n3ds_gpu_chicago_map_release();
}
int n3ds_gpu_chicago_initialize(void)
{
    shader=DVLB_ParseFile((u32 *)engine_chicago_shader,sizeof(engine_chicago_shader));
    stream=linearAlloc(VERTICES*sizeof(*stream));
    if(!shader || !stream) goto fail;
    shaderProgramInit(&program); ready=1;
    if(R_FAILED(shaderProgramSetVsh(&program,&shader->DVLE[0]))) goto fail;
    location=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
    if(location<0) goto fail;
    hologram_binary=DVLB_ParseFile((u32 *)engine_hologram_shader,sizeof(engine_hologram_shader));
    if(!hologram_binary) goto fail;
    shaderProgramInit(&hologram_program);hologram_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&hologram_program,&hologram_binary->DVLE[0]))) goto fail;
    hologram_projection=shaderInstanceGetUniformLocation(hologram_program.vertexShader,"projection");
    hologram_screen=shaderInstanceGetUniformLocation(hologram_program.vertexShader,"screen_rows");
    if(hologram_projection<0 || hologram_screen<0) goto fail;
    const unsigned char *data[]={engine_rigid_chicago_shader,engine_skin_chicago_shader};
    const unsigned int bytes[]={sizeof(engine_rigid_chicago_shader),sizeof(engine_skin_chicago_shader)};
    for(int i=0;i<2;++i) {
        transform_programs[i].binary=DVLB_ParseFile((u32 *)data[i],bytes[i]);if(!transform_programs[i].binary) goto fail;
        shaderProgramInit(&transform_programs[i].program);transform_programs[i].ready=1;
        if(R_FAILED(shaderProgramSetVsh(&transform_programs[i].program,&transform_programs[i].binary->DVLE[0]))) goto fail;
        shaderInstance_s *v=transform_programs[i].program.vertexShader;
        transform_programs[i].projection=shaderInstanceGetUniformLocation(v,"projection");
        transform_programs[i].bone=shaderInstanceGetUniformLocation(v,"bone");
        transform_programs[i].uv=shaderInstanceGetUniformLocation(v,"uv_rows");
        if(transform_programs[i].projection<0 || transform_programs[i].bone<0 || transform_programs[i].uv<0) goto fail;
    }
    return 1;
fail: n3ds_gpu_chicago_dispose(); return 0;
}
void n3ds_gpu_chicago_frame_begin(void)
{
    used=0;++bake_frame;
#ifdef HALO_N3DS_RENDERER_TESTS
    if(bake_frame%300==0) bake_report();
#endif
}
/* Called before native FrameEnd; FrameBegin waits before this arena is reused.
 * Draw validation restricts vertices to this append-only allocation. */
void n3ds_gpu_chicago_flush(void)
{ if(used) GSPGPU_FlushDataCache(stream,used*sizeof(*stream)); }
static unsigned int tiled_offset(unsigned int x,unsigned int y,unsigned int width)
{
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    return ((y/8)*(width/8)+x/8)*64+spread[x&7]+2*spread[y&7];
}
static int wrap(int x,int size,int clamped)
{
    return clamped?(x<0?0:x>=size?size-1:x):((x%size)+size)%size;
}
static void sample(const C3D_Tex *t,float u,float v,int point,int cu,int cv,float result[4])
{
    float x=u*t->width-(point?0:.5f),y=v*t->height-(point?0:.5f);
    int ix=(int)floorf(x),iy=(int)floorf(y);float fx=x-ix,fy=y-iy;
    for(int c=0;c<4;++c) result[c]=0;
    for(int j=0;j<(point?1:2);++j) for(int i=0;i<(point?1:2);++i) {
        int tx=wrap(ix+i,t->width,cu),ty=t->height-1-wrap(iy+j,t->height,cv);
        unsigned int texel=((const unsigned int *)t->data)[tiled_offset(tx,ty,t->width)];
        float weight=point?1:(i?fx:1-fx)*(j?fy:1-fy);
        for(int c=0;c<4;++c) result[c]+=((texel>>(24-c*8))&255)*(weight/255.f);
    }
}
static void *bake_material(const struct native_generic_material *m,const float bounds[4],int meter)
{
    if(!ready || !n3ds_generic_validate(m)) return NULL;
    if(meter==2 && !n3ds_generic_shield(m)) return NULL;
    for(int i=0;i<m->maps;++i) if(!m->textures[i]) return NULL;
    for(int i=0;i<4;++i) if(!isfinite(bounds[i])) return NULL;
    if(bounds[2]<=bounds[0] || bounds[3]<=bounds[1]) return NULL;
    unsigned int identity[4]={0};int reusable=1;
    ++bake_requests;
    for(int i=0;i<m->maps;++i) {
        identity[i]=n3ds_gpu_texture_content_id(m->textures[i]);
        if(!identity[i]) reusable=0;
    }
    if(reusable) for(unsigned int i=0;i<baked_count;++i) {
        if(!bake_cache[i].valid || bake_cache[i].meter!=meter ||
           memcmp(bake_cache[i].identity,identity,sizeof(identity)) ||
           memcmp(bake_cache[i].bounds,bounds,sizeof(bake_cache[i].bounds)) ||
           memcmp(&bake_cache[i].material,m,sizeof(*m))) continue;
        bake_cache[i].last_frame=bake_frame;++bake_hits;
        return &baked[i];
    }
    else ++bake_uncached;
    unsigned int slot=baked_count;
    if(slot==BAKE_SLOTS) {
        uint64_t oldest=bake_frame;
        for(unsigned int i=0;i<baked_count;++i) if(bake_cache[i].last_frame<oldest) {
            oldest=bake_cache[i].last_frame;slot=i;
        }
        if(slot==BAKE_SLOTS) {++bake_full;return NULL;}
    } else {
        if(!C3D_TexInit(&baked[baked_count],BAKE_SIZE,BAKE_SIZE,GPU_RGBA8)) return NULL;
        ++baked_count;
    }
    bake_cache[slot].valid=reusable;bake_cache[slot].last_frame=bake_frame;
    if(reusable) {
        bake_cache[slot].material=*m;bake_cache[slot].meter=meter;
        memcpy(bake_cache[slot].identity,identity,sizeof(identity));
        memcpy(bake_cache[slot].bounds,bounds,sizeof(bake_cache[slot].bounds));
    }
    C3D_Tex *t=&baked[slot];
    /* PICA requires an 8x8 texture. Shade generic materials on a coarser
     * grid, then expand each sample; model meters retain their full grid. */
    unsigned int samples=(meter==1 || meter==3)?BAKE_SIZE:NATIVE_GENERIC_SAMPLE_SIZE;
    bake_texels+=samples*samples;
    unsigned int block=BAKE_SIZE/samples;
    int shield=n3ds_generic_shield(m);
    if(shield) bake_shield_texels+=samples*samples;
    for(unsigned int y=0;y<samples;++y) for(unsigned int x=0;x<samples;++x) {
        float u=bounds[0]+(bounds[2]-bounds[0])*(x+.5f)/samples;
        float v=bounds[1]+(bounds[3]-bounds[1])*(y+.5f)/samples;
        float texels[4][4]={{0}},rgba[4];
        for(int i=0;i<m->maps;++i) {
            const float *a=m->transform[i][0],*b=m->transform[i][1];
            float tu=a[0]*u+a[1]*v+a[3],tv=b[0]*u+b[1]*v+b[3];
            if(meter==2) {
                const C3D_Tex *source=m->textures[i];int point=m->point[i];
                float sx=tu*source->width-(point?0:.5f),sy=tv*source->height-(point?0:.5f);
                int ix=(int)floorf(sx),iy=(int)floorf(sy);float fx=sx-ix,fy=sy-iy;
                for(int y=0;y<(point?1:2);++y) for(int x=0;x<(point?1:2);++x) {
                    int tx=wrap(ix+x,source->width,m->clamp_u[i]);
                    int ty=source->height-1-wrap(iy+y,source->height,m->clamp_v[i]);
                    unsigned int pixel=((const unsigned int *)source->data)[tiled_offset(tx,ty,source->width)];
                    float weight=point?1:(x?fx:1-fx)*(y?fy:1-fy);
                    texels[i][3]+=(pixel&255)*(weight/255.f);
                }
            } else sample(m->textures[i],tu,tv,m->point[i],m->clamp_u[i],m->clamp_v[i],texels[i]);
        }
        if(meter==2) {
            const float alpha[4]={texels[0][3],texels[1][3],texels[2][3],texels[3][3]};
            rgba[0]=rgba[1]=rgba[2]=0;rgba[3]=n3ds_generic_shield_intensity(m,alpha);
        } else if(shield) n3ds_generic_shield_evaluate(m,texels,rgba);
        else n3ds_generic_evaluate(m,texels,rgba);
        /* Tint-mode1 blending ignores source alpha. Preserve the original
         * base-map alpha kill independently of the meter's intermediate alpha. */
        if(meter==1) rgba[3]=texels[0][3];
        unsigned int pixel=0;
        for(int c=0;c<4;++c) pixel|=(unsigned int)(rgba[c]*255.f+.5f)<<(24-c*8);
        for(unsigned int by=0;by<block;++by) for(unsigned int bx=0;bx<block;++bx)
            ((unsigned int *)t->data)[tiled_offset(x*block+bx,BAKE_SIZE-1-(y*block+by),BAKE_SIZE)]=pixel;
    }
    C3D_TexSetFilter(t,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);C3D_TexFlush(t);
    return t;
}
void n3ds_gpu_generic_sample(const struct native_generic_material *m,int map,float u,float v,float result[4])
{
    const float *a=m->transform[map][0],*b=m->transform[map][1];
    sample(m->textures[map],a[0]*u+a[1]*v+a[3],b[0]*u+b[1]*v+b[3],
        m->point[map],m->clamp_u[map],m->clamp_v[map],result);
}
void *n3ds_gpu_generic_bake(const struct native_generic_material *m,const float bounds[4])
{return bake_material(m,bounds,0);}
void *n3ds_gpu_meter_bake(const struct native_generic_material *m,const float bounds[4])
{return bake_material(m,bounds,1);}
void *n3ds_gpu_shield_bake(const struct native_generic_material *m,const float bounds[4])
{return bake_material(m,bounds,2);}
void *n3ds_gpu_hologram_stream_bake(const struct native_generic_material *m,const float bounds[4])
{return bake_material(m,bounds,3);}
struct native_chicago_vertex *n3ds_gpu_chicago_allocate(unsigned int count)
{
    if(!ready || !count || count>VERTICES-used) return NULL;
    struct native_chicago_vertex *result=stream+used; used+=count; return result;
}
static void combine(C3D_TexEnv *env,int mode,int function,GPU_TEVSRC incoming,int replicate)
{
    GPU_COMBINEFUNC op=GPU_REPLACE;
    GPU_TEVSRC a=GPU_PREVIOUS,b=incoming,c=GPU_CONSTANT;
    if(function==1) a=incoming;
    else if(function==2 || function==3) op=GPU_MODULATE;
    else if(function==4) op=GPU_ADD;
    else if(function==7 || function==8) {
        op=GPU_SUBTRACT;
        if(function==7) {a=incoming;b=GPU_PREVIOUS;}
    } else if(function>=9 && function<=12) {
        op=GPU_INTERPOLATE;
        if(function==9 || function==11) {a=incoming;b=GPU_PREVIOUS;}
        c=function<=10?GPU_PREVIOUS:incoming;
    }
    C3D_TexEnvSrc(env,mode,a,b,c);
    C3D_TexEnvFunc(env,mode,op);
    if(mode==C3D_RGB) {
        C3D_TexEnvOpRgb(env,replicate && a==incoming?GPU_TEVOP_RGB_SRC_ALPHA:GPU_TEVOP_RGB_SRC_COLOR,
            replicate && b==incoming?GPU_TEVOP_RGB_SRC_ALPHA:GPU_TEVOP_RGB_SRC_COLOR,
            function>=9?GPU_TEVOP_RGB_SRC_ALPHA:GPU_TEVOP_RGB_SRC_COLOR);
    }
    if(function==3) C3D_TexEnvScale(env,mode,GPU_TEVSCALE_2);
}
int n3ds_gpu_effect_layer_draw(struct native_chicago_vertex *vertices,unsigned int count,int blend,
    unsigned int primary_sampler,unsigned int secondary_sampler,unsigned int geometry_flags)
{
    C3D_Mtx projection;
    void *original[2]={n3ds_gpu_texture_bound(0),n3ds_gpu_texture_bound(1)};
    unsigned short indices[384];
    if(!ready || !vertices || !count || count>384 || count%3 || !original[0] || !original[1] ||
       (blend!=NATIVE_BLEND_ADD && blend!=NATIVE_BLEND_SUBTRACT && blend!=NATIVE_BLEND_MAX) ||
       (primary_sampler&~7u) || (secondary_sampler&~7u) || (geometry_flags&(1u<<7)) ||
       vertices<stream || vertices+count>stream+used || !n3ds_gpu_world_projection((float *)&projection)) return 0;
    /* Only texture unit0 supports projective sampling. Swap the two physical
     * samplers; retain homogeneous Q until rasterization so sloped/crossing
     * quads and both stereo views have the correct screen-anchored mapping. */
    for(int i=0;i<2;++i) {
        unsigned int flags=i?primary_sampler:secondary_sampler;
        unsigned int override=(flags&1?NATIVE_SAMPLER_MAG_POINT|NATIVE_SAMPLER_MIN_POINT:0)|
            (flags&2?NATIVE_SAMPLER_CLAMP_U:0)|(flags&4?NATIVE_SAMPLER_CLAMP_V:0)|
            (i?0:NATIVE_SAMPLER_PROJECTIVE);
        if(!n3ds_gpu_texture_bind_sampler(i,original[1-i],override)) return 0;
    }
    C3D_BindProgram(&program);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&projection);
    C3D_AttrInfo *attributes=C3D_GetAttrInfo();AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);
    for(int i=1;i<4;++i) AttrInfo_AddLoader(attributes,i,GPU_FLOAT,2);
    AttrInfo_AddLoader(attributes,4,GPU_FLOAT,4);
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_CONSTANT);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    env=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0,GPU_CONSTANT);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    if(!(geometry_flags&(1u<<2))) {
        env=C3D_GetTexEnv(2);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,GPU_CONSTANT);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    }
    env=C3D_GetTexEnv(3);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0,GPU_CONSTANT);
    C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(!(geometry_flags&(1u<<3)),GPU_GEQUAL,GPU_WRITE_COLOR&~GPU_WRITE_ALPHA);
    C3D_StencilTest(true,GPU_EQUAL,0,1,0);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    /* Original Xbox uses the same source-color combiner for these three
     * blend modes; only the framebuffer equation differs. No extra pass. */
    n3ds_gpu_framebuffer_blend(blend);
    for(unsigned int i=0;i<count;++i) indices[i]=i;
    int result=n3ds_gpu_indexed_draw(vertices,indices,count,sizeof(*vertices),5,0x43210);
    n3ds_gpu_world_state_restore();
    for(int i=0;i<2;++i) n3ds_gpu_texture_bind(i,original[i]);
    return result;
}
static int chicago_draw(const struct native_chicago_material *m,
    struct native_chicago_vertex *vertices,unsigned int vertex_count,
    const unsigned short *indices,unsigned int index_count,const void *gpu_vertices,
    const float *rows,unsigned int nodes,int skinned,const float *uv_rows,int relative)
{
    C3D_Mtx projection;
    void *textures[3]={0};
    if(!ready || !m || m->maps<1 || m->maps>3 || m->blend<0 || m->blend>=NATIVE_BLEND_COUNT ||
       !(m->fade>=0 && m->fade<=1) || (!vertices && !gpu_vertices) || !indices || !vertex_count ||
       (m->vertex_fade && (m->vertex_fade<1 || m->vertex_fade>2 || (m->vertex_fade==1 && m->maps!=1 && !m->shield_mask))) ||
       (m->meter && (m->maps!=1 || m->fade!=1 || m->vertex_fade)) ||
       (m->vertex_color && (m->maps!=1 || m->vertex_fade || m->meter)) ||
       (m->generic_lit_lerp && (m->maps!=2 || m->blend!=0 || m->vertex_color || m->vertex_fade || m->meter)) ||
       (m->shield_mask && (m->maps!=2 || m->vertex_color || m->meter || m->generic_lit_lerp)) ||
       (!gpu_vertices && ((uintptr_t)vertices<(uintptr_t)stream || (uintptr_t)vertices>(uintptr_t)(stream+used) ||
       vertex_count>(unsigned int)(stream+used-vertices))) ||
       !n3ds_gpu_world_projection((float *)&projection)) return 0;
    if(gpu_vertices && (!rows || !uv_rows || !nodes || nodes>28 || skinned<0 || skinned>1 ||
        m->vertex_fade || m->meter || m->vertex_color || m->generic_lit_lerp || m->shield_mask)) return 0;
    if(m->sky && !n3ds_gpu_sky_projection((float *)&projection)) return 0;
    if(relative && !m->sky) {float origin[3];if(!n3ds_gpu_model_projection((float *)&projection,origin))return 0;}
    for(unsigned int i=0;i<index_count;++i) if(indices[i]>=vertex_count) return 0;
    for(int i=0;i<m->maps-1;++i)
        if(!n3ds_chicago_native_function(m->color_function[i]) || !n3ds_chicago_native_function(m->alpha_function[i])) return 0;
    for(int i=0;i<m->maps;++i) {
        textures[i]=n3ds_gpu_texture_bound(i); if(!textures[i] && !m->vertex_color) return 0;
    }
    if(!gpu_vertices && !m->vertex_color && !m->generic_lit_lerp && m->vertex_fade!=2) for(unsigned int i=0;i<vertex_count;++i)
        for(int c=0;c<4;++c) vertices[i].color[c]=m->vertex_fade?vertices[i].uv[2][0]:1.f;
    if(gpu_vertices) {
        float origin[3]={0};
        if(!m->sky && !n3ds_gpu_model_projection((float *)&projection,origin))return 0;
        C3D_BindProgram(&transform_programs[skinned].program);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,transform_programs[skinned].projection,&projection);
        for(unsigned int i=0;i<nodes*3;++i) C3D_FVUnifSet(GPU_VERTEX_SHADER,transform_programs[skinned].bone+i,rows[i*4],rows[i*4+1],rows[i*4+2],rows[i*4+3]-origin[i%3]);
        for(int i=0;i<6;++i) {
            const float *v=uv_rows+i*4;
            C3D_FVUnifSet(GPU_VERTEX_SHADER,transform_programs[skinned].uv+i,v[0],v[1],0,v[3]);
        }
        C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
        AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
        if(skinned) AttrInfo_AddLoader(a,3,GPU_FLOAT,4);
    } else {
    C3D_BindProgram(&program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&projection);
    C3D_AttrInfo *attributes=C3D_GetAttrInfo(); AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);
    for(int i=1;i<4;++i) AttrInfo_AddLoader(attributes,i,GPU_FLOAT,2);
    AttrInfo_AddLoader(attributes,4,GPU_FLOAT,4);
    }
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0); C3D_TexEnvBufColor(0);
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,m->vertex_color?GPU_PRIMARY_COLOR:GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    for(int i=1;i<m->maps;++i) {
        env=C3D_GetTexEnv(i);
        combine(env,C3D_RGB,m->color_function[i-1],GPU_TEXTURE0+i,m->alpha_replicate[i-1]);
        combine(env,C3D_Alpha,m->alpha_function[i-1],GPU_TEXTURE0+i,0);
    }
    if(m->generic_lit_lerp) {
        env=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE1,GPU_TEXTURE0);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);
        /* Xbox cross-component alpha reads BLUE, not the texture's alpha. */
        C3D_TexEnvOpAlpha(env,GPU_TEVOP_A_ONE_MINUS_SRC_B,0,0);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
        env=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(env,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    }
    if(m->shield_mask) {
        /* tint*(1-mask.a) + intensity.a*mask.rgb, clamped once after addition.
         * Two samplers/stages; alpha and the existing fade/blend stay intact. */
        env=C3D_GetTexEnv(0);C3D_TexEnvColor(env,m->shield_tint);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT,GPU_TEXTURE1,GPU_CONSTANT);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA,0);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
        env=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE1,GPU_PREVIOUS);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MULTIPLY_ADD);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    }
    /* Original blend-specific fade: alpha modes scale alpha; add scales RGB;
     * multiply interpolates toward white (half gray for double multiply). */
    env=C3D_GetTexEnv(m->maps);
    unsigned int fade=(unsigned int)(m->fade*255.f+.5f);
    C3D_TexEnvColor(env,fade*0x01010101u);
    int channel=(m->blend==NATIVE_BLEND_ALPHA)?C3D_Alpha:C3D_RGB;
    if(m->blend==NATIVE_BLEND_PREMULTIPLIED_ALPHA) channel=C3D_Both;
    C3D_TexEnvSrc(env,channel,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
    C3D_TexEnvFunc(env,channel,GPU_MODULATE);
    if(m->fade!=1.f && (m->blend==NATIVE_BLEND_MULTIPLY || m->blend==NATIVE_BLEND_MIN || m->blend==NATIVE_BLEND_DOUBLE_MULTIPLY)) {
        env=C3D_GetTexEnv(m->maps+1);
        unsigned int neutral=m->blend==NATIVE_BLEND_DOUBLE_MULTIPLY?(255-fade)/2:255-fade;
        C3D_TexEnvColor(env,neutral*0x01010101u);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_ADD);
    }
    if(m->vertex_fade) {
        /* Compose angular and animation fades once, before blending. */
        for(unsigned int i=0;i<vertex_count;++i) for(int c=0;c<4;++c) vertices[i].color[c]*=m->fade;
        for(int i=m->maps;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
        env=C3D_GetTexEnv(m->maps);
        C3D_TexEnvSrc(env,channel,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(env,channel,GPU_MODULATE);
        if(m->blend==NATIVE_BLEND_MULTIPLY || m->blend==NATIVE_BLEND_MIN || m->blend==NATIVE_BLEND_DOUBLE_MULTIPLY) {
            env=C3D_GetTexEnv(m->maps+1);
            C3D_TexEnvColor(env,m->blend==NATIVE_BLEND_DOUBLE_MULTIPLY?0x80808080u:0xffffffffu);
            C3D_TexEnvSrc(env,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PREVIOUS);
            C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_MULTIPLY_ADD);
        }
    }
    C3D_DepthTest(!m->sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    /* Xbox D3DCULL_CCW rejects counterclockwise faces, rather than keeping
     * them as the front-facing side. */
    C3D_CullFace(m->two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    C3D_AlphaTest(m->alpha_test,GPU_GREATER,127);
    n3ds_gpu_model_stencil_apply();
    n3ds_gpu_framebuffer_blend(m->blend);
    if(m->meter) {
        /* Apply brightness in TEV so the blend unit only needs constant RGB.
         * This also avoids backends that cannot combine constant-alpha and
         * constant-color factors in the same RGB blending equation. */
        env=C3D_GetTexEnv(4);C3D_TexEnvColor(env,m->meter_tint);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
        C3D_AlphaTest(true,GPU_GREATER,0);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_CONSTANT_COLOR,GPU_ZERO,GPU_ONE);
        C3D_BlendingColor(m->meter_tint);
    }
    for(int i=0;i<m->maps && !m->vertex_color;++i) {
        /* Preserve immutable owner identity even with per-unit sampler state.
         * Identical overrides and restores do not dirty the GPU texture cache. */
        unsigned int flags=(m->point[i]?(NATIVE_SAMPLER_MIN_POINT|((m->generic_lit_lerp || m->shield_mask)?NATIVE_SAMPLER_MAG_POINT:0)):0)|
            (m->clamp_u[i]?NATIVE_SAMPLER_CLAMP_U:0)|(m->clamp_v[i]?NATIVE_SAMPLER_CLAMP_V:0);
        if(!n3ds_gpu_texture_bind_sampler(i,textures[i],flags)) return 0;
    }
    int result=gpu_vertices?n3ds_gpu_indexed_draw(gpu_vertices,indices,index_count,
        skinned?sizeof(struct native_skin_vertex):sizeof(struct native_render_vertex),skinned?4:3,skinned?0x3210:0x210):
        n3ds_gpu_indexed_draw(vertices,indices,index_count,sizeof(*vertices),5,0x43210);
    for(int i=0;i<m->maps && !m->vertex_color;++i) n3ds_gpu_texture_bind(i,textures[i]);
    n3ds_gpu_world_state_restore();
    return result;
}

int n3ds_gpu_chicago_draw(const struct native_chicago_material *m,struct native_chicago_vertex *v,
    unsigned int n,const unsigned short *indices,unsigned int count)
{return chicago_draw(m,v,n,indices,count,NULL,NULL,0,0,NULL,0);}
int n3ds_gpu_chicago_relative_draw(const struct native_chicago_material *m,struct native_chicago_vertex *v,
    unsigned int n,const unsigned short *indices,unsigned int count)
{return chicago_draw(m,v,n,indices,count,NULL,NULL,0,0,NULL,1);}
int n3ds_gpu_chicago_transform_draw(const struct native_chicago_material *m,const void *v,
    unsigned int n,const unsigned short *indices,unsigned int count,const float *rows,unsigned int nodes,int skinned,const float *uv)
{return chicago_draw(m,NULL,n,indices,count,v,rows,nodes,skinned,uv,0);}
#include "engine_chicago_offload_tests.inl"
#include "engine_hologram_gpu.inl"
#include "engine_hologram_tests.inl"
