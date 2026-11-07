#include "engine_algorithm_profile.h"

#ifdef HALO_N3DS_ALGORITHM_PROFILE
#include <stdio.h>
#include <string.h>

long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
void n3ds_log(const char *message);

/* Fixed storage, no allocations or per-event logging. Flush every 120
 * completed frontend loops. Delayed GPU records can carry earlier context.
 * Generation/BSP/gameplay changes split windows early. These
 * records describe ranges, never fictitious per-frame joins of other logs. */
enum { ALGORITHM_KINDS = 3, ALGORITHM_WINDOW_LOOPS = 120 };
struct algorithm_window {
    struct n3ds_algorithm_profile_context first, last;
    unsigned long long values[N3DS_ALGORITHM_METRIC_COUNT];
    unsigned long long first_submission, last_submission;
    unsigned int used, overflow;
};
static struct algorithm_window windows[ALGORITHM_KINDS];
static struct n3ds_algorithm_profile_context current;
static unsigned long long window_serial;
static unsigned int window_loops;
#define N3DS_ALGORITHM_NAME(id, name) name,
static const char *const metric_names[] = { N3DS_ALGORITHM_METRICS(N3DS_ALGORITHM_NAME) };
#undef N3DS_ALGORITHM_NAME

static void flush_window(struct algorithm_window *window)
{
    if (!window->used) return;
    char message[384];
    unsigned long long serial = ++window_serial;
    snprintf(message, sizeof(message),
        "ALGO WINDOW: id=%llu run=%llu kind=%d loop_gameplay=%d generation=%u bsp=%d loops=%llu..%llu boundary_ticks=%ld..%ld submissions=%llu..%llu overflow=%u",
        serial, window->first.run, window->first.kind, window->first.gameplay,
        window->first.generation, window->first.bsp, window->first.loop, window->last.loop,
        window->first.tick, window->last.tick, window->first_submission,
        window->last_submission, window->overflow);
    n3ds_log(message);
    unsigned int count = 0;
    size_t length = 0;
    for (unsigned int metric = 0; metric < N3DS_ALGORITHM_METRIC_COUNT; ++metric) {
        if (!window->values[metric]) continue;
        if (!count) length = (size_t)snprintf(message, sizeof(message), "ALGO METRICS: id=%llu", serial);
        int written = snprintf(message + length, sizeof(message) - length, " %s=%llu",
            metric_names[metric], window->values[metric]);
        /* Names are fixed source strings, at most 36 bytes; three pairs fit. */
        if (written < 0 || (size_t)written >= sizeof(message) - length) break;
        length += (size_t)written;
        if (++count == 3) { n3ds_log(message); count = 0; }
    }
    if (count) n3ds_log(message);
    memset(window, 0, sizeof(*window));
}

static struct algorithm_window *window_for(const struct n3ds_algorithm_profile_context *context)
{
    if (!current.run || !context->run || context->kind < 0 || context->kind >= ALGORITHM_KINDS) return NULL;
    struct algorithm_window *window = &windows[context->kind];
    if (window->used && (window->first.run != context->run ||
        window->first.generation != context->generation || window->first.bsp != context->bsp ||
        window->first.gameplay != context->gameplay)) flush_window(window);
    if (!window->used) { window->first = window->last = *context; window->used = 1; }
    if (context->loop < window->first.loop) window->first = *context;
    if (context->loop >= window->last.loop) window->last = *context;
    if (context->submission) {
        if (!window->first_submission || context->submission < window->first_submission)
            window->first_submission = context->submission;
        if (context->submission > window->last_submission) window->last_submission = context->submission;
    }
    return window;
}

void n3ds_algorithm_profile_add_at(const struct n3ds_algorithm_profile_context *context,
    enum n3ds_algorithm_metric metric, unsigned long long value)
{
    if ((unsigned int)metric >= N3DS_ALGORITHM_METRIC_COUNT || !value) return;
    struct algorithm_window *window = window_for(context);
    if (!window) return;
    unsigned long long prior = window->values[metric];
    if (~0ULL - prior < value) { window->values[metric] = ~0ULL; window->overflow = 1; }
    else window->values[metric] = prior + value;
}
void n3ds_algorithm_profile_max_at(const struct n3ds_algorithm_profile_context *context,
    enum n3ds_algorithm_metric metric, unsigned long long value)
{
    if ((unsigned int)metric >= N3DS_ALGORITHM_METRIC_COUNT || !value) return;
    struct algorithm_window *window = window_for(context);
    if (window && value > window->values[metric]) window->values[metric] = value;
}
void n3ds_algorithm_profile_add(enum n3ds_algorithm_metric metric, unsigned long long value)
{ n3ds_algorithm_profile_add_at(&current, metric, value); }
void n3ds_algorithm_profile_max(enum n3ds_algorithm_metric metric, unsigned long long value)
{ n3ds_algorithm_profile_max_at(&current, metric, value); }

void n3ds_algorithm_profile_begin_loop(unsigned long long loop, long tick,
    unsigned int generation, int bsp, int gameplay)
{
    if (!current.run) {
        current.run = (unsigned long long)n3ds_engine_ticks();
        if (!current.run) current.run = 1;
        char message[256];
        snprintf(message, sizeof(message),
            "ALGO RUN: id=%llu tick_hz=%lld window_loops=%u counter_bytes=%u kinds=0:frontend,1:regular,2:midpoint; instrumentation overhead unmeasured",
            current.run, n3ds_engine_tick_frequency(), ALGORITHM_WINDOW_LOOPS, (unsigned int)sizeof(windows));
        n3ds_log(message);
    }
    current.loop = loop;
    current.gameplay = !!gameplay;
    n3ds_algorithm_profile_context(tick, generation, bsp, 0);
}
void n3ds_algorithm_profile_context(long tick, unsigned int generation, int bsp, int kind)
{
    current.tick = tick; current.generation = generation; current.bsp = bsp;
    current.kind = kind; current.submission = 0;
}
void n3ds_algorithm_profile_submission(unsigned long long submission)
{ current.submission = submission; }
void n3ds_algorithm_profile_tick(long tick) { current.tick = tick; }
void n3ds_algorithm_profile_location(unsigned int generation, int bsp)
{ current.generation = generation; current.bsp = bsp; }
void n3ds_algorithm_profile_snapshot(struct n3ds_algorithm_profile_context *out)
{ *out = current; }
unsigned long long n3ds_algorithm_profile_run_id(void) { return current.run; }
unsigned long long n3ds_algorithm_profile_loop_id(void) { return current.loop; }
void n3ds_algorithm_profile_finish(void)
{
    for (unsigned int kind = 0; kind < ALGORITHM_KINDS; ++kind) flush_window(&windows[kind]);
    window_loops = 0;
}
void n3ds_algorithm_profile_end_loop(void)
{
    if (++window_loops == ALGORITHM_WINDOW_LOOPS) n3ds_algorithm_profile_finish();
}
void n3ds_algorithm_profile_stop(void)
{
    n3ds_algorithm_profile_finish();
    memset(&current, 0, sizeof(current));
    current.kind = -1; /* Teardown is not another sample of the last loop. */
}
#endif
