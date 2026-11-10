#include "halo_pc_bsp.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCENARIO_GROUP 0x73636e72u
#define BSP_SIGNATURE 0x73627370u
#define SCENARIO_BSPS 0x5a4u
#define BSP_BYTES 0x288u
#define LIGHTMAP_STRIDE 0x20u
#define MATERIAL_STRIDE 0x100u

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(char *error, size_t size, const char *message) {
    if (error && size) {
        size_t n = strlen(message);
        if (n >= size) n = size - 1;
        memcpy(error, message, n);
        error[n] = 0;
    }
    return 0;
}

static int block(const uint8_t *bytes, uint32_t size, uint32_t base,
                 uint32_t field, uint32_t stride, uint32_t maximum,
                 uint32_t *count, uint32_t *offset) {
    uint32_t address;
    if (field > size || size - field < 8u) return 0;
    *count = u32(bytes + field);
    address = u32(bytes + field + 4u);
    if (*count > maximum) return 0;
    if (!*count) { *offset = 0; return 1; }
    if (address < base || address - base > size ||
        *count > (size - (address - base)) / stride) return 0;
    *offset = address - base;
    return 1;
}

void halo_pc_bsp_close(struct halo_pc_bsp *bsp) {
    if (!bsp) return;
    free(bsp->bytes);
    memset(bsp, 0, sizeof(*bsp));
}

int halo_pc_bsp_material_count(const struct halo_pc_bsp *bsp,
                               uint32_t lightmap_index, uint32_t *count,
                               char *error, size_t error_size) {
    uint32_t ignored, lightmap;
    if (!bsp || !bsp->bytes || !count || lightmap_index >= bsp->lightmap_count)
        return fail(error, error_size, "BSP lightmap index out of range");
    lightmap = bsp->lightmaps_offset + lightmap_index * LIGHTMAP_STRIDE;
    if (!block(bsp->bytes, bsp->size, bsp->address, lightmap + 0x14u,
               MATERIAL_STRIDE, 65535u, count, &ignored))
        return fail(error, error_size, "BSP material block invalid");
    if (error && error_size) error[0] = 0;
    return 1;
}

int halo_pc_bsp_material_at(const struct halo_pc_bsp *bsp,
                            uint32_t lightmap_index, uint32_t material_index,
                            struct halo_pc_bsp_material *out,
                            char *error, size_t error_size) {
    uint32_t count, offset, lightmap, material, address, bytes, lightmap_vertices;
    if (!bsp || !bsp->bytes || !out || lightmap_index >= bsp->lightmap_count)
        return fail(error, error_size, "BSP lightmap index out of range");
    lightmap = bsp->lightmaps_offset + lightmap_index * LIGHTMAP_STRIDE;
    if (!block(bsp->bytes, bsp->size, bsp->address, lightmap + 0x14u,
               MATERIAL_STRIDE, 65535u, &count, &offset) || material_index >= count)
        return fail(error, error_size, "BSP material block invalid");
    material = offset + material_index * MATERIAL_STRIDE;
    out->shader_group = u32(bsp->bytes + material);
    out->shader_datum = u32(bsp->bytes + material + 12u);
    out->first_surface = u32(bsp->bytes + material + 0x14u);
    out->surface_count = u32(bsp->bytes + material + 0x18u);
    out->vertex_count = u32(bsp->bytes + material + 0xb4u);
    int xbox=bsp->kind==HALO_CACHE_XBOX_V5;
    bytes = u32(bsp->bytes + material + (xbox?0xecu:0xd8u));
    address = u32(bsp->bytes + material + (xbox?0xf8u:0xe4u));
    out->vertex_stride=xbox?32u:56u;out->uv_offset=xbox?24u:48u;
    lightmap_vertices = u32(bsp->bytes + material + 0xc8u);
    if (out->first_surface > bsp->surface_count ||
        out->surface_count > bsp->surface_count - out->first_surface ||
        out->vertex_count > 65535u ||
        lightmap_vertices != (u16(bsp->bytes + lightmap) == 0xffffu ? 0u : out->vertex_count) ||
        (size_t)out->vertex_count * out->vertex_stride + (size_t)lightmap_vertices * (xbox?8u:20u) != bytes ||
        (bytes && (address < bsp->address || address - bsp->address > bsp->size ||
                   bytes > bsp->size - (address - bsp->address))))
        return fail(error, error_size, "BSP material geometry invalid");
    out->vertices_offset = bytes ? address - bsp->address : 0u;
    if (error && error_size) error[0] = 0;
    return 1;
}

static int scenario_refs(const struct halo_cache *cache,
                         uint32_t *count, uint32_t *offset,
                         char *error, size_t error_size) {
    struct halo_cache_tag scenario;
    uint32_t i;
    if (!cache || (cache->kind != HALO_CACHE_PC_V7 && cache->kind != HALO_CACHE_XBOX_V5) || !count || !offset)
        return fail(error, error_size, "Expected retail scenario");
    for (i = 0; i < cache->tag_count; i++) {
        if (halo_cache_tag_at(cache, i, &scenario, error, error_size) &&
            scenario.group == SCENARIO_GROUP) break;
    }
    if (i == cache->tag_count || !scenario.metadata ||
        scenario.metadata_available < SCENARIO_BSPS + 8u)
        return fail(error, error_size, "Scenario tag missing");
    *count = u32(scenario.metadata + SCENARIO_BSPS);
    *offset = u32(scenario.metadata + SCENARIO_BSPS + 4u);
    if (*count > 32u ||
        (*count && !halo_cache_resolve(cache, *offset, (size_t)*count * 0x20u)))
        return fail(error, error_size, "Scenario BSP references invalid");
    if (error && error_size) error[0] = 0;
    return 1;
}

int halo_pc_bsp_count(const struct halo_cache *cache, uint32_t *count,
                      char *error, size_t error_size) {
    uint32_t ignored;
    return scenario_refs(cache, count, &ignored, error, error_size);
}

int halo_pc_bsp_open(struct halo_pc_bsp *bsp, const struct halo_cache *cache,
                     uint32_t bsp_index, char *error, size_t error_size) {
    uint32_t count, offset, ref, file_offset, size, address, structure;
    uint32_t lm, mat_count, materials, mat;
    struct halo_pc_bsp_material material;
    if (!bsp || !cache || (cache->kind != HALO_CACHE_PC_V7 && cache->kind != HALO_CACHE_XBOX_V5))
        return fail(error, error_size, "Expected a retail cache");
    memset(bsp, 0, sizeof(*bsp));
    if (!scenario_refs(cache, &count, &offset, error, error_size)) return 0;
    if (!count || bsp_index >= count)
        return fail(error, error_size, "Scenario BSP references invalid");
    ref = offset + bsp_index * 0x20u;
    const uint8_t *reference = halo_cache_resolve(cache, ref, 0x20u);
    file_offset = u32(reference);
    size = u32(reference + 4u);
    address = u32(reference + 8u);
    if (size < 0x18u + BSP_BYTES || size > 32u * 1024u * 1024u ||
        (size_t)file_offset > cache->file_size || size > cache->file_size - file_offset ||
        file_offset > LONG_MAX)
        return fail(error, error_size, "BSP file extent invalid");
    bsp->bytes = malloc(size);
    if (!bsp->bytes) return fail(error, error_size, "BSP allocation failed");
    bsp->size = size;
    bsp->address = address;
    bsp->kind = cache->kind;
    if (fseek(cache->file, (long)file_offset, SEEK_SET) ||
        halo_cache_read_chunks(cache,cache->file,bsp->bytes,size) != size ||
        u32(bsp->bytes + 0x14u) != BSP_SIGNATURE ||
        u32(bsp->bytes) < address ||
        (structure = u32(bsp->bytes) - address) > size || size - structure < BSP_BYTES ||
        !block(bsp->bytes, size, address, structure + 0xf8u, 6u, 1000000u,
               &bsp->surface_count, &bsp->surfaces_offset) ||
        !block(bsp->bytes, size, address, structure + 0x104u,
               LIGHTMAP_STRIDE, 65535u, &bsp->lightmap_count, &bsp->lightmaps_offset)) {
        halo_pc_bsp_close(bsp);
        return fail(error, error_size, "BSP header or geometry blocks invalid");
    }
    for (lm = 0; lm < bsp->lightmap_count; lm++) {
        uint32_t lightmap = bsp->lightmaps_offset + lm * LIGHTMAP_STRIDE;
        if (!block(bsp->bytes, size, address, lightmap + 0x14u, MATERIAL_STRIDE,
                   65535u, &mat_count, &materials)) goto bad_material;
        for (mat = 0; mat < mat_count; mat++) {
            uint32_t surface;
            if (!halo_pc_bsp_material_at(bsp, lm, mat, &material, error, error_size))
                goto bad_material;
            for (surface = material.first_surface;
                 surface < material.first_surface + material.surface_count; surface++) {
                const uint8_t *tri = bsp->bytes + bsp->surfaces_offset + surface * 6u;
                if (u16(tri) >= material.vertex_count ||
                    u16(tri + 2u) >= material.vertex_count ||
                    u16(tri + 4u) >= material.vertex_count) goto bad_material;
            }
            bsp->material_count++;
            bsp->vertex_count += material.vertex_count;
        }
    }
    /* PC BSP pathfinding surfaces have one byte per collision surface. Their
       0x40 walkable flag is the same data used by the original bot paths. */
    {
        uint32_t collision_count=0,collision_offset=0,flags_count=0,flags_offset=0;
        uint32_t surface_count=0,surface_offset=0,edge_count=0,edge_offset=0;
        uint32_t vertex_count=0,vertex_offset=0;
        if(block(bsp->bytes,size,address,structure+0xb0u,0x60u,32u,
                 &collision_count,&collision_offset) && collision_count &&
           block(bsp->bytes,size,address,structure+0x1e4u,1u,131072u,
                 &flags_count,&flags_offset) &&
           block(bsp->bytes,size,address,collision_offset+0x3cu,0x0cu,131072u,
                 &surface_count,&surface_offset) &&
           block(bsp->bytes,size,address,collision_offset+0x48u,0x18u,262144u,
                 &edge_count,&edge_offset) &&
           block(bsp->bytes,size,address,collision_offset+0x54u,0x10u,131072u,
                 &vertex_count,&vertex_offset) &&
           flags_count==surface_count) {
            bsp->nav_surface_count=surface_count;
            bsp->nav_surfaces_offset=surface_offset;
            bsp->nav_edges_count=edge_count;
            bsp->nav_edges_offset=edge_offset;
            bsp->nav_vertices_count=vertex_count;
            bsp->nav_vertices_offset=vertex_offset;
            bsp->nav_flags_offset=flags_offset;
        }
    }
    if (error && error_size) error[0] = 0;
    return 1;
bad_material:
    halo_pc_bsp_close(bsp);
    return fail(error, error_size, "BSP material or surface indices invalid");
}

int halo_pc_bsp_nav_polygon(const struct halo_pc_bsp *bsp,uint32_t surface_index,
                            float points[8][3]) {
    if(!bsp || !bsp->bytes || !points || surface_index>=bsp->nav_surface_count ||
       !(bsp->bytes[bsp->nav_flags_offset+surface_index]&0x40u))return 0;
    const uint8_t *surface=bsp->bytes+bsp->nav_surfaces_offset+surface_index*0x0cu;
    uint32_t first=u32(surface+4u),edge_index=first;
    if(first>=bsp->nav_edges_count)return 0;
    for(int count=0;count<8;++count){
        if(edge_index>=bsp->nav_edges_count)return 0;
        const uint8_t *edge=bsp->bytes+bsp->nav_edges_offset+edge_index*0x18u;
        int reverse=u32(edge+20u)==surface_index;
        if(!reverse && u32(edge+16u)!=surface_index)return 0;
        uint32_t vertex_index=u32(edge+(reverse?4u:0u));
        if(vertex_index>=bsp->nav_vertices_count)return 0;
        const uint8_t *vertex=bsp->bytes+bsp->nav_vertices_offset+vertex_index*0x10u;
        memcpy(points[count],vertex,3*sizeof(float));
        for(int axis=0;axis<3;++axis)if(!isfinite(points[count][axis]))return 0;
        edge_index=u32(edge+(reverse?12u:8u));
        if(edge_index==first)return count+1>=3?count+1:0;
    }
    return 0;
}

/* Kevin Boggs: halo_pc_bsp_nav_main_component - New3DS preview integration. */
uint32_t halo_pc_bsp_nav_main_component(const struct halo_pc_bsp *bsp,
                                        uint8_t *included,size_t capacity) {
    if(!bsp || !bsp->bytes || !included || !bsp->nav_surface_count ||
       capacity<bsp->nav_surface_count)return 0;
    uint32_t count=bsp->nav_surface_count;
    uint32_t *labels=malloc((size_t)count*sizeof(*labels));
    uint32_t *queue=malloc((size_t)count*sizeof(*queue));
    if(!labels || !queue){free(labels);free(queue);return 0;}
    memset(included,0,count);
    uint32_t best_size=0,best_id=0,id=0;
    for(uint32_t seed=0;seed<count;++seed){
        if(included[seed] || !(bsp->bytes[bsp->nav_flags_offset+seed]&0x40u))continue;
        uint32_t head=0,tail=0;
        queue[tail++]=seed;included[seed]=1;
        while(head<tail){
            uint32_t surface_index=queue[head++];labels[surface_index]=id;
            const uint8_t *surface=bsp->bytes+bsp->nav_surfaces_offset+surface_index*0x0cu;
            uint32_t first=u32(surface+4u),edge_index=first;
            if(first>=bsp->nav_edges_count)continue;
            for(unsigned int step=0;step<8;++step){
                if(edge_index>=bsp->nav_edges_count)break;
                const uint8_t *edge=bsp->bytes+bsp->nav_edges_offset+edge_index*0x18u;
                uint32_t left=u32(edge+16u),right=u32(edge+20u),neighbor,next;
                if(left==surface_index){neighbor=right;next=u32(edge+8u);}
                else if(right==surface_index){neighbor=left;next=u32(edge+12u);}
                else break;
                if(neighbor<count && !included[neighbor] &&
                   (bsp->bytes[bsp->nav_flags_offset+neighbor]&0x40u)){
                    included[neighbor]=1;queue[tail++]=neighbor;
                }
                edge_index=next;
                if(edge_index==first)break;
            }
        }
        if(tail>best_size){best_size=tail;best_id=id;}
        ++id;
    }
    for(uint32_t surface=0;surface<count;++surface)
        included[surface]=included[surface] && labels[surface]==best_id;
    free(labels);free(queue);return best_size;
}

int halo_pc_bsp_read_material(const struct halo_pc_bsp *bsp,
                              const struct halo_pc_bsp_material *material,
                              float *positions, size_t position_floats,
                              uint16_t *indices, size_t index_count,
                              char *error, size_t error_size) {
    uint32_t i, j;
    if (!bsp || !bsp->bytes || !material || !positions || !indices ||
        position_floats < (size_t)material->vertex_count * 3u ||
        index_count < (size_t)material->surface_count * 3u)
        return fail(error, error_size, "BSP output buffers too small");
    for (i = 0; i < material->vertex_count; i++)
        memcpy(positions + (size_t)i * 3u,
               bsp->bytes + material->vertices_offset + (size_t)i * material->vertex_stride, 12u);
    for (i = 0; i < material->surface_count; i++) {
        const uint8_t *surface = bsp->bytes + bsp->surfaces_offset +
                                 (material->first_surface + i) * 6u;
        for (j = 0; j < 3u; j++) indices[(size_t)i * 3u + j] = u16(surface + j * 2u);
    }
    if (error && error_size) error[0] = 0;
    return 1;
}

int halo_pc_bsp_read_material_uv(const struct halo_pc_bsp *bsp,
                                 const struct halo_pc_bsp_material *material,
                                 float *vertices, size_t vertex_floats,
                                 uint16_t *indices, size_t index_count,
                                 char *error, size_t error_size) {
    uint32_t i, j;
    if (!bsp || !bsp->bytes || !material || !vertices || !indices ||
        vertex_floats < (size_t)material->vertex_count * 5u ||
        index_count < (size_t)material->surface_count * 3u)
        return fail(error, error_size, "BSP UV output buffers too small");
    for (i = 0; i < material->vertex_count; i++) {
        const uint8_t *source = bsp->bytes + material->vertices_offset + (size_t)i * material->vertex_stride;
        memcpy(vertices + (size_t)i * 5u, source, 12u);
        memcpy(vertices + (size_t)i * 5u + 3u, source + material->uv_offset, 8u);
    }
    for (i = 0; i < material->surface_count; i++) {
        const uint8_t *surface = bsp->bytes + bsp->surfaces_offset +
                                 (material->first_surface + i) * 6u;
        for (j = 0; j < 3u; j++) indices[(size_t)i * 3u + j] = u16(surface + j * 2u);
    }
    if (error && error_size) error[0] = 0;
    return 1;
}
