#include "halo_pc_model.h"

#include <limits.h>
#include <string.h>

#define GBXMODEL_GROUP 0x6d6f6432u
#define MODEL_GEOMETRIES_OFFSET 208u
#define GEOMETRY_BYTES 0x30u
#define GEOMETRY_PARTS_OFFSET 0x24u
#define PART_BYTES 0x84u
#define PC_VERTEX_BYTES 0x44u
#define MODEL_REGIONS_OFFSET 0xC4u
#define REGION_BYTES 0x4Cu
#define REGION_PERMUTATIONS_OFFSET 0x40u
#define PERMUTATION_BYTES 0x58u
#define PERMUTATION_GEOMETRIES_OFFSET 0x40u
#define MODEL_SHADERS_OFFSET 0xDCu
#define MODEL_SHADER_BYTES 0x20u

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(char *error, size_t size, const char *message) {
    if (error && size) {
        size_t n = strlen(message);
        if (n >= size) n = size - 1;
        memcpy(error, message, n);
        error[n] = '\0';
    }
    return 0;
}

static int model_header(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                        uint32_t *geometry_count, uint32_t *geometry_address,
                        uint32_t *model_file_offset, uint32_t *index_offset,
                        uint32_t *model_bytes, char *error, size_t error_size) {
    const uint8_t *index;
    if (!cache || !tag || cache->kind != HALO_CACHE_PC_V7 || tag->group != GBXMODEL_GROUP ||
        !tag->metadata || tag->metadata_available < MODEL_GEOMETRIES_OFFSET + 12u ||
        cache->tag_size < 40u)
        return fail(error, error_size, "Expected a PC retail gbxmodel tag");
    index = cache->tags;
    *model_file_offset = u32(index + 20);
    *index_offset = u32(index + 28);
    *model_bytes = u32(index + 32);
    if (*model_file_offset > cache->file_size || *model_bytes > cache->file_size - *model_file_offset ||
        *index_offset > *model_bytes)
        return fail(error, error_size, "Model data is outside map");
    *geometry_count = u32(tag->metadata + MODEL_GEOMETRIES_OFFSET);
    *geometry_address = u32(tag->metadata + MODEL_GEOMETRIES_OFFSET + 4u);
    if (*geometry_count == 0 || *geometry_count > 4096u ||
        !halo_cache_resolve(cache, *geometry_address, (size_t)*geometry_count * GEOMETRY_BYTES))
        return fail(error, error_size, "Model geometry block is outside tags");
    return 1;
}

static int geometry_parts(const struct halo_cache *cache, uint32_t address, uint32_t geometry_index,
                          uint32_t *part_count, const uint8_t **parts,
                          char *error, size_t error_size) {
    const uint8_t *geometry = halo_cache_resolve(cache, address + geometry_index * GEOMETRY_BYTES, GEOMETRY_BYTES);
    uint32_t part_address;
    if (!geometry) return fail(error, error_size, "Geometry is outside tags");
    *part_count = u32(geometry + GEOMETRY_PARTS_OFFSET);
    part_address = u32(geometry + GEOMETRY_PARTS_OFFSET + 4u);
    if (*part_count == 0 || *part_count > 4096u ||
        !(*parts = halo_cache_resolve(cache, part_address, (size_t)*part_count * PART_BYTES)))
        return fail(error, error_size, "Model parts are outside tags");
    return 1;
}

static int part_details(const struct halo_cache *cache, const uint8_t *part,
                        uint32_t model_file_offset, uint32_t index_offset, uint32_t model_bytes,
                        struct halo_pc_model_part *out, char *error, size_t error_size) {
    uint32_t vertices = u32(part + 88u);
    uint32_t vertex_offset = u32(part + 100u);
    uint32_t triangles = u32(part + 72u);
    uint32_t strip_offset = u32(part + 76u);
    if (vertices == 0 || vertices > 65535u || triangles > 1000000u ||
        vertex_offset > index_offset || (size_t)vertices * PC_VERTEX_BYTES > index_offset - vertex_offset ||
        strip_offset > model_bytes - index_offset ||
        (size_t)(triangles + 2u) * 2u > model_bytes - index_offset - strip_offset)
        return fail(error, error_size, "Model part vertices or strip are outside model data");
    out->vertex_file_offset = model_file_offset + vertex_offset;
    out->strip_file_offset = model_file_offset + index_offset + strip_offset;
    out->vertex_count = vertices;
    out->strip_triangle_count = triangles;
    out->shader_index = (uint16_t)(part[4] | (uint16_t)part[5] << 8);
    if (out->vertex_file_offset > cache->file_size || out->strip_file_offset > cache->file_size)
        return fail(error, error_size, "Model part file offset overflowed");
    return 1;
}

int halo_pc_model_part_at(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                          uint32_t geometry_index, uint32_t part_index,
                          struct halo_pc_model_part *out,
                          char *error, size_t error_size) {
    uint32_t count, address, model_offset, index_offset, model_bytes, parts_count;
    const uint8_t *parts;
    if (!out || !model_header(cache, tag, &count, &address, &model_offset, &index_offset,
                              &model_bytes, error, error_size)) return 0;
    if (geometry_index >= count ||
        !geometry_parts(cache, address, geometry_index, &parts_count, &parts, error, error_size)) return 0;
    if (part_index >= parts_count) return fail(error, error_size, "Part index is out of range");
    return part_details(cache, parts + (size_t)part_index * PART_BYTES, model_offset,
                        index_offset, model_bytes, out, error, error_size);
}

int halo_pc_model_part_count(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                             uint32_t geometry_index, uint32_t *count,
                             char *error, size_t error_size) {
    uint32_t geometry_count, address, model_offset, index_offset, model_bytes;
    const uint8_t *parts;
    if (!count || !model_header(cache, tag, &geometry_count, &address,
                                &model_offset, &index_offset, &model_bytes,
                                error, error_size) || geometry_index >= geometry_count)
        return fail(error, error_size, "Model geometry index is out of range");
    return geometry_parts(cache, address, geometry_index, count, &parts,
                          error, error_size);
}

int halo_pc_model_default_geometries(const struct halo_cache *cache,
                                      const struct halo_cache_tag *tag,
                                      uint32_t detail, uint16_t *indices,
                                      size_t capacity, uint32_t *count,
                                      char *error, size_t error_size) {
    uint32_t geometry_count, geometry_address, model_offset, index_offset, model_bytes;
    uint32_t regions, region_address, region;
    const uint8_t *region_block;
    if (!indices || !count || !capacity || detail >= 5u ||
        !model_header(cache, tag, &geometry_count, &geometry_address, &model_offset,
                      &index_offset, &model_bytes, error, error_size))
        return fail(error, error_size, "Invalid default model geometry request");
    *count = 0;
    regions = u32(tag->metadata + MODEL_REGIONS_OFFSET);
    region_address = u32(tag->metadata + MODEL_REGIONS_OFFSET + 4u);
    if (regions > 128u) return fail(error, error_size, "Model region count is out of range");
    region_block = regions ? halo_cache_resolve(cache, region_address,
                                               (size_t)regions * REGION_BYTES) : NULL;
    if (regions && !region_block) return fail(error, error_size, "Model regions are outside tags");
    for (region = 0; region < regions; ++region) {
        const uint8_t *entry = region_block + (size_t)region * REGION_BYTES;
        uint32_t permutations = u32(entry + REGION_PERMUTATIONS_OFFSET);
        uint32_t address = u32(entry + REGION_PERMUTATIONS_OFFSET + 4u);
        const uint8_t *permutation;
        uint16_t geometry;
        uint32_t i;
        if (!permutations) continue;
        if (permutations > 256u)
            return fail(error, error_size, "Model permutation count is out of range");
        permutation = halo_cache_resolve(cache, address,
                                         (size_t)permutations * PERMUTATION_BYTES);
        if (!permutation) return fail(error, error_size, "Model permutations are outside tags");
        geometry = (uint16_t)(permutation[PERMUTATION_GEOMETRIES_OFFSET + detail * 2u] |
                  (uint16_t)permutation[PERMUTATION_GEOMETRIES_OFFSET + detail * 2u + 1u] << 8);
        if (geometry == 0xffffu) continue;
        if (geometry >= geometry_count)
            return fail(error, error_size, "Model permutation geometry is out of range");
        for (i = 0; i < *count && indices[i] != geometry; ++i) {}
        if (i == *count) {
            if (*count >= capacity)
                return fail(error, error_size, "Too many default model geometries");
            indices[(*count)++] = geometry;
        }
    }
    if (!*count) { indices[0] = 0; *count = 1; }
    if (error && error_size) error[0] = '\0';
    return 1;
}

int halo_pc_model_shader_datum(const struct halo_cache *cache,
                               const struct halo_cache_tag *tag,
                               uint32_t shader_index, uint32_t *datum,
                               char *error, size_t error_size) {
    uint32_t geometry_count, geometry_address, model_offset, index_offset, model_bytes;
    uint32_t count, address;
    const uint8_t *shaders;
    if (!datum || !model_header(cache, tag, &geometry_count, &geometry_address,
                                &model_offset, &index_offset, &model_bytes,
                                error, error_size))
        return fail(error, error_size, "Invalid model shader request");
    if (tag->metadata_available < MODEL_SHADERS_OFFSET + 8u)
        return fail(error, error_size, "Model shader header is outside metadata");
    count = u32(tag->metadata + MODEL_SHADERS_OFFSET);
    address = u32(tag->metadata + MODEL_SHADERS_OFFSET + 4u);
    if (count > 1024u || shader_index >= count ||
        !(shaders = halo_cache_resolve(cache, address,
                                       (size_t)count * MODEL_SHADER_BYTES)))
        return fail(error, error_size, "Model shader block is outside tags");
    *datum = u32(shaders + (size_t)shader_index * MODEL_SHADER_BYTES + 12u);
    if (error && error_size) error[0] = '\0';
    return 1;
}

int halo_pc_model_inspect(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                          struct halo_pc_model_summary *summary,
                          char *error, size_t error_size) {
    uint32_t count, address, model_offset, index_offset, model_bytes, i, j, parts_count;
    const uint8_t *parts;
    struct halo_pc_model_part part;
    if (!summary || !model_header(cache, tag, &count, &address, &model_offset, &index_offset,
                                  &model_bytes, error, error_size)) return 0;
    memset(summary, 0, sizeof(*summary));
    summary->geometry_count = count;
    for (i = 0; i < count; i++) {
        if (!geometry_parts(cache, address, i, &parts_count, &parts, error, error_size)) return 0;
        for (j = 0; j < parts_count; j++) {
            if (!part_details(cache, parts + (size_t)j * PART_BYTES, model_offset,
                              index_offset, model_bytes, &part, error, error_size)) return 0;
            if (UINT32_MAX - summary->vertex_count < part.vertex_count ||
                UINT32_MAX - summary->strip_triangle_count < part.strip_triangle_count)
                return fail(error, error_size, "Model total count overflowed");
            summary->vertex_count += part.vertex_count;
            summary->strip_triangle_count += part.strip_triangle_count;
            summary->part_count++;
        }
    }
    if (error && error_size) error[0] = '\0';
    return 1;
}

int halo_pc_model_read_part(const struct halo_cache *cache, const struct halo_pc_model_part *part,
                            float *positions, size_t position_floats,
                            uint16_t *indices, size_t index_count,
                            char *error, size_t error_size) {
    uint8_t vertex[PC_VERTEX_BYTES];
    uint8_t pair[2];
    uint32_t i;
    if (!cache || !cache->file || !part || !positions || !indices ||
        position_floats < (size_t)part->vertex_count * 3u ||
        index_count < (size_t)part->strip_triangle_count + 2u)
        return fail(error, error_size, "Model part buffers are too small");
    if (part->vertex_file_offset > LONG_MAX || part->strip_file_offset > LONG_MAX ||
        fseek(cache->file, (long)part->vertex_file_offset, SEEK_SET))
        return fail(error, error_size, "Could not seek to model vertices");
    for (i = 0; i < part->vertex_count; i++) {
        if (fread(vertex, 1, sizeof(vertex), cache->file) != sizeof(vertex))
            return fail(error, error_size, "Could not read model vertices");
        memcpy(positions + (size_t)i * 3u, vertex, 3u * sizeof(float));
    }
    if (fseek(cache->file, (long)part->strip_file_offset, SEEK_SET))
        return fail(error, error_size, "Could not seek to model strip");
    for (i = 0; i < part->strip_triangle_count + 2u; i++) {
        if (fread(pair, 1, 2, cache->file) != 2)
            return fail(error, error_size, "Could not read model strip");
        indices[i] = (uint16_t)((uint16_t)pair[0] | (uint16_t)pair[1] << 8);
        if (indices[i] >= part->vertex_count)
            return fail(error, error_size, "Model strip index exceeds vertex count");
    }
    if (error && error_size) error[0] = '\0';
    return 1;
}

int halo_pc_model_read_part_uv(const struct halo_cache *cache, const struct halo_pc_model_part *part,
                               float *vertices, size_t vertex_floats,
                               uint16_t *indices, size_t index_count,
                               char *error, size_t error_size) {
    uint8_t vertex[PC_VERTEX_BYTES];
    uint8_t pair[2];
    uint32_t i;
    if (!cache || !cache->file || !part || !vertices || !indices ||
        vertex_floats < (size_t)part->vertex_count * 5u ||
        index_count < (size_t)part->strip_triangle_count + 2u)
        return fail(error, error_size, "Model part UV buffers are too small");
    if (part->vertex_file_offset > LONG_MAX || part->strip_file_offset > LONG_MAX ||
        fseek(cache->file, (long)part->vertex_file_offset, SEEK_SET))
        return fail(error, error_size, "Could not seek to model vertices");
    for (i = 0; i < part->vertex_count; i++) {
        if (fread(vertex, 1, sizeof(vertex), cache->file) != sizeof(vertex))
            return fail(error, error_size, "Could not read model vertices");
        memcpy(vertices + (size_t)i * 5u, vertex, 3u * sizeof(float));
        memcpy(vertices + (size_t)i * 5u + 3u, vertex + 48u, 2u * sizeof(float));
    }
    if (fseek(cache->file, (long)part->strip_file_offset, SEEK_SET))
        return fail(error, error_size, "Could not seek to model strip");
    for (i = 0; i < part->strip_triangle_count + 2u; i++) {
        if (fread(pair, 1, 2, cache->file) != 2)
            return fail(error, error_size, "Could not read model strip");
        indices[i] = (uint16_t)((uint16_t)pair[0] | (uint16_t)pair[1] << 8);
        if (indices[i] >= part->vertex_count)
            return fail(error, error_size, "Model strip index exceeds vertex count");
    }
    if (error && error_size) error[0] = '\0';
    return 1;
}
