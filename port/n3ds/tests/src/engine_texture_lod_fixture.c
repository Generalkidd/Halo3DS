#if HALO_N3DS_TEXTURE_LOD_FIXTURE
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_scene_shader.h"

enum {
    LOD_WIDTH=400, LOD_HEIGHT=240, LOD_PIXELS=LOD_WIDTH*LOD_HEIGHT,
    LOD_SURFACES=3, LOD_ROUTE_FRAMES=60, LOD_TIMING_FRAMES=240
};
static const u32 lod_transfer =
    GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

void n3ds_log(const char *message);
void n3ds_engine_pace_frame(long long start);
void n3ds_stall_mark(const char *stage,unsigned int frame,int tick);

struct lod_vertex { float position[3], color[3], uv[3]; };

static void lod_corridor_vertices(struct lod_vertex *vertices)
{
    /* Sand on the floor, cliff rock on the left, brushed metal on the
     * right. Keep geometry, tint and UV repeat identical for each tier. */
    const float positions[LOD_SURFACES][4][3]={
        {{-4,0,0},{4,0,0},{4,0,70},{-4,0,70}},
        {{-4,0,0},{-4,3.5f,0},{-4,3.5f,70},{-4,0,70}},
        {{4,0,0},{4,3.5f,0},{4,3.5f,70},{4,0,70}},
    };
    const float colors[LOD_SURFACES][3]={
        {1.0f,0.90f,0.76f},{0.88f,0.94f,1.0f},{0.78f,0.89f,1.0f}
    };
    const float span[LOD_SURFACES]={5.0f,2.5f,2.5f};
    const unsigned int triangles[]={0,1,2,0,2,3};
    for(unsigned int surface=0;surface<LOD_SURFACES;++surface) {
        for(unsigned int i=0;i<6;++i) {
            struct lod_vertex *vertex=vertices+surface*6+i;
            unsigned int corner=triangles[i];
            memcpy(vertex->position,positions[surface][corner],sizeof(vertex->position));
            memcpy(vertex->color,colors[surface],sizeof(vertex->color));
            vertex->uv[0]=corner==1 || corner==2?span[surface]:0.f;
            vertex->uv[1]=corner>=2?43.75f:0.f;
            vertex->uv[2]=1.f;
        }
    }
}

static int lod_draw(C3D_RenderTarget *target, int projection_location,
    void *textures[4], unsigned int frame)
{
    const float phase=(float)(frame%LOD_ROUTE_FRAMES)/(LOD_ROUTE_FRAMES-1);
    const float eased=phase*phase*(3.f-2.f*phase);
    const float x=-3.55f+6.65f*eased, y=1.42f, z=1.2f+8.8f*phase;
    const float yaw=-0.85f+1.49f*eased;
    C3D_Mtx projection,view,combined;
    Mtx_PerspTilt(&projection,0.95f,(float)LOD_WIDTH/LOD_HEIGHT,0.12f,95.f,false);
    Mtx_LookAt(&view,FVec3_New(x,y,z),FVec3_New(x+sinf(yaw),y-0.13f,z+cosf(yaw)),
        FVec3_New(0.f,1.f,0.f),false);
    Mtx_Multiply(&combined,&projection,&view);
    if(!C3D_FrameBegin(0)) return 0;
    C3D_RenderTargetClear(target,C3D_CLEAR_ALL,0x263849ffu,0);
    C3D_FrameDrawOn(target);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location,&combined);
    for(unsigned int surface=0;surface<LOD_SURFACES;++surface) {
        if(!n3ds_gpu_texture_bind(0,textures[surface])) {
            C3D_FrameEnd(GX_CMDLIST_FLUSH);
            return 0;
        }
        C3D_DrawArrays(GPU_TRIANGLES,surface*6,6);
    }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    return 1;
}

#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 1
static int lod_capture(u32 *pixels,unsigned int frame)
{
    const size_t bytes=LOD_PIXELS*sizeof(*pixels);
    /* Read the display transfer that actually fed the top screen. Allow the
     * posted swap to latch before asking libctru for the front buffer. */
    gspWaitForVBlank();
    u16 width=0,height=0;
    gfxSetDoubleBuffering(GFX_TOP,false);
    const u8 *front=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&width,&height);
    gfxSetDoubleBuffering(GFX_TOP,true);
    if(!front || width!=LOD_HEIGHT || height!=LOD_WIDTH ||
       R_FAILED(GSPGPU_InvalidateDataCache((void *)front,LOD_PIXELS*3))) return 0;
    for(unsigned int i=0;i<LOD_PIXELS;++i)
        pixels[i]=(u32)front[3*i+2]<<24|(u32)front[3*i+1]<<16|(u32)front[3*i]<<8|255u;
    char path[72];
    snprintf(path,sizeof(path),"sdmc:/halo-texture-lod-%03u.rgba",frame);
    FILE *file=fopen(path,"wb");
    if(!file) return 0;
    int wrote=fwrite(pixels,1,bytes,file)==bytes;
    return fclose(file)==0 && wrote;
}
#else
static struct { u64 start,work_end; } lod_times[LOD_TIMING_FRAMES];

static unsigned long lod_microseconds(u64 ticks)
{ return (unsigned long)(ticks*1000000/SYSCLOCK_ARM11); }

static int lod_export_timing(u64 end)
{
    FILE *file=fopen("sdmc:/halo-texture-lod.csv","wb");
    if(!file) return 0;
    int wrote=fputs("frame,work_us,interval_us\n",file)>=0;
    for(unsigned int i=0;wrote && i<LOD_TIMING_FRAMES;++i) {
        u64 next=i+1<LOD_TIMING_FRAMES?lod_times[i+1].start:end;
        wrote=fprintf(file,"%u,%lu,%lu\n",i,
            lod_microseconds(lod_times[i].work_end-lod_times[i].start),
            lod_microseconds(next-lod_times[i].start))>0;
    }
    return fclose(file)==0 && wrote;
}
#endif

int n3ds_texture_lod_fixture(void)
{
    FILE *mode_file=fopen("sdmc:/halo-texture-lod-mode.txt","rb");
    if(!mode_file) {n3ds_log("TEXTURE LOD FAIL: missing mode file");return 1;}
    int digit=fgetc(mode_file), tail=fgetc(mode_file);
    int valid=!ferror(mode_file);
    if(fclose(mode_file) || !valid || digit<'0' || digit>'2' || tail!=EOF) {
        n3ds_log("TEXTURE LOD FAIL: invalid mode file");
        return 1;
    }
    unsigned int mode=(unsigned int)(digit-'0'),count=0;
    void *textures[4]={0};
    if(!n3ds_gpu_texture_lod_load("sdmc:/halo-texture-lod.ntxm",mode,textures,&count)) {
        n3ds_log("TEXTURE LOD FAIL: invalid texture pack");
        return 1;
    }
    C3D_RenderTarget *target=C3D_RenderTargetCreate(LOD_HEIGHT,LOD_WIDTH,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    DVLB_s *shader=DVLB_ParseFile((u32 *)engine_scene_shader,sizeof(engine_scene_shader));
    struct lod_vertex *vertices=linearAlloc(LOD_SURFACES*6*sizeof(*vertices));
#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 1
    u32 *pixels=linearAlloc(LOD_PIXELS*sizeof(*pixels));
    const unsigned int frames=LOD_ROUTE_FRAMES;
#else
    const unsigned int frames=LOD_TIMING_FRAMES;
#endif
    shaderProgram_s program;
    int result=1,program_ready=0;
    if(count!=LOD_SURFACES || !target || !shader || !vertices) goto done;
#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 1
    if(!pixels) goto done;
#endif
    gfxSet3D(false);
    C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,lod_transfer|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
    shaderProgramInit(&program);program_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&program,&shader->DVLE[0]))) goto done;
    int projection_location=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
    if(projection_location<0) goto done;
    C3D_BindProgram(&program);
    lod_corridor_vertices(vertices);
    if(R_FAILED(GSPGPU_FlushDataCache(vertices,LOD_SURFACES*6*sizeof(*vertices)))) goto done;
    C3D_AttrInfo *attributes=C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    for(unsigned int i=0;i<3;++i) AttrInfo_AddLoader(attributes,i,GPU_FLOAT,3);
    if(!n3ds_gpu_vertex_buffer_bind(vertices,sizeof(*vertices),3,0x210)) goto done;
    for(unsigned int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_TEXTURE0);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    for(unsigned int frame=0;frame<frames;++frame) {
        if(!aptMainLoop()) goto done;
        n3ds_stall_mark("texture-lod",frame+1,(int)(frame%LOD_ROUTE_FRAMES));
        u64 start=svcGetSystemTick();
        if(!lod_draw(target,projection_location,textures,frame) || !n3ds_gpu_texture_barrier()) goto done;
#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 1
        if(!lod_capture(pixels,frame)) goto done;
#else
        lod_times[frame].start=start;
        lod_times[frame].work_end=svcGetSystemTick();
#endif
        n3ds_engine_pace_frame((long long)start);
    }
#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 2
    if(!lod_export_timing(svcGetSystemTick())) goto done;
#endif
    result=0;
done:
    if(!n3ds_gpu_texture_barrier()) result=1;
    n3ds_gpu_texture_bind(0,NULL);
    for(unsigned int i=0;i<count;++i) n3ds_gpu_texture_destroy(textures[i]);
    n3ds_gpu_program_park();
    if(program_ready) shaderProgramFree(&program);
    if(shader) DVLB_Free(shader);
    if(target) C3D_RenderTargetDelete(target);
#if HALO_N3DS_TEXTURE_LOD_FIXTURE == 1
    linearFree(pixels);
#endif
    linearFree(vertices);
    char message[96];
    snprintf(message,sizeof(message),"TEXTURE LOD FIXTURE: %s mode=%u",result?"fail":"pass",mode);
    n3ds_log(message);
    return result;
}
#endif
