#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include "engine_text.h"
#include "engine_hud.h"
#include "engine_textures.h"
#include "engine_renderer.h"
#include "engine_text_shader.h"

enum { TEXT_VERTICES=6144 };
static DVLB_s *shader;
static shaderProgram_s program;
static int program_ready,matrix_location;
static C3D_Mtx text_projection;
static float projection_width,projection_height;
static C3D_AttrInfo text_attributes;
static struct native_text_vertex *stream;
static unsigned int used;
static unsigned int deferred,immediate,flushes;
static int dirty;
void n3ds_log(const char *message);
long long n3ds_engine_ticks(void);
static int point_sampled,wrapped;
static C3D_Tex touch_texture;
static int touch_ready;
static unsigned int touch_revision;
static int hud_layout;
static float sensor_x=42,sensor_y=438,sensor_scale=128.f/42.f;
void n3ds_gpu_hud_layout(int group) {hud_layout=group;}
void n3ds_gpu_hud_sensor_origin(float x,float y,float radius)
{sensor_x=x;sensor_y=y;sensor_scale=128.f/radius;}
int n3ds_gpu_touch_panel(const unsigned int *pixels,unsigned int revision)
{
    if(!touch_ready) {
        if(!C3D_TexInit(&touch_texture,512,64,GPU_RGBA8)) return 0;
        touch_ready=1;touch_revision=~revision;
    }
    /* Called once after FrameBegin's queue wait, before this texture is used.
     * Reusing this resource avoids allocating or stalling when counts change. */
    if(touch_revision!=revision) {
        unsigned int *out=touch_texture.data;
        for(unsigned int y=0;y<64;++y) for(unsigned int x=0;x<512;++x) {
            unsigned int ty=63-y;
            unsigned int m=(x&1)|((ty&1)<<1)|((x&2)<<1)|((ty&2)<<2)|((x&4)<<2)|((ty&4)<<3);
            out[((ty>>3)*64+(x>>3))*64+m]=pixels[y*512+x];
        }
        C3D_TexFlush(&touch_texture);touch_revision=revision;
    }
    struct native_text_vertex v[4]={0};
    static const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;++i) {
        v[i].position[0]=512*uv[i][0];v[i].position[1]=184+64*uv[i][1];
        v[i].uv[0]=uv[i][0];v[i].uv[1]=uv[i][1];
        for(int j=0;j<4;++j) v[i].color[j]=1;
    }
    void *saved=n3ds_gpu_texture_bound(0);
    n3ds_gpu_texture_bind(0,&touch_texture);
    int ok=n3ds_gpu_text_begin(320,240,0xffffffff,1,0);
    n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
    if(ok) ok=n3ds_gpu_text_draw(v);
    n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
    return ok;
}
void n3ds_gpu_text_dispose(void)
{
    char message[128];snprintf(message,sizeof(message),"TEXT TRANSFERS: deferred=%u immediate=%u frame_flushes=%u",deferred,immediate,flushes);
    n3ds_log(message);deferred=immediate=flushes=0;dirty=0;
    n3ds_gpu_hud_dispose();
    if(touch_ready) {C3D_TexDelete(&touch_texture);touch_ready=0;}
    n3ds_gpu_program_park();
    if(program_ready) shaderProgramFree(&program);
    if(shader) DVLB_Free(shader);
    linearFree(stream); stream=NULL; shader=NULL; program_ready=0; used=0;
    projection_width=projection_height=0;
}
/* Map-owned copied HUD images may sit between large freed tag blocks. Fixed
 * text streams/shaders remain allocated; these images are recreated lazily. */
void n3ds_gpu_text_map_release(void)
{
    n3ds_gpu_hud_dispose();
    if(touch_ready) {C3D_TexDelete(&touch_texture);touch_ready=0;}
}
int n3ds_gpu_text_initialize(void)
{
    if(shader || stream) return 0;
    shader=DVLB_ParseFile((u32 *)engine_text_shader,sizeof(engine_text_shader));
    stream=linearAlloc(TEXT_VERTICES*sizeof(*stream));
    if(!shader || !stream) goto fail;
    shaderProgramInit(&program); program_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&program,&shader->DVLE[0]))) goto fail;
    matrix_location=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
    if(matrix_location<0) goto fail;
    AttrInfo_Init(&text_attributes);
    AttrInfo_AddLoader(&text_attributes,0,GPU_FLOAT,2);
    AttrInfo_AddLoader(&text_attributes,1,GPU_FLOAT,4);
    AttrInfo_AddLoader(&text_attributes,2,GPU_FLOAT,2);
    return 1;
fail: n3ds_gpu_text_dispose(); return 0;
}
void n3ds_gpu_text_frame_begin(void) { used=0;dirty=0; n3ds_gpu_hud_frame_begin(); }
void n3ds_gpu_text_flush(void)
{ if(dirty && used) {GSPGPU_FlushDataCache(stream,used*sizeof(*stream));++flushes;}dirty=0; }
int n3ds_gpu_text_begin(float width,float height,unsigned int tint,int point,int wrap)
{
    long long stamp=n3ds_engine_ticks();
    if(!program_ready || width<=0 || height<=0) return 0;
    point_sampled=point; wrapped=wrap;
    C3D_BindProgram(&program);
    /* Projection depends only on logical screen dimensions, not each glyph.
     * Always upload it because another material may have used the registers. */
    if(width!=projection_width || height!=projection_height) {
        Mtx_OrthoTilt(&text_projection,0,width,height,0,0,1,true);
        projection_width=width;projection_height=height;
    }
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&text_projection);
    C3D_SetAttrInfo(&text_attributes);
    for(unsigned int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_TexEnvColor(env,tint);
    env=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(env,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    C3D_CullFace(GPU_CULL_NONE); C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
    n3ds_gpu_hud_profile_add(2,n3ds_engine_ticks()-stamp);
    return 1;
}
static int text_draw(const struct native_text_vertex vertices[4],int offscreen)
{
    long long stamp=n3ds_engine_ticks();
    static const unsigned int order[6]={0,1,2,0,2,3};
    C3D_Tex *texture=n3ds_gpu_texture_bound(0);
    if(!texture || !stream || used>TEXT_VERTICES-6) return 0;
    u32 saved_sampler=texture->param;
    /* Glyph upload may have replaced the resource after text_begin. */
    C3D_TexSetFilter(texture,point_sampled?GPU_NEAREST:GPU_LINEAR,point_sampled?GPU_NEAREST:GPU_LINEAR);
    C3D_TexSetWrap(texture,wrapped==2?GPU_CLAMP_TO_BORDER:wrapped?GPU_REPEAT:GPU_CLAMP_TO_EDGE,wrapped==2?GPU_CLAMP_TO_BORDER:wrapped?GPU_REPEAT:GPU_CLAMP_TO_EDGE);
    n3ds_gpu_texture_bind(0,texture);
    for(unsigned int i=0;i<6;++i) {
        stream[used+i]=vertices[order[i]];
        float *p=stream[used+i].position;
        if(!offscreen && hud_layout==1) {p[0]=16+p[0]*1.8f;p[1]=16+p[1]*1.8f;}
        else if(!offscreen && hud_layout==2) {p[0]=624+(p[0]-640)*2;p[1]=16+p[1]*2;}
        else if(!offscreen && hud_layout==3) {p[0]=320+(p[0]-sensor_x)*sensor_scale;p[1]=236+(p[1]-sensor_y)*sensor_scale;}
    }
    if(n3ds_gpu_frame_active()) {dirty=1;++deferred;}
    else {GSPGPU_FlushDataCache(stream+used,6*sizeof(*stream));++immediate;}
    if(!n3ds_gpu_vertex_buffer_bind(stream,sizeof(*stream),3,0x210)) {
        texture->param=saved_sampler; n3ds_gpu_texture_bind(0,texture); return 0;
    }
    C3D_DrawArrays(GPU_TRIANGLES,used,6);
    if(!offscreen && n3ds_gpu_overlay_other_begin()) {C3D_DrawArrays(GPU_TRIANGLES,used,6);n3ds_gpu_overlay_other_end();}
    used+=6;
    /* DrawArrays has emitted the sampler state. Do not change how another
     * material samples this shared texture after the text pass. */
    texture->param=saved_sampler; n3ds_gpu_texture_bind(0,texture);
    n3ds_gpu_hud_profile_add(3,n3ds_engine_ticks()-stamp);
    return 1;
}

int n3ds_gpu_text_draw(const struct native_text_vertex v[4]) {return text_draw(v,0);}
int n3ds_gpu_text_offscreen_draw(const struct native_text_vertex v[4]) {return text_draw(v,1);}
int n3ds_gpu_text_offscreen_begin(float width,float height)
{
    if(!n3ds_gpu_text_begin(width,height,0xffffffff,0,2)) return 0;
    C3D_Mtx projection;Mtx_Ortho(&projection,0,width,0,height,0,1,true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&projection);
    return 1;
}
