#ifndef HALO_N3DS_ENGINE_ALGORITHM_IO_METRICS_H
#define HALO_N3DS_ENGINE_ALGORITHM_IO_METRICS_H

/* Fixed-size, owner-thread counters. Ticks are CPU wall intervals and may
 * overlap: audio decode includes a parallel join, and helper/main run together.
 * Keep names short enough for the shared bounded report buffer. Parallel item
 * counts describe whole batches, not the helper's share. Audio range batches
 * may still run serially; small local audio batches bypass that range API.
 * Sound slot peaks include pending entries; resident bytes count loaded data. */
#define N3DS_ALGORITHM_IO_METRICS(X) \
 X(N3DS_ALG_IO_DEFERRED_DECODE_DISPATCH, "deferred_decode_dispatch_count") \
 X(N3DS_ALG_IO_DEFERRED_DECODE_AGE, "deferred_decode_first_use_age_ticks") \
 X(N3DS_ALG_IO_DEFERRED_DECODE_JOIN, "deferred_decode_first_use_wait_ticks") \
 X(N3DS_ALG_IO_DEFERRED_DECODE_DISCARD, "deferred_decode_discard_count") \
 X(N3DS_ALG_MEM_HEAP_PEAK, "ordinary_heap_sampled_peak_bytes") \
 X(N3DS_ALG_MEM_LINEAR_PEAK, "linear_heap_sampled_peak_bytes") \
 X(N3DS_ALG_MEM_HEAP_EVENT_PEAK, "ordinary_heap_event_peak_bytes") \
 X(N3DS_ALG_MEM_HEAP_ALLOCS, "ordinary_heap_alloc_attempts") \
 X(N3DS_ALG_MEM_HEAP_RESIZES, "ordinary_heap_resize_calls") \
 X(N3DS_ALG_MEM_HEAP_ZERO_RESIZES, "ordinary_heap_zero_resize_calls") \
 X(N3DS_ALG_MEM_HEAP_FREES, "ordinary_heap_free_requests") \
 X(N3DS_ALG_MEM_HEAP_FAILURES, "ordinary_heap_alloc_failures") \
 X(N3DS_ALG_MEM_LINEAR_EVENT_PEAK, "linear_heap_event_peak_bytes") \
 X(N3DS_ALG_MEM_LINEAR_ALLOCS, "linear_heap_alloc_attempts") \
 X(N3DS_ALG_MEM_LINEAR_FREES, "linear_heap_free_requests") \
 X(N3DS_ALG_MEM_LINEAR_FAILURES, "linear_heap_alloc_failures") \
 X(N3DS_ALG_MEM_TEXTURE_STAGE_PEAK, "texture_staging_peak_bytes") \
    X(N3DS_ALG_IO_AUDIO_UPDATES,"audio_update_count") \
    X(N3DS_ALG_IO_AUDIO_ACTIVE_VOICES,"audio_active_voice_count") \
    X(N3DS_ALG_IO_AUDIO_PEAK_VOICES,"audio_peak_voice_count") \
    X(N3DS_ALG_IO_AUDIO_JOBS,"audio_decode_job_count") \
    X(N3DS_ALG_IO_AUDIO_LOCAL_BATCHES,"audio_local_batch_count") \
    X(N3DS_ALG_IO_AUDIO_RANGE_BATCHES,"audio_range_batch_count") \
    X(N3DS_ALG_IO_AUDIO_PCM_BYTES,"audio_pcm_input_bytes") \
    X(N3DS_ALG_IO_AUDIO_ADPCM_BYTES,"audio_adpcm_input_bytes") \
    X(N3DS_ALG_IO_AUDIO_DECODED_FRAMES,"audio_decoded_frame_count") \
    X(N3DS_ALG_IO_AUDIO_PACKETS,"audio_submit_packet_count") \
    X(N3DS_ALG_IO_AUDIO_BAD_JOBS,"audio_bad_job_count") \
    X(N3DS_ALG_IO_AUDIO_DRAINS,"audio_refill_drain_count") \
    X(N3DS_ALG_IO_AUDIO_PLAN_TICKS,"audio_plan_ticks") \
    X(N3DS_ALG_IO_AUDIO_DECODE_TICKS,"audio_decode_join_ticks") \
    X(N3DS_ALG_IO_AUDIO_SUBMIT_TICKS,"audio_submit_ticks") \
    X(N3DS_ALG_IO_AUDIO_TOTAL_TICKS,"audio_total_ticks") \
    X(N3DS_ALG_IO_AUDIO_PEAK_TICKS,"audio_total_peak_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_SERIAL_COUNT,"par_audio_serial_count") \
    X(N3DS_ALG_IO_PAR_AUDIO_SERIAL_ITEMS,"par_audio_serial_item_count") \
    X(N3DS_ALG_IO_PAR_AUDIO_WORKER_COUNT,"par_audio_worker_count") \
    X(N3DS_ALG_IO_PAR_AUDIO_WORKER_ITEMS,"par_audio_worker_item_count") \
    X(N3DS_ALG_IO_PAR_AUDIO_BATCH_TICKS,"par_audio_batch_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_PEAK_TICKS,"par_audio_batch_peak_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_SERIAL_TICKS,"par_audio_serial_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_WAKE_TICKS,"par_audio_wake_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_MAIN_TICKS,"par_audio_main_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_HELPER_TICKS,"par_audio_helper_ticks") \
    X(N3DS_ALG_IO_PAR_AUDIO_JOIN_TICKS,"par_audio_join_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_SERIAL_COUNT,"par_decode_serial_count") \
    X(N3DS_ALG_IO_PAR_DECODE_SERIAL_ITEMS,"par_decode_serial_item_count") \
    X(N3DS_ALG_IO_PAR_DECODE_WORKER_COUNT,"par_decode_worker_count") \
    X(N3DS_ALG_IO_PAR_DECODE_WORKER_ITEMS,"par_decode_worker_item_count") \
    X(N3DS_ALG_IO_PAR_DECODE_BATCH_TICKS,"par_decode_batch_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_PEAK_TICKS,"par_decode_batch_peak_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_SERIAL_TICKS,"par_decode_serial_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_WAKE_TICKS,"par_decode_wake_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_MAIN_TICKS,"par_decode_main_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_HELPER_TICKS,"par_decode_helper_ticks") \
    X(N3DS_ALG_IO_PAR_DECODE_JOIN_TICKS,"par_decode_join_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_SERIAL_COUNT,"par_pose_serial_count") \
    X(N3DS_ALG_IO_PAR_POSE_SERIAL_ITEMS,"par_pose_serial_item_count") \
    X(N3DS_ALG_IO_PAR_POSE_WORKER_COUNT,"par_pose_worker_count") \
    X(N3DS_ALG_IO_PAR_POSE_WORKER_ITEMS,"par_pose_worker_item_count") \
    X(N3DS_ALG_IO_PAR_POSE_BATCH_TICKS,"par_pose_batch_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_PEAK_TICKS,"par_pose_batch_peak_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_SERIAL_TICKS,"par_pose_serial_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_WAKE_TICKS,"par_pose_wake_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_MAIN_TICKS,"par_pose_main_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_HELPER_TICKS,"par_pose_helper_ticks") \
    X(N3DS_ALG_IO_PAR_POSE_JOIN_TICKS,"par_pose_join_ticks") \
    X(N3DS_ALG_IO_SOUND_REQUESTS,"sound_request_count") \
    X(N3DS_ALG_IO_SOUND_READY_HITS,"sound_ready_hit_count") \
    X(N3DS_ALG_IO_SOUND_IDLE_LOADS,"sound_idle_load_count") \
    X(N3DS_ALG_IO_SOUND_BLOCK_LOADS,"sound_block_load_count") \
    X(N3DS_ALG_IO_SOUND_LOADS,"sound_load_count") \
    X(N3DS_ALG_IO_SOUND_LOAD_FAILS,"sound_load_fail_count") \
    X(N3DS_ALG_IO_SOUND_LOAD_BYTES,"sound_load_bytes") \
    X(N3DS_ALG_IO_SOUND_LOAD_TICKS,"sound_load_ticks") \
    X(N3DS_ALG_IO_SOUND_STAGE_HITS,"sound_stage_hit_count") \
    X(N3DS_ALG_IO_SOUND_STAGE_MISSES,"sound_stage_miss_count") \
    X(N3DS_ALG_IO_SOUND_STAGE_BYTES,"sound_stage_copy_bytes") \
    X(N3DS_ALG_IO_SOUND_STAGE_COPY_TICKS,"sound_stage_copy_ticks") \
    X(N3DS_ALG_IO_SOUND_READS,"sound_sync_read_count") \
    X(N3DS_ALG_IO_SOUND_READ_FAILS,"sound_sync_read_fail_count") \
    X(N3DS_ALG_IO_SOUND_READ_BYTES,"sound_sync_read_bytes") \
    X(N3DS_ALG_IO_SOUND_READ_TICKS,"sound_sync_read_ticks") \
    X(N3DS_ALG_IO_SOUND_BUDGET_FAILS,"sound_budget_fail_count") \
    X(N3DS_ALG_IO_SOUND_ALLOC_FAILS,"sound_alloc_fail_count") \
    X(N3DS_ALG_IO_SOUND_ALLOC_ATTEMPT_FAILS,"sound_alloc_attempt_fail_count") \
    X(N3DS_ALG_IO_SOUND_SLOT_FAILS,"sound_slot_fail_count") \
    X(N3DS_ALG_IO_SOUND_BUDGET_EVICTIONS,"sound_budget_evict_count") \
    X(N3DS_ALG_IO_SOUND_ALLOC_EVICTIONS,"sound_alloc_evict_count") \
    X(N3DS_ALG_IO_SOUND_SLOT_EVICTIONS,"sound_slot_evict_count") \
    X(N3DS_ALG_IO_SOUND_SCAN_SLOTS,"sound_scan_slot_count") \
    X(N3DS_ALG_IO_SOUND_PEAK_BYTES,"sound_resident_peak_bytes") \
    X(N3DS_ALG_IO_SOUND_PEAK_SLOTS,"sound_slot_peak_count")

#endif
