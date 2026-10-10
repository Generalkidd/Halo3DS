#ifndef HALO_CACHE_READER_H
#define HALO_CACHE_READER_H

/* Clean-room, portable cache reader informed by the native v1.18 loader at
 * 0x0016d9ec..0x0016e000 and the available HaloRecomp cache definitions.
 * This is a replacement component, not the original 3DS source or ABI. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum halo_cache_kind { HALO_CACHE_XBOX_V5 = 5, HALO_CACHE_PC_V7 = 7 };

struct halo_cache {
    FILE *file;
    uint8_t *tags;
    size_t file_size;
    size_t tag_file_offset;
    size_t tag_size;
    size_t directory_offset;
    uint32_t virtual_base;
    uint32_t tag_count;
    enum halo_cache_kind kind;
};

struct halo_cache_tag {
    uint32_t group;
    uint32_t datum;
    const char *name;
    const uint8_t *metadata;
    size_t metadata_available;
};

struct halo_cache_bitmap {
    uint16_t width, height, format, flags;
    uint32_t pixel_offset, pixel_size;
    size_t first_mip_bytes;
    int external;
};

int halo_cache_open(struct halo_cache *cache, const char *path, char *error, size_t error_size);
int halo_cache_tag_at(const struct halo_cache *cache, uint32_t index,
                      struct halo_cache_tag *out, char *error, size_t error_size);
const uint8_t *halo_cache_resolve(const struct halo_cache *cache, uint32_t address, size_t bytes);
int halo_cache_bitmap_at(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                         uint32_t submap, struct halo_cache_bitmap *out,
                         char *error, size_t error_size);
int halo_cache_read_bitmap(struct halo_cache *cache, const struct halo_cache_bitmap *bitmap,
                           const char *resource_map, void *output, size_t output_size,
                           char *error, size_t error_size);
void halo_cache_close(struct halo_cache *cache);

#endif
