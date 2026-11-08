/* Real newlib calls and vendored linear allocator, with synthetic arenas. */
#include <3ds.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdint.h>
#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include "engine_allocation_profile.h"

static _Alignas(4096) unsigned char linear[65536];
static _Alignas(4096) unsigned char ordinary[262144];
u32 __ctru_linear_heap;
u32 __ctru_linear_heap_size = sizeof(linear);
static struct _reent current_reent;
static size_t heap_size;
struct _reent *__getreent(void) { return &current_reent; }
void __malloc_lock(struct _reent *r) {}
void __malloc_unlock(struct _reent *r) {}
void *__wrap__sbrk_r(struct _reent *r, ptrdiff_t delta)
{
    if (delta < -(ptrdiff_t)heap_size || delta > (ptrdiff_t)(sizeof(ordinary) - heap_size)) {
        r->_errno = ENOMEM;
        return (void *)-1;
    }
    void *out = ordinary + heap_size;
    heap_size += delta;
    return out;
}
void LightLock_Init(LightLock *lock) { *lock = 0; }
void LightLock_Lock(LightLock *lock) { if (*lock) __builtin_trap(); *lock = 1; }
void LightLock_Unlock(LightLock *lock) { if (!*lock) __builtin_trap(); *lock = 0; }
void n3ds_log(const char *message) {}
void abort(void) { __builtin_trap(); }
int halo_n3ds_snprintf(char *out, size_t size, const char *format, ...)
{ if (size) out[0]=0; return 0; }

#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)

int allocator_fixture(void)
{
    __ctru_linear_heap = (u32)linear;
    CHECK(!n3ds_linear_allocator_used());
    n3ds_allocation_profile_start();
    void *a = linearAlloc(513);
    void *b = linearMemAlign(1665, 4096);
    CHECK(a && b && ((uintptr_t)b & 4095) == 0);
    unsigned int charged = linearGetSize(a) + linearGetSize(b);
    CHECK(n3ds_linear_allocator_used() == charged);
    CHECK(!linearMemAlign(256, 48));
    CHECK(!linearAlloc(2 * sizeof(linear)));
    linearFree(a);
    linearFree(b);
    linearFree(NULL);
    CHECK(!n3ds_linear_allocator_used());

    unsigned char *base = malloc(8192);
    CHECK(base && mallinfo().uordblks >= 8192);
    base[0] = 17;
    unsigned char *expanded = realloc(base, 16384);
    CHECK(expanded && expanded[0] == 17);
    CHECK(!realloc(expanded, 1024 * 1024) && errno == ENOMEM && expanded[0] == 17);
    free(expanded);
    CHECK(!malloc(1024 * 1024) && errno == ENOMEM);
    unsigned int free_requests = 1;
    void *zero_allocation = calloc(1, 0);
    if (zero_allocation) {
        free(zero_allocation);
        ++free_requests;
    }
    void *aligned = aligned_alloc(256, 1024);
    CHECK(aligned && ((uintptr_t)aligned & 255) == 0);
    free(aligned);
    ++free_requests;
    unsigned char *small = memalign(128, 4096);
    CHECK(small && ((uintptr_t)small & 127) == 0);
    void *zero_resize = realloc(small, 0);
    if (zero_resize) {
        free(zero_resize);
        ++free_requests;
    }
    struct n3ds_allocation_samples sample;
    n3ds_allocation_profile_drain(&sample, 1);
    CHECK(sample.linear.peak == charged && sample.linear.allocations == 4 &&
          sample.linear.failures == 2 && sample.linear.frees == 2);
    CHECK(sample.ordinary.peak >= 16384 && sample.ordinary.allocations == 5 &&
          sample.ordinary.resizes == 3 && sample.ordinary.zero_resizes == 1 &&
          sample.ordinary.failures == 2 && sample.ordinary.frees == free_requests);
    return 0;
}
