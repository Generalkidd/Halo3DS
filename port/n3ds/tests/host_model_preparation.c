/* Host driver for the production CPU preparation slices. The native model
 * diagnostics separately exercise the actual ARM skinning arithmetic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_model_vertex.h"
#include "engine_model_palette.h"

#define NUMBEROF(a) (sizeof(a)/sizeof(*(a)))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define n3ds_algorithm_profile_add(...) ((void)0)
#define n3ds_algorithm_profile_max(...) ((void)0)
#define vhalt(message) do {fputs(message,stderr);abort();} while(0)

typedef unsigned char byte;
typedef struct { float i,j; } real_vector2d;
typedef struct { float values[13]; } real_matrix4x3;
struct model_vertex_compressed {
    struct {float x,y,z;} position;
    unsigned int normal,binormal,tangent;
    short texcoord[2];
    byte nodes[2];
    short node_weight;
};
struct native_render_vertex {float position[3],color[3],uv[2];};
struct decoded_stream {
    int rigid_node;
    unsigned int count;
    struct native_render_vertex *local;
    unsigned long long revision;
    const struct model_vertex_compressed *address;
};
struct test_skin {const real_matrix4x3 *node_matrices; unsigned int node_matrix_count;};
struct model_skin_range {
    const struct test_skin *skin;
    const real_vector2d *scale;
    struct native_render_vertex *output;
    const void *normals;
    int lighting;
};
enum { POSE_BYTES=2*1024*1024 };
static unsigned int generation=1,frame=1,allocations,fill_calls;
static int reject_allocation;
static void n3ds_log(const char *message) {puts(message);}
static int n3ds_gpu_frame_active(void) {return 0;}
static int n3ds_gpu_texture_barrier(void) {return 1;}
static unsigned int n3ds_engine_cache_generation(void) {return generation;}
static unsigned long long n3ds_gpu_model_frame_serial(void) {return frame;}
static void *n3ds_gpu_geometry_optional_allocate(unsigned int bytes)
{
    if(reject_allocation)return NULL;
    ++allocations;
    return malloc(bytes);
}
static void n3ds_gpu_geometry_free(void *pointer) {free(pointer);}
static void n3ds_gpu_geometry_flush(void *pointer,unsigned int bytes) {(void)pointer;(void)bytes;}
int n3ds_input_platform_original_model(void) {return 0;}

static int skin_vertex_range(void *context,unsigned int begin,unsigned int end)
{
    struct model_skin_range *range=context;
    ++fill_calls;
    for(unsigned int i=begin;i<end;++i) {
        range->output[i]=(struct native_render_vertex){.position={range->skin->node_matrices->values[0]+i},
            .color={(float)range->lighting},.uv={range->scale->i,range->scale->j}};
    }
    return 1;
}
static int n3ds_parallel_range(int (*job)(void *,unsigned int,unsigned int),void *context,
    unsigned int count,unsigned int minimum,unsigned int kind)
{
    (void)minimum;
    (void)kind;
    return job(context,0,count);
}
static void *test_realloc(void *pointer,size_t bytes)
{
    ++allocations;
    return reject_allocation?NULL:realloc(pointer,bytes);
}

#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
struct n3ds_cache_view {
    byte *tags;
    unsigned int tag_bytes,source_tags_crc;
    byte header[2048];
};
static void crc_new(unsigned long *crc) {*crc=0xffffffffu;}
static void crc_checksum_buffer(unsigned long *crc,const void *input,unsigned int size)
{
    const byte *bytes=input;
    for(unsigned int i=0;i<size;++i) {
        *crc^=bytes[i];
        for(unsigned int bit=0;bit<8;++bit)
            *crc=(*crc>>1)^((*crc&1)?0xedb88320u:0);
    }
}
static FILE *test_cooked_open(const char *path,const char *mode)
{
    assert(!strcmp(path,"sdmc:/halo-source/fixture.nmg"));
    return fopen("fixture.nmg",mode);
}
#define fopen test_cooked_open
#endif
#include "engine_model_compact.inl"
#undef fopen
#define realloc test_realloc
#include "engine_model_midpoint_skin.inl"
#undef realloc

#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
static void compare_compaction(void)
{
    unsigned int original_allocations=allocations;
    assert(model_compact_remap_tests());
    unsigned int state=17;
    for(unsigned int trial=0;trial<200;++trial) {
        struct model_vertex_compressed raw[256]={0};
        byte native[256*48]={0};
        unsigned short corners[300],expected[300],representatives[256];
        unsigned int stride=trial&1?sizeof(struct native_render_vertex):sizeof(struct native_skin_vertex),used=0;
        for(unsigned int i=0;i<256;++i) {
            state=state*1664525u+1013904223u;
            unsigned int attribute=(state>>16)%13;
            raw[i].tangent=attribute%3;
            native[i*stride]=attribute;
        }
        for(unsigned int i=0;i<300;++i) {
            state=state*1664525u+1013904223u;
            unsigned int source=corners[i]=(state>>16)%256;
            unsigned int slot=0;
            for(;slot<used;++slot) {
                unsigned int first=representatives[slot];
                if(!memcmp(raw+source,raw+first,sizeof(*raw)) &&
                   !memcmp(native+source*stride,native+first*stride,stride))break;
            }
            if(slot==used)representatives[used++]=source;
            expected[i]=slot;
        }
        struct decoded_stream stream={.rigid_node=trial&1?0:-1,.count=256,.local=(void *)native,.address=raw};
        const short triangle_type=trial&1;
        reject_allocation=1;
        assert(!model_compact_get(raw,corners,100,triangle_type,&stream,corners,300));
        reject_allocation=0;
        assert(!model_compact_get(raw,corners,100,triangle_type,&stream,corners,300));
        ++frame;
        const struct model_compact_mesh *mesh=model_compact_get(raw,corners,100,triangle_type,&stream,corners,300);
        assert(mesh && model_compact_get(raw,corners,100,triangle_type,&stream,corners,300)==mesh);
        assert(!memcmp(mesh->indices,expected,sizeof(expected)));
        for(unsigned int i=0;i<300;++i)
            assert(!memcmp((byte *)mesh->vertices+mesh->indices[i]*stride,native+corners[i]*stride,stride));
#if HALO_N3DS_ALGO_MODEL_COMPACT == N3DS_ALGORITHM_VERIFY
        assert(model_compact_verify(raw,native,mesh->vertices,corners,mesh->indices,representatives,300,stride,used));
        ((byte *)mesh->vertices)[stride-1]^=1;
        assert(!model_compact_verify(raw,native,mesh->vertices,corners,mesh->indices,representatives,300,stride,used));
#endif
        model_compact_release();
        assert(!model_compact_bytes);
    }
    assert(allocations-original_allocations==200);
    puts("PASS: 200 rigid and weighted compactions match ordered corners and recover after pressure");
}
#endif

#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
static void write_cooked_fixture(const struct n3ds_cache_view *view,const void *payload,
    unsigned int size,int corrupt_crc)
{
    struct model_cooked_header header={
        MODEL_COOKED_MAGIC,MODEL_COOKED_VERSION,MODEL_COOKED_EXACT,
        view->tag_bytes,view->source_tags_crc,model_cooked_crc(view->header,sizeof(view->header)),
        1,size,model_cooked_crc(payload,size)^corrupt_crc
    };
    FILE *file=test_cooked_open("sdmc:/halo-source/fixture.nmg","wb");
    assert(file && fwrite(&header,sizeof(header),1,file)==1 && fwrite(payload,size,1,file)==1);
    assert(!fclose(file));
}

static void compare_cooked_plans(void)
{
    struct {
        struct model_vertex_compressed vertices[256];
        unsigned short indices[6];
    } tags={.indices={7,2,1,3,2,0}};
    tags.vertices[2].tangent=1;
    tags.vertices[3].binormal=1;
    struct native_render_vertex native[256]={0};
    native[2].uv[0]=.25f;
    native[3].uv[1]=.5f;
    struct n3ds_cache_view view={.tags=(void *)&tags,.tag_bytes=sizeof(tags)};
    view.source_tags_crc=model_cooked_crc(view.tags,view.tag_bytes);
    struct decoded_stream stream={.rigid_node=0,.count=256,.local=native,.address=tags.vertices};
    struct {
        struct model_cooked_record record;
        unsigned short indices[11];
    } payload={
        .record={{0,sizeof(tags.vertices),256,2,0},6,MODEL_COOKED_EXACT?3:5,sizeof(payload.record)},
        .indices={0,1,2,3,7,4,2,1,3,2,0}
    };
    if(MODEL_COOKED_EXACT) {
        const unsigned short compact[]={7,2,3,0,1,0,2,1,0};
        memcpy(payload.indices,compact,sizeof(compact));
    }
    unsigned int size=sizeof(payload.record)+(payload.record.used+payload.record.corners)*sizeof(unsigned short);
    write_cooked_fixture(&view,&payload,size,0);
    n3ds_model_geometry_load("fixture",&view);
    const struct model_cooked_record *plan=model_cooked_find(tags.vertices,tags.indices,256,2,0,6);
    assert(plan && !model_cooked_find(tags.vertices,tags.indices,256,2,1,6));
    unsigned short remap[256];
    memset(remap,255,sizeof(remap));
    unsigned int referenced;
    assert(model_cooked_remap(plan,&stream,sizeof(*native),tags.indices,remap,&referenced) && referenced==5);
    const struct model_compact_mesh *mesh=model_compact_get(tags.vertices,tags.indices,2,0,&stream,tags.indices,6);
    assert(mesh && !memcmp(mesh->indices,payload.indices+payload.record.used,sizeof(tags.indices)));
    model_compact_release();

    /* Keep the archive structurally valid and re-check every current corner. */
    payload.indices[payload.record.used]^=1;
    write_cooked_fixture(&view,&payload,size,0);
    n3ds_model_geometry_load("fixture",&view);
    plan=model_cooked_find(tags.vertices,tags.indices,256,2,0,6);
    assert(plan);
    memset(remap,255,sizeof(remap));
    assert(!model_cooked_remap(plan,&stream,sizeof(*native),tags.indices,remap,&referenced));
    mesh=model_compact_get(tags.vertices,tags.indices,2,0,&stream,tags.indices,6);
    assert(mesh);
    for(unsigned int i=0;i<6;++i)
        assert(!memcmp((const struct native_render_vertex *)mesh->vertices+mesh->indices[i],
                       native+tags.indices[i],sizeof(*native)));
    model_compact_release();
    write_cooked_fixture(&view,&payload,size,1);
    n3ds_model_geometry_load("fixture",&view);
    assert(!model_cooked.entries);
    write_cooked_fixture(&view,&payload,size,0);
    ++view.source_tags_crc;
    n3ds_model_geometry_load("fixture",&view);
    assert(!model_cooked.entries);
    puts("PASS: cooked archive validates ordered tuples and rejects damaged/stale plans with runtime fallback");
}
#endif

static void compare_midpoint_scratch(void)
{
    real_matrix4x3 matrix={{2}};
    real_vector2d uv={1,2};
    struct test_skin skin={&matrix,1};
    struct model_skin_range range={.skin=&skin,.scale=&uv};
    struct decoded_stream stream={.count=3,.revision=7};
    const struct native_render_vertex *pose=skin_midpoint_stream(&stream,&range,17);
    assert(pose && pose[2].position[0]==4 && pose[2].uv[1]==2 && fill_calls==1);
    struct native_render_vertex submitted[3];
    memcpy(submitted,pose,sizeof(submitted));
    assert(skin_midpoint_stream(&stream,&range,17)==pose && fill_calls==1);

    range.lighting=1;
    assert(skin_midpoint_stream(&stream,&range,17)==pose && fill_calls==2 && pose[2].color[0]==1);
    range.lighting=0;
    assert(skin_midpoint_stream(&stream,&range,17)==pose && fill_calls==2);
    uv.j=4;
    assert(skin_midpoint_stream(&stream,&range,17)==pose && fill_calls==3 && pose[2].uv[1]==4);
    matrix.values[0]=7;
    assert(skin_midpoint_stream(&stream,&range,17)==pose && fill_calls==4 && pose[2].position[0]==9);
    assert(skin_midpoint_stream(&stream,&range,18)==pose && fill_calls==5);
    ++stream.revision;
    assert(skin_midpoint_stream(&stream,&range,18)==pose && fill_calls==6);
    ++generation;
    assert(skin_midpoint_stream(&stream,&range,18)==pose && fill_calls==7);
    ++frame;
    assert(skin_midpoint_stream(&stream,&range,18)==pose && fill_calls==8);
    assert(submitted[2].position[0]==4 && submitted[2].uv[1]==2 && submitted[2].color[0]==0);

    stream.count=4;
    reject_allocation=1;
    assert(!skin_midpoint_stream(&stream,&range,18));
    unsigned int attempted=allocations;
    reject_allocation=0;
    assert(!skin_midpoint_stream(&stream,&range,18) && allocations==attempted);
    stream.count=3;
    assert(skin_midpoint_stream(&stream,&range,18)==pose && fill_calls==8);
    ++frame;
    stream.count=4;
    assert(skin_midpoint_stream(&stream,&range,18) && fill_calls==9);
    stream.count=65535;
    assert(!skin_midpoint_stream(&stream,&range,18) && fill_calls==9);
    midpoint_skin_clear();
    assert(!midpoint_skin.capacity && !midpoint_skin.valid && !midpoint_skin.vertices);
    puts("PASS: midpoint scratch reuses exact frame-local keys, isolates copied output and recovers after pressure");
}

static void compare_skin_layout(void)
{
    struct native_skin_vertex vertex={.position={1,-2,3},.normal={0,1,-1},.uv={.25f,.75f}};
    const float original[]={1,-2,3,0,1,-1,.25f,.75f};
    for(unsigned int weight=0;weight<=32767;++weight) {
        unsigned int first=3*(weight%29),second=3*((weight*7)%29);
        float first_weight=weight/32767.f,second_weight=1.f-first_weight;
        const float expected[]={first,second,first_weight,second_weight};
        float actual[4];
        n3ds_skin_vertex_set_bones(&vertex,first,second,first_weight,second_weight);
        n3ds_skin_vertex_read_bones(&vertex,actual);
        assert(!memcmp(expected,actual,sizeof(expected)));
        assert(!memcmp(original,&vertex,sizeof(original)));
    }
    /* The runtime float lane is copied exactly, including the sign of zero. */
    float actual[4],expected[]={0.f,84.f,1.f,-0.f};
    n3ds_skin_vertex_set_bones(&vertex,0,84,1.f,-0.f);
    n3ds_skin_vertex_read_bones(&vertex,actual);
    assert(!memcmp(expected,actual,sizeof(expected)));
    printf("PASS: %zu-byte skin tuple reconstructs the original weights and row offsets\n",sizeof(vertex));
}

static void compare_palette_order(void)
{
    const unsigned char original[]={63,15,0,29,27,5,61};
    const unsigned char expected[]={0,5,15,27,29,61,63};
    unsigned char palette[sizeof(original)],remap[64];
    for(unsigned int rotation=0;rotation<sizeof(original);++rotation) {
        memset(remap,255,sizeof(remap));
        for(unsigned int slot=0;slot<sizeof(original);++slot) {
            unsigned char bone=original[(slot+rotation)%sizeof(original)];
            palette[slot]=bone;
            remap[bone]=slot;
        }
        n3ds_model_order_compact_palette(palette,remap);
        assert(!memcmp(expected,palette,sizeof(expected)));
        for(unsigned int slot=0;slot<sizeof(original);++slot) {
            unsigned char bone=original[slot];
            assert(palette[remap[bone]]==bone);
        }
        assert(remap[28]==255);
    }
    puts("PASS: compact palette order is independent of encounter order and preserves each selected bone");
}

int main(void)
{
    compare_skin_layout();
    compare_palette_order();
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
    compare_cooked_plans();
#endif
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    compare_compaction();
#endif
    compare_midpoint_scratch();
    return 0;
}
