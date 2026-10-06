#include "cseries.h"
#include "cache_reader.h"
#include "cache_build_compatibility.h"

void n3ds_log(const char *message);
/* Standalone cache tools have no linear allocator. The native platform supplies
 * strong definitions; explicit ownership keeps both close paths symmetric. */
__attribute__((weak)) void *n3ds_cache_linear_allocate(unsigned long bytes) { (void)bytes;return NULL; }
__attribute__((weak)) void n3ds_cache_linear_release(void *pointer) { (void)pointer; }
__attribute__((weak)) int n3ds_cache_prefer_linear(unsigned long bytes) { (void)bytes;return 0; }
__attribute__((weak)) unsigned long n3ds_cache_heap_allocation_size(unsigned long bytes) { return bytes; }
__attribute__((weak)) void n3ds_cache_progress(const char *stage,unsigned long done,unsigned long total)
{ (void)stage;(void)done;(void)total; }

int n3ds_cache_read_region(FILE *stream,void *destination,unsigned long bytes,const char *stage)
{
    unsigned long done=0;
    n3ds_cache_progress(stage,0,bytes);
    while(done<bytes) {
        unsigned long chunk=MIN(bytes-done,65536);
        if(fread((byte *)destination+done,1,chunk,stream)!=chunk) return 0;
        done+=chunk;n3ds_cache_progress(stage,done,bytes);
    }
    return 1;
}

void *n3ds_cache_allocate(struct n3ds_cache_view *view)
{
    /* Bypass cseries' fatal debug_malloc: fragmentation can exhaust a contiguous
     * normal-heap block even while the linear pool has enough room. */
    void *result=NULL;
    view->tags_linear=0;
    /* Large campaign tag caches must not consume the normal heap's model and
     * font working space merely because their allocation happens to fit. */
    if(n3ds_cache_prefer_linear(view->tag_bytes)) {
        result=n3ds_cache_linear_allocate(view->tag_bytes);
        if(result) {view->tags_linear=1;n3ds_log("STARTUP: large map cache placed in linear memory to reserve CPU working space");return result;}
    }
    /* Reusable large-block size classes keep nearby BSP sizes from requiring
     * a new contiguous hole after each map. The platform opts in only on
     * original models. Padding is bounded and never changes file ranges. */
    unsigned long reserved=n3ds_cache_heap_allocation_size(view->tag_bytes);
    if(reserved>view->tag_bytes && reserved-view->tag_bytes<512UL*1024) {
        result=(malloc)(reserved);
        if(result) {
            char message[128];snprintf(message,sizeof(message),"MAP HEAP BLOCK: requested=%lu reserved=%lu",view->tag_bytes,reserved);n3ds_log(message);
        }
    }
    /* A size-class request must never exclude an exact-sized block that fits. */
    if(!result) result=(malloc)(view->tag_bytes);
    if(!result) {
        result=n3ds_cache_linear_allocate(view->tag_bytes);
        if(result) {char message[128];view->tags_linear=1;
            snprintf(message,sizeof(message),"STARTUP: map cache linear fallback bytes=%lu",view->tag_bytes);n3ds_log(message);}
    }
    return result;
}
_Static_assert(sizeof(struct cache_file_header) == 2048, "Xbox cache header ABI");
_Static_assert(sizeof(struct cache_file_tag_header) == 36, "Xbox tag header ABI");
_Static_assert(sizeof(struct cache_file_tag_instance) == 32, "Xbox directory ABI");

void *n3ds_cache_resolve(const struct n3ds_cache_view *view, const void *xbox_address, unsigned long bytes)
{
    unsigned long address = (unsigned long)xbox_address;
    unsigned long offset;
    if (!view->tags || address < view->xbox_base) return NULL;
    offset = address - view->xbox_base;
    if (offset > view->tag_bytes || bytes > view->tag_bytes - offset) return NULL;
    return view->tags + offset;
}

void n3ds_cache_close(struct n3ds_cache_view *view)
{
    if(view->tags_linear) n3ds_cache_linear_release(view->tags);
    else free(view->tags);
    memset(view, 0, sizeof(*view));
}

struct cache_file_tag_instance *n3ds_cache_tag(const struct n3ds_cache_view *view, long handle, unsigned long group)
{
    unsigned long index = (unsigned long)handle & 0xffff;
    struct cache_file_tag_instance *instance;
    if (!view->tag_header || handle == NONE || index >= (unsigned long)view->tag_header->tag_count) return NULL;
    instance = &view->instances[index];
    if (instance->tag_index != handle || (unsigned long)instance->group_tag != group) return NULL;
    return instance;
}

int n3ds_cache_open(struct n3ds_cache_view *view, const char *path, int allow_retail)
{
    FILE *stream = NULL;
    long file_bytes;
    unsigned long count, index;
    const char *failure = "cache open failed";
    memset(view, 0, sizeof(*view));
    stream = fopen(path, "rb");
    if (!stream) goto fail;
    failure = "cache header read failed";
    if (fread(&view->header, 1, sizeof(view->header), stream) != sizeof(view->header)) goto fail;
    failure = "invalid cache header or build";
    if (view->header.header_signature != 'head' || view->header.footer_signature != 'foot' ||
        view->header.version != 5 || !memchr(view->header.name, 0, 32) ||
        !memchr(view->header.build, 0, 32) || !cache_build_is_supported(view->header.build, allow_retail)) goto fail;
    failure = "invalid expanded cache extent";
    if (fseek(stream, 0, SEEK_END) || (file_bytes = ftell(stream)) < 0 ||
        view->header.file_length < 2048 || view->header.file_length > 0x11600000 ||
        view->header.file_length > file_bytes || view->header.tag_data_offset < 2048 ||
        view->header.tag_data_size < 36 || view->header.tag_data_size > 0x1600000 ||
        view->header.tag_data_offset > view->header.file_length - view->header.tag_data_size) goto fail;
    view->tag_bytes = (unsigned long)view->header.tag_data_size;
    view->xbox_base = 0x803a6000;
    {
        char message[160];extern void debug_dump_memory(void);
        snprintf(message,sizeof(message),"STARTUP: tag allocation map=%s bytes=%lu",view->header.name,view->tag_bytes);n3ds_log(message);
        debug_dump_memory();
    }
    view->tags = n3ds_cache_allocate(view);
#ifdef HALO_N3DS_BOOT_TRACE
    n3ds_log("STARTUP: TRACE tag allocator returned");
#endif
    failure = "tag region allocation failed";
    if (!view->tags) goto fail;
    failure = "tag region read failed";
    if (fseek(stream, view->header.tag_data_offset, SEEK_SET)) goto fail;
#ifdef HALO_N3DS_BOOT_TRACE
    n3ds_log("STARTUP: TRACE tag seek completed; starting read/progress");
#endif
    if (!n3ds_cache_read_region(stream,view->tags,view->tag_bytes,"Reading map")) goto fail;
#ifdef HALO_N3DS_BOOT_TRACE
    n3ds_log("STARTUP: TRACE tag read completed");
#endif
    fclose(stream);
    stream = NULL;
    view->tag_header = (struct cache_file_tag_header *)view->tags;
    failure = "invalid tag directory";
    if (view->tag_header->signature != 'tags' || view->tag_header->tag_count <= 0 || view->tag_header->tag_count > 65535) goto fail;
    count = (unsigned long)view->tag_header->tag_count;
    view->instances = n3ds_cache_resolve(view, view->tag_header->tag_instances, count * sizeof(*view->instances));
    if (!view->instances || ((unsigned long)view->instances & 3)) goto fail;
    for (index = 0; index < count; ++index) {
        char *name = n3ds_cache_resolve(view, view->instances[index].name, 1);
        unsigned long remaining;
        if (!name || ((unsigned long)view->instances[index].tag_index & 0xffff) != index) goto fail;
        remaining = view->tag_bytes - ((byte *)name - view->tags);
        if (!memchr(name, 0, remaining < 256 ? remaining : 256)) goto fail;
    }
    return 0;
fail:
    if (stream) fclose(stream);
    n3ds_log(failure);
    n3ds_cache_close(view);
    return 1;
}
