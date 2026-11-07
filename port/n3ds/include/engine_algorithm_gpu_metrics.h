#ifndef HALO_N3DS_ENGINE_ALGORITHM_GPU_METRICS_H
#define HALO_N3DS_ENGINE_ALGORITHM_GPU_METRICS_H

/* Renderer-owned scope. Draw counters cover calls issued in
 * engine_gpu_renderer.c, not draws issued directly by other GPU modules.
 * Recorded draws/flushes/passes include a midpoint later discarded before
 * FrameEnd. Only submission/completion metrics carry real submission IDs.
 * Queue time includes transfers; retire wait includes the existing FrameBegin
 * synchronization. Neither is a GPU busy percentage or scanout duration. */
#define N3DS_ALGORITHM_GPU_METRICS(X) \
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
    X(N3DS_ALGORITHM_GPU_COMMAND_USAGE_PPM_MAX,"gpu.command_usage_ppm_max") \
    X(N3DS_ALGORITHM_GPU_QUEUE_NS,"gpu.queue_ns") \
    X(N3DS_ALGORITHM_GPU_PREPARE_NS,"gpu.prepare_ns") \
    X(N3DS_ALGORITHM_GPU_RETIRE_WAIT_NS,"gpu.retire_wait_ns") \
    X(N3DS_ALGORITHM_GPU_INVALID_TIMERS,"gpu.invalid_timers")

#endif
