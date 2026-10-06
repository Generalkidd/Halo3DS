#include "cseries.h"
#include "engine_decal_storage.h"
#include "memory/data.h"
void n3ds_log(const char *message);
static long protected_index, purged_index, purge_count;
static boolean locked(long index) { return index==protected_index; }
static void purge(long index) { purged_index=index; ++purge_count; }

int halo_engine_decal_storage_tests(void)
{
    struct n3ds_decal_storage s={0};
    struct lruv_cache_block *block;
    long handles[4], replacement, i, j, old_page;
    byte *p, *base;
#define CHECK(e) do { if (!(e)) { n3ds_log("DECAL STORAGE FAIL: " #e); return 1; } } while (0)
    protected_index=purged_index=NONE; purge_count=0;
    n3ds_decal_storage_initialize(&s,purge,locked);
    base=s.vertices;
    CHECK(!n3ds_decal_storage_lock(&s,NONE,32));
    CHECK(!n3ds_decal_storage_lock_slot(&s,0,32));
    CHECK(!n3ds_decal_storage_lock_slot(&s,-1,32));
    CHECK(!n3ds_decal_storage_lock_slot(&s,2048,32));
    for (i=0;i<4;++i) {
        handles[i]=lruv_block_new(s.cache,40960); CHECK(handles[i]!=NONE);
        p=n3ds_decal_storage_lock(&s,handles[i],40960); CHECK(p==base+i*40960);
        memset(p,0x31+i,40960);
        CHECK(!n3ds_decal_storage_lock(&s,handles[i],32));
        n3ds_decal_storage_unlock(&s);
        CHECK(!n3ds_decal_storage_lock(&s,handles[i]&0xffff,32));
        CHECK(n3ds_decal_storage_lock_slot(&s,handles[i]&0xffff,40960)==p);
        CHECK(!n3ds_decal_storage_lock_slot(&s,handles[i]&0xffff,32));
        n3ds_decal_storage_unlock(&s);
        CHECK(!n3ds_decal_storage_lock_slot(&s,handles[i]&0xffff,40976));
        CHECK(!n3ds_decal_storage_lock(&s,handles[i],40976));
        CHECK(!n3ds_decal_storage_lock(&s,handles[i],-16));
        CHECK(!n3ds_decal_storage_lock(&s,handles[i],31));
    }
    CHECK(lruv_block_new(s.cache,32)==NONE && !purge_count);
    /* Original LRU policy protects both current-frame blocks and explicitly
     * locked decals. After a frame, allocation evicts an unprotected block. */
    lruv_idle(s.cache); protected_index=handles[0];
    replacement=lruv_block_new(s.cache,40960);
    CHECK(replacement!=NONE && purge_count==1 && purged_index!=protected_index);
    CHECK(!n3ds_decal_storage_lock(&s,purged_index,32));
    p=n3ds_decal_storage_lock(&s,replacement,40960); CHECK(p);
    memset(p,0xab,40960); n3ds_decal_storage_unlock(&s);
    for (i=0;i<4;++i) if (handles[i]!=purged_index) {
        p=n3ds_decal_storage_lock(&s,handles[i],40960); CHECK(p);
        for (j=0;j<40960;++j) CHECK(p[j]==0x31+i);
        n3ds_decal_storage_unlock(&s);
    }
    /* Reject corrupt snapshot ranges before constructing an out-of-bounds
     * pointer, including a block which begins inside but ends past the arena. */
    block=datum_get(s.cache->blocks,replacement); old_page=block->first_page_index;
    block->first_page_index=2559; CHECK(!n3ds_decal_storage_lock(&s,replacement,32));
    block->first_page_index=-1; CHECK(!n3ds_decal_storage_lock(&s,replacement,32));
    block->first_page_index=old_page;
    protected_index=NONE; lruv_flush(s.cache);
    CHECK(!n3ds_decal_storage_lock_slot(&s,0,32));
    CHECK(purge_count==5 && s.cache->first_block_index==NONE);
    replacement=lruv_block_new(s.cache,N3DS_DECAL_BYTES); CHECK(replacement!=NONE);
    p=n3ds_decal_storage_lock(&s,replacement,N3DS_DECAL_BYTES); CHECK(p==base);
    memset(p,0x7d,N3DS_DECAL_BYTES); n3ds_decal_storage_unlock(&s);
    CHECK(!n3ds_decal_storage_lock(&s,replacement,N3DS_DECAL_BYTES+16));
    lruv_flush(s.cache); n3ds_decal_storage_dispose(&s);
    CHECK(!s.cache && !s.vertices && !s.locked);
    CHECK(!n3ds_decal_storage_lock(&s,replacement,32));
    /* Disposal relinquishes ownership without freeing the game-state arena. */
    for (j=0;j<N3DS_DECAL_BYTES;++j) CHECK(base[j]==0x7d);
    n3ds_log("PASS: native decal storage, original LRU eviction/locked blocks, stale handles, bounds, persistent bytes and arena ownership");
    n3ds_log("PASS: original short decal slots resolve only live bounded allocations; full handles retain generation checks");
    return 0;
#undef CHECK
}
