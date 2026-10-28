#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "engine_frame_profile_stats.h"

enum {
    N3DS_PROFILE_MENU, N3DS_PROFILE_GAMEPLAY, N3DS_PROFILE_GAMEPLAY_UI, N3DS_PROFILE_CINEMATIC
};

struct n3ds_frame_profile_context {
    long long frame;
    int scene, original, stereo, quality;
    char map[33];
    unsigned int loading_submissions;
};

static struct {
    struct n3ds_frame_profile_stats stats;
    struct n3ds_frame_profile_sample pending_sample;
    struct n3ds_frame_profile_context context, pending_context;
    long long first_frame, last_frame;
    unsigned int windows, incomplete_windows, errors, aborted, user_stops, loading_overlaps;
    int have_context, pending, open;
} frame_profile;

static void n3ds_frame_profile_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void n3ds_frame_profile_log(const char *format, ...)
{
    char text[640];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: could not format a complete record");
    } else n3ds_log(text);
}

static const char *n3ds_frame_profile_scene(int scene)
{
    switch (scene) {
    case N3DS_PROFILE_MENU: return "menu";
    case N3DS_PROFILE_GAMEPLAY: return "gameplay";
    case N3DS_PROFILE_GAMEPLAY_UI: return "gameplay-ui";
    case N3DS_PROFILE_CINEMATIC: return "cinematic";
    default: return "unknown";
    }
}

static int n3ds_frame_profile_same_context(const struct n3ds_frame_profile_context *a,
    const struct n3ds_frame_profile_context *b)
{
    return a->scene == b->scene && a->original == b->original && a->stereo == b->stereo &&
        a->quality == b->quality && !strcmp(a->map, b->map);
}

static void n3ds_frame_profile_emit(int continue_streak)
{
    struct n3ds_frame_profile_stats *stats = &frame_profile.stats;
    struct n3ds_frame_profile_summary summary;
    if (!stats->count && !stats->invalid && !stats->dropped) {
        if (!continue_streak) stats->over_streak = stats->wall_streak = 0;
        return;
    }
    unsigned int work_streak = continue_streak ? stats->over_streak : 0;
    unsigned int wall_streak = continue_streak ? stats->wall_streak : 0;
    unsigned int window = ++frame_profile.windows;
    int metadata_missing = !frame_profile.context.map[0] || !strcmp(frame_profile.context.map, "none");
    int complete = !stats->invalid && !stats->dropped && !stats->queue_missing &&
        !stats->queue_invalid && !stats->gpu_begin_anomalies && !stats->gpu_submission_anomalies && !metadata_missing;
    if (!complete) n3ds_frame_profile_increment(&frame_profile.incomplete_windows);
    n3ds_frame_profile_log("FRAME PROFILE WINDOW: id=%u scene=%s map=%s model=%s stereo=%d quality=%d first=%lld last=%lld samples=%u invalid=%u dropped=%u metadata_missing=%d gpu_begins=%llu gpu_begins_anomalous=%u gpu_submissions=%llu gpu_submissions_anomalous=%u queue_samples=%u queue_missing=%u queue_invalid=%u status=%s presentation=unverified",
        window, n3ds_frame_profile_scene(frame_profile.context.scene), frame_profile.context.map,
        frame_profile.context.original ? "original" : "new", frame_profile.context.stereo,
        frame_profile.context.quality, frame_profile.first_frame, frame_profile.last_frame,
        stats->count, stats->invalid, stats->dropped, metadata_missing, stats->gpu_begins_total,
        stats->gpu_begin_anomalies, stats->gpu_submissions_total, stats->gpu_submission_anomalies,
        stats->queue_count, stats->queue_missing, stats->queue_invalid, complete ? "complete" : "incomplete");
    long long frequency = n3ds_engine_tick_frequency();
    if (stats->count && frequency <= 0) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: invalid tick frequency; timings unavailable");
    } else if (n3ds_frame_profile_summarize(stats, &summary)) {
        double scale = 1000.0 / frequency;
        double mean_scale = scale / stats->count;
        n3ds_frame_profile_log("FRAME PROFILE TIMING: id=%u work_ms_p50=%.3f work_ms_p95=%.3f work_ms_p99=%.3f work_ms_max=%.3f wall_ms_p50=%.3f wall_ms_p95=%.3f wall_ms_p99=%.3f wall_ms_max=%.3f work_over_30hz=%u work_max_streak=%u wall_over_30hz=%u wall_max_streak=%u vblank_callbacks=%llu vblank_max_per_frame=%u",
            window, summary.work_p50 * scale, summary.work_p95 * scale, summary.work_p99 * scale, summary.work_max * scale,
            summary.wall_p50 * scale, summary.wall_p95 * scale, summary.wall_p99 * scale, summary.wall_max * scale,
            stats->work_over_budget, stats->max_over_streak, stats->wall_over_budget, stats->max_wall_streak,
            stats->vblank_total, stats->max_vblank_span);
        n3ds_frame_profile_log("FRAME PROFILE STAGES: id=%u mean_ms services=%.3f input=%.3f simulation=%.3f audio=%.3f render=%.3f tail=%.3f pacing=%.3f gap=%.3f",
            window, stats->stage_ticks[0] * mean_scale, stats->stage_ticks[1] * mean_scale,
            stats->stage_ticks[2] * mean_scale, stats->stage_ticks[3] * mean_scale,
            stats->stage_ticks[4] * mean_scale, stats->tail_ticks * mean_scale,
            stats->pacing_ticks * mean_scale, stats->gap_ticks * mean_scale);
        if (stats->gpu_wait_count)
            n3ds_frame_profile_log("FRAME PROFILE GPU WAIT: id=%u samples=%u mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f",
                window, stats->gpu_wait_count, stats->gpu_wait_total_ticks * scale / stats->gpu_wait_count,
                summary.wait_p50 * scale, summary.wait_p95 * scale, summary.wait_p99 * scale, summary.wait_max * scale);
        else n3ds_frame_profile_log("FRAME PROFILE GPU WAIT: id=%u unavailable", window);
        if (stats->queue_count)
            n3ds_frame_profile_log("FRAME PROFILE GPU: id=%u samples=%u previous_queue_mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f; overlaps CPU and includes transfers",
                window, stats->queue_count, stats->queue_total_ms / stats->queue_count,
                summary.queue_p50, summary.queue_p95, summary.queue_p99, summary.queue_max);
        else n3ds_frame_profile_log("FRAME PROFILE GPU: id=%u previous_queue=unavailable", window);
    } else n3ds_frame_profile_log("FRAME PROFILE TIMING: id=%u no valid samples", window);
    memset(stats, 0, sizeof(*stats));
    stats->over_streak = work_streak;
    stats->wall_streak = wall_streak;
}

static void n3ds_frame_profile_complete_pending(long long end, uint32_t vblank)
{
    struct n3ds_frame_profile_context *context = &frame_profile.pending_context;
    struct n3ds_frame_profile_sample *sample = &frame_profile.pending_sample;
    sample->end = end;
    sample->vblank_end = vblank;
    if (context->loading_submissions) {
        n3ds_frame_profile_emit(0);
        frame_profile.have_context = 0;
        n3ds_frame_profile_increment(&frame_profile.loading_overlaps);
        n3ds_frame_profile_log("FRAME PROFILE EXCLUDED: frame=%lld loading_submissions=%u; mixed loading and active work",
            context->frame, context->loading_submissions);
    } else {
        if (!frame_profile.have_context || !n3ds_frame_profile_same_context(&frame_profile.context, context)) {
            n3ds_frame_profile_emit(0);
            frame_profile.context = *context;
            frame_profile.have_context = 1;
            sample->previous_queue_available = 0;
        }
        if (!frame_profile.stats.count && !frame_profile.stats.invalid && !frame_profile.stats.dropped)
            frame_profile.first_frame = context->frame;
        frame_profile.last_frame = context->frame;
        long long frequency = n3ds_engine_tick_frequency();
        n3ds_frame_profile_record(&frame_profile.stats, sample, frequency > 0 ? (unsigned long long)frequency : 0);
        if (frame_profile.stats.count + frame_profile.stats.invalid + frame_profile.stats.dropped >= N3DS_FRAME_PROFILE_CAPACITY)
            n3ds_frame_profile_emit(1);
    }
    frame_profile.pending = 0;
}

static void n3ds_frame_profile_start(void)
{
    memset(&frame_profile, 0, sizeof(frame_profile));
    long long frequency = n3ds_engine_tick_frequency();
    if (frequency <= 0) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: invalid tick frequency");
    }
    n3ds_frame_profile_log("FRAME PROFILE START: version=1 window=%u tick_hz=%lld presentation=unverified",
        (unsigned int)N3DS_FRAME_PROFILE_CAPACITY, frequency);
}

static void n3ds_frame_profile_begin(long long start, uint32_t vblank)
{
    if (frame_profile.open) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: previous frame was not completed");
    }
    if (frame_profile.pending) n3ds_frame_profile_complete_pending(start, vblank);
    frame_profile.open = 1;
}

static void n3ds_frame_profile_end(const struct n3ds_frame_profile_sample *sample,
    const struct n3ds_frame_profile_context *context)
{
    if (!frame_profile.open || frame_profile.pending) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: frame completion without a matching begin");
        return;
    }
    frame_profile.pending_sample = *sample;
    frame_profile.pending_context = *context;
    frame_profile.open = 0;
    frame_profile.pending = 1;
}

static void n3ds_frame_profile_user_stop(void)
{
    if (!frame_profile.open || frame_profile.pending) {
        n3ds_frame_profile_increment(&frame_profile.errors);
        n3ds_log("FRAME PROFILE ERROR: user stop without an open frame");
        return;
    }
    frame_profile.open = 0;
    n3ds_frame_profile_increment(&frame_profile.user_stops);
    n3ds_log("FRAME PROFILE STOP: user stopped before rendering; partial iteration excluded");
}

static void n3ds_frame_profile_finish(long long end, uint32_t vblank)
{
    if (frame_profile.open) {
        n3ds_frame_profile_increment(&frame_profile.aborted);
        n3ds_log("FRAME PROFILE ABORTED: loop ended before this frame was rendered");
        frame_profile.open = 0;
    }
    if (frame_profile.pending) n3ds_frame_profile_complete_pending(end, vblank);
    n3ds_frame_profile_emit(0);
    n3ds_frame_profile_log("FRAME PROFILE END: windows=%u incomplete_windows=%u errors=%u aborted=%u user_stops=%u loading_overlaps=%u presentation=unverified",
        frame_profile.windows, frame_profile.incomplete_windows, frame_profile.errors,
        frame_profile.aborted, frame_profile.user_stops, frame_profile.loading_overlaps);
}
