#ifndef HALO_N3DS_ENGINE_ALGORITHM_GPU_METRICS_H
#define HALO_N3DS_ENGINE_ALGORITHM_GPU_METRICS_H

/* Renderer-owned scope. Draw counters cover calls issued in
 * engine_gpu_renderer.c, not draws issued directly by other GPU modules.
 * Recorded draws/flushes/passes include a midpoint later discarded before
 * FrameEnd. Only submission/completion metrics carry real submission IDs.
 * Queue time includes transfers; retire wait includes the existing FrameBegin
 * synchronization. Neither is a GPU busy percentage or scanout duration. */
#define N3DS_ALGORITHM_GPU_METRICS(X) \
    X(N3DS_ALG_GPU_BSP_RESOLVED_ADJACENT, "bsp_resolved_adjacent_count") \
    X(N3DS_ALGORITHM_GPU_SUBMISSIONS,"gpu.submissions") \
    X(N3DS_ALGORITHM_GPU_COMPLETED_QUEUES,"gpu.completed_queues") \
    X(N3DS_ALGORITHM_GPU_PENDING_AT_EXIT,"gpu.pending_at_exit") \
    X(N3DS_ALGORITHM_GPU_CANCELED_MIDPOINTS,"gpu.canceled_midpoints") \
    X(N3DS_ALGORITHM_GPU_RENDERER_DRAW_CALLS,"gpu.renderer_draw_calls") \
    X(N3DS_ALGORITHM_GPU_RENDERER_TRIANGLES,"gpu.renderer_triangles") \
    X(N3DS_ALGORITHM_GPU_WATER_BASE_PASSES,"gpu.water_base_passes") \
    X(N3DS_ALGORITHM_GPU_WATER_REFLECTION_PASSES,"gpu.water_reflection_passes") \
    X(N3DS_ALGORITHM_GPU_BSP_MERGED_GROUPS,"gpu.bsp_merged_groups") \
    X(N3DS_ALGORITHM_GPU_BSP_PACKETS_SAVED,"gpu.bsp_packets_saved") \
    X(N3DS_ALGORITHM_GPU_BSP_DRAWS_SAVED,"gpu.bsp_draws_saved") \
    X(N3DS_ALGORITHM_GPU_FRAME_INDEX_FLUSH_BYTES,"gpu.frame_index_flush_bytes") \
    X(N3DS_ALGORITHM_GPU_FRAME_MODEL_FLUSH_BYTES,"gpu.frame_model_flush_bytes") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_REQUESTS,"gpu.model_compact_requests") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_HITS,"gpu.model_compact_hits") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_BUILDS,"gpu.model_compact_builds") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_REFERENCED,"gpu.model_compact_referenced_vertices") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_DUPLICATES,"gpu.model_compact_duplicate_vertices") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_UPLOAD,"gpu.model_compact_upload_bytes") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_RESIDENT_PEAK,"gpu.model_compact_resident_bytes_peak") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_SCRATCH_PEAK,"gpu.model_compact_scratch_bytes_peak") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_PREPARE,"gpu.model_compact_prepare_ticks") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_FALLBACKS,"gpu.model_compact_reference_fallbacks") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_PRESSURE,"gpu.model_compact_pressure") \
    X(N3DS_ALGORITHM_GPU_MODEL_COMPACT_VERIFIED_CORNERS,"gpu.model_compact_verified_corners") \
    X(N3DS_ALGORITHM_GPU_MODEL_COOKED_VALIDATED,"gpu.model_cooked_plans_validated") \
    X(N3DS_ALGORITHM_GPU_MODEL_COOKED_INVALID,"gpu.model_cooked_plan_runtime_fallbacks") \
    X(N3DS_ALGORITHM_GPU_WEIGHTED_VERTICES,"gpu.model_weighted_vertices_prepared") \
    X(N3DS_ALGORITHM_GPU_WEIGHTED_STREAM_BYTES,"gpu.model_weighted_stream_bytes_prepared") \
    X(N3DS_ALGORITHM_GPU_COMPACT_PALETTES_ORDERED,"gpu.model_compact_palettes_ordered") \
    X(N3DS_ALGORITHM_GPU_COMPACT_PALETTE_SLOTS_CHANGED,"gpu.model_compact_palette_slots_changed") \
    X(N3DS_ALGORITHM_GPU_MODEL_BONE_ROW_WRITES,"gpu.model_bone_row_writes") \
    X(N3DS_ALGORITHM_GPU_COMMAND_USAGE_PPM_MAX,"gpu.command_usage_ppm_max") \
    X(N3DS_ALGORITHM_GPU_QUEUE_NS,"gpu.queue_ns") \
    X(N3DS_ALGORITHM_GPU_PREPARE_NS,"gpu.prepare_ns") \
    X(N3DS_ALGORITHM_GPU_RETIRE_WAIT_NS,"gpu.retire_wait_ns") \
    X(N3DS_ALGORITHM_GPU_INVALID_TIMERS,"gpu.invalid_timers")

#endif
