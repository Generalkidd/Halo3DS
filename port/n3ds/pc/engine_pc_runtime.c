/* Diagnostic PC v7 tag handoff for discovering the remaining runtime ABI
 * differences. This unit is inert in ordinary launcher builds. */
#include "cseries.h"
#include "cache_files.h"
#include "crc.h"
#include "data.h"
#include "tag_groups.h"
#include "scenario_definitions.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "cache_reader.h"
#include "engine_pc_runtime.h"
#include "cache_file_formats.h"
#include "engine_pc_models.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_N3DS_EXPERIMENTAL_PC_UI

void n3ds_log(const char *message);

struct pc_file {
    FILE *stream;
    struct cache_file_source source;
};

static struct {
    int active;
    uint8_t *tags;
    uint32_t capacity;
    void *model_geometry;
    uint32_t model_geometry_bytes;
    struct cache_file_header header;
    struct pc_file map;
    struct pc_file resource_files[NUMBER_OF_RESOURCE_MAP_TYPES];
    struct resource_map resource_storage[NUMBER_OF_RESOURCE_MAP_TYPES];
    struct resource_map *resources[NUMBER_OF_RESOURCE_MAP_TYPES];
} pc;
static int pc_overlay_mode;

static int read_at(void *context, uint32_t offset, uint32_t size, void *buffer)
{
    FILE *stream = context;
    return offset <= 0x7fffffffU && size <= 0x7fffffffU - offset &&
        fseek(stream, (long)offset, SEEK_SET) == 0 &&
        fread(buffer, 1, size, stream) == size;
}

static int open_file(struct pc_file *file, const char *path)
{
    long size;
    memset(file, 0, sizeof(*file));
    file->stream = fopen(path, "rb");
    if (!file->stream) return 0;
    if (fseek(file->stream, 0, SEEK_END) || (size = ftell(file->stream)) < 0) {
        fclose(file->stream); file->stream = NULL;
        return 0;
    }
    file->source.context = file->stream;
    file->source.read = read_at;
    file->source.size = (uint32_t)size;
    return 1;
}

static void close_file(struct pc_file *file)
{
    if (file->stream) fclose(file->stream);
    memset(file, 0, sizeof(*file));
}

static int valid_name(const char *name)
{
    if (!name || !*name || strlen(name) > 31) return 0;
    for (const char *cursor = name; *cursor; ++cursor)
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) return 0;
    return 1;
}

int n3ds_pc_map_available(const char *name)
{
    struct pc_file file = {0};
    struct cache_file_identity identity;
    enum cache_file_status status;
    char path[160];
    if (!valid_name(name)) return 0;
    snprintf(path, sizeof(path), "sdmc:/halo-source/pc-retail/%s.map", name);
    if (!open_file(&file, path)) return 0;
    status = cache_file_identify(&file.source, &identity);
    close_file(&file);
    return status == _cache_file_status_ok &&
        identity.format == _cache_file_format_pc_retail_cache &&
        !strcmp(identity.name, name);
}

void n3ds_pc_tags_close(void)
{
    free(pc.model_geometry);
    for (int type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; ++type) {
        if (pc.resources[type]) resource_map_close(pc.resources[type]);
        close_file(&pc.resource_files[type]);
    }
    close_file(&pc.map);
    memset(&pc, 0, sizeof(pc));
    pc_overlay_mode = 0;
}

int n3ds_pc_tags_active(void) { return pc.active && !pc_overlay_mode; }
int n3ds_pc_tags_loaded(void) { return pc.active; }
void n3ds_pc_tags_use_overlay(void) { if (pc.active) pc_overlay_mode = 1; }

int n3ds_pc_overlay_contains(const void *address, unsigned long bytes)
{
    uintptr_t base = (uintptr_t)pc.tags;
    uintptr_t pointer = (uintptr_t)address;
    return pc.active && pc_overlay_mode && base && pointer >= base &&
        pointer - base <= pc.capacity && bytes <= pc.capacity - (pointer - base);
}

void *n3ds_pc_model_resolve_external(const struct n3ds_cache_view *view,
    const void *address, unsigned long bytes)
{
    uintptr_t base = (uintptr_t)pc.model_geometry;
    uintptr_t pointer = (uintptr_t)address;
    if (!pc.active || !view || (!pc_overlay_mode && view->tags != pc.tags) || !base ||
        pointer < base || pointer - base > pc.model_geometry_bytes ||
        bytes > pc.model_geometry_bytes - (pointer - base)) return NULL;
    return (void *)address;
}

int n3ds_pc_tags_open(const char *name, struct n3ds_cache_view *view)
{
    static const char *const names[NUMBER_OF_RESOURCE_MAP_TYPES] = {
        NULL, "bitmaps.map", "sounds.map", "loc.map"
    };
    struct cache_file_identity identity;
    struct custom_edition_load_report load;
    struct custom_edition_conversion_report conversion;
    struct custom_edition_rebase_report rebase;
    enum cache_file_status status;
    char path[160], message[256];
    uint32_t used;
    if (pc.active || !valid_name(name) || !view) return 1;
    memset(view, 0, sizeof(*view));
    snprintf(path, sizeof(path), "sdmc:/halo-source/pc-retail/%s.map", name);
    if (!open_file(&pc.map, path)) goto fail;
    status = cache_file_identify(&pc.map.source, &identity);
    if (status != _cache_file_status_ok ||
        identity.format != _cache_file_format_pc_retail_cache ||
        strcmp(identity.name, name)) goto fail;
    if (!pc.map.source.read(pc.map.source.context, 0, sizeof(view->header),
        &view->header)) goto fail;
    for (int type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; ++type) {
        snprintf(path, sizeof(path), "sdmc:/halo-source/pc-retail/%s", names[type]);
        if (!open_file(&pc.resource_files[type], path)) continue;
        status = resource_map_open(&pc.resource_files[type].source,
            (enum resource_map_type)type, &pc.resource_storage[type]);
        if (status != _cache_file_status_ok) goto fail;
        pc.resources[type] = &pc.resource_storage[type];
    }
    view->tag_bytes = custom_edition_tag_cache_bytes(&identity);
    view->tags = n3ds_cache_allocate(view);
    if (!view->tags) goto fail;
    status = custom_edition_cache_load(&pc.map.source, pc.resources,
        view->tags, view->tag_bytes, &load);
    if (status != _cache_file_status_ok) {
        snprintf(message, sizeof(message), "EXPERIMENTAL PC: tag load failed status=%d tag=%ld",
            (int)status, (long)load.problem_tag_index); n3ds_log(message);
        goto fail;
    }
    used = load.tag_data_bytes + load.resource_tag_bytes;
    custom_edition_cache_combine_resource_offsets(view->tags, used,
        0x40000000U, 0x60000000U);
    status = custom_edition_cache_convert(view->tags, used, &conversion);
    if (status != _cache_file_status_ok) goto fail;
    status = custom_edition_cache_rebase_known(view->tags, used, view->tag_bytes,
        (uint32_t)(uintptr_t)view->tags, &rebase);
    if (status != _cache_file_status_ok || rebase.unknown_tag_count) goto fail;
    if (!n3ds_pc_models_convert(view->tags, used, view->tag_bytes,
        &load, &pc.map.source, &pc.model_geometry, &pc.model_geometry_bytes)) goto fail;
    /* Xbox tag_index is 0x24 bytes; PC's is 0x28. These fields align except
     * the signature, which PC keeps at +0x24. The 3DS binder checks +0x20.
     * PC model-data size at +0x20 is retained separately by the load report. */
    *(uint32_t *)(view->tags + 0x20) = 'tags';
    view->tag_header = (struct cache_file_tag_header *)view->tags;
    view->instances = view->tag_header->tag_instances;
    view->xbox_base = (unsigned long)view->tags;
    pc.tags = view->tags;
    pc.capacity = view->tag_bytes;
    pc.header = view->header;
    pc.active = 1;
    snprintf(message, sizeof(message),
        "EXPERIMENTAL PC: partial %s handoff tags=%ld pointers=%lu bytes=%lu",
        name, (long)load.tag_count, (unsigned long)rebase.pointer_count,
        (unsigned long)used);
    n3ds_log(message);
    return 0;
fail:
    n3ds_log("EXPERIMENTAL PC: loader stopped before engine handoff");
    n3ds_cache_close(view);
    n3ds_pc_tags_close();
    return 1;
}

int n3ds_pc_cache_range_valid(unsigned long offset, unsigned long bytes)
{
    const struct cache_file_source *source;
    uint32_t relative;
    if (!pc.active || !bytes || bytes > UINT32_MAX || offset > UINT32_MAX)
        return 0;
    if (offset >= 0x60000000U) {
        source = &pc.resource_files[_resource_map_sounds].source;
        relative = (uint32_t)(offset - 0x60000000U);
    } else if (offset >= 0x40000000U) {
        source = &pc.resource_files[_resource_map_bitmaps].source;
        relative = (uint32_t)(offset - 0x40000000U);
    } else {
        source = &pc.map.source;
        relative = (uint32_t)offset;
    }
    return source->read && relative <= source->size && bytes <= source->size - relative;
}

int n3ds_pc_cache_read(unsigned long offset, unsigned long bytes, void *destination)
{
    if (!destination || !n3ds_pc_cache_range_valid(offset, bytes)) return 0;
    if (offset >= 0x60000000U)
        return pc.resource_files[_resource_map_sounds].source.read(
            pc.resource_files[_resource_map_sounds].source.context,
            (uint32_t)(offset - 0x60000000U), (uint32_t)bytes, destination);
    if (offset >= 0x40000000U)
        return pc.resource_files[_resource_map_bitmaps].source.read(
            pc.resource_files[_resource_map_bitmaps].source.context,
            (uint32_t)(offset - 0x40000000U), (uint32_t)bytes, destination);
    return pc.map.source.read(pc.map.source.context,
        (uint32_t)offset, (uint32_t)bytes, destination);
}

int n3ds_pc_bsp_load(struct n3ds_cache_view *view,
    const struct scenario_structure_bsp_reference *reference)
{
    struct custom_edition_bsp_rebase_report report;
    enum cache_file_status status;
    uint32_t address, size, file_offset, base, difference;
    char message[160];
    if (!n3ds_pc_tags_active() || !view || !reference) return 1;
    address = (uint32_t)(uintptr_t)reference->base_address;
    size = (uint32_t)reference->file_size;
    file_offset = (uint32_t)reference->file_offset;
    base = (uint32_t)(uintptr_t)pc.tags;
    if (address < base || size < 0x18 || size > pc.capacity ||
        (difference = address - base) > pc.capacity - size ||
        file_offset > pc.map.source.size || size > pc.map.source.size - file_offset)
        return 1;
    memset(view, 0, sizeof(*view));
    view->header = pc.header;
    view->tags = pc.tags + difference;
    view->tag_bytes = size;
    view->xbox_base = address;
    if (!pc.map.source.read(pc.map.source.context, file_offset, size, view->tags))
        return 1;
    status = custom_edition_bsp_rebase_known(view->tags, size,
        CUSTOM_EDITION_TAG_CACHE_ADDRESS + difference, address, &report);
    snprintf(message, sizeof(message), "EXPERIMENTAL PC: BSP status=%d pointers=%lu",
        (int)status, (unsigned long)report.pointer_count);
    n3ds_log(message);
    if (status == _cache_file_status_ok) {
        uint32_t structure = 0, count = 0, palette = 0;
        memcpy(&structure, view->tags, sizeof(structure));
        if (structure >= address && structure - address <= size - 0x1c0) {
            uint32_t at = structure - address + 0x1b4;
            memcpy(&count, view->tags + at, sizeof(count));
            memcpy(&palette, view->tags + at + 4, sizeof(palette));
            snprintf(message, sizeof(message),
                "EXPERIMENTAL PC: weather palette count=%lu raw=%08lx base=%08lx",
                (unsigned long)count, (unsigned long)palette, (unsigned long)address);
            n3ds_log(message);
            if (count && palette >= address && palette - address <= size - 0xf0) {
                uint32_t wind_group, wind_index;
                memcpy(&wind_group, view->tags + palette - address + 0x80, 4);
                memcpy(&wind_index, view->tags + palette - address + 0x8c, 4);
                snprintf(message, sizeof(message),
                    "EXPERIMENTAL PC: weather wind group=%08lx index=%08lx",
                    (unsigned long)wind_group, (unsigned long)wind_index);
                n3ds_log(message);
            }
        }
    }
    return status == _cache_file_status_ok ? 0 : 1;
}

#else
/* Keep the source in the normal manifest while making every symbol inert in
 * regular builds. Linker garbage collection omits the diagnostic unit. */
int n3ds_pc_map_available(const char *name) { (void)name; return 0; }
int n3ds_pc_tags_open(const char *name, struct n3ds_cache_view *view)
{ (void)name; (void)view; return 1; }
void n3ds_pc_tags_close(void) {}
int n3ds_pc_tags_active(void) { return 0; }
int n3ds_pc_tags_loaded(void) { return 0; }
void n3ds_pc_tags_use_overlay(void) {}
int n3ds_pc_overlay_contains(const void *address, unsigned long bytes)
{ (void)address; (void)bytes; return 0; }
int n3ds_pc_cache_read(unsigned long offset, unsigned long bytes, void *destination)
{ (void)offset; (void)bytes; (void)destination; return 0; }
int n3ds_pc_cache_range_valid(unsigned long offset, unsigned long bytes)
{ (void)offset; (void)bytes; return 0; }
int n3ds_pc_bsp_load(struct n3ds_cache_view *view,
    const struct scenario_structure_bsp_reference *reference)
{ (void)view; (void)reference; return 1; }
#endif
