#include "cseries.h"
#include "engine_decal_storage.h"
#include "memory/data.h"
#include "saved games/game_state.h"

void n3ds_decal_storage_initialize(struct n3ds_decal_storage *s,
    lruv_delete_block_proc purge, lruv_locked_block_proc locked)
{
    assert(s && !s->cache && !s->vertices && !s->locked);
    /* Preserve the original GPU partition's allocation and snapshot layout.
     * On 3DS both partitions are native RAM, not Xbox physical addresses. */
    s->vertices=game_state_gpu_malloc("decal vertices",NULL,N3DS_DECAL_BYTES);
    s->cache=game_state_lruv_cache_new("decal vertex cache",2560,6,2048,purge,locked);
    assert(s->vertices && s->cache);
}

void n3ds_decal_storage_dispose(struct n3ds_decal_storage *s)
{
    assert(s && s->cache && s->vertices && !s->locked);
    /* Both allocations belong to game_state. lruv_delete would incorrectly
     * pass an interior arena pointer to free(). Owners flush decals first. */
    assert(s->cache->first_block_index==NONE);
    memset(s,0,sizeof(*s));
}

void *n3ds_decal_storage_lock(struct n3ds_decal_storage *s, long index, long bytes)
{
    struct lruv_cache_block *block;
    unsigned long offset, capacity;
    if (!s || !s->cache || !s->vertices || s->locked || bytes<=0 || (bytes&15) || !(index>>16)) return NULL;
    block=datum_try_and_get(s->cache->blocks,index);
    if (!block || block->first_page_index<0 || block->first_page_index>=2560 ||
        block->page_count<=0 || block->page_count>2560-block->first_page_index) return NULL;
    offset=(unsigned long)block->first_page_index*64;
    capacity=(unsigned long)block->page_count*64;
    if ((unsigned long)bytes>capacity || (unsigned long)bytes>N3DS_DECAL_BYTES-offset) return NULL;
    s->locked=TRUE;
    return s->vertices+offset;
}

void n3ds_decal_storage_unlock(struct n3ds_decal_storage *s)
{
    assert(s && s->cache && s->locked);
    s->locked=FALSE;
}

void *n3ds_decal_storage_lock_slot(struct n3ds_decal_storage *s, long slot, long bytes)
{
    struct datum_header *header;
    struct data_array *blocks;
    unsigned long handle;
    if(!s || !s->cache || !(blocks=s->cache->blocks) || !blocks->valid ||
       slot<0 || slot>=blocks->maximum_count) return NULL;
    header=(struct datum_header *)((byte *)blocks->data+slot*blocks->size);
    if(!header->identifier) return NULL;
    /* A slot-only API cannot detect a prior generation. Recover the current
     * generation only here; full-handle callers retain stale-handle rejection. */
    handle=((unsigned long)(unsigned short)header->identifier<<16)|(unsigned long)slot;
    return n3ds_decal_storage_lock(s,(long)handle,bytes);
}
