/* New3DS integration additions by Kevin Boggs. */
/* Included by the GPU owner. Serial replay is the reference. The optional
 * New-model worker exclusively owns Citro3D until the next explicit join;
 * unsupported state/resource operations fence before touching its context. */
#include <malloc.h>
struct __attribute__((aligned(16))) native_render_command_header {
    n3ds_render_command_function execute;
    unsigned int size;
};
static struct native_render_command_queue {
    unsigned char *data;
    unsigned int capacity,used,count;
    unsigned int active,replaying,allocation_attempted;
    unsigned long long frame;
    unsigned long long commands,bytes;
    unsigned int peak,maximum_span;
    unsigned int drains[N3DS_RENDER_FENCE_COUNT];
    unsigned long long ticks[N3DS_RENDER_FENCE_COUNT];
} render_commands;

#ifdef HALO_N3DS_RENDER_WORKER
#ifndef HALO_N3DS_RENDER_COMMANDS
#error Render worker requires owned render commands
#endif
static Thread render_worker;
static LightEvent render_worker_wake,render_worker_done;
static int render_worker_attempted,render_worker_pending,render_worker_stop,render_worker_buffer_failed;
static unsigned char *render_worker_spare;
static unsigned int render_worker_spare_capacity,render_worker_jobs;
static unsigned long long render_worker_time,render_worker_join_time;
static struct {
    unsigned char *data;
    unsigned int bytes,count,capacity;
    unsigned long long ticks;
    int result;
} render_worker_job;
#endif

/* Kevin Boggs: render_command_on_worker */
static int render_command_on_worker(void)
{
#ifdef HALO_N3DS_RENDER_WORKER
    return render_worker && threadGetCurrent()==render_worker;
#else
    return 0;
#endif
}

/* Kevin Boggs: render_command_execute_range */
static int render_command_execute_range(const unsigned char *data,unsigned int bytes)
{
    unsigned int offset=0;
    while(offset<bytes) {
        if(bytes-offset<sizeof(struct native_render_command_header))return 0;
        const struct native_render_command_header *command=(const void *)(data+offset);
        if(command->size<sizeof(*command) || (command->size&15) || command->size>bytes-offset ||
            !command->execute || !command->execute(command+1))return 0;
        offset+=command->size;
    }
    return offset==bytes;
}

/* Kevin Boggs: render_command_account */
static void render_command_account(enum n3ds_render_fence reason,unsigned int bytes,
    unsigned int count,unsigned long long ticks)
{
    render_commands.ticks[reason]+=ticks;++render_commands.drains[reason];
    render_commands.commands+=count;render_commands.bytes+=bytes;
    if(count>render_commands.maximum_span)render_commands.maximum_span=count;
    if(bytes>render_commands.peak)render_commands.peak=bytes;
}

#ifdef HALO_N3DS_RENDER_WORKER
/* Kevin Boggs: render_command_worker_main */
static void render_command_worker_main(void *unused)
{
    (void)unused;__builtin_arm_set_fpscr(0x03000000u);
    for(;;) {
        LightEvent_Wait(&render_worker_wake);__sync_synchronize();
        if(render_worker_stop)break;
        unsigned long long start=svcGetSystemTick();
        render_worker_job.result=render_command_execute_range(render_worker_job.data,render_worker_job.bytes);
        render_worker_job.ticks=svcGetSystemTick()-start;
        __sync_synchronize();LightEvent_Signal(&render_worker_done);
    }
}
#endif

/* Kevin Boggs: n3ds_render_commands_wait */
void n3ds_render_commands_wait(void)
{
#ifdef HALO_N3DS_RENDER_WORKER
    if(render_command_on_worker() || !render_worker_pending)return;
    unsigned long long before=svcGetSystemTick();
    LightEvent_Wait(&render_worker_done);__sync_synchronize();
    render_worker_join_time+=svcGetSystemTick()-before;
    render_worker_time+=render_worker_job.ticks;++render_worker_jobs;
    render_command_account(N3DS_RENDER_FENCE_HANDOFF,render_worker_job.bytes,render_worker_job.count,render_worker_job.ticks);
    render_worker_spare=render_worker_job.data;render_worker_spare_capacity=render_worker_job.capacity;
    render_worker_job.data=NULL;render_worker_pending=0;
    if(!render_worker_job.result) {n3ds_log("RENDER COMMAND FATAL: worker command failed");abort();}
#endif
}

/* Kevin Boggs: n3ds_render_commands_submit */
int n3ds_render_commands_submit(void)
{
#ifdef HALO_N3DS_RENDER_WORKER
    if(render_command_on_worker() || render_commands.replaying || render_commands.count<8 || render_worker_buffer_failed)return 0;
    n3ds_render_commands_wait();
    if(!render_worker_attempted) {
        render_worker_attempted=1;
        extern int n3ds_input_platform_original_model(void);
        if(!n3ds_input_platform_original_model()) {
            LightEvent_Init(&render_worker_wake,RESET_ONESHOT);LightEvent_Init(&render_worker_done,RESET_ONESHOT);
            s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
            render_worker=threadCreate(render_command_worker_main,NULL,32768,priority,2,false);
            n3ds_log(render_worker?"RENDER WORKER: core2 owned-command submission enabled":"RENDER WORKER: unavailable; keeping serial replay");
        }
    }
    if(!render_worker)return 0;
    if(render_worker_spare_capacity!=render_commands.capacity) {
        free(render_worker_spare);render_worker_spare=memalign(16,render_commands.capacity);
        render_worker_spare_capacity=render_worker_spare?render_commands.capacity:0;
    }
    if(!render_worker_spare) {render_worker_buffer_failed=1;n3ds_log("RENDER WORKER: no spare command buffer; keeping serial replay");return 0;}
    render_worker_job.data=render_commands.data;render_worker_job.bytes=render_commands.used;
    render_worker_job.count=render_commands.count;render_worker_job.capacity=render_commands.capacity;
    render_commands.data=render_worker_spare;render_worker_spare=NULL;render_worker_spare_capacity=0;
    render_commands.used=render_commands.count=0;render_worker_pending=1;
    __sync_synchronize();LightEvent_Signal(&render_worker_wake);return 1;
#else
    return 0;
#endif
}

/* Kevin Boggs: n3ds_render_commands_recording */
int n3ds_render_commands_recording(void)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    if(render_command_on_worker())return 0;
    return render_commands.active && !render_commands.replaying;
#else
    return 0;
#endif
}

/* Kevin Boggs: n3ds_render_commands_replaying */
int n3ds_render_commands_replaying(void)
{
    /* Do not read the producer's replay flag from the worker. */
    return render_command_on_worker() || render_commands.replaying;
}

/* Kevin Boggs: n3ds_render_commands_kick */
void n3ds_render_commands_kick(void)
{
#ifdef HALO_N3DS_RENDER_WORKER
    if(n3ds_render_commands_recording() && render_commands.count>=32)n3ds_render_commands_submit();
#endif
}

/* Kevin Boggs: n3ds_render_commands_flush */
void n3ds_render_commands_flush(enum n3ds_render_fence reason)
{
    if(render_command_on_worker() || render_commands.replaying)return;
    n3ds_render_commands_wait();
    if(!render_commands.count)return;
    if((unsigned int)reason>=N3DS_RENDER_FENCE_COUNT)abort();
    unsigned long long before=svcGetSystemTick();
    render_commands.replaying=1;
    if(!render_command_execute_range(render_commands.data,render_commands.used)) {
        n3ds_log("RENDER COMMAND FATAL: invalid or failed owned command");abort();
    }
    render_commands.replaying=0;
    render_command_account(reason,render_commands.used,render_commands.count,svcGetSystemTick()-before);
    render_commands.used=render_commands.count=0;
}

/* Kevin Boggs: n3ds_render_command_allocate */
void *n3ds_render_command_allocate(n3ds_render_command_function execute,unsigned int bytes)
{
    if(!n3ds_render_commands_recording() || !execute)return NULL;
    if(!render_commands.allocation_attempted) {
        extern int n3ds_input_platform_original_model(void);
        render_commands.allocation_attempted=1;
        render_commands.capacity=n3ds_input_platform_original_model()?64*1024:512*1024;
        /* Citro3D matrices in payloads require 16-byte alignment. */
        render_commands.data=memalign(16,render_commands.capacity);
    }
    if(!render_commands.data)return NULL;
    if(render_commands.capacity<sizeof(struct native_render_command_header)+15 ||
       bytes>render_commands.capacity-sizeof(struct native_render_command_header)-15) {
        n3ds_render_commands_flush(N3DS_RENDER_FENCE_CAPACITY);return NULL;
    }
    unsigned int size=(sizeof(struct native_render_command_header)+bytes+15)&~15u;
    if(size>render_commands.capacity-render_commands.used)n3ds_render_commands_flush(N3DS_RENDER_FENCE_CAPACITY);
    struct native_render_command_header *command=(void *)(render_commands.data+render_commands.used);
    *command=(struct native_render_command_header){execute,size};
    render_commands.used+=size;++render_commands.count;
    return command+1;
}

/* Kevin Boggs: n3ds_render_commands_begin */
void n3ds_render_commands_begin(unsigned long long frame)
{
    n3ds_render_commands_wait();
    if(render_commands.count || render_commands.active || render_commands.replaying)abort();
    render_commands.frame=frame;render_commands.active=1;
}

/* Kevin Boggs: n3ds_render_commands_end */
void n3ds_render_commands_end(void)
{
    n3ds_render_commands_flush(N3DS_RENDER_FENCE_PRESENT);render_commands.active=0;
}

/* Kevin Boggs: n3ds_render_commands_release */
void n3ds_render_commands_release(void)
{
    n3ds_render_commands_flush(N3DS_RENDER_FENCE_LIFETIME);
#ifdef HALO_N3DS_RENDER_WORKER
    if(render_worker) {
        render_worker_stop=1;__sync_synchronize();LightEvent_Signal(&render_worker_wake);
        if(R_FAILED(threadJoin(render_worker,U64_MAX)))abort();
        threadFree(render_worker);render_worker=NULL;
    }
    free(render_worker_spare);render_worker_spare=NULL;render_worker_spare_capacity=0;
    render_worker_stop=render_worker_attempted=render_worker_buffer_failed=0;
#endif
    free(render_commands.data);render_commands.data=NULL;
    render_commands.used=render_commands.count=render_commands.capacity=0;
    render_commands.allocation_attempted=render_commands.active=0;
}

/* Kevin Boggs: n3ds_render_commands_report */
void n3ds_render_commands_report(void)
{
    n3ds_render_commands_wait();
    char line[192];
    snprintf(line,sizeof(line),"RESULT: render commands count=%llu payload_bytes=%llu peak=%u max_span=%u pending=%u",
        render_commands.commands,render_commands.bytes,render_commands.peak,render_commands.maximum_span,render_commands.count);
    n3ds_log(line);
#ifdef HALO_N3DS_RENDER_WORKER
    snprintf(line,sizeof(line),"RESULT: render worker jobs=%u replay_ms=%.4f join_ms=%.4f",render_worker_jobs,
        render_worker_time*1000./SYSCLOCK_ARM11,render_worker_join_time*1000./SYSCLOCK_ARM11);n3ds_log(line);
#endif
    for(unsigned int i=0;i<N3DS_RENDER_FENCE_COUNT;++i) {
        snprintf(line,sizeof(line),"RESULT: render fence reason=%u drains=%u replay_ms=%.4f",i,
            render_commands.drains[i],render_commands.ticks[i]*1000./SYSCLOCK_ARM11);
        n3ds_log(line);
    }
}

static unsigned int render_test_sequence,render_test_sum;
struct render_test_payload {unsigned int sequence,value;};
/* Kevin Boggs: render_command_test_execute */
static int render_command_test_execute(const void *payload)
{
    const struct render_test_payload *p=payload;
    if(((uintptr_t)payload&15) || p->sequence!=render_test_sequence++ || n3ds_render_commands_recording())return 0;
    render_test_sum+=p->value;
    /* A nested state operation during replay must neither recurse nor discard
     * the tail of the command list. */
    n3ds_render_commands_flush(N3DS_RENDER_FENCE_STATE);return 1;
}

/* Kevin Boggs: n3ds_render_commands_tests */
int n3ds_render_commands_tests(void)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    n3ds_render_commands_wait();
    if(render_commands.active || render_commands.used)return 0;
    /* A small arena forces repeated drains and an oversized-command fallback.
     * Run without GPU calls, and restore the production arena afterwards. */
    struct native_render_command_queue saved=render_commands;
    unsigned char memory[96] __attribute__((aligned(16)));memset(&render_commands,0,sizeof(render_commands));
    render_commands.data=(void *)memory;render_commands.capacity=sizeof(memory);render_commands.allocation_attempted=1;
    int ok=0;
    render_test_sequence=render_test_sum=0;n3ds_render_commands_begin(123);
    for(unsigned int i=0;i<100;++i) {
        struct render_test_payload source={i,i*3+1};
        struct render_test_payload *copy=n3ds_render_command_allocate(render_command_test_execute,sizeof(source));
        if(!copy)goto done;*copy=source;source.value=0;
    }
    if(n3ds_render_command_allocate(render_command_test_execute,0xffffffffu))goto done;
    n3ds_render_commands_end();
    ok=render_test_sequence==100 && render_test_sum==14950 && !render_commands.count && !render_commands.active;
done:
    render_commands=saved;
    if(!ok)return 0;
    n3ds_log("RESULT: PASS render command ownership, order, capacity, oversize and replay reentrancy");
#endif
    return 1;
}

/* Kevin Boggs: n3ds_render_worker_tests */
int n3ds_render_worker_tests(void)
{
#ifdef HALO_N3DS_RENDER_WORKER
    n3ds_render_commands_wait();
    if(render_commands.active || render_commands.used)return 0;
    render_test_sequence=render_test_sum=0;n3ds_render_commands_begin(456);
    for(unsigned int batch=0;batch<3;++batch) {
        for(unsigned int i=0;i<32;++i) {
            unsigned int sequence=batch*32+i;
            struct render_test_payload *copy=n3ds_render_command_allocate(render_command_test_execute,sizeof(*copy));
            if(!copy) {n3ds_render_commands_end();return 0;}
            *copy=(struct render_test_payload){sequence,sequence*3+1};
        }
        /* Fill the other buffer while the previous one may still be replaying. */
        if(!n3ds_render_commands_submit())n3ds_render_commands_flush(N3DS_RENDER_FENCE_STATE);
    }
    n3ds_render_commands_end();
    if(render_test_sequence!=96 || render_test_sum!=13776)return 0;
    char line[160];snprintf(line,sizeof(line),"RESULT: PASS render worker buffer handoff: ordered=96 checksum=13776 jobs=%u active=%d",render_worker_jobs,render_worker!=NULL);n3ds_log(line);
#endif
    return 1;
}
