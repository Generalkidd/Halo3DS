#ifndef N3DS_ENGINE_TRANSPARENT_H
#define N3DS_ENGINE_TRANSPARENT_H
#include "rasterizer/rasterizer_model_types.h"
/* Original January packet ABI, shared by the native submitter and consumer. */
struct render_sort_filth {
    short *previous_group_presorted_index_reference, *next_group_presorted_index_reference;
    short group_index, next_part_index, part_index;
    word pad;
};
struct transparent_geometry_group {
    unsigned long geometry_flags;
    long object_index, source_object_index;
    struct shader *shader;
    short shader_permutation_index;
    word pad12;
    struct render_model_effect effect;
    real_vector2d model_base_map_scale;
    long dynamic_triangle_buffer_index;
    union {
        const struct triangle_buffer *triangle_buffer;
        void (*render_proc)(long object_index,long widget_index);
    };
    long first_triangle_index, triangle_count, dynamic_vertex_buffer_index;
    const struct vertex_buffer *vertex_buffer;
    const struct bitmap_data *lightmap;
    const real_matrix4x3 *node_matrices;
    short node_matrix_count;
    word pad66;
    const struct render_lighting *lighting;
    const struct render_animation *animation;
    real z_sort;
    real_point3d centroid;
    real_plane3d plane;
    long sorted_index;
    short previous_group_presorted_index, next_group_presorted_index;
    long active_camouflage_transparent_source_object_index;
    boolean sort_last, cortana_hack;
    byte pad9e[2];
};
typedef char native_transparent_group_size[sizeof(struct transparent_geometry_group)==0xa0?1:-1];
typedef char native_transparent_group_depth[offsetof(struct transparent_geometry_group,z_sort)==0x70?1:-1];
typedef char native_transparent_group_pose[offsetof(struct transparent_geometry_group,node_matrices)==0x60?1:-1];
void n3ds_water_bsp_draw(const struct transparent_geometry_group *);
void n3ds_water_bsp_release(void);
void n3ds_transparent_begin_view(void);
void n3ds_transparent_draw_queued(void);
void n3ds_model_draw_group(const struct transparent_geometry_group *group);
int halo_engine_transparent_tests(void);
int n3ds_effect_vertex_reuse_tests(void);
struct transparent_geometry_group *_rasterizer_model_transparent_geometry_submit(
    struct shader *, short, const struct triangle_buffer *, long, long,
    const struct vertex_buffer *, long, const real_point3d *, struct render_sort_filth *);
#endif
