#include "cseries.h"
#include "errors.h"
#include "real_math.h"
#include "cseries_windows.h"

void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
unsigned long n3ds_engine_available_memory(void);
struct memory_status {
    unsigned long minimum_available_memory, maximum_available_memory;
};

int halo_engine_service_tests(void)
{
    struct memory_status memory = {~0UL, 0};
    char oversized[1400];
    unsigned long before, after;
#define CHECK(expr) do { if (!(expr)) { n3ds_log("NATIVE SERVICES FAIL: " #expr); return 1; } } while (0)
    CHECK(global_real_argb_white->alpha == 1.f && global_real_argb_white->red == 1.f);
    CHECK(global_real_rgb_black->red == 0.f && global_real_rgb_blue->blue == 1.f);
    error(_error_silent, "native diagnostic: %I64d %.2f", 4294967297LL, 2.5);
    CHECK(strstr(error_get(), "4294967297 2.50") && !errors_handle());
    error(_error_delayed, "native delayed diagnostic");
    CHECK(strstr(error_get(), "native delayed diagnostic") && errors_handle());
    CHECK(!errors_handle());
    memset(oversized, 'x', sizeof(oversized)-1); oversized[sizeof(oversized)-1] = 0;
    error(_error_log, "%s", oversized);
    CHECK(error_globals.message_buffer_size > 0 && error_globals.message_buffer_size < 1024);
    CHECK(error_get()[error_globals.message_buffer_size] == 0 && !errors_handle());
    before = system_milliseconds();
    n3ds_engine_sleep_milliseconds(3);
    after = system_milliseconds();
    CHECK((unsigned long)(after-before) >= 2 && (unsigned long)(after-before) < 5000);
    check_memory_status(&memory, "native services verification");
    CHECK(memory.minimum_available_memory == memory.maximum_available_memory);
    CHECK(memory.minimum_available_memory > 0 && n3ds_engine_available_memory() > 0);
    n3ds_log("PASS: original error priorities, bounded native logging, colors and platform services");
    return 0;
#undef CHECK
}
