#ifndef HALO_PC_MODEL_H
#define HALO_PC_MODEL_H

#include "halo_cache_reader.h"

#include <stddef.h>
#include <stdint.h>

/* A validated Xbox model or PC gbxmodel part; offsets belong to its map. */
struct halo_pc_model_part {
    uint32_t vertex_file_offset;
    uint32_t strip_file_offset;
    uint32_t vertex_stride;
    uint32_t vertex_count;
    uint32_t strip_triangle_count;
    uint16_t shader_index;
};

struct halo_pc_model_summary {
    uint32_t geometry_count;
    uint32_t part_count;
    uint32_t vertex_count;
    uint32_t strip_triangle_count;
};

int halo_pc_model_inspect(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                          struct halo_pc_model_summary *summary,
                          char *error, size_t error_size);

int halo_pc_model_part_at(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                          uint32_t geometry_index, uint32_t part_index,
                          struct halo_pc_model_part *out,
                          char *error, size_t error_size);

int halo_pc_model_part_count(const struct halo_cache *cache, const struct halo_cache_tag *tag,
                             uint32_t geometry_index, uint32_t *count,
                             char *error, size_t error_size);

/* One default permutation per region at the selected detail level. The
 * returned geometry indices are unique and validated against the model. */
int halo_pc_model_default_geometries(const struct halo_cache *cache,
                                      const struct halo_cache_tag *tag,
                                      uint32_t detail, uint16_t *indices,
                                      size_t capacity, uint32_t *count,
                                      char *error, size_t error_size);

/* Resolve a part's shader slot to the owner map's shader tag datum. */
int halo_pc_model_shader_datum(const struct halo_cache *cache,
                               const struct halo_cache_tag *tag,
                               uint32_t shader_index, uint32_t *datum,
                               char *error, size_t error_size);

/* Read positions and strip indices for a part. The caller owns the buffers.
   Positions are xyz float triplets; indices are the source triangle strip. */
int halo_pc_model_read_part(const struct halo_cache *cache, const struct halo_pc_model_part *part,
                            float *positions, size_t position_floats,
                            uint16_t *indices, size_t index_count,
                            char *error, size_t error_size);

/* Read interleaved xyz, uv floats from the PC 0x44-byte vertex format. */
int halo_pc_model_read_part_uv(const struct halo_cache *cache, const struct halo_pc_model_part *part,
                               float *vertices, size_t vertex_floats,
                               uint16_t *indices, size_t index_count,
                               char *error, size_t error_size);

/* Read xyz, uv, and authored xyz normal from each 0x44-byte PC vertex. */
int halo_pc_model_read_part_uv_normal(const struct halo_cache *cache,
                                      const struct halo_pc_model_part *part,
                                      float *vertices, size_t vertex_floats,
                                      uint16_t *indices, size_t index_count,
                                      char *error, size_t error_size);

#endif
