#define BUILDING_CSERIES
#include "cseries.h"
#include "engine_diagnostics.h"
void n3ds_log(const char *message);

void debug_dump_memory(void)
{
    struct native_memory_snapshot state;
    char message[320];
    n3ds_engine_memory_snapshot(&state);
    snprintf(message,sizeof(message),"NATIVE MEMORY: heap_used=%u heap_free=%u linear_free=%u application_free=%u application_size=%u application_used=%u query_ok=%u free_valid=%u bytes",
        state.heap_used,state.heap_free,state.linear_free,state.application_free,
        state.application_size,state.application_used,state.application_query_ok,state.application_free_valid);
    n3ds_log(message);
    if(!state.application_free_valid)
        n3ds_log("Native uncommitted application memory unavailable: kernel region reports failed or inconsistent; zero above is excluded from available-memory estimate");
}
void debug_dump_memory_for_file(const char *file)
{
    if(file) {
        char message[192];
        snprintf(message,sizeof(message),"Native memory source filter unavailable (%.96s): allocator has no file/line metadata; following totals are unfiltered",file);
        n3ds_log(message);
    }
    debug_dump_memory();
}
void debug_dump_memory_by_file(void)
{
    n3ds_log("Native memory grouping by source unavailable: allocator has no file/line metadata; following totals are unfiltered");
    debug_dump_memory();
}
