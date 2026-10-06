#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine_widgets.h"
#include "engine_textures.h"
#include "engine_renderer.h"
void n3ds_log(const char *message);
static unsigned int expected(unsigned int texture,unsigned int color,unsigned int destination,unsigned int tint)
{
    unsigned int result=destination&255;
    for(unsigned int shift=8;shift<=24;shift+=8) {
        double t=((texture>>shift)&255)/255.0,c=((color>>shift)&255)/255.0;
        double value=(c*t+(1-c)*(tint/255.0)*pow(t,6))*(color&255)+((destination>>shift)&255);
        unsigned int channel=(unsigned int)fmin(255,floor(value+.5));
        result|=channel<<shift;
    }
    return result;
}
int n3ds_gpu_widget_tests(void)
{
    C3D_RenderTarget *target=C3D_RenderTargetCreate(64,64,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    u32 *pixels=linearAlloc(64*64*4);unsigned int texels[64];void *texture=NULL;
    struct native_widget_vertex vertices[4];unsigned int rows[16][9],max_difference=0;
    int active_frame=0,result=1;FILE *proof=NULL;char message[192];
#define CHECK(x) do{if(!(x)){snprintf(message,sizeof(message),"WIDGET GPU FAIL line %d: %s",__LINE__,#x);n3ds_log(message);goto done;}}while(0)
    CHECK(target && pixels);
    for(unsigned int test=0;test<16;++test) {
        unsigned int texture_color=test<12 ? 0x2bb3e300U|(test%3)*127U : 0xffffffffU;
        unsigned int color=test<12 ? 0x5078c800U|((test/3)%2?197U:91U) : 0x600000ffU;
        unsigned int destination=test<12 ? 0x1f253ba7U : 0x000000a7U;
        unsigned int tint=test<12 ? (test/6?255U:(test%2?128U:0U)) : 0;
        unsigned int flags=test<12 ? 0 : test-12;
        for(unsigned int i=0;i<64;++i) texels[i]=texture_color;
        texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
        C3D_Tex *tex=texture;u32 sampler=tex->param,border=tex->border;
        CHECK(C3D_FrameBegin(C3D_FRAME_SYNCDRAW));active_frame=1;
        n3ds_gpu_widgets_frame_begin();C3D_RenderTargetClear(target,C3D_CLEAR_ALL,destination,0);C3D_FrameDrawOn(target);
        CHECK(n3ds_gpu_texture_bind(0,texture));
        const float positions[4][2]={{0,0},{64,0},{64,64},{0,64}};
        for(unsigned int i=0;i<4;++i) {
            vertices[i]=(struct native_widget_vertex){{positions[i][0],positions[i][1],.25f},
                {((color>>24)&255)/255.f,((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f},{.5f,.5f}};
        }
        CHECK(!n3ds_gpu_widget_draw(vertices,0,0,64,tint/255.f,flags));
        CHECK(n3ds_gpu_widget_draw(vertices,0,64,64,tint/255.f,flags));
        CHECK(tex->param==sampler && tex->border==border);
        if(test>=12) {
            /* Near red writes depth only with both flags. Far blue is rejected
             * in that case and contributes when testing or writing is off. */
            for(unsigned int i=0;i<4;++i) {vertices[i].position[2]=.75f;vertices[i].color[0]=0;vertices[i].color[2]=96/255.f;}
            CHECK(n3ds_gpu_widget_draw(vertices,0,64,64,0,flags));
        }
        memset(vertices,0xa5,sizeof(vertices)); /* Submitted vertices must own their storage. */
        C3D_FrameEnd(GX_CMDLIST_FLUSH);active_frame=0;
        C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(64,64),pixels,GX_BUFFER_DIM(64,64),
            GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        GSPGPU_InvalidateDataCache(pixels,64*64*4);
        unsigned int reference=test<12?expected(texture_color,color,destination,tint):flags==3?0x600000a7U:0x600060a7U;
        unsigned int difference=0;
        for(unsigned int y=16;y<48;++y) for(unsigned int x=16;x<48;++x) for(unsigned int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*64+x]>>shift)&255)-(int)((reference>>shift)&255));
            if(delta>difference) difference=delta;
        }
        if(difference>max_difference) max_difference=difference;
        unsigned int row[9]={test,texture_color,color,destination,tint,flags,reference,pixels[32*64+32],difference};
        memcpy(rows[test],row,sizeof(row));
        if(difference>3) {snprintf(message,sizeof(message),"WIDGET GPU vector %u expected %08x got %08lx difference %u",test,reference,(unsigned long)pixels[32*64+32],difference);n3ds_log(message);goto done;}
        n3ds_gpu_texture_destroy(texture);texture=NULL;
    }
    proof=fopen("sdmc:/halo-native-widgets.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: native sprite tint curve, vertex alpha, additive blend and depth flags: 16 GPU vectors, 16384 pixels, max difference %u",max_difference);n3ds_log(message);
    n3ds_log("PASS: sprite draws own submitted vertices and preserve texture sampler and framebuffer alpha");result=0;
done:
    if(active_frame) C3D_FrameEnd(GX_CMDLIST_FLUSH);
    if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(texture);n3ds_gpu_program_park();
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    if(target) C3D_RenderTargetDelete(target);linearFree(pixels);return result;
}
