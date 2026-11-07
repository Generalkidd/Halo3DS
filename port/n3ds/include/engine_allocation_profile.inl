/* Included by the platform unit with the native newlib and libctru ABI.
 * Public wrappers observe the allocator after it returns. Their only shared
 * write is to this fixed record, which the frontend owner drains. */
static LightLock allocation_samples_lock;
static unsigned int allocation_samples_active;
static struct n3ds_allocation_samples allocation_samples;

void n3ds_allocation_profile_start(void)
{
    /* Main starts the only capture before creating gameplay workers. */
    LightLock_Init(&allocation_samples_lock);
    memset(&allocation_samples, 0, sizeof(allocation_samples));
    __atomic_store_n(&allocation_samples_active, 1, __ATOMIC_RELEASE);
}

int n3ds_allocation_profile_active(void)
{
    return __atomic_load_n(&allocation_samples_active, __ATOMIC_ACQUIRE);
}

void n3ds_allocation_profile_drain(struct n3ds_allocation_samples *samples, int stop)
{
    LightLock_Lock(&allocation_samples_lock);
    if (stop) __atomic_store_n(&allocation_samples_active, 0, __ATOMIC_RELEASE);
    *samples = allocation_samples;
    memset(&allocation_samples, 0, sizeof(allocation_samples));
    LightLock_Unlock(&allocation_samples_lock);
}

static void allocation_observe(struct n3ds_allocation_sample *sample,
    enum n3ds_allocation_operation operation, int failure, int zero_resize, unsigned int used)
{
    /* Never query an allocator or publish profiler/log records under this lock. */
    LightLock_Lock(&allocation_samples_lock);
    if (n3ds_allocation_profile_active()) {
        switch (operation) {
            case N3DS_ALLOCATION_ALLOCATE: ++sample->allocations; break;
            case N3DS_ALLOCATION_RESIZE: ++sample->resizes; sample->zero_resizes += !!zero_resize; break;
            case N3DS_ALLOCATION_FREE: ++sample->frees; break;
        }
        sample->failures += !!failure;
        if (used > sample->peak) sample->peak = used;
    }
    LightLock_Unlock(&allocation_samples_lock);
}

void n3ds_allocation_profile_heap_event(enum n3ds_allocation_operation operation, int failure, int zero_resize)
{
    if (!n3ds_allocation_profile_active()) return;
    int original_errno = errno;
    struct mallinfo info = mallinfo();
    allocation_observe(&allocation_samples.ordinary, operation, failure, zero_resize,
        info.uordblks > 0 ? (unsigned int)info.uordblks : 0);
    errno = original_errno;
}

void n3ds_allocation_profile_linear_event(enum n3ds_allocation_operation operation, int failure, unsigned int used)
{
    allocation_observe(&allocation_samples.linear, operation, failure, 0, used);
}

void *__real_malloc(size_t bytes);
void *__real_calloc(size_t count, size_t size);
void *__real_memalign(size_t alignment, size_t bytes);
void *__real_aligned_alloc(size_t alignment, size_t bytes);

void *__wrap_malloc(size_t bytes)
{
    void *result = __real_malloc(bytes);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_ALLOCATE, !result && bytes != 0, 0);
    return result;
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *result = __real_calloc(count, size);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_ALLOCATE, !result && count != 0 && size != 0, 0);
    return result;
}

void *__wrap_memalign(size_t alignment, size_t bytes)
{
    void *result = __real_memalign(alignment, bytes);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_ALLOCATE, !result && bytes != 0, 0);
    return result;
}

void *__wrap_aligned_alloc(size_t alignment, size_t bytes)
{
    void *result = __real_aligned_alloc(alignment, bytes);
    n3ds_allocation_profile_heap_event(N3DS_ALLOCATION_ALLOCATE, !result && bytes != 0, 0);
    return result;
}
