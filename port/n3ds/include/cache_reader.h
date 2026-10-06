#ifndef HALO_N3DS_CACHE_READER_H
#define HALO_N3DS_CACHE_READER_H
#include "cache_file_format.h"

/* Disk pointers stay as Xbox virtual addresses until resolved explicitly.
 * Never hand these unrelocated structures to simulation or rendering code. */
struct n3ds_cache_view {
    struct cache_file_header header;
    byte *tags;
    unsigned long tag_bytes;
    unsigned long xbox_base;
    struct cache_file_tag_header *tag_header;
    struct cache_file_tag_instance *instances;
    int tags_linear;
};
void *n3ds_cache_allocate(struct n3ds_cache_view *view);
/* Bounded I/O permits loading feedback without another map-sized buffer. */
int n3ds_cache_read_region(FILE *stream,void *destination,unsigned long bytes,const char *stage);
void n3ds_cache_progress(const char *stage,unsigned long done,unsigned long total);
int n3ds_cache_open(struct n3ds_cache_view *view, const char *path, int allow_retail);
void n3ds_cache_close(struct n3ds_cache_view *view);
void *n3ds_cache_resolve(const struct n3ds_cache_view *view, const void *xbox_address, unsigned long bytes);
struct cache_file_tag_instance *n3ds_cache_tag(const struct n3ds_cache_view *view, long handle, unsigned long group);
int halo_cache_tests(void);
#endif
