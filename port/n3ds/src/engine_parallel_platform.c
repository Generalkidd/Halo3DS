#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_parallel.h"
#include "engine_algorithm_profile.h"
void n3ds_log(const char *);
static Thread worker;
static LightEvent wake,complete;
static int initialized,stop,worker_result,worker_core=-1;
static u32 saved_limit;
static int changed_limit;
static n3ds_range_function job;
static void *job_context;
static unsigned int job_begin,job_end;
static unsigned int jobs[3],items[3];
static u64 worker_ticks,main_ticks[3],wait_ticks[3],other_ticks[3];
#ifdef HALO_N3DS_ALGORITHM_PROFILE
/* The owner publishes dispatch fields with the job; the helper publishes only
 * this job's wake delay before the existing completion event. No helper calls
 * the shared main-thread collector. Categories: audio, decode, skin/pose. */
static u64 algorithm_dispatch_tick,algorithm_wake_ticks;
static int algorithm_profile_job;
enum { PAR_SERIAL_COUNT,PAR_SERIAL_ITEMS,PAR_WORKER_COUNT,PAR_WORKER_ITEMS,
    PAR_BATCH_TICKS,PAR_PEAK_TICKS,PAR_SERIAL_TICKS,PAR_WAKE_TICKS,
    PAR_MAIN_TICKS,PAR_HELPER_TICKS,PAR_JOIN_TICKS };
#define PAR_PROFILE_IDS(kind) { \
    N3DS_ALG_IO_PAR_##kind##_SERIAL_COUNT,N3DS_ALG_IO_PAR_##kind##_SERIAL_ITEMS, \
    N3DS_ALG_IO_PAR_##kind##_WORKER_COUNT,N3DS_ALG_IO_PAR_##kind##_WORKER_ITEMS, \
    N3DS_ALG_IO_PAR_##kind##_BATCH_TICKS,N3DS_ALG_IO_PAR_##kind##_PEAK_TICKS, \
    N3DS_ALG_IO_PAR_##kind##_SERIAL_TICKS,N3DS_ALG_IO_PAR_##kind##_WAKE_TICKS, \
    N3DS_ALG_IO_PAR_##kind##_MAIN_TICKS,N3DS_ALG_IO_PAR_##kind##_HELPER_TICKS, \
    N3DS_ALG_IO_PAR_##kind##_JOIN_TICKS }
static const unsigned int algorithm_parallel_metrics[3][11]={
    PAR_PROFILE_IDS(AUDIO),PAR_PROFILE_IDS(DECODE),PAR_PROFILE_IDS(POSE)};
#undef PAR_PROFILE_IDS
static int algorithm_parallel_serial(n3ds_range_function function,void *context,
    unsigned int count,unsigned int category,u64 batch_start)
{
    if(category>=3) return function(context,0,count);
    u64 start=svcGetSystemTick();int result=function(context,0,count);
    u64 end=svcGetSystemTick();const unsigned int *m=algorithm_parallel_metrics[category];
    n3ds_algorithm_profile_add(m[PAR_SERIAL_COUNT],1);
    n3ds_algorithm_profile_add(m[PAR_SERIAL_ITEMS],count);
    n3ds_algorithm_profile_add(m[PAR_SERIAL_TICKS],end-batch_start);
    n3ds_algorithm_profile_add(m[PAR_BATCH_TICKS],end-batch_start);
    n3ds_algorithm_profile_max(m[PAR_PEAK_TICKS],end-batch_start);
    n3ds_algorithm_profile_add(m[PAR_MAIN_TICKS],end-start);
    return result;
}
#define PARALLEL_RUN_SERIAL() algorithm_parallel_serial(function,context,count,category,algorithm_batch_start)
#else
#define PARALLEL_RUN_SERIAL() function(context,0,count)
#endif
static void worker_main(void *unused)
{
    (void)unused;
    __builtin_arm_set_fpscr(0x03000000u);
    LightEvent_Signal(&complete);
    for(;;) {
        LightEvent_Wait(&wake);__sync_synchronize();
        if(stop) break;
        u64 start=svcGetSystemTick();
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        if(algorithm_profile_job)algorithm_wake_ticks=start-algorithm_dispatch_tick;
#endif
        worker_result=job(job_context,job_begin,job_end);
        worker_ticks=svcGetSystemTick()-start;
        __sync_synchronize();LightEvent_Signal(&complete);
    }
}
void n3ds_parallel_initialize(void)
{
    if(initialized) return;initialized=1;stop=0;
    LightEvent_Init(&wake,RESET_ONESHOT);LightEvent_Init(&complete,RESET_ONESHOT);
    bool newer=false;APT_CheckNew3DS(&newer);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
#ifndef HALO_N3DS_PARALLEL_SERIAL
    if(newer) {worker=threadCreate(worker_main,NULL,32768,priority,2,false);if(worker) worker_core=2;}
    if(!worker && R_SUCCEEDED(APT_GetAppCpuTimeLimit(&saved_limit))) {
        u32 requested=newer?89:30;
        if(saved_limit>requested) requested=saved_limit;
        if(R_SUCCEEDED(APT_SetAppCpuTimeLimit(requested))) {
            changed_limit=requested!=saved_limit;
            worker=threadCreate(worker_main,NULL,32768,priority,1,false);
            if(worker) worker_core=1;
        }
    }
#endif
    if(worker) {
        LightEvent_Wait(&complete);
    }
    if(!worker && changed_limit) {APT_SetAppCpuTimeLimit(saved_limit);changed_limit=0;}
    if(worker) atexit(n3ds_parallel_shutdown);
    /* threadCreate's explicit binding is authoritative. Do not use SVC 0x11
     * for validation: Azahar does not implement it and leaves arbitrary r0. */
    char message[160];snprintf(message,sizeof(message),"CPU WORKER: binding_core=%d active=%d; disjoint range jobs, synchronous lifetime fence",worker_core,worker!=NULL);n3ds_log(message);
}
void n3ds_parallel_shutdown(void)
{
    if(worker) {
        Handle handle=threadGetHandle(worker);
        stop=1;__sync_synchronize();LightEvent_Signal(&wake);
        if(svcWaitSynchronization(handle,U64_MAX)!=0) svcBreak(USERBREAK_PANIC);
        threadFree(worker);worker=NULL;
    }
    if(changed_limit) {APT_SetAppCpuTimeLimit(saved_limit);changed_limit=0;}
}
int n3ds_parallel_range(n3ds_range_function function,void *context,unsigned int count,unsigned int minimum,unsigned int category)
{
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    u64 algorithm_batch_start=category<3?svcGetSystemTick():0;
#endif
    /* Original/shared-system-core scheduling is expensive for short jobs.
     * Measured 4097-vertex poses benefit; 256/1025-vertex poses and eight
     * audio packets do not. Keep the common small batches local. */
    if(worker_core==1 && category<3) {
        /* Only large poses have demonstrated a benefit on shared core1.
         * Leave audio and vertex decoding local until measured otherwise. */
        if(category!=2) return PARALLEL_RUN_SERIAL();
        unsigned int shared_minimum=4096;
        if(minimum<shared_minimum) minimum=shared_minimum;
    }
    if(!worker || count<minimum || count<2) return PARALLEL_RUN_SERIAL();
    /* Syscore time is shared with the OS; never give it half a full-speed
     * New-model appcore's work. Dedicated core2 can take half. */
    unsigned int share=worker_core==2?count/2:count/4;
    if(!share) return PARALLEL_RUN_SERIAL();
    job=function;job_context=context;job_begin=count-share;job_end=count;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    algorithm_profile_job=category<3;
    if(algorithm_profile_job)algorithm_dispatch_tick=svcGetSystemTick();
#endif
    __sync_synchronize();LightEvent_Signal(&wake);
    u64 start=svcGetSystemTick();int result=function(context,0,job_begin);
    u64 end=svcGetSystemTick();LightEvent_Wait(&complete);__sync_synchronize();
    u64 finished=svcGetSystemTick();
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    if(category<3) {
        const unsigned int *m=algorithm_parallel_metrics[category];
        n3ds_algorithm_profile_add(m[PAR_WORKER_COUNT],1);
        n3ds_algorithm_profile_add(m[PAR_WORKER_ITEMS],count);
        n3ds_algorithm_profile_add(m[PAR_BATCH_TICKS],finished-algorithm_batch_start);
        n3ds_algorithm_profile_max(m[PAR_PEAK_TICKS],finished-algorithm_batch_start);
        n3ds_algorithm_profile_add(m[PAR_WAKE_TICKS],algorithm_wake_ticks);
        n3ds_algorithm_profile_add(m[PAR_MAIN_TICKS],end-start);
        n3ds_algorithm_profile_add(m[PAR_HELPER_TICKS],worker_ticks);
        n3ds_algorithm_profile_add(m[PAR_JOIN_TICKS],finished-end);
    }
#endif
    if(category<3) {
        main_ticks[category]+=end-start;wait_ticks[category]+=finished-end;other_ticks[category]+=worker_ticks;
        items[category]+=count;
        if(++jobs[category]%300==0) {
            char message[224];double scale=1000.0/SYSCLOCK_ARM11/300;
            snprintf(message,sizeof(message),"CPU WORKER PROFILE: category=%u jobs=300 items=%u main_ms=%.3f join_ms=%.3f worker_ms=%.3f core=%d",category,items[category],main_ticks[category]*scale,wait_ticks[category]*scale,other_ticks[category]*scale,worker_core);n3ds_log(message);
            main_ticks[category]=wait_ticks[category]=other_ticks[category]=0;items[category]=0;
        }
    }
    return result && worker_result;
}
#undef PARALLEL_RUN_SERIAL
static int test_range(void *context,unsigned int begin,unsigned int end)
{
    unsigned int *out=context;
    for(unsigned int i=begin;i<end;++i) {
        unsigned int x=i+1;
        for(unsigned int n=0;n<64;++n) x=x*1664525u+1013904223u;
        out[i]=x;
    }
    return 1;
}
int n3ds_parallel_tests(void)
{
    static unsigned int actual[1025],expected[1025];
    const unsigned int counts[]={0,1,2,3,4,7,32,255,256,1025};
    for(unsigned int repeat=0;repeat<12;++repeat) for(unsigned int i=0;i<sizeof(counts)/sizeof(counts[0]);++i) {
        memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
        test_range(expected,0,counts[i]);
        if(!n3ds_parallel_range(test_range,actual,counts[i],2,3) || memcmp(actual,expected,sizeof(actual))) return 0;
    }
    n3ds_log("PASS: CPU worker 120 range comparisons; zero/odd/tiny/large ranges and unchanged guards");return 1;
}
