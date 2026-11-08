#ifndef HALO_N3DS_ENGINE_PREPARED_VIEW_H
#define HALO_N3DS_ENGINE_PREPARED_VIEW_H

#include "render/render.h"
#include "objects/objects.h"
#include "engine_prepared_object_marker.h"

/* CPU-only result for one view. Camera/frusta, cluster order and logical
 * object IDs are owned by the caller until the regular frame consumes them. */
struct n3ds_prepared_visibility {
    long leaf_index, cluster_index;
    short visible_sky_index, cluster_count;
    boolean visible_sky_model;
    unsigned long visible_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
    struct rendered_cluster clusters[MAXIMUM_RENDERED_CLUSTERS];
};

struct n3ds_prepared_objects {
    struct n3ds_prepared_marker marker;
    short count;
    long indices[MAXIMUM_RENDERED_OBJECTS];
};

boolean n3ds_prepare_visibility_allowed(void);
void n3ds_prepare_visibility(const struct render_camera *, const struct render_frustum *,
    struct n3ds_prepared_visibility *);
void n3ds_publish_visibility(const struct n3ds_prepared_visibility *);
boolean n3ds_prepared_visibility_matches(const struct n3ds_prepared_visibility *);
boolean n3ds_frusta_match(const struct render_frustum *, const struct render_frustum *);
short n3ds_prepared_visibility_find_objects(const struct n3ds_prepared_visibility *,
    long *result_indices, short maximum_count,
    long (*cluster_get_first)(long *, short), long (*cluster_get_next)(long *),
    void (*get_bounding_sphere)(long, real_point3d *, real *),
    boolean (*unmarked)(long), long (*mark)(long));
boolean n3ds_prepare_render_objects(const struct n3ds_prepared_visibility *,
    struct n3ds_prepared_objects *);
boolean n3ds_render_prepared_objects_with_environment(void (*)(const void *), const void *,
    const struct n3ds_prepared_objects *);

#endif
