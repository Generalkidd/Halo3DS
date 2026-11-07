#ifndef ENGINE_ALGORITHM_CPU_METRICS_H
#define ENGINE_ALGORITHM_CPU_METRICS_H

/* Main-owner work only. *_nodes_calls and *_vertices_calls count element
 * operations, not function invocations. Batch counts describe attempted work;
 * a failing batch may have stopped early. Bytes count explicit copies, not
 * memory traffic inferred from cache hits. No worker updates or inner clocks. */
#define N3DS_ALGORITHM_CPU_METRICS(X) \
 X(N3DS_ALG_CPU_ANIM_ROOT_MATRIX, "anim_root_matrix_calls") \
 X(N3DS_ALG_CPU_ANIM_ROOT_VELOCITY, "anim_root_velocity_calls") \
 X(N3DS_ALG_CPU_ANIM_FULL, "anim_full_pose_calls") \
 X(N3DS_ALG_CPU_ANIM_HIT, "anim_cache_hit_calls") \
 X(N3DS_ALG_CPU_ANIM_MISS, "anim_cache_miss_calls") \
 X(N3DS_ALG_CPU_ANIM_BYPASS, "anim_cache_bypass_calls") \
 X(N3DS_ALG_CPU_ANIM_COPY, "anim_pose_copy_bytes") \
 X(N3DS_ALG_CPU_ANIM_COMPRESSED, "anim_compressed_decode_calls") \
 X(N3DS_ALG_CPU_ANIM_RAW, "anim_raw_decode_calls") \
 X(N3DS_ALG_CPU_ANIM_DECODE_NODES, "anim_decode_nodes_calls") \
 X(N3DS_ALG_CPU_ANIM_DEFAULT, "anim_default_pose_calls") \
 X(N3DS_ALG_CPU_ANIM_INTERPOLATE, "anim_interpolate_calls") \
 X(N3DS_ALG_CPU_ANIM_INTERPOLATE_NODES, "anim_interpolate_nodes_calls") \
 X(N3DS_ALG_CPU_ANIM_HIERARCHY, "anim_hierarchy_calls") \
 X(N3DS_ALG_CPU_ANIM_HIERARCHY_NODES, "anim_hierarchy_nodes_calls") \
 X(N3DS_ALG_CPU_VISIBILITY, "visibility_compute_calls") \
 X(N3DS_ALG_CPU_VIS_CLUSTER, "visibility_cluster_visit_calls") \
 X(N3DS_ALG_CPU_VIS_RECTANGLE, "visibility_rectangle_calls") \
 X(N3DS_ALG_CPU_VIS_PLANES, "visibility_planes_calls") \
 X(N3DS_ALG_CPU_VIS_PLANE_TEST, "visibility_plane_tests_calls") \
 X(N3DS_ALG_CPU_PORTAL_REQUEST, "portal_hull_request_calls") \
 X(N3DS_ALG_CPU_PORTAL_HIT, "portal_cache_hit_calls") \
 X(N3DS_ALG_CPU_PORTAL_PROJECT, "portal_projection_calls") \
 X(N3DS_ALG_CPU_PORTAL_TRANSFORM, "portal_point_transform_calls") \
 X(N3DS_ALG_CPU_PORTAL_COPY, "portal_cached_copy_bytes") \
 X(N3DS_ALG_CPU_BATCH_HIT, "bsp_candidate_cache_hit_calls") \
 X(N3DS_ALG_CPU_BATCH_MISS, "bsp_candidate_cache_miss_calls") \
 X(N3DS_ALG_CPU_BATCH_BYPASS, "bsp_candidate_bypass_calls") \
 X(N3DS_ALG_CPU_INDEX_SCAN, "bsp_triangle_owner_scan_calls") \
 X(N3DS_ALG_CPU_INDEX_COPY, "bsp_visible_index_copy_bytes") \
 X(N3DS_ALG_CPU_COLLISION_SPHERE, "collision_model_sphere_calls") \
 X(N3DS_ALG_CPU_COLLISION_POINT, "collision_model_point_calls") \
 X(N3DS_ALG_CPU_COLLISION_VECTOR, "collision_model_vector_calls") \
 X(N3DS_ALG_CPU_COLLISION_PILL, "collision_model_pill_calls") \
 X(N3DS_ALG_CPU_COLLISION_FEATURES, "collision_model_features_calls") \
 X(N3DS_ALG_CPU_COLLISION_INVERSE, "collision_model_inverse_calls") \
 X(N3DS_ALG_CPU_COLLISION_POINT_XFORM, "collision_model_point_xform_calls") \
 X(N3DS_ALG_CPU_COLLISION_VECTOR_XFORM, "collision_model_vector_xform_calls") \
 X(N3DS_ALG_CPU_DECODE_HIT, "model_decode_hit_calls") \
 X(N3DS_ALG_CPU_DECODE_MISS, "model_decode_miss_calls") \
 X(N3DS_ALG_CPU_DECODE_VERTICES, "model_decode_batch_vertices_calls") \
 X(N3DS_ALG_CPU_DECODE_COPY, "model_decode_shadow_copy_bytes") \
 X(N3DS_ALG_CPU_DECODE_FAILURE, "model_decode_fill_failure_calls") \
 X(N3DS_ALG_CPU_POSE_HIT, "model_pose_hit_calls") \
 X(N3DS_ALG_CPU_POSE_MISS, "model_pose_miss_calls") \
 X(N3DS_ALG_CPU_POSE_EVICT, "model_pose_budget_evict_calls") \
 X(N3DS_ALG_CPU_POSE_FALLBACK, "model_pose_unavailable_calls") \
 X(N3DS_ALG_CPU_SKIN_VERTICES, "model_skin_batch_vertices_calls") \
 X(N3DS_ALG_CPU_POSE_COPY, "model_pose_matrix_copy_bytes") \
 X(N3DS_ALG_CPU_NORMAL_HIT, "model_normal_hit_calls") \
 X(N3DS_ALG_CPU_NORMAL_MISS, "model_normal_miss_calls") \
 X(N3DS_ALG_CPU_NORMAL_EVICT, "model_normal_budget_evict_calls") \
 X(N3DS_ALG_CPU_NORMAL_VERTICES, "model_normal_batch_vertices_calls") \
 X(N3DS_ALG_CPU_NORMAL_COPY, "model_normal_matrix_copy_bytes")

#endif
