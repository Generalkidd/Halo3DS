#ifndef N3DS_ENGINE_DECAL_STORAGE_H
#define N3DS_ENGINE_DECAL_STORAGE_H
#include "memory/lruv_cache.h"
/* Persistent CPU vertices. The renderer must copy these into frame-owned GPU
 * memory before drawing; this storage is not a PICA vertex buffer. */
enum { N3DS_DECAL_BYTES = 163840 };
struct n3ds_decal_storage {
    struct lruv_cache *cache;
    byte *vertices;
    boolean locked;
};
void n3ds_decal_storage_initialize(struct n3ds_decal_storage *storage,
    lruv_delete_block_proc purge, lruv_locked_block_proc locked);
void n3ds_decal_storage_dispose(struct n3ds_decal_storage *storage);
void *n3ds_decal_storage_lock(struct n3ds_decal_storage *storage, long index, long bytes);
/* Halo's public rasterizer ABI uses a 16-bit absolute slot. */
void *n3ds_decal_storage_lock_slot(struct n3ds_decal_storage *storage, long slot, long bytes);
void n3ds_decal_storage_unlock(struct n3ds_decal_storage *storage);
int halo_engine_decal_storage_tests(void);
#endif
