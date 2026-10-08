#ifndef HALO_N3DS_ENGINE_PC_MODELS_H
#define HALO_N3DS_ENGINE_PC_MODELS_H

#include <stdint.h>
struct cache_file_source;
struct custom_edition_load_report;

/* Validates every PC model part before compacting it to the Xbox part
 * layout. Vertex and strip buffers are placed between loaded tags and the
 * reserved BSP region in the same native tag arena. */
int n3ds_pc_models_convert(uint8_t *tags, uint32_t used, uint32_t capacity,
    const struct custom_edition_load_report *report,
    const struct cache_file_source *map, uint32_t *end_offset);

#endif
