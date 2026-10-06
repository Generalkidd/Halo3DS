#include <3ds.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine_diagnostics.h"

/* Original AI path states alone occupy about 112 KiB per nested caller.
 * B30's firing-position search exceeds the former 256 KiB stack (observed SP
 * below its mapped lower bound). Keep original AI behavior with a bounded
 * 1 MiB main-thread stack, including room for nested path/collision work. */
u32 __stacksize__ = 1024 * 1024;

int n3ds_engine_float_tests(void)
{
    extern void real_math_reset_precision(void);
    extern void n3ds_log(const char *);
    /* Exercise the original game's reset, including after a generic reset.
     * Volatile operands keep the exceptional arithmetic on the actual CPU. */
    __builtin_arm_set_fpscr(0);
    real_math_reset_precision();
    unsigned int control=__builtin_arm_get_fpscr();
    if(control!=0x03000000u) {n3ds_log("STARTUP FAIL: native floating-point reset lost VFP11 defaults");return 0;}
    volatile float tiny=1.e-30f,smaller=1.e-20f,zero=0.f;
    volatile float underflow=tiny*smaller,nan=zero/zero;
    if(underflow!=0.f || nan==nan) {n3ds_log("STARTUP FAIL: native floating-point exceptional arithmetic");return 0;}
    real_math_reset_precision();
    if(__builtin_arm_get_fpscr()!=0x03000000u) return 0;
    n3ds_log("PASS: native floating-point reset FPSCR=03000000; underflow flush and NaN comparison completed");
    return 1;
}

long long n3ds_engine_ticks(void) { return (long long)svcGetSystemTick(); }
long long n3ds_engine_tick_frequency(void) { return SYSCLOCK_ARM11; }
int n3ds_engine_app_running(void) { return aptMainLoop(); }
float n3ds_platform_3d_slider(void) { return osGet3DSliderState(); }
int n3ds_input_platform_original_model(void)
{
    static int original=-1;
    bool new_model=true;
    /* Model does not change during a process. Avoid service IPC in rendering. */
    if(original<0) original=R_SUCCEEDED(APT_CheckNew3DS(&new_model)) && !new_model;
    return original;
}
void n3ds_engine_pace_frame(long long start)
{
    s64 elapsed=(s64)svcGetSystemTick()-start;
    s64 remaining=(s64)SYSCLOCK_ARM11/30-elapsed;
    if(remaining>0) svcSleepThread(remaining*1000000000LL/SYSCLOCK_ARM11);
}

void *n3ds_state_allocate(unsigned int bytes) { return linearMemAlign(bytes, 4096); }
void n3ds_state_release(void *pointer) { linearFree(pointer); }
void *n3ds_cache_linear_allocate(unsigned long bytes) { return linearAlloc(bytes); }
unsigned long n3ds_cache_heap_allocation_size(unsigned long bytes)
{
    /* A 5.15 MiB C10 BSP and 5.26 MiB C40 BSP can reuse a 5.5 MiB
     * block. Exact allocations left the old block just too small on reload,
     * even with over 10 MiB free across fragmented original-model heaps.
     * Keep the same heap budget and leave New models/small caches unchanged. */
    if(n3ds_input_platform_original_model() && bytes>=4U*1024*1024 && bytes<=0x1600000U)
        return (bytes+0x7ffffU)&~0x7ffffU;
    return bytes;
}
int n3ds_cache_prefer_linear(unsigned long bytes)
{ return n3ds_input_platform_original_model() && bytes>=8U*1024*1024 &&
    /* Keep the same five-MiB GPU remainder already exercised by A30's linear
     * fallback. A six-MiB threshold could let the map barely fit the CPU heap,
     * leaving no room for relocations, BSP data or subsequent model work. */
    linearSpaceFree()>=bytes+5U*1024*1024; }
void n3ds_cache_linear_release(void *pointer) { linearFree(pointer); }
void n3ds_state_flush(void *pointer, unsigned int bytes) {
    extern void n3ds_object_light_cache_reset(void);
    n3ds_object_light_cache_reset(); /* restored cluster references belong to the snapshot */
    GSPGPU_FlushDataCache(pointer, bytes);
}
int n3ds_state_make_directory(void)
{
    if (mkdir("sdmc:/halo-source", 0777) && errno != EEXIST) return -1;
    if (mkdir("sdmc:/halo-source/state", 0777) && errno != EEXIST) return -1;
    return 0;
}

void n3ds_engine_sleep_milliseconds(long milliseconds)
{
    if (milliseconds > 0) svcSleepThread((s64)milliseconds * 1000000);
}
void n3ds_engine_sleep(unsigned int milliseconds)
{
    if(milliseconds==0xffffffffU) {
        for(;;) svcSleepThread(1000000000LL);
    }
    /* Zero yields the current timeslice; convert before multiplication. */
    svcSleepThread((s64)milliseconds*1000000LL);
}

unsigned long system_milliseconds(void)
{
    u64 ticks = svcGetSystemTick(), frequency = SYSCLOCK_ARM11;
    return (unsigned long)((ticks/frequency)*1000 + (ticks%frequency)*1000/frequency);
}
/* Halo uses epoch seconds for seeding, separate from monotonic milliseconds. */
unsigned long system_seconds(void) { return (unsigned long)time(NULL); }
void n3ds_engine_console_clear(void) { consoleClear(); }
int n3ds_engine_read_cfg_language(void)
{
    u8 language=0;
    if(R_FAILED(cfguInit())) return -1;
    Result result=CFGU_GetSystemLanguage(&language);
    cfguExit();
    return R_SUCCEEDED(result)?(int)language:-1;
}
unsigned long n3ds_engine_available_memory(void)
{
    struct native_memory_snapshot state;
    n3ds_engine_memory_snapshot(&state);
    /* Include uncommitted pages only when both region reports agree. Otherwise
     * return the known free arena space, not a wrapped near-4GiB estimate. */
    return state.application_free + state.heap_free + state.linear_free;
}
void n3ds_engine_memory_snapshot(struct native_memory_snapshot *result)
{
    struct mallinfo heap=mallinfo();
    s64 used=0;
    Result status=svcGetSystemInfo(&used,0,MEMREGION_APPLICATION);
    memset(result,0,sizeof(*result));
    result->heap_used=heap.uordblks>0?(unsigned int)heap.uordblks:0;
    result->heap_free=heap.fordblks>0?(unsigned int)heap.fordblks:0;
    result->linear_free=(unsigned int)linearSpaceFree();
    result->application_size=(unsigned int)osGetMemRegionSize(MEMREGION_APPLICATION);
    result->application_query_ok=R_SUCCEEDED(status) && used>=0 && (u64)used<=0xffffffffULL;
    if(result->application_query_ok) {
        result->application_used=(unsigned int)used;
        if(result->application_used<=result->application_size) {
            result->application_free_valid=1;
            result->application_free=result->application_size-result->application_used;
        }
    }
}

/* Dormant frame watchdog: no SD I/O unless a frame stage stops advancing.
 * Use its own file/stream so a blocked main-thread logger is not required. */
static Thread stall_thread;
static volatile int stall_stop;
static volatile u32 stall_tick,stall_frame;
static const char *volatile stall_stage;
static volatile int stall_game_time;
void n3ds_stall_mark(const char *stage,unsigned int frame,int game_time)
{
    stall_stage=stage;stall_frame=frame;stall_game_time=game_time;
    __asm__ volatile("" ::: "memory");stall_tick=(u32)svcGetSystemTick();
}
static void stall_watch(void *unused)
{
    (void)unused;u32 reported=0;
    while(!stall_stop) {
        svcSleepThread(500000000LL);
        u32 at=stall_tick,now=(u32)svcGetSystemTick();
        /* Unsigned low ticks wrap every 16 s; sample twice/sec, threshold 5 s.
         * Report a stage once; loading/SD stalls can be slow rather than fatal. */
        if(stall_stage && at!=reported && now-at>5U*SYSCLOCK_ARM11) {
            const char *stage=stall_stage;u32 frame=stall_frame;int game_time=stall_game_time;
            if(at!=stall_tick) continue;
            FILE *file=fopen("sdmc:/halo-source-stall.log","a");
            if(file) {
                fprintf(file,"Frame progress stalled >5s: frame=%lu time=%d stage=%s (may be slow I/O, not necessarily a crash)\n",(unsigned long)frame,game_time,stage);
                fclose(file);
            }
            reported=at;
        }
    }
}
void n3ds_stall_watch_stop(void);
void n3ds_stall_watch_start(void)
{
    if(stall_thread) return;
    stall_stop=0;stall_stage=NULL;stall_tick=0;
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    stall_thread=threadCreate(stall_watch,NULL,16384,priority>0x18?priority-1:priority,-2,false);
    if(stall_thread) atexit(n3ds_stall_watch_stop);
}
void n3ds_stall_watch_stop(void)
{
    if(!stall_thread) return;
    Handle handle=threadGetHandle(stall_thread);
    stall_stop=1;
    if(svcWaitSynchronization(handle,U64_MAX)!=0) svcBreak(USERBREAK_PANIC);
    threadFree(stall_thread);stall_thread=NULL;stall_stage=NULL;
}

unsigned long n3ds_cache_heap_headroom(void)
{
    extern u32 __ctru_heap_size;
    struct mallinfo memory=mallinfo();
    return memory.uordblks<__ctru_heap_size?__ctru_heap_size-memory.uordblks:0;
}
