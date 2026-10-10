#ifndef HALO_PC_BSP_H
#define HALO_PC_BSP_H

#include "halo_cache_reader.h"
#include <stddef.h>
#include <stdint.h>

/* PC retail structure BSP geometry. The caller retains the source map while
   opening; close releases the copied BSP data. No game-engine pointers. */
struct halo_pc_bsp {
    uint8_t *bytes;
    uint32_t size;
    uint32_t address;
    uint32_t surface_count;
    uint32_t surfaces_offset;
    uint32_t lightmap_count;
    uint32_t lightmaps_offset;
    uint32_t material_count;
    uint32_t vertex_count;
    uint32_t nav_surface_count;
    uint32_t nav_surfaces_offset;
    uint32_t nav_edges_count;
    uint32_t nav_edges_offset;
    uint32_t nav_vertices_count;
    uint32_t nav_vertices_offset;
    uint32_t nav_flags_offset;
};

struct halo_pc_bsp_material {
    uint32_t vertices_offset;
    uint32_t vertex_count;
    uint32_t first_surface;
    uint32_t surface_count;
    uint32_t shader_group;
    uint32_t shader_datum;
};

int halo_pc_bsp_open(struct halo_pc_bsp *bsp, const struct halo_cache *cache,
                     uint32_t bsp_index, char *error, size_t error_size);
int halo_pc_bsp_material_at(const struct halo_pc_bsp *bsp,
                            uint32_t lightmap_index, uint32_t material_index,
                            struct halo_pc_bsp_material *out,
                            char *error, size_t error_size);
int halo_pc_bsp_material_count(const struct halo_pc_bsp *bsp,
                               uint32_t lightmap_index, uint32_t *count,
                               char *error, size_t error_size);
int halo_pc_bsp_read_material(const struct halo_pc_bsp *bsp,
                              const struct halo_pc_bsp_material *material,
                              float *positions, size_t position_floats,
                              uint16_t *indices, size_t index_count,
                              char *error, size_t error_size);
int halo_pc_bsp_read_material_uv(const struct halo_pc_bsp *bsp,
                                 const struct halo_pc_bsp_material *material,
                                 float *vertices, size_t vertex_floats,
                                 uint16_t *indices, size_t index_count,
                                 char *error, size_t error_size);
void halo_pc_bsp_close(struct halo_pc_bsp *bsp);
int halo_pc_bsp_count(const struct halo_cache *cache, uint32_t *count,
                      char *error, size_t error_size);
/* Returns a walkable collision polygon (0x40 in the BSP pathfinding flags).
   Output contains up to eight xyz points; zero means invalid or nonwalkable. */
int halo_pc_bsp_nav_polygon(const struct halo_pc_bsp *bsp,uint32_t surface_index,
                            float points[8][3]);
/* Marks the largest edge-connected group of authored walkable surfaces.
   The output has nav_surface_count bytes. Zero means no usable component. */
uint32_t halo_pc_bsp_nav_main_component(const struct halo_pc_bsp *bsp,
                                        uint8_t *included,size_t capacity);

#endif
