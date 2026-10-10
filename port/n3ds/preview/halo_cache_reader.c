#include "halo_cache_reader.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 2048u
#define MAX_TAG_BYTES (64u * 1024u * 1024u)
#define MAX_TAGS 65535u

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static int fail(char *error, size_t size, const char *message) {
    if (error != NULL && size != 0) {
        size_t length = strlen(message);
        if (length >= size) length = size - 1;
        memcpy(error, message, length);
        error[length] = '\0';
    }
    return 0;
}

void halo_cache_close(struct halo_cache *cache) {
    if (cache == NULL) return;
    if (cache->file != NULL) fclose(cache->file);
    free(cache->tags);
    memset(cache, 0, sizeof(*cache));
}

const uint8_t *halo_cache_resolve(const struct halo_cache *cache, uint32_t address, size_t bytes) {
    if (cache == NULL || cache->tags == NULL || address < cache->virtual_base) return NULL;
    size_t offset = (size_t)(address - cache->virtual_base);
    if (bytes > cache->tag_size || offset > cache->tag_size - bytes) return NULL;
    return cache->tags + offset;
}

int halo_cache_open(struct halo_cache *cache, const char *path, char *error, size_t error_size) {
    uint8_t header[HEADER_BYTES];
    uint32_t tag_offset, tag_size, directory_address, count, signature_offset;
    long end;
    if (cache == NULL || path == NULL) return fail(error, error_size, "Invalid argument");
    memset(cache, 0, sizeof(*cache));
    cache->file = fopen(path, "rb");
    if (cache->file == NULL) return fail(error, error_size, "Could not open map");
    if (fseek(cache->file, 0, SEEK_END) != 0 || (end = ftell(cache->file)) < 0 ||
        fseek(cache->file, 0, SEEK_SET) != 0) goto invalid_file;
    cache->file_size = (size_t)end;
    if (cache->file_size < HEADER_BYTES || fread(header, 1, HEADER_BYTES, cache->file) != HEADER_BYTES)
        goto invalid_file;
    if (memcmp(header, "daeh", 4) != 0 || memcmp(header + 2044, "toof", 4) != 0)
        goto invalid_file;
    cache->kind = (enum halo_cache_kind)read_u32(header + 4);
    if (cache->kind != HALO_CACHE_XBOX_V5 && cache->kind != HALO_CACHE_PC_V7)
        goto invalid_file;
    if ((size_t)read_u32(header + 8) != cache->file_size)
        goto invalid_file; /* also rejects a compressed Xbox cache */
    tag_offset = read_u32(header + 16);
    tag_size = read_u32(header + 20);
    if (tag_offset < HEADER_BYTES || tag_size < 40 || tag_size > MAX_TAG_BYTES ||
        tag_size > cache->file_size || (size_t)tag_offset > cache->file_size - tag_size)
        goto invalid_file;
    cache->tag_file_offset = tag_offset;
    cache->tag_size = tag_size;
    cache->virtual_base = cache->kind == HALO_CACHE_XBOX_V5 ? 0x803a6000u : 0x40440000u;
    cache->tags = (uint8_t *)malloc(tag_size);
    if (cache->tags == NULL) goto no_memory;
    if (fseek(cache->file, (long)tag_offset, SEEK_SET) != 0 ||
        fread(cache->tags, 1, tag_size, cache->file) != tag_size)
        goto invalid_file;
    signature_offset = cache->kind == HALO_CACHE_XBOX_V5 ? 32u : 36u;
    if (memcmp(cache->tags + signature_offset, "sgat", 4) != 0) goto invalid_file;
    directory_address = read_u32(cache->tags);
    count = read_u32(cache->tags + 12);
    if (count == 0 || count > MAX_TAGS || directory_address < cache->virtual_base)
        goto invalid_file;
    cache->directory_offset = (size_t)(directory_address - cache->virtual_base);
    if (cache->directory_offset > cache->tag_size ||
        (size_t)count * 32u > cache->tag_size - cache->directory_offset)
        goto invalid_file;
    cache->tag_count = count;
    if (error != NULL && error_size != 0) error[0] = '\0';
    return 1;
invalid_file:
    halo_cache_close(cache);
    return fail(error, error_size, "Invalid, compressed or truncated cache");
no_memory:
    halo_cache_close(cache);
    return fail(error, error_size, "Not enough memory for tag data");
}

int halo_cache_tag_at(const struct halo_cache *cache, uint32_t index,
                      struct halo_cache_tag *out, char *error, size_t error_size) {
    const uint8_t *entry, *name, *meta;
    size_t available, name_length;
    if (cache == NULL || out == NULL || cache->tags == NULL || index >= cache->tag_count)
        return fail(error, error_size, "Tag index is out of range");
    entry = cache->tags + cache->directory_offset + (size_t)index * 32u;
    name = halo_cache_resolve(cache, read_u32(entry + 16), 1);
    if (name == NULL) return fail(error, error_size, "Tag name is out of range");
    available = cache->tag_size - (size_t)(name - cache->tags);
    name_length = 0;
    while (name_length < available && name_length < 256 && name[name_length] != 0) name_length++;
    if (name_length == available || name_length == 256)
        return fail(error, error_size, "Unterminated tag name");
    meta = halo_cache_resolve(cache, read_u32(entry + 20), 1);
    out->group = read_u32(entry);
    out->datum = read_u32(entry + 12);
    out->name = (const char *)name;
    out->metadata = meta;
    out->metadata_available = meta == NULL ? 0 : cache->tag_size - (size_t)(meta - cache->tags);
    if (error != NULL && error_size != 0) error[0] = '\0';
    return 1;
}

int halo_cache_bitmap_at(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                         uint32_t submap, struct halo_cache_bitmap *out,
                         char *error, size_t error_size) {
    const uint8_t *block;
    uint32_t count, address;
    size_t block_size;
    if (cache == NULL || tag == NULL || out == NULL || tag->group != 0x6269746du ||
        tag->metadata == NULL || tag->metadata_available < 108)
        return fail(error, error_size, "Selected tag is not a bitmap");
    count = read_u32(tag->metadata + 96);
    address = read_u32(tag->metadata + 100);
    if (count == 0 || count > 4096 || submap >= count)
        return fail(error, error_size, "Bitmap submap is out of range");
    block = halo_cache_resolve(cache, address, (size_t)count * 48u);
    if (block == NULL) return fail(error, error_size, "Bitmap blocks are out of range");
    block += (size_t)submap * 48u;
    if (read_u32(block) != 0x6269746du || read_u16(block + 8) != 1 || read_u16(block + 10) != 0)
        return fail(error, error_size, "Unsupported bitmap type");
    out->width = read_u16(block + 4);
    out->height = read_u16(block + 6);
    out->format = read_u16(block + 12);
    out->flags = read_u16(block + 14);
    out->pixel_offset = read_u32(block + 24);
    out->pixel_size = read_u32(block + 28);
    out->external = (out->flags & 256u) != 0;
    if (out->width < 4 || out->height < 4 || out->width > 2048 || out->height > 2048 ||
        (out->format != 14 && out->format != 15 && out->format != 16))
        return fail(error, error_size, "Unsupported bitmap dimensions or format");
    block_size = out->format == 14 ? 8u : 16u;
    out->first_mip_bytes = (size_t)((out->width + 3u) / 4u) *
                           (size_t)((out->height + 3u) / 4u) * block_size;
    if (out->pixel_size < out->first_mip_bytes || out->pixel_size > 32u * 1024u * 1024u)
        return fail(error, error_size, "Bitmap pixel extent is invalid");
    if (error != NULL && error_size != 0) error[0] = '\0';
    return 1;
}

int halo_cache_read_bitmap(struct halo_cache *cache, const struct halo_cache_bitmap *bitmap,
                           const char *resource_map, void *output, size_t output_size,
                           char *error, size_t error_size) {
    FILE *source;
    long length;
    int close_source;
    if (cache == NULL || bitmap == NULL || output == NULL || output_size < bitmap->first_mip_bytes)
        return fail(error, error_size, "Bitmap output buffer is too small");
    if (bitmap->external) {
        if (resource_map == NULL) return fail(error, error_size, "External bitmaps.map is required");
        source = fopen(resource_map, "rb");
        if (source == NULL) return fail(error, error_size, "Could not open bitmaps.map");
        close_source = 1;
    } else {
        source = cache->file;
        if (source == NULL) return fail(error, error_size, "Map is closed");
        close_source = 0;
    }
    if (fseek(source, 0, SEEK_END) != 0 || (length = ftell(source)) < 0 ||
        (size_t)bitmap->pixel_offset > (size_t)length ||
        bitmap->first_mip_bytes > (size_t)length - bitmap->pixel_offset ||
        fseek(source, (long)bitmap->pixel_offset, SEEK_SET) != 0 ||
        fread(output, 1, bitmap->first_mip_bytes, source) != bitmap->first_mip_bytes) {
        if (close_source) fclose(source);
        return fail(error, error_size, "Bitmap pixels are outside resource file");
    }
    if (close_source) fclose(source);
    if (error != NULL && error_size != 0) error[0] = '\0';
    return 1;
}
