#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <stdio.h>
#include "engine_textures.h"
#include "engine_renderer.h"
#include "texture_test_shader.h"

/* Original engine ABI: a signed 16-bit selector, no SDK/CRT structures cross. */
void rasterizer_set_framebuffer_blend_function(short mode);
void n3ds_log(const char *message);
struct blend_vertex { float position[3],uv[2]; };
static unsigned int expected_channel(unsigned int mode,unsigned int s,unsigned int d,unsigned int a)
{
    int numerator;
    switch(mode) {
    case 0: numerator=s*a+d*(255-a); break;
    case 1: numerator=s*d; break;
    case 2: numerator=2*s*d; break;
    case 3: numerator=255*(s+d); break;
    case 4: numerator=255*((int)d-(int)s); break;
    case 5: return s<d ? s : d;
    case 6: return s>d ? s : d;
    case 7: numerator=255*s+d*(255-a); break;
    default: abort();
    }
    if(numerator<0) return 0;
    if(numerator>255*255) return 255;
    return (numerator+127)/255;
}
int n3ds_gpu_blend_tests(void)
{
    static const unsigned char source[4][4]={{43,179,227,101},{221,97,11,0},{21,131,249,255},{241,223,199,197}};
    static const unsigned char destination[4][4]={{211,89,37,173},{41,201,173,149},{241,59,17,31},{231,213,243,229}};
    C3D_RenderTarget *target=NULL; DVLB_s *shader=NULL; shaderProgram_s program;
    struct blend_vertex *vertices=NULL; u32 *readback=NULL;
    int program_ready=0,result=1;
    FILE *proof=NULL;
    unsigned int rows[32][6],row=0,maximum_difference=0;
    target=C3D_RenderTargetCreate(64,64,GPU_RB_RGBA8,-1);
    shader=DVLB_ParseFile((u32 *)texture_test_shader,sizeof(texture_test_shader));
    vertices=linearAlloc(6*sizeof(*vertices)); readback=linearAlloc(64*64*4);
    if(!target || !shader || !vertices || !readback) goto done;
    shaderProgramInit(&program); program_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&program,&shader->DVLE[0]))) goto done;
    C3D_BindProgram(&program);
    {
        static const float corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
        for(unsigned int i=0;i<6;++i) vertices[i]=(struct blend_vertex){{corners[i][0],corners[i][1],0},{0,0}};
    }
    GSPGPU_FlushDataCache(vertices,6*sizeof(*vertices));
    C3D_AttrInfo *attributes=C3D_GetAttrInfo(); AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3); AttrInfo_AddLoader(attributes,1,GPU_FLOAT,2);
    if(!n3ds_gpu_vertex_buffer_bind(vertices,sizeof(*vertices),2,0x10)) goto done;
    for(unsigned int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv *environment=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(environment,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);
    C3D_TexEnvFunc(environment,C3D_Both,GPU_REPLACE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR); C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false,GPU_ALWAYS,0); C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
    for(unsigned int vector=0;vector<4;++vector) for(unsigned int mode=0;mode<8;++mode) {
        const unsigned char *s=source[vector],*d=destination[vector];
        unsigned int source_word=(unsigned int)s[0]<<24|(unsigned int)s[1]<<16|(unsigned int)s[2]<<8|s[3];
        unsigned int destination_word=(unsigned int)d[0]<<24|(unsigned int)d[1]<<16|(unsigned int)d[2]<<8|d[3];
        unsigned int expected=0,difference=0;
        for(unsigned int channel=0;channel<4;++channel) expected=(expected<<8)|expected_channel(mode,s[channel],d[channel],s[3]);
        if(!C3D_FrameBegin(0)) goto done;
        C3D_RenderTargetClear(target,C3D_CLEAR_COLOR,destination_word,0);
        C3D_FrameDrawOn(target);
        C3D_TexEnvColor(C3D_GetTexEnv(0),s[0]|(unsigned int)s[1]<<8|(unsigned int)s[2]<<16|(unsigned int)s[3]<<24);
        rasterizer_set_framebuffer_blend_function((short)mode);
        if(n3ds_gpu_framebuffer_blend(-1) || n3ds_gpu_framebuffer_blend(8)) {
            C3D_FrameEnd(GX_CMDLIST_FLUSH); goto done;
        }
        C3D_DrawArrays(GPU_TRIANGLES,0,6); C3D_FrameEnd(GX_CMDLIST_FLUSH);
        C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(64,64),readback,GX_BUFFER_DIM(64,64),
            GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        GSPGPU_InvalidateDataCache(readback,64*64*4);
        for(unsigned int y=16;y<48;++y) for(unsigned int x=16;x<48;++x) for(unsigned int shift=0;shift<32;shift+=8) {
            int delta=(int)((readback[y*64+x]>>shift)&255)-(int)((expected>>shift)&255);
            if(delta<0) delta=-delta;
            if((unsigned int)delta>difference) difference=delta;
        }
        rows[row][0]=mode; rows[row][1]=source_word; rows[row][2]=destination_word;
        rows[row][3]=expected; rows[row][4]=readback[32*64+32]; rows[row][5]=difference; ++row;
        if(difference>maximum_difference) maximum_difference=difference;
        if(difference>2) {
            char message[192];
            snprintf(message,sizeof(message),"GPU BLEND FAIL: mode %u vector %u expected %08x got %08lx max difference %u",mode,vector,expected,(unsigned long)readback[32*64+32],difference);
            n3ds_log(message); goto done;
        }
    }
    proof=fopen("sdmc:/halo-native-blend.bin","wb");
    if(!proof || fwrite(rows,sizeof(rows),1,proof)!=1) goto done;
    if(fclose(proof)) { proof=NULL; goto done; } proof=NULL;
    {
        char message[192];
        snprintf(message,sizeof(message),"PASS: all 8 original framebuffer blend modes, 32 RGBA cases, 32768 GPU pixels; max channel difference %u",maximum_difference);
        n3ds_log(message);
    }
    result=0;
done:
    if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    n3ds_gpu_program_park();
    if(program_ready) shaderProgramFree(&program);
    if(shader) DVLB_Free(shader);
    if(target) C3D_RenderTargetDelete(target);
    linearFree(readback); linearFree(vertices);
    return result;
}
