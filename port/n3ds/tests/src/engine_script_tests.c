#include "cseries.h"
#include "data.h"
#include "hs.h"
#include "profile.h"

void n3ds_log(const char *message);
int halo_engine_hs_cast_tests(void);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
void n3ds_engine_sleep_milliseconds(long milliseconds);
extern struct data_array *hs_thread_data, *hs_global_data;
extern const short hs_external_global_count;

int halo_engine_script_tests(void)
{
    static struct profile_section outer = {"native outer", NONE, TRUE};
    static struct profile_section inner = {"native inner", NONE, TRUE};
    long i, cast_result;
    long long start, end;
    char message[160];
#define CHECK(expr) do { if (!(expr)) { n3ds_log("SCRIPT SUPPORT FAIL: " #expr); return 1; } } while (0)
    cast_result = halo_engine_hs_cast_tests();
    if (cast_result) {
        snprintf(message, sizeof(message), "SCRIPT ABI FAIL at validation line %ld", cast_result);
        n3ds_log(message);
        return 1;
    }
    n3ds_log("PASS: original script value conversions through typed ARM callbacks");
    hs_runtime_initialize();
    CHECK(hs_thread_data && hs_thread_data->maximum_count == 256 && hs_thread_data->count == 0 && hs_thread_data->size == 0x218);
    CHECK(hs_global_data && hs_global_data->valid && hs_global_data->actual_count == hs_external_global_count);
    for (i = 0; i < hs_external_global_count; ++i) {
        struct datum_header *global = datum_get(hs_global_data, DATUM_INDEX_NEW(i, 0xaced));
        CHECK((unsigned short)global->identifier == 0xaced);
    }
    snprintf(message, sizeof(message), "PASS: original scripting startup allocated %d external globals and 256 thread slots", hs_external_global_count);
    n3ds_log(message);
    hs_runtime_dispose();
    CHECK(!hs_global_data->valid);

    profile_initialize();
    profile_timebase_ticks = TRUE;
    profile_tick_start();
    start = n3ds_engine_ticks();
    profile_enter(outer);
    profile_enter(inner);
    n3ds_engine_sleep_milliseconds(2);
    profile_exit(inner);
    profile_exit(outer);
    end = n3ds_engine_ticks();
    profile_tick_end();
    CHECK(inner.frame_call_count == 1 && outer.frame_call_count == 1);
    CHECK(inner.stack_depth == NONE && outer.stack_depth == NONE);
    CHECK(inner.frame_elapsed_timebase > 0 && outer.frame_elapsed_timebase >= inner.frame_elapsed_timebase);
    CHECK(outer.frame_elapsed_timebase <= end-start && n3ds_engine_tick_frequency() > 0);
    profile_tick_start();
    CHECK(outer.total_call_count == 1 && inner.total_call_count == 1);
    CHECK(outer.total_elapsed_timebase >= inner.total_elapsed_timebase && outer.frame_call_count == 0);
    profile_tick_end();
    profile_timebase_ticks = FALSE;
    n3ds_log("PASS: original nested profiler measures ARM11 ticks and rolls up samples");
    return 0;
#undef CHECK
}
