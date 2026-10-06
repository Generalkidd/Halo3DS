/* Native PICA resource allocation. These handles are C3D_Tex, never D3D objects. */
#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "engine_textures.h"

enum { PLASMA_TEXTURE_SLOTS=16, PLASMA_TEXTURE_BYTES=128*1024 };
static struct {void *owner,*resource;unsigned int identity,bytes;} plasma_textures[PLASMA_TEXTURE_SLOTS];
static unsigned int plasma_texture_bytes;
/* Original-model RGBA4 font atlases need fresh immutable versions while a
 * frame is queued. Reserve four 32 KiB images at first font creation, before
 * world allocations fragment linear memory. Larger bursts retain malloc/
 * linear fallback; leased images are never overwritten or reclaimed early. */
enum { FONT_TEXTURE_SLOTS=4 };
static C3D_Tex font_textures[FONT_TEXTURE_SLOTS];
static unsigned char font_leased[FONT_TEXTURE_SLOTS];
static unsigned int font_texture_count,font_pool_initialized;
unsigned int n3ds_gpu_texture_variant_bytes(void) {return plasma_texture_bytes;}
unsigned int n3ds_gpu_texture_memory_free(void) {return (unsigned int)linearSpaceFree();}
static C3D_Tex neutral;
static int initialized;
static void *bound[3];
/* Citro3D retains descriptor pointers until draw submission. Own a stable copy
 * rather than retaining stack-local sampler overrides used by effect paths. */
static C3D_Tex bound_sampler[3];
static C3D_TexCube bound_cube[3];
static unsigned int bound_identity[3];
static int bound_valid[3];
static unsigned int bind_requests,bind_updates;
/* Derivative identities must not displace the256 ordinary map identities.
 * Keep256 hash buckets; the16 extra records cost192 bytes on ARM32. */
enum { IMMUTABLE_BUCKETS=256, IMMUTABLE_SLOTS=256+PLASMA_TEXTURE_SLOTS };
static struct {void *resource;unsigned int id;unsigned short next;} immutable[IMMUTABLE_SLOTS];
/* Index+1 chains keep lookups bounded without tombstones after map churn. */
static unsigned short immutable_bucket[IMMUTABLE_BUCKETS];
static unsigned int next_identity;
static unsigned int identity_bucket(void *resource)
{
    uintptr_t key=(uintptr_t)resource;
    key>>=3;key^=key>>8;key^=key>>16;
    return key&(IMMUTABLE_BUCKETS-1);
}
unsigned int n3ds_gpu_texture_content_id(void *resource)
{
    if(!resource) return 0;
    for(unsigned int link=immutable_bucket[identity_bucket(resource)];link;link=immutable[link-1].next)
        if(immutable[link-1].resource==resource) return immutable[link-1].id;
    return 0;
}
unsigned int n3ds_gpu_texture_publish_immutable(void *resource)
{
    C3D_Tex *texture=resource;
    if(!initialized || !texture || !texture->data) return 0;
    unsigned int id=n3ds_gpu_texture_content_id(resource);if(id) return id;
    /* Exhaustion falls back to exact content checks; never recycle an ID. */
    if(next_identity==~0u) return 0;
    for(unsigned int i=0;i<IMMUTABLE_SLOTS;++i) if(!immutable[i].resource) {
        unsigned int bucket=identity_bucket(resource);
        immutable[i].resource=resource;immutable[i].id=++next_identity;
        immutable[i].next=immutable_bucket[bucket];immutable_bucket[bucket]=i+1;
        for(unsigned int stage=0;stage<3;++stage) if(bound[stage]==resource) bound_valid[stage]=0;
        return next_identity;
    }
    return 0;
}
static void forget_identity(void *resource)
{
    if(!resource) return;
    unsigned short *link=immutable_bucket+identity_bucket(resource);
    while(*link) {
        unsigned int i=*link-1;
        if(immutable[i].resource==resource) {
            *link=immutable[i].next;memset(&immutable[i],0,sizeof(immutable[i]));return;
        }
        link=&immutable[i].next;
    }
}
int n3ds_gpu_texture_identity_tests(void)
{
    C3D_Tex fake[IMMUTABLE_SLOTS+1];unsigned int ids[IMMUTABLE_SLOTS],pixel=0;
    if(!initialized) return 0;
    for(unsigned int i=0;i<IMMUTABLE_SLOTS;++i) if(immutable[i].resource) return 0;
    memset(fake,0,sizeof(fake));
    for(unsigned int i=0;i<=IMMUTABLE_SLOTS;++i) fake[i].data=&pixel;
    if(n3ds_gpu_texture_content_id(fake) || n3ds_gpu_texture_publish_immutable(NULL)) return 0;
    for(unsigned int i=0;i<IMMUTABLE_SLOTS;++i) {
        ids[i]=n3ds_gpu_texture_publish_immutable(fake+i);
        if(!ids[i] || n3ds_gpu_texture_content_id(fake+i)!=ids[i] || n3ds_gpu_texture_publish_immutable(fake+i)!=ids[i]) return 0;
        if(i && ids[i]<=ids[i-1]) return 0;
    }
    if(n3ds_gpu_texture_publish_immutable(fake+IMMUTABLE_SLOTS)) return 0;
    forget_identity(fake+17);
    if(n3ds_gpu_texture_content_id(fake+17)) return 0;
    unsigned int replacement=n3ds_gpu_texture_publish_immutable(fake+17);
    if(replacement<=ids[IMMUTABLE_SLOTS-1]) return 0;
    /* Delete/reinsert in a permuted order, checking every surviving chain.
     * This catches unlinking a bucket head or interior node incorrectly. */
    for(unsigned int pass=0;pass<2;++pass) for(unsigned int j=0;j<IMMUTABLE_SLOTS;++j) {
        unsigned int i=(j*73)%IMMUTABLE_SLOTS;
        forget_identity(fake+i);
        if(n3ds_gpu_texture_content_id(fake+i)) return 0;
        for(unsigned int k=0;k<IMMUTABLE_SLOTS;++k) if(k!=i && !n3ds_gpu_texture_content_id(fake+k)) return 0;
        if(!n3ds_gpu_texture_publish_immutable(fake+i)) return 0;
    }
    for(unsigned int i=0;i<IMMUTABLE_SLOTS;++i) forget_identity(fake+i);
    for(unsigned int i=0;i<IMMUTABLE_SLOTS;++i) if(n3ds_gpu_texture_content_id(fake+i)) return 0;
    for(unsigned int i=0;i<IMMUTABLE_BUCKETS;++i) if(immutable_bucket[i]) return 0;
    return 1;
}

int n3ds_gpu_textures_initialize(void)
{
    if (initialized || !C3D_TexInit(&neutral, 8, 8, GPU_RGBA8)) return 0;
    memset(neutral.data, 255, 8*8*4);
    C3D_TexFlush(&neutral);
    initialized = 1;
    memset(bound_valid,0,sizeof(bound_valid));
    for (unsigned int stage = 0; stage < 3; ++stage) n3ds_gpu_texture_bind(stage,NULL);
    return 1;
}
static inline __attribute__((always_inline)) int bind_descriptor(unsigned int stage,void *resource,C3D_Tex sampler)
{
    if (!initialized || stage >= 3) return 0;
    int cube=C3D_TexGetType(&sampler)==GPU_TEX_CUBE_MAP || C3D_TexGetType(&sampler)==GPU_TEX_SHADOW_CUBE;
    if(stage && C3D_TexGetType(&sampler)!=GPU_TEX_2D) return 0;
    ++bind_requests;
    unsigned int identity=bound_valid[stage] && resource==bound[stage] ?
        bound_identity[stage] : n3ds_gpu_texture_content_id(resource);
    int changed=!bound_valid[stage];
    if(cube) {
        if(!sampler.cube) return 0;
        changed=changed || memcmp(&bound_cube[stage],sampler.cube,sizeof(C3D_TexCube));
        bound_cube[stage]=*sampler.cube;sampler.cube=&bound_cube[stage];
    }
    changed=changed || memcmp(&bound_sampler[stage],&sampler,sizeof(sampler));
    /* Mutable pixel contents require a real bind even with unchanged sampler
     * bits. A newly published allocation also invalidates address reuse. */
    if(changed || (resource && !identity) || identity!=bound_identity[stage]) {
        bound_sampler[stage]=sampler;bound_valid[stage]=1;bound_identity[stage]=identity;
        C3D_TexBind(stage,&bound_sampler[stage]);++bind_updates;
    }
    bound[stage] = resource;
    return 1;
}
int n3ds_gpu_texture_bind(unsigned int stage,void *resource)
{
    if(!initialized || stage>=3) return 0;
    return bind_descriptor(stage,resource,*(C3D_Tex *)(resource?resource:&neutral));
}
int n3ds_gpu_texture_bind_sampler(unsigned int stage,void *resource,unsigned int flags)
{
    if(!initialized || stage>=3 || (flags&~31u)) return 0;
    C3D_Tex sampler=*(C3D_Tex *)(resource?resource:&neutral);
    if(flags&NATIVE_SAMPLER_PROJECTIVE) {
        if(stage || C3D_TexGetType(&sampler)!=GPU_TEX_2D) return 0;
        sampler.param=(sampler.param&~(7u<<28))|(GPU_TEX_PROJECTION<<28);
    }
    C3D_TexSetFilter(&sampler,flags&NATIVE_SAMPLER_MAG_POINT?GPU_NEAREST:GPU_LINEAR,
        flags&NATIVE_SAMPLER_MIN_POINT?GPU_NEAREST:GPU_LINEAR);
    C3D_TexSetWrap(&sampler,flags&NATIVE_SAMPLER_CLAMP_U?GPU_CLAMP_TO_EDGE:GPU_REPEAT,
        flags&NATIVE_SAMPLER_CLAMP_V?GPU_CLAMP_TO_EDGE:GPU_REPEAT);
    return bind_descriptor(stage,resource,sampler);
}
void *n3ds_gpu_texture_bound(unsigned int stage)
{
    return stage < 3 ? bound[stage] : NULL;
}
int n3ds_gpu_texture_sampler_tests(void)
{
    unsigned int texels[64];for(int i=0;i<64;++i) texels[i]=0x123456ff;
    void *original[3]={bound[0],bound[1],bound[2]};
    C3D_Tex *texture=n3ds_gpu_texture_create(texels,8,8);int result=0;
    if(!texture) return 0;
    C3D_Tex owner=*texture;unsigned int before;
#define SAMPLER_CHECK(e) do {if(!(e)) goto done;}while(0)
    SAMPLER_CHECK(n3ds_gpu_texture_bind(0,texture));before=bind_updates;
    SAMPLER_CHECK(n3ds_gpu_texture_bind_sampler(0,texture,0));
    SAMPLER_CHECK(bind_updates==before+1); /* mutable owner must rebind */
    SAMPLER_CHECK(n3ds_gpu_texture_publish_immutable(texture));
    SAMPLER_CHECK(n3ds_gpu_texture_bind(0,texture));before=bind_updates;
    for(int i=0;i<100;++i) {
        SAMPLER_CHECK(n3ds_gpu_texture_bind_sampler(0,texture,0));
        SAMPLER_CHECK(n3ds_gpu_texture_bind(0,texture));
    }
    SAMPLER_CHECK(bind_updates==before);
    for(unsigned int stage=0;stage<3;++stage) for(unsigned int flags=0;flags<(stage?16:32);++flags) {
        C3D_Tex expected=owner;
        C3D_TexSetFilter(&expected,flags&1?GPU_NEAREST:GPU_LINEAR,flags&2?GPU_NEAREST:GPU_LINEAR);
        C3D_TexSetWrap(&expected,flags&4?GPU_CLAMP_TO_EDGE:GPU_REPEAT,flags&8?GPU_CLAMP_TO_EDGE:GPU_REPEAT);
        if(flags&16) expected.param=(expected.param&~(7u<<28))|(GPU_TEX_PROJECTION<<28);
        SAMPLER_CHECK(n3ds_gpu_texture_bind_sampler(stage,texture,flags));
        SAMPLER_CHECK(bound[stage]==texture && !memcmp(&owner,texture,sizeof(owner)) &&
            !memcmp(&expected,bound_sampler+stage,sizeof(expected)));
        before=bind_updates;
        SAMPLER_CHECK(n3ds_gpu_texture_bind_sampler(stage,texture,flags));
        SAMPLER_CHECK(bind_updates==before);
        SAMPLER_CHECK(n3ds_gpu_texture_bind(stage,texture));
        SAMPLER_CHECK(!memcmp(&owner,bound_sampler+stage,sizeof(owner)));
    }
    SAMPLER_CHECK(!n3ds_gpu_texture_bind_sampler(1,texture,16) && !n3ds_gpu_texture_bind_sampler(0,texture,32));
    result=1;
done:
    for(unsigned int stage=0;stage<3;++stage) n3ds_gpu_texture_bind(stage,original[stage]);
    n3ds_gpu_texture_destroy(texture);
    if(result) {extern void n3ds_log(const char *);n3ds_log("PASS: sampler owners: mutable rebinding, publication, 64 filter/wrap/projective combinations, immutable reuse and original descriptor restoration");}
#undef SAMPLER_CHECK
    return result;
}
int n3ds_gpu_texture_wrap(unsigned int stage,int clamp_u,int clamp_v)
{
    if(!initialized || stage>=3 || !bound_valid[stage]) return 0;
    u32 previous=bound_sampler[stage].param;
    C3D_TexSetWrap(&bound_sampler[stage],clamp_u?GPU_CLAMP_TO_EDGE:GPU_REPEAT,clamp_v?GPU_CLAMP_TO_EDGE:GPU_REPEAT);
    if(previous!=bound_sampler[stage].param) {C3D_TexBind(stage,&bound_sampler[stage]);++bind_updates;}
    return 1;
}
void n3ds_gpu_textures_dispose(void)
{
    if (!initialized) return;
    if (!n3ds_gpu_texture_barrier()) abort();
    {extern void n3ds_log(const char *);char message[120];
     snprintf(message,sizeof(message),"NATIVE SAMPLERS: requests=%u descriptor_updates=%u",bind_requests,bind_updates);n3ds_log(message);}
    for(unsigned int i=0;i<PLASMA_TEXTURE_SLOTS;++i) if(plasma_textures[i].resource) {
        void *child=plasma_textures[i].resource;memset(plasma_textures+i,0,sizeof(plasma_textures[i]));
        n3ds_gpu_texture_destroy(child);
    }
    plasma_texture_bytes=0;
    bind_requests=bind_updates=0;
    memset(bound, 0, sizeof(bound));
    memset(bound_valid,0,sizeof(bound_valid));
    memset(immutable,0,sizeof(immutable));
    memset(immutable_bucket,0,sizeof(immutable_bucket));
    for(unsigned int i=0;i<font_texture_count;++i) C3D_TexDelete(font_textures+i);
    memset(font_textures,0,sizeof(font_textures));memset(font_leased,0,sizeof(font_leased));
    font_texture_count=font_pool_initialized=0;
    C3D_TexDelete(&neutral);
    initialized = 0;
    /* Caller immediately finalizes Citro3D; no draw may follow disposal. */
}

static void *texture_create(const void *pixels, unsigned int width, unsigned int height, GPU_TEXCOLOR format)
{
    C3D_Tex *texture;
    if (!pixels || width < 8 || height < 8 || width > 512 || height > 512 ||
        (width & (width-1)) || (height & (height-1))) return NULL;
    texture=NULL;
    if(width==128 && height==128 && format==GPU_RGBA4) {
        if(!font_pool_initialized) {
            font_pool_initialized=1;
            for(unsigned int i=0;i<FONT_TEXTURE_SLOTS;++i) {
                if(!C3D_TexInit(font_textures+i,128,128,GPU_RGBA4)) break;
                ++font_texture_count;
            }
        }
        for(unsigned int i=0;i<font_texture_count;++i) if(!font_leased[i]) {
            font_leased[i]=1;texture=font_textures+i;break;
        }
    }
    if(!texture) {
        texture=calloc(1,sizeof(*texture));if(!texture)return NULL;
        if(!C3D_TexInit(texture,width,height,format)){free(texture);return NULL;}
    }
    memcpy(texture->data, pixels, texture->size);
    C3D_TexSetFilter(texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(texture, GPU_REPEAT, GPU_REPEAT);
    C3D_TexFlush(texture);
    return texture;
}
void *n3ds_gpu_texture_create(const unsigned int *pixels, unsigned int width, unsigned int height)
{ return texture_create(pixels,width,height,GPU_RGBA8); }
void *n3ds_gpu_texture_create_rgba4(const void *pixels, unsigned int width, unsigned int height)
{ return texture_create(pixels,width,height,GPU_RGBA4); }
void *n3ds_gpu_texture_create_cube(const unsigned char *rgba,unsigned int side)
{
    if(!initialized || !rgba || side<8 || side>64 || (side&(side-1))) return NULL;
    C3D_Tex *texture=calloc(1,sizeof(C3D_Tex)+sizeof(C3D_TexCube));if(!texture) return NULL;
    C3D_TexCube *cube=(C3D_TexCube *)(texture+1);
    if(!C3D_TexInitWithParams(texture,cube,(C3D_TexInitParams){side,side,0,GPU_RGBA8,GPU_TEX_CUBE_MAP,false})) {free(texture);return NULL;}
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    for(unsigned int face=0;face<6;++face) {
        unsigned int *out=cube->data[face];
        for(unsigned int y=0;y<side;++y) for(unsigned int x=0;x<side;++x) {
            unsigned int native_y=side-1-y;
            unsigned int offset=((native_y/8)*(side/8)+x/8)*64+spread[x&7]+2*spread[native_y&7];
            const unsigned char *pixel=rgba+((face*side+y)*side+x)*4;
            out[offset]=(unsigned int)pixel[0]<<24 | (unsigned int)pixel[1]<<16 | (unsigned int)pixel[2]<<8 | pixel[3];
        }
    }
    C3D_TexSetFilter(texture,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    /* This Citro3D version flushes tex->data as a 2D image. For a cube that
     * member is the face-pointer table, so flush each actual face explicitly. */
    for(unsigned int face=0;face<6;++face) if(R_FAILED(GSPGPU_FlushDataCache(cube->data[face],side*side*4))) {
        C3D_TexDelete(texture);free(texture);return NULL;
    }
    return texture;
}
int n3ds_gpu_texture_barrier(void)
{
    /* FrameBegin waits for submitted GPU commands. Refuse resource reclamation
     * inside an active frame: its commands may still refer to these textures. */
    /* The suspend hook drained the queue before HOME took the GPU. Closing
     * must free resources without submitting an empty frame or waiting VBlank. */
    if(aptShouldClose()) return 1;
    if (!C3D_FrameBegin(0)) return 0;
    /* No CPU vertex data is submitted by this barrier. Flushing all linear
     * memory here can overwrite GPU-written scanout buffers with stale data. */
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    return 1;
}
void n3ds_gpu_texture_destroy(void *resource)
{
    if (!resource) return;
    for(unsigned int i=0;i<PLASMA_TEXTURE_SLOTS;++i) if(plasma_textures[i].owner==resource) {
        void *child=plasma_textures[i].resource;
        plasma_texture_bytes-=plasma_textures[i].bytes;memset(plasma_textures+i,0,sizeof(plasma_textures[i]));
        n3ds_gpu_texture_destroy(child);
    }
    forget_identity(resource);
    for (unsigned int stage = 0; stage < 3; ++stage)
        if (bound[stage] == resource) n3ds_gpu_texture_bind(stage, NULL);
    for(unsigned int i=0;i<font_texture_count;++i) if(resource==font_textures+i) {
        if(!font_leased[i])abort();font_leased[i]=0;return;
    }
    C3D_TexDelete(resource);
    free(resource);
}
unsigned int n3ds_gpu_texture_checksum(void *resource)
{
    C3D_Tex *texture = resource;
    unsigned int hash = 2166136261U;
    unsigned int faces=C3D_TexGetType(texture)==GPU_TEX_CUBE_MAP?6:1;
    for(unsigned int face=0;face<faces;++face) {
        const unsigned char *data=faces==6?texture->cube->data[face]:texture->data;
        for (unsigned int i = 0; i < texture->size; ++i) hash = (hash ^ data[i])*16777619U;
    }
    return hash;
}

void *n3ds_gpu_texture_plasma(void *resource,unsigned int available)
{
    C3D_Tex *source=resource;unsigned int identity=n3ds_gpu_texture_content_id(resource),slot=PLASMA_TEXTURE_SLOTS;
    if(!initialized || !identity || !source || C3D_TexGetType(source)!=GPU_TEX_2D || source->fmt!=GPU_RGBA8 ||
       source->size!=(unsigned int)source->width*source->height*4) return NULL;
    for(unsigned int i=0;i<PLASMA_TEXTURE_SLOTS;++i) {
        if(plasma_textures[i].owner==source && plasma_textures[i].identity==identity) return plasma_textures[i].resource;
        if(!plasma_textures[i].resource && slot==PLASMA_TEXTURE_SLOTS) slot=i;
    }
    if(slot==PLASMA_TEXTURE_SLOTS || plasma_texture_bytes>available || source->size>available-plasma_texture_bytes ||
       plasma_texture_bytes>PLASMA_TEXTURE_BYTES || source->size>PLASMA_TEXTURE_BYTES-plasma_texture_bytes) return NULL;
    unsigned int *copy=malloc(source->size);if(!copy) return NULL;
    const unsigned int *original=source->data;
    /* RGBA -> ARGB, same tiled coordinates. A linear permutation commutes
     * with bilinear filtering; changing alpha into a nonlinear normal does not. */
    for(unsigned int i=0;i<source->size/4;++i) copy[i]=(original[i]>>8)|(original[i]<<24);
    void *result=n3ds_gpu_texture_create(copy,source->width,source->height);free(copy);if(!result) return NULL;
    n3ds_gpu_texture_publish_immutable(result);
    plasma_textures[slot].owner=source;plasma_textures[slot].resource=result;
    plasma_textures[slot].identity=identity;plasma_textures[slot].bytes=source->size;plasma_texture_bytes+=source->size;
    return result;
}
int n3ds_gpu_texture_plasma_tests(void)
{
    void *owners[PLASMA_TEXTURE_SLOTS+1]={0};unsigned int pixels[64],old_bytes=plasma_texture_bytes;int ok=0;
    if(old_bytes) return 1;
    for(unsigned int i=0;i<64;++i) pixels[i]=0x01020304u+i*0x0305070bu;
    for(unsigned int i=0;i<PLASMA_TEXTURE_SLOTS+1;++i) {
        owners[i]=n3ds_gpu_texture_create(pixels,8,8);if(!owners[i]) goto done;
        if(n3ds_gpu_texture_plasma(owners[i],PLASMA_TEXTURE_BYTES)) goto done;
        if(!n3ds_gpu_texture_publish_immutable(owners[i])) goto done;
        if(i==PLASMA_TEXTURE_SLOTS) {if(n3ds_gpu_texture_plasma(owners[i],PLASMA_TEXTURE_BYTES)) goto done;break;}
        if(n3ds_gpu_texture_plasma(owners[i],plasma_texture_bytes+255)) goto done;
        C3D_Tex *copy=n3ds_gpu_texture_plasma(owners[i],PLASMA_TEXTURE_BYTES);if(!copy) goto done;
        if(copy!=n3ds_gpu_texture_plasma(owners[i],PLASMA_TEXTURE_BYTES)) goto done;
        for(unsigned int p=0;p<64;++p) if(((unsigned int *)copy->data)[p]!=((pixels[p]>>8)|(pixels[p]<<24)) ||
            ((unsigned int *)((C3D_Tex *)owners[i])->data)[p]!=pixels[p]) goto done;
    }
    if(plasma_texture_bytes!=PLASMA_TEXTURE_SLOTS*256) goto done;
    n3ds_gpu_texture_destroy(owners[0]);owners[0]=NULL;
    if(plasma_texture_bytes!=(PLASMA_TEXTURE_SLOTS-1)*256 || !n3ds_gpu_texture_plasma(owners[PLASMA_TEXTURE_SLOTS],PLASMA_TEXTURE_BYTES)) goto done;
    ok=1;
done:
    if(!n3ds_gpu_texture_barrier()) abort();
    for(unsigned int i=0;i<PLASMA_TEXTURE_SLOTS+1;++i) n3ds_gpu_texture_destroy(owners[i]);
    if(plasma_texture_bytes!=old_bytes) ok=0;
    if(ok) {extern void n3ds_log(const char *);n3ds_log("PASS: plasma texture variants: exact channel permutation, immutable-only, reuse, budget rejection, slot exhaustion and owner cleanup");}
    return !ok;
}
