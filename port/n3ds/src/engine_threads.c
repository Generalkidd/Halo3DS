#include "cseries.h"
#include "bungie_net/common/thread.h"
#include "engine_threads.h"
struct engine_thread_start { unsigned long (__stdcall *function)(void *); void *argument; };
static unsigned int run_engine_thread(void *argument)
{
    struct engine_thread_start copy=*(struct engine_thread_start *)argument;
    free(argument);
    return (unsigned int)copy.function(copy.argument);
}
boolean create_thread(word flags,unsigned long (__stdcall *function)(void *),void *argument,struct thread_reference **reference)
{
    struct engine_thread_start *start;
    unsigned int token;
    assert(function && reference);
    *reference=NULL;
    start=malloc(sizeof(*start)); if(!start) return FALSE;
    *start=(struct engine_thread_start){function,argument};
    token=n3ds_thread_create(flags,run_engine_thread,start);
    if(!token) { free(start); return FALSE; }
    /* References are opaque tokens, never dereferenced by engine callers. */
    *reference=(struct thread_reference *)(unsigned long)token;
    return TRUE;
}
boolean thread_has_exited(struct thread_reference *reference)
{
    return n3ds_thread_exited((unsigned long)reference);
}
void dispose_thread(struct thread_reference *reference)
{
    assert(n3ds_thread_close((unsigned long)reference));
}
boolean create_mutex(struct mutex_reference **reference)
{
    assert(reference);
    *reference=(struct mutex_reference *)(unsigned long)n3ds_mutex_create();
    return *reference!=NULL;
}
boolean take_mutex(struct mutex_reference *reference,unsigned long timeout_ms)
{
    return n3ds_mutex_take((unsigned long)reference,timeout_ms);
}
void release_mutex(struct mutex_reference *reference)
{
    assert(n3ds_mutex_release((unsigned long)reference));
}
void dispose_mutex(struct mutex_reference *reference)
{
    assert(n3ds_mutex_close((unsigned long)reference));
}
