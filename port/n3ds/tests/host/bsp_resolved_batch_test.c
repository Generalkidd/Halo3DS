/* Run the production packet admission, ownership and concatenation code
 * with immutable synthetic geometry and a renderer that records each draw. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_algorithm_features.h"

typedef unsigned char byte;
enum { NATIVE_SAMPLER_CLAMP_U=1, NATIVE_SAMPLER_CLAMP_V=2 };
struct shader {int dummy;};
struct native_render_vertex {float position[3],color[3],uv[2];};
struct native_geometry {
    unsigned int vertex_count,index_count;
    const struct native_render_vertex *vertices;
    const float *light_uv;
    const unsigned short *indices;
};
static unsigned int bsp_filter_frame=1;
static void *bound[3];
static unsigned int sampler[3];
static struct {
    void *textures[3];
    unsigned int indices;
} draws[4];
static unsigned int draw_count,corner_count;
static float vertex_markers[12],light_uv_markers[12];

#define n3ds_algorithm_profile_add(...) ((void)0)

int n3ds_input_platform_original_model(void) {return 0;}
void n3ds_log(const char *message) {puts(message);}
static int n3ds_presentation_check_midpoint(void) {return 0;}
static int n3ds_presentation_midpoint_rejected(void) {return 0;}
static void *n3ds_gpu_geometry_optional_allocate(unsigned int bytes) {return malloc(bytes);}
static void n3ds_gpu_geometry_free(void *memory) {free(memory);}
static void n3ds_gpu_geometry_flush(void *memory,unsigned int bytes) {(void)memory;(void)bytes;}
static int n3ds_gpu_texture_bind_sampler(unsigned int stage,void *texture,unsigned int flags)
{
    bound[stage]=texture;
    sampler[stage]=flags;
    return 1;
}
static int n3ds_gpu_texture_bind(unsigned int stage,void *texture)
{
    return n3ds_gpu_texture_bind_sampler(stage,texture,0);
}
static int n3ds_gpu_environment_draw(const struct native_render_vertex *vertices,const float *light_uv,
    const unsigned short *indices,unsigned int count,int lightmap,int detail,float u,float v,
    unsigned int material,int alpha)
{
    assert(draw_count<4 && corner_count+count<=12);
    assert(lightmap==1 && detail==2 && u==2.f && v==3.f && material==0xff102030 && !alpha);
    assert(sampler[0]==0 && sampler[1]==(NATIVE_SAMPLER_CLAMP_U|NATIVE_SAMPLER_CLAMP_V) && sampler[2]==0);
    memcpy(draws[draw_count].textures,bound,sizeof(bound));
    draws[draw_count++].indices=count;
    for(unsigned int i=0;i<count;++i) {
        unsigned int vertex=indices[i];
        vertex_markers[corner_count]=vertices[vertex].position[0];
        light_uv_markers[corner_count++]=light_uv[vertex*2];
    }
    return 1;
}

#include "engine_bsp_batch.inl"

int main(void)
{
    struct shader shaders[2]={0};
    int textures[4];
    const struct native_render_vertex vertices[2][3]={
        {{.position={1}},{.position={2}},{.position={3}}},
        {{.position={4}},{.position={5}},{.position={6}}}
    };
    const float uv[2][6]={{.1f,0,.2f,0,.3f,0},{.4f,0,.5f,0,.6f,0}};
    const unsigned short indices[3]={0,2,1};
    const struct native_geometry geometry[2]={
        {3,3,vertices[0],uv[0],indices},
        {3,3,vertices[1],uv[1],indices}
    };
    const struct bsp_draw_packet first={
        .owner=geometry,.shader=shaders,.permutation=1,.lightmap_group=10,.lightmap_index=2,
        .textures={textures,textures+1,textures+2},.lightmap=1,.detail=2,.alpha=0,
        .scale={2.f,3.f},.material=0xff102030
    };
    struct bsp_draw_packet second=first;
    second.owner=geometry+1;
    assert(bsp_packet_key_equal(&first,&second));
    const int resolved=HALO_N3DS_ALGO_BSP_RESOLVED_BATCH != N3DS_ALGORITHM_REFERENCE;
    second.shader=shaders+1;
    second.permutation=2;
    second.lightmap_group=11;
    second.lightmap_index=3;
    assert(bsp_packet_key_equal(&first,&second)==resolved);

    for(unsigned int field=0;field<9;++field) {
        struct bsp_draw_packet different=first;
        switch(field) {
        case 0: different.textures[0]=textures+3;break;
        case 1: different.textures[1]=NULL;break;
        case 2: different.textures[2]=NULL;break;
        case 3: different.lightmap=0;break;
        case 4: different.detail=3;break;
        case 5: different.alpha=1;break;
        case 6: different.scale[0]=1.f;break;
        case 7: different.scale[1]=1.f;break;
        case 8: different.material^=1;break;
        }
        assert(!bsp_packet_key_equal(&first,&different));
    }

    bsp_batch_submit(&first);
    bsp_batch_submit(&second);
    bsp_batch_flush();
    assert(draw_count==(resolved?1:2) && corner_count==6 && draws[0].indices==(resolved?6:3));
    const float expected_vertices[]={1,3,2,4,6,5};
    const float expected_uv[]={.1f,.3f,.2f,.4f,.6f,.5f};
    assert(!memcmp(vertex_markers,expected_vertices,sizeof(expected_vertices)));
    assert(!memcmp(light_uv_markers,expected_uv,sizeof(expected_uv)));
    assert(!memcmp(draws[0].textures,first.textures,sizeof(bound)));

    /* Resolve the next handles before submitting, as the rasterizer does.
     * Flushing the old packet must restore its own handles and leave the
     * next packet's saved handles intact for its later draw. */
    bsp_batch_release();
    ++bsp_filter_frame;
    draw_count=corner_count=0;
    second.textures[0]=textures+3;
    bsp_batch_submit(&first);
    n3ds_gpu_texture_bind(0,second.textures[0]);
    bsp_batch_submit(&second);
    bsp_batch_flush();
    assert(draw_count==2 && corner_count==6);
    assert(!memcmp(draws[0].textures,first.textures,sizeof(bound)));
    assert(!memcmp(draws[1].textures,second.textures,sizeof(bound)));
    assert(!memcmp(vertex_markers,expected_vertices,sizeof(expected_vertices)));
    bsp_batch_release();
    printf("PASS: %s BSP packets preserve resolved state, separate unequal packets and retain corner order\n",
           resolved?"resolved":"reference");
    return 0;
}
