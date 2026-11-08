#ifndef HALO_N3DS_ENGINE_ALGORITHM_PROFILE_H
#define HALO_N3DS_ENGINE_ALGORITHM_PROFILE_H

#include "engine_algorithm_features.h"

/* This collector belongs to the frontend/render owner. Helpers accumulate in
 * their existing per-job storage; the owner records it only AFTER joining.
 * Counters never become authoritative state and do not enter checkpoints. */
struct n3ds_algorithm_profile_context {
    unsigned long long run, loop, submission;
    long tick;
    unsigned int generation;
    int bsp, kind, gameplay;
};

#ifdef HALO_N3DS_ALGORITHM_PROFILE
#include "engine_algorithm_cpu_metrics.h"
#include "engine_algorithm_io_metrics.h"
#include "engine_algorithm_gpu_metrics.h"
#define N3DS_ALGORITHM_METRICS(X) \
    N3DS_ALGORITHM_CPU_METRICS(X) \
    N3DS_ALGORITHM_IO_METRICS(X) \
    N3DS_ALGORITHM_GPU_METRICS(X)
#define N3DS_ALGORITHM_ENUM(id, name) id,
enum n3ds_algorithm_metric {
    N3DS_ALGORITHM_METRICS(N3DS_ALGORITHM_ENUM)
    N3DS_ALGORITHM_METRIC_COUNT
};
#undef N3DS_ALGORITHM_ENUM
void n3ds_algorithm_profile_begin_loop(unsigned long long loop, long tick,
    unsigned int generation, int bsp, int gameplay);
void n3ds_algorithm_profile_context(long tick, unsigned int generation, int bsp, int kind);
void n3ds_algorithm_profile_tick(long tick);
void n3ds_algorithm_profile_location(unsigned int generation, int bsp);
void n3ds_algorithm_profile_submission(unsigned long long submission);
void n3ds_algorithm_profile_snapshot(struct n3ds_algorithm_profile_context *out);
unsigned long long n3ds_algorithm_profile_run_id(void);
unsigned long long n3ds_algorithm_profile_loop_id(void);
void n3ds_algorithm_profile_add(enum n3ds_algorithm_metric metric, unsigned long long value);
void n3ds_algorithm_profile_max(enum n3ds_algorithm_metric metric, unsigned long long value);
void n3ds_algorithm_profile_add_at(const struct n3ds_algorithm_profile_context *context,
    enum n3ds_algorithm_metric metric, unsigned long long value);
void n3ds_algorithm_profile_max_at(const struct n3ds_algorithm_profile_context *context,
    enum n3ds_algorithm_metric metric, unsigned long long value);
void n3ds_algorithm_profile_end_loop(void);
void n3ds_algorithm_profile_staging_acquire(unsigned int bytes);
void n3ds_algorithm_profile_staging_release(unsigned int bytes);
void n3ds_algorithm_profile_finish(void);
void n3ds_algorithm_profile_stop(void);
void n3ds_gpu_algorithm_profile_finish(void);
#else
/* Discard arguments too: baseline builds add no clocks, lookups or calls. */
#define n3ds_algorithm_profile_begin_loop(...) ((void)0)
#define n3ds_algorithm_profile_context(...) ((void)0)
#define n3ds_algorithm_profile_tick(...) ((void)0)
#define n3ds_algorithm_profile_location(...) ((void)0)
#define n3ds_algorithm_profile_submission(...) ((void)0)
#define n3ds_algorithm_profile_snapshot(...) ((void)0)
#define n3ds_algorithm_profile_run_id() 0ULL
#define n3ds_algorithm_profile_loop_id() 0ULL
#define n3ds_algorithm_profile_add(...) ((void)0)
#define n3ds_algorithm_profile_max(...) ((void)0)
#define n3ds_algorithm_profile_add_at(...) ((void)0)
#define n3ds_algorithm_profile_max_at(...) ((void)0)
#define n3ds_algorithm_profile_end_loop() ((void)0)
#define n3ds_algorithm_profile_staging_acquire(...) ((void)0)
#define n3ds_algorithm_profile_staging_release(...) ((void)0)
#define n3ds_algorithm_profile_finish() ((void)0)
#define n3ds_algorithm_profile_stop() ((void)0)
#define n3ds_gpu_algorithm_profile_finish() ((void)0)
#endif

#endif
