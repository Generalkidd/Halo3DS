#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "engine_textures.h"
#include "engine_renderer.h"
#include "texture_test_shader.h"

void n3ds_log(const char *message);
struct test_vertex { float position[3], uv[2]; };
/* Exercise an atlas mutation and destruction between two queued draws in one
 * frame. Each half must retain its own texture contents after CPU deletion. */
int n3ds_gpu_texture_mutation_test(void (*mutate)(int), const unsigned int colors[2])
{
    C3D_RenderTarget *target=NULL; DVLB_s *shader=NULL; shaderProgram_s program;
    struct test_vertex *vertices=NULL; u32 *readback=NULL;
    int program_ready=0,result=1;
    target=C3D_RenderTargetCreate(64,64,GPU_RB_RGBA8,-1);
    shader=DVLB_ParseFile((u32 *)texture_test_shader,sizeof(texture_test_shader));
    vertices=linearAlloc(12*sizeof(*vertices)); readback=linearAlloc(64*64*4);
    if(!target || !shader || !vertices || !readback) goto done;
    shaderProgramInit(&program); program_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&program,&shader->DVLE[0]))) goto done;
    C3D_BindProgram(&program);
    for(unsigned int half=0;half<2;++half) {
        const float corners[6][2]={{-1,-1},{0,-1},{0,1},{-1,-1},{0,1},{-1,1}};
        for(unsigned int i=0;i<6;++i)
            vertices[half*6+i]=(struct test_vertex){{corners[i][0]+half,corners[i][1],0},{0.5f,0.5f}};
    }
    GSPGPU_FlushDataCache(vertices,12*sizeof(*vertices));
    C3D_AttrInfo *attributes=C3D_GetAttrInfo(); AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3); AttrInfo_AddLoader(attributes,1,GPU_FLOAT,2);
    if(!n3ds_gpu_vertex_buffer_bind(vertices,sizeof(*vertices),2,0x10)) goto done;
    for(unsigned int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR); C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    if(!C3D_FrameBegin(0)) goto done;
    C3D_RenderTargetClear(target,C3D_CLEAR_COLOR,0,0); C3D_FrameDrawOn(target);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
    mutate(0);
    C3D_DrawArrays(GPU_TRIANGLES,6,6);
    mutate(1);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(64,64),readback,GX_BUFFER_DIM(64,64),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(readback,64*64*4);
    result=0;
    for(unsigned int half=0;half<2;++half) for(unsigned int y=8;y<56;++y)
        for(unsigned int x=8;x<24;++x) if(readback[y*64+x+32*half]!=colors[half]) result=1;
    if(result) {
        char message[128];
        snprintf(message,sizeof(message),"GPU MUTATION FAIL: expected %08x %08x got %08lx %08lx",
            colors[0],colors[1],(unsigned long)readback[32*64+16],(unsigned long)readback[32*64+48]);
        n3ds_log(message);
    }
done:
    if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_program_park();
    if(program_ready) shaderProgramFree(&program);
    if(shader) DVLB_Free(shader);
    if(target) C3D_RenderTargetDelete(target);
    linearFree(readback); linearFree(vertices);
    return result;
}
int n3ds_gpu_texture_sampling_test(unsigned int stage, const unsigned int expected_corners[4])
{
    C3D_Tex *texture = n3ds_gpu_texture_bound(stage);
    C3D_RenderTarget *target = NULL;
    DVLB_s *shader = NULL;
    shaderProgram_s program;
    int program_ready = 0, result = 1;
    struct test_vertex *vertices = NULL;
    u32 *readback = NULL;
    void *saved_bindings[3];
    if (!texture || stage >= 3) return 1;
    for (unsigned int i = 0; i < 3; ++i) { saved_bindings[i] = n3ds_gpu_texture_bound(i); if (i != stage) n3ds_gpu_texture_bind(i, NULL); }
    target = C3D_RenderTargetCreate(64, 64, GPU_RB_RGBA8, -1);
    shader = DVLB_ParseFile((u32 *)texture_test_shader, sizeof(texture_test_shader));
    vertices = linearAlloc(6*sizeof(*vertices));
    readback = linearAlloc(64*64*4);
    if (!target || !shader || !vertices || !readback) goto done;
    shaderProgramInit(&program); program_ready = 1;
    if (R_FAILED(shaderProgramSetVsh(&program, &shader->DVLE[0]))) goto done;
    C3D_BindProgram(&program);
    {
        const float corners[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
        for (unsigned int i = 0; i < 6; ++i) {
            vertices[i].position[0] = corners[i][0]; vertices[i].position[1] = corners[i][1]; vertices[i].position[2] = 0;
            vertices[i].uv[0] = 0.5f/texture->width; vertices[i].uv[1] = 0.5f/texture->height;
        }
    }
    GSPGPU_FlushDataCache(vertices, 6*sizeof(*vertices));
    C3D_AttrInfo *attributes = C3D_GetAttrInfo(); AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3); AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
    if(!n3ds_gpu_vertex_buffer_bind(vertices,sizeof(*vertices),2,0x10)) goto done;
    for (unsigned int i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    GPU_TEVSRC source = stage == 0 ? GPU_TEXTURE0 : stage == 1 ? GPU_TEXTURE1 : GPU_TEXTURE2;
    C3D_TexEnv *environment = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(environment, C3D_Both, source, source, source);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
    C3D_TexSetFilter(texture, GPU_NEAREST, GPU_NEAREST);
    n3ds_gpu_texture_bind(stage, texture); /* Make changed sampler state dirty. */
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE); C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    result = 0;
    for (unsigned int corner = 0; corner < 4; ++corner) {
        u32 expected = expected_corners[corner];
        if (expected == 0xffffffffU) { result = 1; break; } /* Must differ from neutral units. */
        for (unsigned int i = 0; i < 6; ++i) {
            vertices[i].uv[0] = (corner & 1) ? 1.0f-0.5f/texture->width : 0.5f/texture->width;
            vertices[i].uv[1] = (corner & 2) ? 1.0f-0.5f/texture->height : 0.5f/texture->height;
        }
        GSPGPU_FlushDataCache(vertices, 6*sizeof(*vertices));
        if (!C3D_FrameBegin(0)) { result = 1; break; }
        C3D_RenderTargetClear(target, C3D_CLEAR_COLOR, ~expected, 0);
        C3D_FrameDrawOn(target); C3D_DrawArrays(GPU_TRIANGLES, 0, 6); C3D_FrameEnd(0);
        C3D_SyncDisplayTransfer((u32 *)target->frameBuf.colorBuf, GX_BUFFER_DIM(64,64), readback, GX_BUFFER_DIM(64,64),
            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        GSPGPU_InvalidateDataCache(readback, 64*64*4);
        for (unsigned int y = 16; y < 48; ++y) for (unsigned int x = 16; x < 48; ++x)
            if (readback[y*64+x] != expected) result = 1;
        if (result) {
            char message[140];
            snprintf(message, sizeof(message), "GPU SAMPLE FAIL: stage %u corner %u expected %08lx got %08lx", stage, corner,
                     (unsigned long)expected, (unsigned long)readback[32*64+32]);
            n3ds_log(message); break;
        }
    }

done:
    if (!n3ds_gpu_texture_barrier()) abort();
    C3D_TexSetFilter(texture, GPU_LINEAR, GPU_LINEAR);
    for (unsigned int i = 0; i < 3; ++i) n3ds_gpu_texture_bind(i, saved_bindings[i]);
    n3ds_gpu_program_park();
    if (program_ready) shaderProgramFree(&program);
    if (shader) DVLB_Free(shader);
    if (target) C3D_RenderTargetDelete(target);
    linearFree(readback); linearFree(vertices);
    return result;
}
