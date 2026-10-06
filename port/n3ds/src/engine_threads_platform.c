#include <3ds.h>
#include <string.h>
#include "engine_threads.h"
#include "engine_files.h"

enum { SLOTS=32, MAX_GENERATION=0x03ffffff, STACK_BYTES=0x4000 };
struct native_thread {
    Thread thread; Handle handle; LightEvent start;
    unsigned int generation,result;
    unsigned int (*entry)(void *); void *argument;
    int published,completed;
};
struct native_mutex { Handle handle; unsigned int generation,owner,depth,waiters; };
static struct native_thread threads[SLOTS];
static struct native_mutex mutexes[SLOTS];
static LightLock table_lock; /* newlib/ctru's zero initializer */
static s32 normal_priority;
static unsigned int token(unsigned int generation,unsigned int index,unsigned int mutex)
{ return (generation<<6)|(index<<1)|mutex; }
static struct native_thread *get_thread(unsigned int value)
{
    struct native_thread *t=&threads[(value>>1)&31];
    return value && !(value&1) && t->published && (t->thread || t->completed) && t->generation==(value>>6) ? t : NULL;
}
static struct native_mutex *get_mutex(unsigned int value)
{
    struct native_mutex *m=&mutexes[(value>>1)&31];
    return (value&1) && m->handle && m->generation==(value>>6) ? m : NULL;
}
static unsigned int current_id(void)
{
    u32 id=0;
    if(R_FAILED(svcGetThreadId(&id,CUR_THREAD_HANDLE))) svcBreak(USERBREAK_PANIC);
    return id;
}
static void collect_locked(void)
{
    for(unsigned int i=0;i<SLOTS;++i) {
        struct native_thread *t=&threads[i];
        if(t->thread && svcWaitSynchronization(t->handle,0)==0) {
            /* Kernel completion, not libctru's earlier 'finished' flag, is the fence. */
            threadFree(t->thread); t->thread=NULL; t->handle=0;
            /* A published token retains the result, not the finished worker's
             * stack/TLS/reent heap allocations. Save/profile callers may keep
             * their token through several maps before requesting disposal. */
            t->completed=1;
            t->entry=NULL; t->argument=NULL;
        }
    }
}
void n3ds_threads_collect(void)
{ LightLock_Lock(&table_lock); collect_locked(); LightLock_Unlock(&table_lock); }
static void entrypoint(void *argument)
{
    struct native_thread *t=argument;
    /* Keep the thread alive until its real kernel handle has been captured. */
    LightEvent_Wait(&t->start);
    unsigned int result=t->entry(t->argument),id=current_id();
    n3ds_files_thread_exit();
    LightLock_Lock(&table_lock);
    /* Original take_mutex treats abandoned ownership as successful acquisition.
     * Native worker exit releases its locks; recursive levels share one kernel
     * acquisition. No waiter can observe a stale owner record. */
    for(unsigned int i=0;i<SLOTS;++i) if(mutexes[i].handle && mutexes[i].owner==id && mutexes[i].depth) {
        if(R_FAILED(svcReleaseMutex(mutexes[i].handle))) svcBreak(USERBREAK_PANIC);
        mutexes[i].owner=mutexes[i].depth=0;
    }
    t->result=result;
    LightLock_Unlock(&table_lock);
    /* libctru frees the embedded reent structure with the stack, but not its
     * lazily allocated numeric-conversion scratch. A profile worker's float
     * formatting would leave these blocks pinned between large map buffers.
     * Standard FILE pointers are inherited/shared, and engine file ownership
     * is handled above: do not let newlib cleanup close those shared streams. */
    struct _reent *reent=__getreent();
    reent->__cleanup=NULL;
    _reclaim_reent(reent);
}
unsigned int n3ds_thread_create(unsigned int flags,unsigned int (*entry)(void *),void *argument)
{
    unsigned int result=0;
    if(!entry) return 0;
    LightLock_Lock(&table_lock); collect_locked();
    if(!normal_priority && R_FAILED(svcGetThreadPriority(&normal_priority,CUR_THREAD_HANDLE))) normal_priority=0x30;
    int priority=normal_priority+((flags&2)?2:(flags&4)?-2:0);
    if(priority<0x18) priority=0x18;
    if(priority>0x3f) priority=0x3f;
    for(unsigned int i=0;i<SLOTS;++i) if(!threads[i].thread && !threads[i].published && threads[i].generation<MAX_GENERATION) {
        struct native_thread *t=&threads[i];
        LightEvent_Init(&t->start,RESET_STICKY);
        t->entry=entry; t->argument=argument; t->result=0;t->completed=0;
        t->thread=threadCreate(entrypoint,t,STACK_BYTES,priority,-2,false);
        if(!t->thread) { t->entry=NULL; t->argument=NULL; break; }
        t->handle=threadGetHandle(t->thread);
        if(!t->handle || t->handle==~0U) svcBreak(USERBREAK_PANIC);
        ++t->generation; t->published=1;
        result=token(t->generation,i,0);
        LightEvent_Signal(&t->start);
        break;
    }
    LightLock_Unlock(&table_lock); return result;
}
int n3ds_thread_result(unsigned int value,unsigned int *result)
{
    int exited=0;
    LightLock_Lock(&table_lock);
    collect_locked();
    struct native_thread *t=get_thread(value);
    /* Timeout (0x09401bfe) is a positive status, not R_FAILED. */
    if(t && t->completed) {
        if(result) *result=t->result;
        exited=1;
    }
    LightLock_Unlock(&table_lock); return exited;
}
int n3ds_thread_exited(unsigned int value) { return n3ds_thread_result(value,NULL); }
int n3ds_thread_close(unsigned int value)
{
    int success=0;
    LightLock_Lock(&table_lock);
    struct native_thread *t=get_thread(value);
    if(t) { t->published=0; success=1; }
    collect_locked();
    LightLock_Unlock(&table_lock); return success;
}
unsigned int n3ds_mutex_create(void)
{
    unsigned int result=0;
    LightLock_Lock(&table_lock);
    for(unsigned int i=0;i<SLOTS;++i) if(!mutexes[i].handle && mutexes[i].generation<MAX_GENERATION) {
        struct native_mutex *m=&mutexes[i];
        Handle handle;
        if(R_FAILED(svcCreateMutex(&handle,false))) break;
        m->handle=handle; m->owner=m->depth=m->waiters=0; ++m->generation;
        result=token(m->generation,i,1); break;
    }
    LightLock_Unlock(&table_lock); return result;
}
int n3ds_mutex_take(unsigned int value,unsigned int timeout_ms)
{
    unsigned int id=current_id();
    LightLock_Lock(&table_lock);
    struct native_mutex *m=get_mutex(value);
    if(!m) { LightLock_Unlock(&table_lock); return 0; }
    if(m->owner==id && m->depth) {
        int success=m->depth!=~0U; if(success) ++m->depth;
        LightLock_Unlock(&table_lock); return success;
    }
    ++m->waiters; Handle handle=m->handle;
    LightLock_Unlock(&table_lock);
    s64 timeout=timeout_ms==~0U ? -1 : (s64)timeout_ms*1000000;
    Result result=svcWaitSynchronization(handle,timeout);
    LightLock_Lock(&table_lock); --m->waiters;
    if(result==0) { m->owner=id; m->depth=1; }
    LightLock_Unlock(&table_lock); return result==0;
}
int n3ds_mutex_release(unsigned int value)
{
    unsigned int id=current_id(); int success=0;
    LightLock_Lock(&table_lock);
    struct native_mutex *m=get_mutex(value);
    if(m && m->owner==id && m->depth) {
        if(m->depth>1) { --m->depth; success=1; }
        else if(R_SUCCEEDED(svcReleaseMutex(m->handle))) { m->depth=m->owner=0; success=1; }
    }
    LightLock_Unlock(&table_lock); return success;
}
int n3ds_mutex_close(unsigned int value)
{
    int success=0;
    LightLock_Lock(&table_lock);
    struct native_mutex *m=get_mutex(value);
    /* Reclamation requires no owner or in-flight waiter. Reject misuse without
     * destroying the mutex underneath a blocked call or orphaning a lock. */
    if(m && !m->depth && !m->waiters && R_SUCCEEDED(svcCloseHandle(m->handle))) {
        m->handle=0; success=1;
    }
    LightLock_Unlock(&table_lock); return success;
}
unsigned int n3ds_thread_count(void)
{
    unsigned int count=0;
    LightLock_Lock(&table_lock); collect_locked();
    for(unsigned int i=0;i<SLOTS;++i) count+=threads[i].thread!=NULL;
    LightLock_Unlock(&table_lock); return count;
}
unsigned int n3ds_mutex_count(void)
{
    unsigned int count=0;
    LightLock_Lock(&table_lock);
    for(unsigned int i=0;i<SLOTS;++i) count+=mutexes[i].handle!=0;
    LightLock_Unlock(&table_lock); return count;
}
