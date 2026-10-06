/* Match the raw libc ownership used by runtime.c's debug_malloc/debug_free.
 * Xbox debug headers and random-fill diagnostics are not present on this heap. */
#define BUILDING_CSERIES
#include "cseries.h"

void *debug_realloc(void *pointer,unsigned int size,const char *file,long line)
{
    if ((!pointer && !size) || size>=0x10000000U) {
        display_assert("debug_realloc requires pointer or size, and size < 256 MiB",(char *)file,line,TRUE);
        halt_and_catch_fire();
        return NULL;
    }
    /* C libraries differ on realloc(p,0); original engine requires NULL after
     * freeing, including dynamic_array_delete's empty-state invariant. */
    if (!size) { free(pointer); return NULL; }
    /* A failed nonzero realloc leaves the caller's original allocation intact.
     * Unlike debug_malloc, original resize callers can handle a NULL result. */
    return realloc(pointer,size);
}
