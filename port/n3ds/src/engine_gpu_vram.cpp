/* Halo3DS: share the synchronized bounded metadata arena with the linear pool.
 * The libctru allocation policy and vendor license are retained in
 * ../vendor/libctru-allocator, with altered-source notices. */
#include <stdlib.h>
extern "C" {
#include <3ds/types.h>
#include <3ds/os.h>
#include <3ds/util/rbtree.h>
#include <3ds/allocator/vram.h>
}
void *metadata_allocate(size_t bytes);
void metadata_release(void *pointer);
#define MemPool HaloVramMemPool
#define MemBlock HaloVramMemBlock
#define MemChunk HaloVramMemChunk
#define malloc metadata_allocate
#define free metadata_release
#include "../vendor/libctru-allocator/mem_pool.cpp"
#include "../vendor/libctru-allocator/vram.cpp"
#undef malloc
#undef free
static_assert(sizeof(HaloVramMemBlock)<=24,"free-block metadata slot");
static_assert(sizeof(addrMapNode)<=24,"allocated-block metadata slot");
