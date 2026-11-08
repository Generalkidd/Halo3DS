/* Private renderer-TU overlay. Never compiled into gameplay by build.py. */
#include "internal.h"
#include <sys/stat.h>
#ifndef ED_REFERENCE_ONLY
#define ED_REFERENCE_ONLY 0
#endif
enum { ED_PIXELS=240*400, ED_BYTES=ED_PIXELS*4, ED_CASES=10, ED_PHASES=ED_REFERENCE_ONLY?2:4 };
static unsigned ed_finished;
static void ed_watchdog(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<1200 && !__atomic_load_n(&ed_finished,__ATOMIC_ACQUIRE);++i)svcSleepThread(100000000);
    /* Never free buffers after a stalled queue. Terminate the whole diagnostic. */
    if(!__atomic_load_n(&ed_finished,__ATOMIC_ACQUIRE))svcExitProcess();
}
static int ed_capture(u32 *out,C3D_RenderTarget *rt)
{
    extern int n3ds_c3d_texture_mirror_wait_idle(void);
    extern int n3ds_c3d_texture_mirror_copy(void *,void *,unsigned);
    if(!n3ds_c3d_texture_mirror_wait_idle())return 0;
    for(unsigned plane=0;plane<2;++plane) {
        u32 *dst=out+plane*ED_PIXELS;
        if(aptShouldClose() || !gspHasGpuRight() || R_FAILED(GSPGPU_FlushDataCache(dst,ED_BYTES)))return 0;
        if(!n3ds_c3d_texture_mirror_copy(plane?rt->frameBuf.depthBuf:rt->frameBuf.colorBuf,dst,ED_BYTES))return 0;
        if(R_FAILED(GSPGPU_InvalidateDataCache(dst,ED_BYTES)))return 0;
    }
    return 1;
}
static void ed_state(unsigned phase)
{
    C3D_EarlyDepthTest(phase>=2,phase==3?GPU_EARLYDEPTH_GREATER:GPU_EARLYDEPTH_GEQUAL,0);
    /* SDK framebuffer update may clear early-depth. Install ALL pending state
     * first, then explicitly clear using the emitted DATA=0 configuration. */
    C3Di_UpdateContext();
    GPUCMD_AddWrite(GPUREG_EARLYDEPTH_CLEAR,1);
}
static int ed_commands(FILE *log,unsigned test,unsigned phase,unsigned segment)
{
    u32 *buffer,capacity,end;GPUCMD_GetBuffer(&buffer,&capacity,&end);
    if(!buffer || end>capacity)return 0;
    unsigned draws=0,seen=0,cleared=0;u32 state[4]={0};
    for(u32 offset=0;offset<end;) {
        if(end-offset<2)return 0;
        u32 h=buffer[offset+1],extra=(h>>20)&0x7ff,words=(3+extra)&~1u,reg=h&0x3ff;
        if(words>end-offset)return 0;
        for(u32 i=0;i<=extra;++i) {
            u32 value=buffer[offset+(i?i+1:0)];
            if(reg==GPUREG_EARLYDEPTH_TEST1 || reg==GPUREG_EARLYDEPTH_TEST2 || reg==GPUREG_EARLYDEPTH_FUNC ||
               reg==GPUREG_EARLYDEPTH_DATA || reg==GPUREG_EARLYDEPTH_CLEAR || reg==GPUREG_DRAWARRAYS || reg==GPUREG_DRAWELEMENTS)
                fprintf(log,"command,%u,%u,%u,%lu,%03lx,%01lx,%08lx\n",test,phase,segment,(unsigned long)offset,(unsigned long)reg,(unsigned long)((h>>16)&15),(unsigned long)value);
            const unsigned tracked[]={GPUREG_EARLYDEPTH_TEST1,GPUREG_EARLYDEPTH_TEST2,GPUREG_EARLYDEPTH_FUNC,GPUREG_EARLYDEPTH_DATA};
            for(unsigned t=0;t<4;++t)if(reg==tracked[t]) {
                u32 mask=0;for(unsigned b=0;b<4;++b)if(h&(0x10000u<<b))mask|=0xffu<<(b*8);
                state[t]=(state[t]&~mask)|(value&mask);seen|=1u<<t;
            }
            if(reg==GPUREG_EARLYDEPTH_CLEAR && (value&1) && seen==15)cleared=1;
            if((reg==GPUREG_DRAWARRAYS || reg==GPUREG_DRAWELEMENTS) && (value&1)) {
                if(seen!=15 || !cleared || (state[0]&1)!=(phase>=2) || (state[1]&1)!=(phase>=2) ||
                   (state[2]&3)!=(phase==3?GPU_EARLYDEPTH_GREATER:GPU_EARLYDEPTH_GEQUAL) || (state[3]&0xffffff))return 0;
                ++draws;
            }
            reg+=h>>31;
        }
        offset+=words;
    }
    return draws==(test==7?1:2);
}
int n3ds_early_depth_fixture(void)
{
    const char *names[]={"far-near","near-far","equal-depth","adjacent-float-z","alpha-cutout","stencil-replace-and-fail","target-roundtrip","command-list-split","depth-write-disabled","stencil-depth-fail"};
    int active=0,ok=0;unsigned comparisons=0,mismatches=0;
    FILE *log=NULL;Thread watchdog=NULL;C3D_RenderTarget *other=NULL;
    struct native_render_vertex *vertices=NULL;float *uv=NULL;unsigned short *indices=NULL;u32 *images=NULL;void *texture=NULL;
    mkdir("sdmc:/early-depth",0777);
    log=fopen("sdmc:/early-depth/results.csv","w");if(!log)goto done;
    fprintf(log,"schema,1\nconfiguration,RGBA8,D24S8,240,400,GEQUAL,clear0,early-reference0\nphases,off,off-repeat,GEQUAL0,GREATER0\nstatus,NOT_A_PERFORMANCE_TEST\n");fflush(log);
    fprintf(log,"reference_only,%u\n",ED_REFERENCE_ONLY);
    fprintf(log,"fixture_sha256,%s\n",ED_SOURCE_SHA256);
    u8 model=255;Result model_result=cfguInit();
    if(R_SUCCEEDED(model_result)) {model_result=CFGU_GetSystemModel(&model);cfguExit();}
    fprintf(log,"hardware,model_result,%08lx,model,%u,system_tick,%llu\n",(unsigned long)model_result,model,(unsigned long long)svcGetSystemTick());
    s64 clock=-1,l2=-1;
    if(R_FAILED(svcGetSystemInfo(&clock,0x10001,0)))clock=-1;
    if(R_FAILED(svcGetSystemInfo(&l2,0x10001,2)))l2=-1;
    fprintf(log,"clock_mhz,%lld,l2,%lld,minus_one_unavailable\n",(long long)clock,(long long)l2);fflush(log);
    __atomic_store_n(&ed_finished,0,__ATOMIC_RELEASE);
    watchdog=threadCreate(ed_watchdog,NULL,4096,0x20,-2,false);if(!watchdog)goto done;
    images=linearAlloc(ED_BYTES*4);vertices=linearAlloc(8*sizeof(*vertices));uv=linearAlloc(8*sizeof(*uv));indices=linearAlloc(12);
    other=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!images || !vertices || !uv || !indices || !other)goto done;
    const unsigned short corners[]={0,2,1,0,3,2};memcpy(indices,corners,sizeof(corners));
    for(unsigned i=0;i<4;++i) {uv[2*i]=(i==1 || i==2)?.9375f:.0625f;uv[2*i+1]=i>=2?.9375f:.0625f;}
    unsigned int texels[64];for(unsigned i=0;i<64;++i)texels[i]=(i&4)?0xffffff20u:0xffffffffu;
    texture=n3ds_gpu_texture_create(texels,8,8);if(!texture)goto done;
    if(R_FAILED(GSPGPU_FlushDataCache(uv,8*sizeof(*uv))) || R_FAILED(GSPGPU_FlushDataCache(indices,12)))goto done;
    n3ds_gpu_world_budget(1);n3ds_gpu_frame_sync(0);
    for(unsigned test=0;test<ED_CASES;++test) {
        for(unsigned q=0;q<2;++q)for(unsigned i=0;i<4;++i) {
            float z=q?-2.f:-3.f;
            if(test==1 || test==8 || test==9)z=q?-3.f:-2.f;
            if(test==2)z=-2.f;
            if(test==3)z=q?nextafterf(-2.f,-3.f):-2.f;
            int right=i==1 || i==2,up=i>=2;
            /* Same projected extent for near/far: leaves known clear border. */
            float radius=-z*.32f;
            vertices[4*q+i]=(struct native_render_vertex){{(right?1:-1)*radius,(up?1:-1)*radius,z},{q?1.f:0.f,q?0.f:1.f,0.f},{uv[2*i],uv[2*i+1]}};
        }
        if(R_FAILED(GSPGPU_FlushDataCache(vertices,8*sizeof(*vertices))))goto done;
        for(unsigned phase=0;phase<ED_PHASES;++phase) {
            if(!aptMainLoop() || !gspHasGpuRight())goto done;
            if(!n3ds_gpu_frame_begin())goto done;active=1;
            n3ds_gpu_texture_mirror_idle(0,1);n3ds_gpu_texture_mirror_select(0);
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            n3ds_gpu_world_view_begin();n3ds_gpu_window_begin(&camera);
            C3D_BindProgram(&environment_program);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,&current_projection);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,1,1,0,0);
            C3D_SetAttrInfo(&environment_attributes[0]);C3D_FixedAttribSet(4,0,0,0,1);C3D_FixedAttribSet(5,1,0,0,0);
            C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
            C3D_StencilTest(false,GPU_ALWAYS,0,255,255);C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
            C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
            C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_TEXTURE0,GPU_CONSTANT);
            C3D_TexEnvFunc(C3D_GetTexEnv(0),C3D_Both,GPU_REPLACE);
            if(!n3ds_gpu_texture_bind(0,texture))goto done;
            ed_state(phase);
            for(unsigned q=0;q<2;++q) {
                if(test==4 && q) {
                    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
                    C3D_AlphaTest(true,GPU_GREATER,127);
                }
                if(test==5 || test==9) {
                    C3D_StencilTest(true,q?GPU_EQUAL:GPU_ALWAYS,q && test==5?2:1,255,255);
                    C3D_StencilOp(q?GPU_STENCIL_INCR:GPU_STENCIL_KEEP,GPU_STENCIL_INCR,GPU_STENCIL_REPLACE);
                }
                if(test==8 && !q)C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
                if(test==8 && q)C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
                if(q && test==6) {
                    C3D_FrameDrawOn(other);C3Di_UpdateContext();
                    C3D_FrameDrawOn(target);ed_state(phase);
                }
                if(q && test==7) {
                    if(!ed_commands(log,test,phase,0))goto done;
                    C3D_FrameSplit(GX_CMDLIST_FLUSH);ed_state(phase);
                }
                if(!environment_vertex_buffer_bind(vertices+4*q,uv,0))goto done;
                C3D_DrawElements(GPU_TRIANGLES,6,C3D_UNSIGNED_SHORT,indices);
            }
            if(!ed_commands(log,test,phase,test==7?1:0))goto done;
            n3ds_gpu_present();active=0;
            u32 *capture=images+(phase?2*ED_PIXELS:0);
            if(!ed_capture(capture,target))goto done;
            for(unsigned plane=0;plane<2;++plane) {
                char path[128];snprintf(path,sizeof(path),"sdmc:/early-depth/case%02u-phase%u.%s",test,phase,plane?"d24s8":"rgba");
                FILE *f=fopen(path,"wb");if(!f)goto done;
                int written=fwrite(capture+plane*ED_PIXELS,1,ED_BYTES,f)==ED_BYTES;written&=fclose(f)==0;if(!written)goto done;
            }
            unsigned color=0,depth=0,covered=0,clear=0,red=0,green=0,stencil2=0;
            u32 min_depth=0xffffff,max_depth=0;
            for(unsigned i=0;i<ED_PIXELS;++i) {
                u32 d=capture[ED_PIXELS+i];covered+=d!=0;stencil2+=(d>>24)==2;
                if(d&0xffffff) {if((d&0xffffff)<min_depth)min_depth=d&0xffffff;if((d&0xffffff)>max_depth)max_depth=d&0xffffff;}
                clear+=capture[i]==0x102030ff;
                red+=capture[i]==0xff0000ff;green+=capture[i]==0x00ff00ff;
                if(phase) {color+=images[i]!=capture[i];depth+=images[ED_PIXELS+i]!=capture[ED_PIXELS+i];}
            }
            fprintf(log,"depth_control,%u,%u,%lu,%lu,%u\n",test,phase,(unsigned long)min_depth,(unsigned long)max_depth,stencil2);
            fprintf(log,"result,%u,%s,%u,%u,%u,%u,%u,%u,%u\n",test,names[test],phase,color,depth,covered,clear,red,green);fflush(log);
            /* Reference must contain geometry AND untouched background. */
            if(phase<2 && (covered<1000 || clear<1000 || red+green<1000))goto done;
            if(phase<2) {
                /* Known baseline winners, independent of early-depth. */
                if((test==0 || test==2 || test==6 || test==7 || test==8) && (red<1000 || green))goto done;
                if((test==1 || test==5 || test==9) && (green<1000 || red))goto done;
                if(test==4 && (red<1000 || green<1000 || min_depth==max_depth))goto done;
                if((test==5 || test==9) && stencil2!=covered)goto done;
            }
            if(phase==1 && (color || depth))goto done;
            if(phase>=2) {++comparisons;mismatches+=!!(color || depth);}
        }
    }
    ok=1;
done:
    if(active)n3ds_gpu_present();
    extern int n3ds_c3d_texture_mirror_wait_idle(void);
    if(!n3ds_c3d_texture_mirror_wait_idle())svcExitProcess();
    C3D_EarlyDepthTest(false,GPU_EARLYDEPTH_GREATER,0);
    if(log) {fprintf(log,"complete,%d,candidate_comparisons,%u,different_outputs,%u\n",ok,comparisons,mismatches);fclose(log);}
    n3ds_gpu_texture_destroy(texture);if(other)C3D_RenderTargetDelete(other);
    linearFree(images);linearFree(vertices);linearFree(uv);linearFree(indices);
    __atomic_store_n(&ed_finished,1,__ATOMIC_RELEASE);
    if(watchdog) {if(R_FAILED(threadJoin(watchdog,1000000000)))svcExitProcess();threadFree(watchdog);}
    n3ds_log(ok?"EARLY DEPTH: characterization complete; inspect sdmc:/early-depth/results.csv; no performance or equivalence conclusion":"EARLY DEPTH: fixture failed; inspect partial results; candidate differences alone are not failure");
    return ok;
}
