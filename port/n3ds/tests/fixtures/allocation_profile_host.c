/* Use actual collector/public wrappers with deterministic allocator snapshots. */
#define HALO_N3DS_ALLOCATION_PROFILE 1
#define HALO_N3DS_ALGORITHM_PROFILE 1
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "engine_allocation_profile.h"

typedef pthread_mutex_t LightLock;
static _Thread_local int lock_held, current_used, snapshot_calls;

static void LightLock_Init(LightLock *lock) { assert(!pthread_mutex_init(lock, NULL)); }
static void LightLock_Lock(LightLock *lock)
{
    assert(!lock_held && !pthread_mutex_lock(lock));
    lock_held = 1;
}
static void LightLock_Unlock(LightLock *lock)
{
    assert(lock_held);
    lock_held = 0;
    assert(!pthread_mutex_unlock(lock));
}

struct mallinfo { int uordblks; };
static struct mallinfo mallinfo(void)
{
    assert(!lock_held);
    ++snapshot_calls;
    errno = EINTR;
    return (struct mallinfo){.uordblks = current_used};
}

#include "engine_allocation_profile.inl"

static unsigned char storage;

void *__real_malloc(size_t bytes)
{
    if (!bytes) return NULL;
    if (bytes > 10 * 1024 * 1024) {
        errno = ENOMEM;
        return NULL;
    }
    current_used = (int)bytes;
    return &storage;
}
void *__real_calloc(size_t count, size_t bytes)
{
    if (count && bytes > SIZE_MAX / count) {
        errno = ENOMEM;
        return NULL;
    }
    return __real_malloc(count * bytes);
}
void *__real_memalign(size_t alignment, size_t bytes)
{
    assert(alignment == 128);
    return __real_malloc(bytes);
}
void *__real_aligned_alloc(size_t alignment, size_t bytes)
{
    assert(alignment == 256 && bytes % alignment == 0);
    return __real_malloc(bytes);
}

static void *worker(void *context)
{
    unsigned int bytes = (unsigned int)(uintptr_t)context;
    for (unsigned int i = 0; i < 30; ++i)
        assert(__wrap_malloc(bytes + i) == &storage);
    assert(snapshot_calls == 30);
    return NULL;
}

int main(void)
{
    struct n3ds_allocation_samples samples;
    errno = EBUSY;
    assert(__wrap_malloc(10) == &storage && !snapshot_calls && errno == EBUSY);
    n3ds_allocation_profile_start();
    assert(__wrap_malloc(8 * 1024 * 1024) == &storage && errno == EBUSY);
    assert(__wrap_calloc(2, 256) == &storage && errno == EBUSY);
    assert(!__wrap_calloc(SIZE_MAX, 2) && errno == ENOMEM);
    assert(!__wrap_malloc(0) && errno == ENOMEM);
    assert(__wrap_memalign(128, 512) == &storage && errno == ENOMEM);
    assert(__wrap_aligned_alloc(256, 256) == &storage && errno == ENOMEM);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_RESIZE, 1, 0);
    current_used = 0;
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_RESIZE, 0, 1);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_FREE, 0, 0);
    n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_ALLOCATE, 0, 8192);
    n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_ALLOCATE, 1, 8192);
    n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_FREE, 0, 0);

    n3ds_allocation_profile_drain(&samples, 0);
    assert(samples.ordinary.peak == 8 * 1024 * 1024 && samples.ordinary.allocations == 6 &&
        samples.ordinary.resizes == 2 && samples.ordinary.zero_resizes == 1 &&
        samples.ordinary.frees == 1 && samples.ordinary.failures == 2);
    assert(samples.linear.peak == 8192 && samples.linear.allocations == 2 &&
        samples.linear.frees == 1 && samples.linear.failures == 1);

    pthread_t helpers[4];
    for (unsigned int i = 0; i < 4; ++i)
        assert(!pthread_create(&helpers[i], NULL, worker, (void *)(uintptr_t)(4096 * (i + 1))));
    for (unsigned int i = 0; i < 4; ++i)
        assert(!pthread_join(helpers[i], NULL));
    n3ds_allocation_profile_drain(&samples, 1);
    assert(samples.ordinary.allocations == 120 && samples.ordinary.peak == 16384 + 29 &&
        !samples.ordinary.frees && !samples.ordinary.failures && !samples.linear.allocations);
    assert(__wrap_malloc(16385) == &storage && snapshot_calls == 9);
    n3ds_allocation_profile_linear_event(N3DS_ALLOCATION_ALLOCATE, 0, 8192);
    n3ds_allocation_profile_drain(&samples, 0);
    assert(!samples.ordinary.allocations && !samples.linear.allocations);
    assert(!pthread_mutex_destroy(&allocation_samples_lock));
    puts("PASS: allocation events preserve errno and return values, retain transient sampled peaks, and drain worker samples safely");
}
