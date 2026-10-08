/* Opt-in diagnostic: exercise the PC retail cache reader on ARM without
 * publishing partially converted tags to the Xbox engine. No game data is
 * linked into the executable. */
#include "cache_file_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void n3ds_log(const char *message);

struct pc_probe_file {
    FILE *stream;
    struct cache_file_source source;
};

static int read_at(void *context, uint32_t offset, uint32_t size, void *buffer)
{
    FILE *stream = context;
    return offset <= 0x7fffffffU && size <= 0x7fffffffU - offset &&
        fseek(stream, (long)offset, SEEK_SET) == 0 &&
        fread(buffer, 1, size, stream) == size;
}

static int open_file(struct pc_probe_file *file, const char *path)
{
    long size;
    memset(file, 0, sizeof(*file));
    file->stream = fopen(path, "rb");
    if (!file->stream) return 0;
    if (fseek(file->stream, 0, SEEK_END) || (size = ftell(file->stream)) < 0) {
        fclose(file->stream);
        file->stream = NULL;
        return 0;
    }
    file->source.context = file->stream;
    file->source.read = read_at;
    file->source.size = (uint32_t)size;
    return 1;
}

static void close_file(struct pc_probe_file *file)
{
    if (file->stream) fclose(file->stream);
    memset(file, 0, sizeof(*file));
}

static void status_log(const char *step, enum cache_file_status status)
{
    char message[192];
    snprintf(message, sizeof(message), "PC PROBE: %s: %s", step,
        cache_file_status_describe(status));
    n3ds_log(message);
}

static uint32_t word_at(const void *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

int n3ds_pc_cache_probe(const char *name)
{
    static const char *const resources[NUMBER_OF_RESOURCE_MAP_TYPES] = {
        NULL, "bitmaps.map", "sounds.map", "loc.map"
    };
    struct pc_probe_file map = {0}, files[NUMBER_OF_RESOURCE_MAP_TYPES] = {{0}};
    struct resource_map storage[NUMBER_OF_RESOURCE_MAP_TYPES] = {{0}};
    struct resource_map *opened[NUMBER_OF_RESOURCE_MAP_TYPES] = {0};
    struct cache_file_identity identity;
    struct custom_edition_load_report load;
    struct custom_edition_conversion_report conversion;
    struct custom_edition_rebase_report rebase;
    struct custom_edition_bsp_rebase_report bsp_rebase;
    enum cache_file_status status;
    uint8_t *tags = NULL;
    uint32_t capacity, used;
    char path[160], message[240];
    int ok = 0;

    if (!name || !*name || strlen(name) > 31) goto done;
    for (const char *p = name; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
            goto done;
    snprintf(path, sizeof(path), "sdmc:/halo-source/pc-retail/%s.map", name);
    if (!open_file(&map, path)) {n3ds_log("PC PROBE: map unavailable"); goto done;}
    status = cache_file_identify(&map.source, &identity);
    status_log("identify", status);
    if (status != _cache_file_status_ok ||
        identity.format != _cache_file_format_pc_retail_cache ||
        strcmp(identity.name, name)) goto done;
    snprintf(message, sizeof(message), "PC PROBE: %s version=%ld file=%lu tags=%lu",
        identity.name, (long)identity.version,
        (unsigned long)identity.file_length, (unsigned long)identity.tag_data_size);
    n3ds_log(message);

    for (int type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; ++type) {
        snprintf(path, sizeof(path), "sdmc:/halo-source/pc-retail/%s", resources[type]);
        if (!open_file(&files[type], path)) continue;
        status = resource_map_open(&files[type].source, (enum resource_map_type)type,
            &storage[type]);
        if (status != _cache_file_status_ok) {status_log("resource map", status); goto done;}
        opened[type] = &storage[type];
    }
    capacity = custom_edition_tag_cache_bytes(&identity);
    tags = malloc(capacity);
    if (!tags) {n3ds_log("PC PROBE: tag allocation failed"); goto done;}
    status = custom_edition_cache_load(&map.source, opened, tags, capacity, &load);
    status_log("load", status);
    if (status != _cache_file_status_ok) {
        snprintf(message, sizeof(message), "PC PROBE: load problem tag=%ld location=%08lx",
            (long)load.problem_tag_index, (unsigned long)load.problem_location);
        n3ds_log(message);
        goto done;
    }
    used = load.tag_data_bytes + load.resource_tag_bytes;
    snprintf(message, sizeof(message), "PC PROBE: tags=%ld bsp=%ld used=%lu resource=%lu",
        (long)load.tag_count, (long)load.structure_bsp_count,
        (unsigned long)used, (unsigned long)load.resource_tag_bytes);
    n3ds_log(message);
    custom_edition_cache_combine_resource_offsets(tags, used, 0x40000000U, 0x60000000U);
    status = custom_edition_cache_convert(tags, used, &conversion);
    status_log("convert", status);
    if (status != _cache_file_status_ok) goto done;
    status = custom_edition_cache_rebase_known(tags, used, capacity,
        (uint32_t)(uintptr_t)tags, &rebase);
    status_log("known pointer rebase", status);
    if (status != _cache_file_status_ok) goto done;
    snprintf(message, sizeof(message),
        "PC PROBE: pointers=%lu known=%lu unknown=%lu special_bsp=%lu",
        (unsigned long)rebase.pointer_count, (unsigned long)rebase.known_tag_count,
        (unsigned long)rebase.unknown_tag_count,
        (unsigned long)rebase.special_bsp_tag_count);
    n3ds_log(message);
    {
        /* The scenario block was already checked by cache_load and rebased
         * by rebase_known. Read one BSP into its reserved tag arena slot and
         * exercise the separate bounded BSP pass on actual ARM memory. */
        uint32_t scenario_handle = word_at(tags + 4);
        uint8_t *scenario = custom_edition_cache_tag_get(tags, used,
            scenario_handle, 'scnr', 0x5B0);
        if (!scenario) {n3ds_log("PC PROBE: rebased scenario unavailable"); goto done;}
        const uint8_t *block = scenario + 0x5A4;
        int32_t count = (int32_t)word_at(block);
        if (count != load.structure_bsp_count || count < 0 || count > 32)
            {n3ds_log("PC PROBE: BSP block count mismatch"); goto done;}
        for (int i = 0; i < count; ++i) {
            const uint8_t *ref = custom_edition_cache_block_element(tags, used,
                block, i, 0x20);
            if (!ref) {n3ds_log("PC PROBE: BSP reference unavailable"); goto done;}
            uint32_t file_offset = word_at(ref);
            uint32_t bytes = word_at(ref + 4);
            uint32_t address = word_at(ref + 8);
            uint32_t base = (uint32_t)(uintptr_t)tags;
            if (address < base || bytes < 0x18 || bytes > capacity ||
                address - base > capacity - bytes ||
                file_offset > map.source.size || bytes > map.source.size - file_offset)
                {n3ds_log("PC PROBE: BSP extent invalid"); goto done;}
            uint8_t *destination = tags + (address - base);
            if (!map.source.read(map.source.context, file_offset, bytes, destination))
                {n3ds_log("PC PROBE: BSP read failed"); goto done;}
            status = custom_edition_bsp_rebase_known(destination, bytes,
                CUSTOM_EDITION_TAG_CACHE_ADDRESS + (address - base), address,
                &bsp_rebase);
            status_log("BSP known pointer rebase", status);
            if (status != _cache_file_status_ok) goto done;
            snprintf(message, sizeof(message),
                "PC PROBE: BSP %d bytes=%lu pointers=%lu lightmaps=%lu materials=%lu",
                i, (unsigned long)bytes, (unsigned long)bsp_rebase.pointer_count,
                (unsigned long)bsp_rebase.lightmap_count,
                (unsigned long)bsp_rebase.material_count);
            n3ds_log(message);
        }
    }
    ok = 1;
done:
    free(tags);
    for (int type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; ++type) {
        if (opened[type]) resource_map_close(opened[type]);
        close_file(&files[type]);
    }
    close_file(&map);
    n3ds_log(ok ? "PC PROBE RESULT: PASS" : "PC PROBE RESULT: FAIL");
    return ok ? 0 : 1;
}
