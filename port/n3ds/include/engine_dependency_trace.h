#ifndef HALO_N3DS_ENGINE_DEPENDENCY_TRACE_H
#define HALO_N3DS_ENGINE_DEPENDENCY_TRACE_H

#include <stdint.h>

/* Independent of aggregate profiling and all renderer/algorithm candidates.
 * No structure in this header belongs to authoritative game/checkpoint state. */
#define N3DS_TRACE_SCHEMA 1u
#define N3DS_TRACE_CAPACITY 8192u
#define N3DS_TRACE_CALLBACK_CAPACITY 256u
#define N3DS_TRACE_QUEUE_CAPACITY 16u
#ifndef HALO_N3DS_DEPENDENCY_TRACE_LOOPS
#define HALO_N3DS_DEPENDENCY_TRACE_LOOPS 120
#endif
#if HALO_N3DS_DEPENDENCY_TRACE_LOOPS < 1 || HALO_N3DS_DEPENDENCY_TRACE_LOOPS > 18000
#error "Dependency trace loop limit must be 1..18000"
#endif

struct n3ds_trace_context {
    uint32_t loop, epoch, generation, attempt, view, submission, job;
    int32_t tick, bsp;
    uint8_t kind;
    uint8_t reserved[3];
};

/* Stable on-disk IDs. Append new IDs; do not renumber an existing schema. */
enum n3ds_trace_event {
    N3DS_TRACE_LOOP_BEGIN = 1, N3DS_TRACE_LOOP_END,
    N3DS_TRACE_TICK_BEGIN, N3DS_TRACE_TICK_END,
    N3DS_TRACE_FRAME_UPDATE_BEGIN, N3DS_TRACE_FRAME_UPDATE_END,
    N3DS_TRACE_RESTORE, N3DS_TRACE_LOCATION,
    N3DS_TRACE_ATTEMPT_BEGIN, N3DS_TRACE_ATTEMPT_END,
    N3DS_TRACE_VIEW_BEGIN, N3DS_TRACE_VIEW_END,
    N3DS_TRACE_FRAME_SERVICES_BEGIN, N3DS_TRACE_FRAME_SERVICES_END,
    N3DS_TRACE_THREAD_COLLECT_BEGIN, N3DS_TRACE_THREAD_COLLECT_END,
    N3DS_TRACE_TEXTURE_SERVICE_BEGIN, N3DS_TRACE_TEXTURE_SERVICE_END,
    N3DS_TRACE_FRUSTUM_BEGIN, N3DS_TRACE_FRUSTUM_END,
    N3DS_TRACE_VISIBILITY_BEGIN, N3DS_TRACE_VISIBILITY_END,
    N3DS_TRACE_OBJECT_DISCOVERY_BEGIN, N3DS_TRACE_OBJECT_DISCOVERY_END,
    N3DS_TRACE_OBJECT_RENDER_BEGIN, N3DS_TRACE_OBJECT_RENDER_END,
    N3DS_TRACE_GPU_ACQUIRE_BEGIN, N3DS_TRACE_GPU_ACQUIRE_END,
    N3DS_TRACE_GPU_ACQUIRE_REJECT,
    N3DS_TRACE_GPU_FRAME_END_BEGIN, N3DS_TRACE_GPU_FRAME_END_END,
    N3DS_TRACE_GPU_TEXTURE_FENCE_BEGIN, N3DS_TRACE_GPU_TEXTURE_FENCE_END,
    N3DS_TRACE_GPU_SWAP_WAIT_BEGIN, N3DS_TRACE_GPU_SWAP_WAIT_END,
    N3DS_TRACE_MIDPOINT_CANCEL,
    N3DS_TRACE_QUEUE_RUN, N3DS_TRACE_QUEUE_COMPLETE,
    N3DS_TRACE_TOP_SWAP_POSTED, N3DS_TRACE_TOP_PENDING_CLEAR,
    N3DS_TRACE_TOP_ACK_UNOBSERVED, N3DS_TRACE_VBLANK,
    N3DS_TRACE_WORKER_DISPATCH, N3DS_TRACE_WORKER_START, N3DS_TRACE_WORKER_END,
    N3DS_TRACE_MAIN_WORK_BEGIN, N3DS_TRACE_MAIN_WORK_END,
    N3DS_TRACE_JOIN_BEGIN, N3DS_TRACE_JOIN_END,
    N3DS_TRACE_SERIAL_BEGIN, N3DS_TRACE_SERIAL_END
};
enum n3ds_trace_attempt_result {
    N3DS_TRACE_ATTEMPT_COMPLETE, N3DS_TRACE_ATTEMPT_TEXTURE,
    N3DS_TRACE_ATTEMPT_DEADLINE, N3DS_TRACE_ATTEMPT_GPU_BUSY,
    N3DS_TRACE_ATTEMPT_PREPARATION_REJECT
};

#ifdef HALO_N3DS_DEPENDENCY_TRACE
void n3ds_trace_initialize(void);
void n3ds_trace_finish(void);
void n3ds_trace_stop(void);
void n3ds_trace_begin_loop(unsigned int loop, long tick, unsigned int generation, int bsp, int gameplay);
void n3ds_trace_end_loop(void);
void n3ds_trace_tick(long tick);
void n3ds_trace_restore(long tick);
void n3ds_trace_location(unsigned int generation, int bsp);
void n3ds_trace_attempt_begin(int kind, long tick, unsigned int generation, int bsp);
void n3ds_trace_attempt_end(unsigned int reason);
void n3ds_trace_view_begin(unsigned int eye);
void n3ds_trace_view_end(void);
void n3ds_trace_snapshot(struct n3ds_trace_context *out);
unsigned int n3ds_trace_job_begin(void);
unsigned long long n3ds_trace_clock(void);
void n3ds_trace_event(enum n3ds_trace_event event, unsigned int arg0, unsigned int arg1);
void n3ds_trace_event_at(const struct n3ds_trace_context *context, enum n3ds_trace_event event,
    unsigned long long timestamp, unsigned int arg0, unsigned int arg1);
/* Queue-run and poll belong to the owner; completion and VBlank belong to
 * their separate callback producers. None of these APIs waits for the GPU. */
void n3ds_trace_queue_run(void);
void n3ds_trace_queue_complete_begin(void);
void n3ds_trace_queue_complete(int top_swap);
void n3ds_trace_vblank(void);
void n3ds_trace_gpu_poll(void);
#else
/* Disabled builds evaluate no arguments, clocks, lookups or callbacks. */
#define n3ds_trace_initialize() ((void)0)
#define n3ds_trace_finish() ((void)0)
#define n3ds_trace_stop() ((void)0)
#define n3ds_trace_begin_loop(...) ((void)0)
#define n3ds_trace_end_loop() ((void)0)
#define n3ds_trace_tick(...) ((void)0)
#define n3ds_trace_restore(...) ((void)0)
#define n3ds_trace_location(...) ((void)0)
#define n3ds_trace_attempt_begin(...) ((void)0)
#define n3ds_trace_attempt_end(...) ((void)0)
#define n3ds_trace_view_begin(...) ((void)0)
#define n3ds_trace_view_end() ((void)0)
#define n3ds_trace_snapshot(...) ((void)0)
#define n3ds_trace_job_begin() 0u
#define n3ds_trace_clock() 0ULL
#define n3ds_trace_event(...) ((void)0)
#define n3ds_trace_event_at(...) ((void)0)
#define n3ds_trace_queue_run() ((void)0)
#define n3ds_trace_queue_complete_begin() ((void)0)
#define n3ds_trace_queue_complete(...) ((void)0)
#define n3ds_trace_vblank() ((void)0)
#define n3ds_trace_gpu_poll() ((void)0)
#endif
#endif
