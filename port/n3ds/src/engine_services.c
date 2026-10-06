/* Engine-facing services backed by the native clock, memory and log console. */
#include "cseries.h"
#include "errors.h"
#include "real_math.h"

void n3ds_log(const char *message);
void n3ds_engine_console_clear(void);
unsigned long n3ds_engine_available_memory(void);
struct memory_status {
    unsigned long minimum_available_memory, maximum_available_memory;
};

/* Standalone bootstrap has no original terminal state. Full game links use
 * original console/terminal owners, keeping their queue and input behavior. */
#ifndef HALO_N3DS_WORLD_SERVICES
static void native_message(const char *format, va_list args)
{
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    n3ds_log(message);
}
void terminal_printf(const real_argb_color *color, const char *format, ...)
{
    va_list args;
    (void)color; /* The native diagnostic console is monochrome. */
    va_start(args, format); native_message(format, args); va_end(args);
}
void console_printf(boolean clear, const char *format, ...)
{
    va_list args;
    if (clear) n3ds_engine_console_clear();
    va_start(args, format); native_message(format, args); va_end(args);
}
void console_warning(const char *format, ...)
{
    va_list args;
    va_start(args, format); native_message(format, args); va_end(args);
}
#endif
void check_memory_status(struct memory_status *status, const char *location)
{
    unsigned long available = n3ds_engine_available_memory();
    assert(status);
    if (available < status->minimum_available_memory) status->minimum_available_memory = available;
    if (available > status->maximum_available_memory) status->maximum_available_memory = available;
    if (status->maximum_available_memory-status->minimum_available_memory > 16*1024)
        error(_error_silent, "memory check at %s: available-memory range is %lu bytes", location,
              status->maximum_available_memory-status->minimum_available_memory);
}
