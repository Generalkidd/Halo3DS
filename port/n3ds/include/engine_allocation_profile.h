#ifndef HALO_N3DS_ENGINE_ALLOCATION_PROFILE_H
#define HALO_N3DS_ENGINE_ALLOCATION_PROFILE_H

#if defined(HALO_N3DS_ALLOCATION_PROFILE) && !defined(HALO_N3DS_ALGORITHM_PROFILE)
#error "Allocation event profiling requires algorithm profiling"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HALO_N3DS_ALGORITHM_PROFILE
unsigned int n3ds_linear_allocator_used(void);
#endif

#ifdef HALO_N3DS_ALLOCATION_PROFILE
#define N3DS_ALLOCATION_PROFILE_CONFIGURED 1
enum n3ds_allocation_operation {
    N3DS_ALLOCATION_ALLOCATE, N3DS_ALLOCATION_RESIZE, N3DS_ALLOCATION_FREE
};

struct n3ds_allocation_sample {
    unsigned long long allocations, resizes, zero_resizes, frees, failures;
    unsigned int peak;
};
struct n3ds_allocation_samples {
    struct n3ds_allocation_sample ordinary, linear;
};

void n3ds_allocation_profile_start(void);
int n3ds_allocation_profile_active(void);
void n3ds_allocation_profile_heap_event(enum n3ds_allocation_operation operation, int failure, int zero_resize);
void n3ds_allocation_profile_linear_event(enum n3ds_allocation_operation operation, int failure, unsigned int used);
void n3ds_allocation_profile_drain(struct n3ds_allocation_samples *samples, int stop);
#else
#define N3DS_ALLOCATION_PROFILE_CONFIGURED 0
#define n3ds_allocation_profile_start() ((void)0)
#define n3ds_allocation_profile_heap_event(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif
