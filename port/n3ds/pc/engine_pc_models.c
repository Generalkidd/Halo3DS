/* New3DS integration additions by Kevin Boggs. */
/* PC gbxmodel parts to the original model layout, with buffers resident in
 * the bounded tag arena. Only used by the opt-in PC runtime diagnostic. */
#include "cseries.h"
#include "tag_groups.h"
#include "model_definitions.h"
#include "rasterizer_geometry.h"
#include "rasterizer_model_types.h"
#include "cache_file_formats.h"
#include "engine_pc_models.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void n3ds_log(const char *message);

struct pc_model_geometry {
    byte reserved[0x24];
    struct tag_block parts;
};

struct xbox_model_part {
    unsigned long flags;
    short shader_index;
    char previous_part_index;
    char next_part_index;
    short centroid_primary_node_index;
    short centroid_secondary_node_index;
    real centroid_primary_node_weight;
    real centroid_secondary_node_weight;
    real_point3d centroid;
    struct tag_block uncompressed_vertices;
    struct tag_block compressed_vertices;
    struct tag_block triangles;
    struct triangle_buffer triangle_buffer;
    struct vertex_buffer vertex_buffer;
};

struct pc_model_part {
    unsigned long flags;
    short shader_index;
    char previous_part_index;
    char next_part_index;
    short centroid_primary_node_index;
    short centroid_secondary_node_index;
    real centroid_primary_node_weight;
    real centroid_secondary_node_weight;
    real_point3d centroid;
    struct tag_block uncompressed_vertices;
    struct tag_block compressed_vertices;
    struct tag_block triangles;
    short strip_type;
    word pad1;
    long strip_triangle_count;
    unsigned long strip_offset;
    unsigned long unused1;
    short vertex_type;
    word pad2;
    long vertex_count;
    unsigned long unused2[2];
    unsigned long vertex_offset;
    byte pad3[3];
    byte local_node_count;
    byte local_node_indices[MAXIMUM_NODES_PER_MODEL_GEOMETRY_PART];
    word pad4;
};

_Static_assert(sizeof(struct pc_model_geometry) == 0x30, "PC geometry layout");
_Static_assert(sizeof(struct pc_model_part) == 0x84, "PC model part layout");
_Static_assert(sizeof(struct xbox_model_part) == 0x68, "Xbox model part layout");
_Static_assert(sizeof(struct model_vertex_uncompressed) == 0x44, "model vertex layout");

struct totals {uint32_t models, parts, vertices, indices;};

/* Kevin Boggs: finite_vector - New3DS integration. */
static int finite_vector(const real_vector3d *v)
{
    return isfinite(v->i) && isfinite(v->j) && isfinite(v->k) &&
        fabsf(v->i) <= 1.005f && fabsf(v->j) <= 1.005f &&
        fabsf(v->k) <= 1.005f;
}

/* Kevin Boggs: walk - New3DS integration. */
static int walk(uint8_t *tags, uint32_t used, uint32_t limit,
    const struct custom_edition_load_report *report, const uint8_t *model_data,
    uint8_t *geometry_storage, uint32_t *cursor, struct totals *totals, int mutate)
{
#define MODEL_FAIL(reason) do { n3ds_log("EXPERIMENTAL PC: model reject " reason); return 0; } while (0)
    int32_t tag_index = -1;
    struct model *model;
    while ((model = custom_edition_cache_tag_next(tags, used, 'mod2',
        sizeof(*model), &tag_index)) != NULL) {
        int local = (model->flags & 2U) != 0;
        if (model->nodes.count < 1 || model->nodes.count > MAXIMUM_NODES_PER_MODEL ||
            model->geometries.count < 0 || model->geometries.count > MAXIMUM_GEOMETRIES_PER_MODEL ||
            model->shaders.count < 0 || model->shaders.count > MAXIMUM_SHADERS_PER_MODEL)
            MODEL_FAIL("header counts");
        ++totals->models;
        for (long g = 0; g < model->geometries.count; ++g) {
            struct pc_model_geometry *geometry = custom_edition_cache_block_element(
                tags, used, &model->geometries, g, sizeof(*geometry));
            if (!geometry || geometry->parts.count < 0 ||
                geometry->parts.count > MAXIMUM_PARTS_PER_MODEL_GEOMETRY) MODEL_FAIL("geometry parts");
            for (long p = 0; p < geometry->parts.count; ++p) {
                struct pc_model_part *source = custom_edition_cache_block_element(
                    tags, used, &geometry->parts, p, sizeof(*source));
                struct pc_model_part part;
                uint32_t vertex_bytes, strip_bytes, vertex_at, strip_at;
                if (!source) {
                    n3ds_log("EXPERIMENTAL PC: model part outside tag arena");
                    return 0;
                }
                part = *source;
                if (part.shader_index < 0 || part.shader_index >= model->shaders.count ||
                    part.vertex_type != 4 || part.strip_type != 1 ||
                    part.vertex_count < 1 || part.vertex_count > 65535 ||
                    part.strip_triangle_count < 1 || part.strip_triangle_count > 65533 ||
                    (local && (!part.local_node_count ||
                        part.local_node_count > MAXIMUM_NODES_PER_MODEL_GEOMETRY_PART))) {
                    char detail[180];
                    snprintf(detail, sizeof(detail),
                        "EXPERIMENTAL PC: invalid model tag=%ld geometry=%ld part=%ld shader=%d/%ld vertex=%d count=%ld strip=%d count=%ld local=%d nodes=%u",
                        (long)tag_index, g, p, part.shader_index, model->shaders.count,
                        part.vertex_type, part.vertex_count, part.strip_type,
                        part.strip_triangle_count, local, part.local_node_count);
                    n3ds_log(detail);
                    return 0;
                }
                vertex_bytes = (uint32_t)part.vertex_count * sizeof(struct model_vertex_uncompressed);
                strip_bytes = ((uint32_t)part.strip_triangle_count + 2U) * sizeof(word);
                if (part.vertex_offset > report->model_index_data_offset ||
                    vertex_bytes > report->model_index_data_offset - part.vertex_offset ||
                    part.strip_offset > report->model_data_bytes - report->model_index_data_offset ||
                    strip_bytes > report->model_data_bytes - report->model_index_data_offset - part.strip_offset)
                    MODEL_FAIL("model data offset");
                const struct model_vertex_uncompressed *vertices =
                    (const void *)(model_data + part.vertex_offset);
                const word *indices = (const void *)(model_data +
                    report->model_index_data_offset + part.strip_offset);
                for (long i = 0; i < part.vertex_count; ++i) {
                    const struct model_vertex_uncompressed *v = vertices + i;
                    long bound = local ? part.local_node_count : model->nodes.count;
                    if (v->nodes[0] < 0 || v->nodes[0] >= bound ||
                        v->nodes[1] < 0 || v->nodes[1] >= bound ||
                        !finite_vector(&v->normal) ||
                        !finite_vector(&v->binormal) ||
                        !finite_vector(&v->tangent)) MODEL_FAIL("vertex nodes or vectors");
                }
                for (uint32_t i = 0; i < (uint32_t)part.strip_triangle_count + 2U; ++i)
                    if (indices[i] >= part.vertex_count) MODEL_FAIL("strip index");
                if (local) for (uint32_t i = 0; i < part.local_node_count; ++i)
                    if (part.local_node_indices[i] >= model->nodes.count) MODEL_FAIL("local node index");
                if (*cursor > UINT32_MAX - 3U) MODEL_FAIL("vertex cursor overflow");
                vertex_at = (*cursor + 3U) & ~3U;
                if (vertex_at > limit || vertex_bytes > limit - vertex_at) {
                    char detail[192];
                    snprintf(detail, sizeof(detail),
                        "EXPERIMENTAL PC: model storage limit=%lu cursor=%lu vertex=%lu tag=%ld part=%ld",
                        (unsigned long)limit,
                        (unsigned long)*cursor, (unsigned long)vertex_bytes,
                        (long)tag_index, p);
                    n3ds_log(detail);
                    MODEL_FAIL("vertex arena capacity");
                }
                if (vertex_at > UINT32_MAX - vertex_bytes - 3U)
                    MODEL_FAIL("index cursor overflow");
                strip_at = (vertex_at + vertex_bytes + 3U) & ~3U;
                if (strip_at > limit || strip_bytes > limit - strip_at) MODEL_FAIL("index arena capacity");
                *cursor = strip_at + strip_bytes;
                ++totals->parts;
                totals->vertices += (uint32_t)part.vertex_count;
                totals->indices += (uint32_t)part.strip_triangle_count + 2U;
                if (!mutate) continue;
                struct model_vertex_uncompressed *destination =
                    (void *)(geometry_storage + vertex_at);
                word *strip = (void *)(geometry_storage + strip_at);
                memcpy(destination, vertices, vertex_bytes);
                memcpy(strip, indices, strip_bytes);
                if (local) for (long i = 0; i < part.vertex_count; ++i) {
                    destination[i].nodes[0] = part.local_node_indices[destination[i].nodes[0]];
                    destination[i].nodes[1] = part.local_node_indices[destination[i].nodes[1]];
                }
                struct xbox_model_part *out =
                    (void *)((uint8_t *)source - p * sizeof(part) +
                        p * sizeof(struct xbox_model_part));
                memset(out, 0, sizeof(*out));
                out->flags = part.flags & ~2U;
                out->shader_index = part.shader_index;
                out->previous_part_index = part.previous_part_index;
                out->next_part_index = part.next_part_index;
                out->centroid_primary_node_index = part.centroid_primary_node_index;
                out->centroid_secondary_node_index = part.centroid_secondary_node_index;
                out->centroid_primary_node_weight = part.centroid_primary_node_weight;
                out->centroid_secondary_node_weight = part.centroid_secondary_node_weight;
                out->centroid = part.centroid;
                out->uncompressed_vertices = part.uncompressed_vertices;
                out->compressed_vertices = part.compressed_vertices;
                out->triangles = part.triangles;
                out->triangle_buffer.type = 1;
                out->triangle_buffer.count = part.strip_triangle_count;
                out->triangle_buffer.base_address = strip;
                out->vertex_buffer.type = 4;
                out->vertex_buffer.count = part.vertex_count;
                out->vertex_buffer.base_address = destination;
            }
        }
        if (mutate) model->flags &= ~2U;
    }
#undef MODEL_FAIL
    return 1;
}

/* Kevin Boggs: n3ds_pc_models_convert - New3DS integration. */
int n3ds_pc_models_convert(uint8_t *tags, uint32_t used, uint32_t capacity,
    const struct custom_edition_load_report *report,
    const struct cache_file_source *map, void **geometry_owner,
    uint32_t *geometry_bytes)
{
    uint32_t cursor, check_cursor;
    struct totals checked = {0}, converted = {0};
    uint8_t *model_data, *geometry_storage = NULL;
    char message[180];
    if (!tags || !report || !map || !geometry_owner || !geometry_bytes || used > capacity ||
        report->model_index_data_offset > report->model_data_bytes ||
        report->model_data_bytes > map->size ||
        report->model_data_offset > map->size - report->model_data_bytes)
        return 0;
    *geometry_owner = NULL;
    *geometry_bytes = 0;
    model_data = malloc(report->model_data_bytes ? report->model_data_bytes : 1);
    if (!model_data) return 0;
    if (!map->read(map->context, report->model_data_offset,
        report->model_data_bytes, model_data)) {free(model_data);return 0;}
    cursor = 0;
    check_cursor = 0;
    if (!walk(tags, used, UINT32_MAX, report, model_data, NULL,
        &check_cursor, &checked, 0)) {
        free(model_data);
        n3ds_log("EXPERIMENTAL PC: model validation failed");
        return 0;
    }
    geometry_storage = malloc(check_cursor ? check_cursor : 1);
    if (!geometry_storage) {
        free(model_data);
        n3ds_log("EXPERIMENTAL PC: model geometry allocation failed");
        return 0;
    }
    if (!walk(tags, used, check_cursor, report, model_data, geometry_storage,
            &cursor, &converted, 1) ||
        memcmp(&checked, &converted, sizeof(checked)) || cursor != check_cursor) {
        free(geometry_storage);
        free(model_data);
        n3ds_log("EXPERIMENTAL PC: model conversion failed");
        return 0;
    }
    free(model_data);
    int32_t regrouped = custom_edition_cache_tags_regroup(tags, used, 'mod2', 'mode');
    if (regrouped != (int32_t)checked.models) {free(geometry_storage); return 0;}
    *geometry_owner = geometry_storage;
    *geometry_bytes = cursor;
    snprintf(message, sizeof(message),
        "EXPERIMENTAL PC: models=%lu parts=%lu vertices=%lu strips=%lu geometry_bytes=%lu",
        (unsigned long)checked.models, (unsigned long)checked.parts,
        (unsigned long)checked.vertices, (unsigned long)checked.indices,
        (unsigned long)cursor);
    n3ds_log(message);
    return 1;
}
