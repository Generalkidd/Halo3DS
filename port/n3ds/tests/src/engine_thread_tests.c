#include "cseries.h"
#include "bungie_net/common/thread.h"
#include "engine_threads.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
struct work_state { struct mutex_reference *mutex; unsigned int entered,done,counter,errors,elapsed; int mode; };
static unsigned long __stdcall worker(void *argument)
{
    struct work_state *s=argument;
    __atomic_fetch_add(&s->entered,1,__ATOMIC_RELEASE);
    if(s->mode==0) {
        if(n3ds_mutex_release((unsigned long)s->mutex)) ++s->errors;
        if(take_mutex(s->mutex,0)) { ++s->errors; release_mutex(s->mutex); }
        long long start=n3ds_engine_ticks();
        if(take_mutex(s->mutex,20)) { ++s->errors; release_mutex(s->mutex); }
        s->elapsed=(unsigned int)((n3ds_engine_ticks()-start)*1000/n3ds_engine_tick_frequency());
        if(s->elapsed<10 || s->elapsed>2000) ++s->errors;
        return 0x103; /* A result equal to STILL_ACTIVE must not hide kernel exit. */
    }
    if(s->mode==2) {
        if(!take_mutex(s->mutex,~0UL) || !take_mutex(s->mutex,0)) return 0;
        return 0x8000000fUL; /* Deliberately abandon both recursive levels. */
    }
    for(unsigned int i=0;i<(s->mode==3?256U:1U);++i) {
        if(!take_mutex(s->mutex,~0UL)) { __atomic_fetch_add(&s->errors,1,__ATOMIC_RELAXED); break; }
        ++s->counter;
        release_mutex(s->mutex);
    }
    __atomic_fetch_add(&s->done,1,__ATOMIC_RELEASE);
    return 0x12345678UL;
}
static int wait_thread(struct thread_reference *thread)
{
    long long end=n3ds_engine_ticks()+3*n3ds_engine_tick_frequency();
    while(!thread_has_exited(thread)) {
        if(n3ds_engine_ticks()>end) return 0;
        n3ds_engine_sleep_milliseconds(1);
    }
    return 1;
}
static int wait_entered(struct work_state *state,unsigned int count)
{
    long long end=n3ds_engine_ticks()+3*n3ds_engine_tick_frequency();
    while(__atomic_load_n(&state->entered,__ATOMIC_ACQUIRE)<count) {
        if(n3ds_engine_ticks()>end) return 0;
        n3ds_engine_sleep_milliseconds(1);
    }
    return 1;
}
int halo_engine_thread_tests(void)
{
    struct mutex_reference *mutex=NULL,*mutexes[32]={0},*extra_mutex=NULL;
    struct thread_reference *thread=NULL,*old_thread=NULL,*threads[32]={0},*extra_thread=NULL;
    struct work_state state={0}; unsigned int result;
#define CHECK(x) do { if(!(x)) { n3ds_log("THREAD ENGINE FAIL: " #x); return 1; } } while(0)
    CHECK(!n3ds_thread_count() && !n3ds_mutex_count());
    CHECK(create_mutex(&mutex)); state.mutex=mutex;
    CHECK(take_mutex(mutex,0) && take_mutex(mutex,0));
    CHECK(!n3ds_mutex_close((unsigned long)mutex));
    CHECK(create_thread(2,worker,&state,&thread)); CHECK(wait_thread(thread));
    CHECK(n3ds_thread_result((unsigned long)thread,&result) && result==0x103 && !state.errors);
    /* The logical result survives physical stack/TLS reclamation. */
    n3ds_threads_collect();CHECK(!n3ds_thread_count());
    CHECK(n3ds_thread_result((unsigned long)thread,&result) && result==0x103);
    CHECK(!n3ds_mutex_take((unsigned long)thread,0));
    old_thread=thread; dispose_thread(thread);
    CHECK(!thread_has_exited(old_thread) && !n3ds_thread_close((unsigned long)old_thread));
    release_mutex(mutex); /* One recursive level remains locked. */
    state.entered=state.errors=0;
    CHECK(create_thread(4,worker,&state,&thread) && thread!=old_thread);
    CHECK(wait_thread(thread) && !state.errors); dispose_thread(thread);
    release_mutex(mutex);
    n3ds_log("PASS: native workers, recursive mutexes, zero/timed waits, owner checks and kernel-completion exit status");
    state=(struct work_state){.mutex=mutex,.mode=3};
    for(unsigned int i=0;i<4;++i) CHECK(create_thread(i&1?2:4,worker,&state,&threads[i]));
    for(unsigned int i=0;i<4;++i) { CHECK(wait_thread(threads[i])); dispose_thread(threads[i]); }
    CHECK(state.counter==1024 && !state.errors && state.done==4);
    state=(struct work_state){.mutex=mutex,.mode=1};
    CHECK(take_mutex(mutex,0));
    CHECK(create_thread(0,worker,&state,&thread) && wait_entered(&state,1));
    CHECK(!thread_has_exited(thread));
    old_thread=thread; dispose_thread(thread);
    CHECK(!thread_has_exited(old_thread) && n3ds_thread_count()==1);
    release_mutex(mutex);
    {
        long long end=n3ds_engine_ticks()+3*n3ds_engine_tick_frequency();
        while(n3ds_thread_count()) { CHECK(n3ds_engine_ticks()<end); n3ds_engine_sleep_milliseconds(1); }
    }
    CHECK(__atomic_load_n(&state.done,__ATOMIC_ACQUIRE)==1 && state.counter==1 && !state.errors);
    state=(struct work_state){.mutex=mutex,.mode=2};
    CHECK(create_thread(0,worker,&state,&thread) && wait_thread(thread));
    CHECK(n3ds_thread_result((unsigned long)thread,&result) && result==0x8000000fU);
    dispose_thread(thread);
    CHECK(take_mutex(mutex,0)); release_mutex(mutex);
    n3ds_log("PASS: 1024 contended updates, infinite wait, close while running and abandoned mutex recovery");
    state=(struct work_state){.mutex=mutex,.mode=1};
    CHECK(take_mutex(mutex,0));
    for(unsigned int i=0;i<32;++i) CHECK(create_thread(0,worker,&state,&threads[i]));
    CHECK(wait_entered(&state,32) && n3ds_thread_count()==32);
    CHECK(!create_thread(0,worker,&state,&extra_thread) && !extra_thread);
    dispose_thread(threads[0]);
    CHECK(!create_thread(0,worker,&state,&extra_thread) && !extra_thread);
    release_mutex(mutex);
    for(unsigned int i=1;i<32;++i) { CHECK(wait_thread(threads[i])); dispose_thread(threads[i]); }
    {
        long long end=n3ds_engine_ticks()+3*n3ds_engine_tick_frequency();
        while(n3ds_thread_count()) { CHECK(n3ds_engine_ticks()<end); n3ds_engine_sleep_milliseconds(1); }
    }
    CHECK(state.counter==32 && state.done==32 && !state.errors);
    /* Completed, still-published tokens reserve their slots until closed.
     * They must not retain native worker memory or alias a new generation. */
    state=(struct work_state){.mutex=mutex,.mode=1};
    for(unsigned int i=0;i<32;++i) CHECK(create_thread(0,worker,&state,&threads[i]));
    for(unsigned int i=0;i<32;++i) CHECK(wait_thread(threads[i]));
    n3ds_threads_collect();CHECK(!n3ds_thread_count());
    CHECK(!create_thread(0,worker,&state,&extra_thread) && !extra_thread);
    for(unsigned int i=0;i<32;++i) {
        CHECK(n3ds_thread_result((unsigned long)threads[i],&result) && result==0x12345678U);
        dispose_thread(threads[i]);
    }
    CHECK(state.counter==32 && state.done==32 && !state.errors);
    n3ds_log("PASS: completed worker storage reclaimed while 32 stable result tokens remain queryable until disposal");
    dispose_mutex(mutex);
    for(unsigned int i=0;i<32;++i) CHECK(create_mutex(&mutexes[i]));
    CHECK(n3ds_mutex_count()==32 && !create_mutex(&extra_mutex) && !extra_mutex);
    CHECK(!take_mutex(mutex,0) && !n3ds_mutex_close((unsigned long)mutex));
    CHECK(!n3ds_thread_close((unsigned long)mutexes[0]));
    for(unsigned int i=0;i<32;++i) dispose_mutex(mutexes[i]);
    CHECK(!n3ds_thread_count() && !n3ds_mutex_count());
    n3ds_log("PASS: 32-worker and 32-mutex budgets, stale/wrong-kind references and complete kernel resource reclamation");
    return 0;
}
