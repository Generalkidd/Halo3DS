/* Halo3DS: keep libctru's linear allocation policy, but give its
 * tiny metadata nodes stable, bounded storage instead of fragmenting malloc.
 * Vendor license and altered-source notices are in ../vendor/libctru-allocator.
 * The shared metadata arena is synchronized across linear and VRAM callers. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "engine_allocation_profile.h"
extern "C" {
#include <3ds/types.h>
#include <3ds/util/rbtree.h>
#include <3ds/allocator/linear.h>
#include <3ds/synchronization.h>
}

enum { NODE_COUNT=2048, NODE_BYTES=24, NO_NODE=65535 };
struct alignas(8) node_storage { unsigned char bytes[NODE_BYTES]; };
static node_storage nodes[NODE_COUNT];
static uint16_t next_node[NODE_COUNT],first_node;
static uint32_t node_used[NODE_COUNT/32];
static unsigned pool_initialized,pool_live,pool_peak,pool_fallbacks;
static LightLock metadata_lock;
void *metadata_allocate(size_t bytes)
{
    LightLock_Lock(&metadata_lock);
    if(!pool_initialized) {
        for(unsigned i=0;i<NODE_COUNT;i++) next_node[i]=i+1;
        next_node[NODE_COUNT-1]=NO_NODE;first_node=0;pool_initialized=1;
    }
    if(bytes<=NODE_BYTES && first_node!=NO_NODE) {
        unsigned i=first_node;first_node=next_node[i];
        node_used[i/32]|=1U<<(i%32);
        if(++pool_live>pool_peak)pool_peak=pool_live;
        void *result=nodes[i].bytes;
        LightLock_Unlock(&metadata_lock);
        return result;
    }
    ++pool_fallbacks;
    LightLock_Unlock(&metadata_lock);
    return malloc(bytes);
}
void metadata_release(void *pointer)
{
    uintptr_t p=(uintptr_t)pointer,begin=(uintptr_t)nodes;
    if(p>=begin && p-begin<sizeof(nodes)) {
        LightLock_Lock(&metadata_lock);
        unsigned offset=p-begin,i=offset/sizeof(node_storage);
        if(offset%sizeof(node_storage) || !(node_used[i/32]&(1U<<(i%32)))) abort();
        node_used[i/32]&=~(1U<<(i%32));
        next_node[i]=first_node;first_node=i;--pool_live;
        LightLock_Unlock(&metadata_lock);
    } else free(pointer);
}
extern "C" void n3ds_linear_metadata_stats(unsigned *live,unsigned *peak,unsigned *fallbacks)
{ LightLock_Lock(&metadata_lock);*live=pool_live;*peak=pool_peak;*fallbacks=pool_fallbacks;LightLock_Unlock(&metadata_lock); }

/* Private class names keep the linear and VRAM allocator methods distinct.
 * These macros affect only the included allocator code. */
#define MemPool HaloLinearMemPool
#define MemBlock HaloLinearMemBlock
#define MemChunk HaloLinearMemChunk
#define malloc metadata_allocate
#define free metadata_release
#include "../vendor/libctru-allocator/mem_pool.cpp"
#ifdef HALO_N3DS_ALLOCATION_PROFILE
#define linearMemAlign n3ds_linear_align_impl
#define linearAlloc n3ds_linear_alloc_impl
#define linearFree n3ds_linear_free_impl
#endif
#include "../vendor/libctru-allocator/linear.cpp"
#undef malloc
#undef free
static_assert(sizeof(HaloLinearMemBlock)<=NODE_BYTES,"free-block metadata slot");
static_assert(sizeof(addrMapNode)<=NODE_BYTES,"allocated-block metadata slot");

#ifdef HALO_N3DS_ALGORITHM_PROFILE
extern "C" unsigned int n3ds_linear_allocator_used(void)
{
    if (!linear_pool_initialized) return 0;
    unsigned int free_bytes = linearSpaceFree();
    return free_bytes <= __ctru_linear_heap_size ? __ctru_linear_heap_size - free_bytes : 0;
}
#endif

#ifdef HALO_N3DS_ALLOCATION_PROFILE
#undef linearMemAlign
#undef linearAlloc
#undef linearFree

static void observe_linear_allocation(size_t bytes, const void *result)
{
    if (n3ds_allocation_profile_active())
        n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_ALLOCATE, !result && bytes != 0,
            n3ds_linear_allocator_used());
}

extern "C" void *linearMemAlign(size_t bytes, size_t alignment)
{
    void *result = n3ds_linear_align_impl(bytes, alignment);
    observe_linear_allocation(bytes, result);
    return result;
}

extern "C" void *linearAlloc(size_t bytes)
{
    void *result = n3ds_linear_alloc_impl(bytes);
    observe_linear_allocation(bytes, result);
    return result;
}

extern "C" void linearFree(void *pointer)
{
    bool observe = pointer != nullptr && n3ds_allocation_profile_active();
    n3ds_linear_free_impl(pointer);
    if (observe)
        n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_FREE, 0, n3ds_linear_allocator_used());
}
#endif
