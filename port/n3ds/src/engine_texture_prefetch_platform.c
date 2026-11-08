#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "engine_texture_prefetch.h"

static Thread reader;
static Handle reader_thread;
static LightEvent work;
static int stop, cancelled, busy, ready, success, exit_registered;
static struct {
    char path[128];
    struct native_texture_read_range ranges[NATIVE_TEXTURE_READ_RANGES];
    unsigned int count;
    n3ds_texture_prepare_fn prepare;
    void *context;
} job;
static struct native_texture_read_result stats;

static void reader_main(void *unused)
{
    (void)unused;
    __builtin_arm_set_fpscr(0x03000000u);
    char path[sizeof(job.path)] = {0};
    Handle file = 0;
    for (;;) {
        LightEvent_Wait(&work);
        if (__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) break;
        if (!__atomic_load_n(&busy, __ATOMIC_ACQUIRE)) continue;
        long long start = svcGetSystemTick();
        if (!file || strcmp(path, job.path)) {
            if (file) FSFILE_Close(file);
            file = 0;
            if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC,
                    fsMakePath(PATH_EMPTY, NULL), fsMakePath(PATH_ASCII, job.path + 5), FS_OPEN_READ, 0)))
                strcpy(path, job.path);
            else file = 0;
        }
        unsigned int total=0,completed=0;
        for(unsigned int i=0;i<job.count;++i) {
            const struct native_texture_read_range *range=&job.ranges[i];
            unsigned int done=0;
            while(file && done<range->bytes && !__atomic_load_n(&cancelled,__ATOMIC_ACQUIRE)) {
                unsigned int count=range->bytes-done;
                if(count>32*1024) count=32*1024;
                u32 got=0;
                if(R_FAILED(FSFILE_Read(file,&got,(u64)range->offset+done,(unsigned char *)range->destination+done,count)) ||
                   !got || got>count) {
                    FSFILE_Close(file);file=0;break;
                }
                done+=got;
            }
            total+=done;
            if(done!=range->bytes) break;
            ++completed;
        }
        stats.bytes = total;
        stats.read_ticks = svcGetSystemTick() - start;
        stats.prepare_ticks = 0;
        success = 0;
        if (completed == job.count && !__atomic_load_n(&cancelled, __ATOMIC_ACQUIRE)) {
            start = svcGetSystemTick();
            success = job.prepare(job.context);
            stats.prepare_ticks = svcGetSystemTick() - start;
        }
        __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
    }
    if (file) FSFILE_Close(file);
}

int n3ds_texture_reader_submit_ranges(const char *path,const struct native_texture_read_range *ranges,
    unsigned int count,n3ds_texture_prepare_fn prepare,void *context)
{
    if (__atomic_load_n(&busy, __ATOMIC_RELAXED) || !path || strncmp(path, "sdmc:/", 6) ||
            strlen(path) >= sizeof(job.path) || !ranges || !count || count>NATIVE_TEXTURE_READ_RANGES || !prepare) return 0;
    unsigned int total=0;
    for(unsigned int i=0;i<count;++i) {
        const struct native_texture_read_range *range=&ranges[i];
        if(!range->bytes || range->bytes>NATIVE_TEXTURE_READ_LIMIT-total ||
           range->offset>~0u-range->bytes || !range->destination) return 0;
        total+=range->bytes;
    }
    if (!reader) {
        LightEvent_Init(&work, RESET_ONESHOT);
        __atomic_store_n(&stop, 0, __ATOMIC_RELEASE);
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        if (priority < 0x3d) priority += 2;
        /* Geometry/audio jobs have higher priority on the same New-model
         * helper core. An app without access to core 2 can still read ahead. */
        reader = threadCreate(reader_main, NULL, 24 * 1024, priority, 2, false);
        if (!reader) reader = threadCreate(reader_main, NULL, 24 * 1024, priority, -2, false);
        if (!reader) return 0;
        reader_thread = threadGetHandle(reader);
        if (!exit_registered) {
            atexit(n3ds_texture_reader_shutdown);
            exit_registered = 1;
        }
    }
    strcpy(job.path, path);
    memcpy(job.ranges,ranges,count*sizeof(*ranges));job.count=count;
    job.prepare = prepare;
    job.context = context;
    __atomic_store_n(&cancelled, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&ready, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&busy, 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&work);
    return 1;
}
int n3ds_texture_reader_submit(const char *path,unsigned int offset,
    unsigned int bytes,void *destination,n3ds_texture_prepare_fn prepare,void *context)
{
    const struct native_texture_read_range range={offset,bytes,destination};
    return n3ds_texture_reader_submit_ranges(path,&range,1,prepare,context);
}

int n3ds_texture_reader_poll(struct native_texture_read_result *result)
{
    if (!__atomic_load_n(&busy, __ATOMIC_RELAXED) || !__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return 0;
    *result = stats;
    __atomic_store_n(&busy, 0, __ATOMIC_RELAXED);
    return success ? 1 : -1;
}

void n3ds_texture_reader_cancel(void)
{
    __atomic_store_n(&cancelled, 1, __ATOMIC_RELEASE);
}
int n3ds_texture_reader_cancelled(void)
{return __atomic_load_n(&cancelled,__ATOMIC_ACQUIRE);}

void n3ds_texture_reader_shutdown(void)
{
    if (!reader) return;
    n3ds_texture_reader_cancel();
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    LightEvent_Signal(&work);
    if (svcWaitSynchronization(reader_thread, U64_MAX) != 0) svcBreak(USERBREAK_PANIC);
    threadFree(reader);
    reader = NULL;
    reader_thread = 0;
    __atomic_store_n(&busy, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&ready, 0, __ATOMIC_RELAXED);
}
