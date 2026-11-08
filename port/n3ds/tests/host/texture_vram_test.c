/* Production mirror helper under a queue/cache/allocator model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
enum {GPU_RGBA8=0,GPU_RGB8=1,GPU_RGBA4=2,GPU_TEX_2D=0,GPU_TEX_CUBE_MAP=1};
typedef struct {void *data;unsigned fmt,size,width,height,param,border,lodBias,maxLevel,minLevel;} C3D_Tex;
enum {IMMUTABLE_SLOTS=272,IDENTITY_SLOTS=336,IMMUTABLE_BUCKETS=256};
static struct {void *resource;unsigned id;unsigned short next;} immutable[IDENTITY_SLOTS];
static unsigned short immutable_bucket[IMMUTABLE_BUCKETS];
static C3D_Tex neutral,bound_sampler[3];
static void *bound[3];
static int initialized=1,bound_valid[3];
static unsigned bind_updates;
static int active,closing,gpu_right=1,flush_fail,allocation_fail,lose_after_copy,copy_fail;
static unsigned allocation_extra;
static unsigned waits,transfers,flushes,binds,vram_used,vram_total=6*1024*1024;
static struct {void *data;unsigned bytes;} allocations[128],linear[128];
static struct {void *source,*destination;unsigned bytes;} dma;
static unsigned identity_bucket(void *p) {return ((uintptr_t)p>>3)&255;}
static unsigned C3D_TexGetType(C3D_Tex *t) {return t->param>>28;}
static int C3D_TexCalcMaxLevel(unsigned w,unsigned h) {unsigned n=w<h?w:h;int result=-3;while(n>1){++result;n>>=1;}return result;}
static unsigned C3D_TexCalcTotalSize(unsigned bytes,unsigned max) {return (bytes-(bytes>>(2*(max+1))))*4/3;}
static int addrIsVRAM(void *p) {for(unsigned i=0;i<128;++i)if(allocations[i].data==p)return 1;return 0;}
static unsigned linearGetSize(void *p) {for(unsigned i=0;i<128;++i)if(linear[i].data==p)return linear[i].bytes;return 0;}
static unsigned vramSpaceFree(void) {return vram_total-vram_used;}
static unsigned vramGetSize(void *p) {for(unsigned i=0;i<128;++i)if(allocations[i].data==p)return allocations[i].bytes;return 0;}
static void *vramAlloc(unsigned bytes) {
    bytes=((bytes+127u)&~127u)+allocation_extra;
    if(allocation_fail || bytes>vramSpaceFree())return NULL;
    for(unsigned i=0;i<128;++i)if(!allocations[i].data) {
        void *p=malloc(bytes);assert(p);allocations[i].data=p;allocations[i].bytes=bytes;vram_used+=bytes;return p;
    }
    assert(0);return NULL;
}
static void vramFree(void *p) {
    assert(!dma.bytes);
    for(unsigned i=0;i<128;++i)if(allocations[i].data==p) {
        vram_used-=allocations[i].bytes;free(p);memset(allocations+i,0,sizeof(allocations[i]));return;
    }
    assert(0);
}
static int n3ds_gpu_frame_active(void) {return active;}
static int aptShouldClose(void) {return closing;}
static int gspHasGpuRight(void) {return gpu_right;}
static int n3ds_c3d_texture_mirror_wait_idle(void) {
    assert(!active);++waits;
    if(dma.bytes) {memcpy(dma.destination,dma.source,dma.bytes);memset(&dma,0,sizeof(dma));}
    return 1;
}
static int GSPGPU_FlushDataCache(void *p,unsigned bytes) {assert(linearGetSize(p)>=bytes && !dma.bytes);++flushes;return flush_fail?-1:0;}
#define R_FAILED(x) ((x)<0)
static int n3ds_c3d_texture_mirror_copy(void *src,void *dst,unsigned bytes) {
    assert(!active && !dma.bytes && gpu_right && !closing);
    if(copy_fail)return 0;
    assert(linearGetSize(src)>=bytes && addrIsVRAM(dst));
    dma.source=src;dma.destination=dst;dma.bytes=bytes;++transfers;
    /* Deliberately return before DMA completes: PPF is not a lifetime fence. */
    if(lose_after_copy)gpu_right=0;
    assert(n3ds_c3d_texture_mirror_wait_idle());return 1;
}
static void C3D_TexBind(unsigned stage,C3D_Tex *sampler) {assert(stage<3 && sampler==bound_sampler+stage);++binds;}
#define HALO_N3DS_TEXTURE_VRAM_MIRRORS 1
#include "engine_texture_vram.inl"
static unsigned next_id;
static void publish(C3D_Tex *t,unsigned slot) {
    assert(slot<IDENTITY_SLOTS && !immutable[slot].resource);
    unsigned bucket=identity_bucket(t);
    immutable[slot].resource=t;immutable[slot].id=++next_id;
    immutable[slot].next=immutable_bucket[bucket];immutable_bucket[bucket]=slot+1;
}
static void withdraw(C3D_Tex *t) {
    texture_mirror_forget(t);
    unsigned short *link=immutable_bucket+identity_bucket(t);
    while(*link) {unsigned slot=*link-1;if(immutable[slot].resource==t) {
        *link=immutable[slot].next;memset(immutable+slot,0,sizeof(immutable[slot]));return;
    }link=&immutable[slot].next;}
}
static C3D_Tex make(unsigned width,unsigned format,unsigned mip,unsigned slot) {
    C3D_Tex t={.fmt=format,.width=width,.height=width,.size=width*width*(format==GPU_RGB8?3:4),.maxLevel=mip};
    unsigned bytes=C3D_TexCalcTotalSize(t.size,mip);t.data=malloc(bytes);assert(t.data);
    for(unsigned i=0;i<bytes;++i)((unsigned char *)t.data)[i]=(i*73+slot*19)&255;
    linear[slot].data=t.data;linear[slot].bytes=bytes;return t;
}
static void reset(void) {
    active=closing=flush_fail=allocation_fail=lose_after_copy=copy_fail=0;gpu_right=1;
    texture_mirror_dispose();assert(!vram_used && !dma.bytes);
    memset(bound,0,sizeof(bound));memset(bound_sampler,0,sizeof(bound_sampler));memset(bound_valid,0,sizeof(bound_valid));
    memset(immutable,0,sizeof(immutable));memset(immutable_bucket,0,sizeof(immutable_bucket));
    for(unsigned i=0;i<128;++i) {free(linear[i].data);memset(linear+i,0,sizeof(linear[i]));}
    waits=transfers=flushes=binds=bind_updates=allocation_extra=0;vram_total=6*1024*1024;
}
int main(void) {
    C3D_Tex t[70];
    t[0]=make(16,GPU_RGBA8,1,0);t[1]=make(16,GPU_RGB8,1,1);publish(t,0);publish(t+1,1);
    n3ds_gpu_texture_mirror_idle(1,0);texture_mirror_request(t);texture_mirror_request(t+1);
    active=1;n3ds_gpu_texture_mirror_idle(1,0);assert(!transfers);active=0;
    n3ds_gpu_texture_mirror_idle(1,1);assert(!transfers);
    n3ds_gpu_texture_mirror_idle(1,0);assert(transfers==1 && waits==2 && !dma.bytes);
    struct texture_mirror *m=texture_mirror_find(t,texture_mirror_identity(t));assert(m && m->state==MIRROR_READY && m->bytes==1280 && !memcmp(m->data,t[0].data,m->bytes));
    bound[0]=t;bound_sampler[0]=t[0];bound_sampler[0].param=0x12345;bound_sampler[0].lodBias=97;bound_valid[0]=1;
    n3ds_gpu_texture_mirror_select(1);assert(bound_sampler[0].data==m->data && binds==1);
    assert(bound_sampler[0].param==0x12345 && bound_sampler[0].lodBias==97 && t[0].param==0);
    n3ds_gpu_texture_mirror_select(0);assert(bound_sampler[0].data==t[0].data && binds==2);
    n3ds_gpu_texture_mirror_select(1);assert(binds==3);
    /* Cache invalidation is not permission to leave an installed stale address. */
    bound_valid[0]=0;active=1;withdraw(t);assert(m->state==MIRROR_RETIRED && bound_sampler[0].data==t[0].data && vram_used==1280);
    n3ds_gpu_texture_mirror_idle(1,0);assert(vram_used==1280);active=0;
    n3ds_gpu_texture_mirror_idle(1,1);assert(vram_used==1280 && transfers==1);
    n3ds_gpu_texture_mirror_idle(1,0);assert(transfers==2 && vram_used==1024);
    m=texture_mirror_find(t+1,texture_mirror_identity(t+1));assert(m && m->state==MIRROR_READY && !memcmp(m->data,t[1].data,960));
    /* Queued owner expiry, address reuse, and mutable revision exclusion. */
    t[2]=make(8,GPU_RGBA8,0,2);publish(t+2,2);texture_mirror_request(t+2);withdraw(t+2);publish(t+2,IMMUTABLE_SLOTS);texture_mirror_request(t+2);
    assert(!texture_mirror_identity(t+2));unsigned before=transfers;n3ds_gpu_texture_mirror_idle(1,0);assert(transfers==before);
    reset();
    /* Residency cap counts exact bytes; each boundary uploads at most once. */
    n3ds_gpu_texture_mirror_idle(1,0);
    for(unsigned i=0;i<17;++i){t[i]=make(128,GPU_RGBA8,0,i);publish(t+i,i);texture_mirror_request(t+i);}
    for(unsigned i=0;i<17;++i){before=transfers;n3ds_gpu_texture_mirror_idle(1,0);assert(transfers-before<=1);}
    unsigned resident,bytes,uploads,failures;n3ds_gpu_texture_mirror_stats(&resident,&bytes,&uploads,&failures);
    assert(resident==16 && bytes==1024*1024 && uploads==16 && !failures);
    n3ds_gpu_texture_mirror_idle(1,0);assert(transfers==16);reset();
    /* Space reserve and allocation failures do not retry each frame. */
    t[0]=make(128,GPU_RGBA8,0,0);publish(t,0);n3ds_gpu_texture_mirror_idle(1,0);texture_mirror_request(t);
    vram_total=1024*1024+65535;n3ds_gpu_texture_mirror_idle(1,0);assert(!transfers && !vram_used);
    vram_total=6*1024*1024;n3ds_gpu_texture_mirror_idle(1,0);assert(!transfers);reset();
    for(unsigned failure=0;failure<5;++failure) {
        t[0]=make(8,GPU_RGBA8,0,0);publish(t,0);n3ds_gpu_texture_mirror_idle(1,0);texture_mirror_request(t);
        allocation_fail=failure==0;flush_fail=failure==1;copy_fail=failure==2;lose_after_copy=failure==3;allocation_extra=failure==4?1024*1024:0;
        n3ds_gpu_texture_mirror_idle(1,0);
        assert(!vram_used && !dma.bytes && texture_mirror_failures==1);
        assert(transfers==(failure==3));
        gpu_right=1;allocation_fail=flush_fail=lose_after_copy=copy_fail=0;n3ds_gpu_texture_mirror_idle(1,0);assert(transfers==(failure==3));reset();
    }
    /* No admission while frozen; unsupported/oversize/cube images stay linear. */
    t[0]=make(128,GPU_RGBA8,1,0);publish(t,0);n3ds_gpu_texture_mirror_idle(1,0);texture_mirror_request(t);assert(!texture_mirror_find(t,texture_mirror_identity(t)));
    t[1]=make(8,GPU_RGBA8,0,1);publish(t+1,1);n3ds_gpu_texture_mirror_idle(1,1);texture_mirror_request(t+1);assert(!texture_mirror_find(t+1,texture_mirror_identity(t+1)));
    n3ds_gpu_texture_mirror_idle(1,0);t[1].param=GPU_TEX_CUBE_MAP<<28;texture_mirror_request(t+1);assert(!texture_mirror_find(t+1,texture_mirror_identity(t+1)));
    t[1].param=0;t[1].fmt=GPU_RGBA4;texture_mirror_request(t+1);assert(!texture_mirror_find(t+1,texture_mirror_identity(t+1)));reset();
    puts("PASS: production VRAM mirrors: exact mip bytes, one-upload bound, cap/reserve, retained sampler transitions, immutable lifetime, freeze and DMA failure retirement");
    return 0;
}
