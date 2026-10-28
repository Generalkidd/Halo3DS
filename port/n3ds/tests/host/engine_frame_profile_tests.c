#include <stdio.h>
#include <string.h>
#include "../../include/engine_frame_profile_stats.h"

static char logs[64][640];
static unsigned int log_count;
static long long tick_frequency = 3000;

void n3ds_log(const char *text)
{
    if (log_count >= sizeof(logs) / sizeof(logs[0])) {
        fputs("host test log buffer exhausted\n", stderr);
        abort();
    }
    snprintf(logs[log_count++], sizeof(logs[0]), "%s", text);
}

long long n3ds_engine_tick_frequency(void) { return tick_frequency; }
#include "../../include/engine_frame_profile.inl"

#define CHECK(condition) do { \
    if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } \
} while (0)

static struct n3ds_frame_profile_sample sample_for(unsigned long long work)
{
    struct n3ds_frame_profile_sample sample = {0};
    sample.start = 1000;
    sample.gpu_begins = 1;
    sample.gpu_submissions = 1;
    for (unsigned int i = 0; i < N3DS_FRAME_PROFILE_STAGES; ++i) sample.stages[i] = sample.start;
    sample.stages[N3DS_FRAME_PROFILE_STAGES - 1] += (long long)work;
    sample.pace_start = sample.pace_end = sample.end = sample.stages[N3DS_FRAME_PROFILE_STAGES - 1];
    return sample;
}

static int test_percentiles(void)
{
    struct n3ds_frame_profile_stats stats = {0};
    struct n3ds_frame_profile_summary summary;
    CHECK(!n3ds_frame_profile_summarize(&stats, &summary));
    for (unsigned int i = 1; i <= 100; ++i) {
        struct n3ds_frame_profile_sample sample = sample_for(i);
        sample.end += 10;
        CHECK(n3ds_frame_profile_record(&stats, &sample, 3000000));
    }
    CHECK(n3ds_frame_profile_summarize(&stats, &summary));
    CHECK(summary.work_p50 == 50 && summary.work_p95 == 95 && summary.work_p99 == 99 && summary.work_max == 100);
    CHECK(summary.wall_p50 == 60 && summary.wall_p95 == 105 && summary.wall_p99 == 109 && summary.wall_max == 110);
    CHECK(stats.count == 100 && stats.queue_missing == 100 && !stats.invalid && !stats.dropped);
    memset(&stats, 0, sizeof(stats));
    const unsigned int unordered[] = {7, 1, 4, 2, 6, 3, 5};
    for (unsigned int i = 0; i < sizeof(unordered) / sizeof(unordered[0]); ++i) {
        struct n3ds_frame_profile_sample sample = sample_for(unordered[i]);
        CHECK(n3ds_frame_profile_record(&stats, &sample, 3000));
    }
    CHECK(n3ds_frame_profile_summarize(&stats, &summary));
    CHECK(summary.work_p50 == 4 && summary.work_p95 == 7 && summary.work_p99 == 7);
    return 0;
}

static int test_deadline_and_streaks(void)
{
    const unsigned long long frequency = 268123480, budget = frequency / 30;
    struct n3ds_frame_profile_stats stats = {0};
    struct n3ds_frame_profile_sample sample = sample_for(budget);
    CHECK(n3ds_frame_profile_record(&stats, &sample, frequency));
    CHECK(!stats.work_over_budget);
    sample = sample_for(budget + 1);
    CHECK(n3ds_frame_profile_record(&stats, &sample, frequency));
    CHECK(n3ds_frame_profile_record(&stats, &sample, frequency));
    sample = sample_for(budget);
    CHECK(n3ds_frame_profile_record(&stats, &sample, frequency));
    sample = sample_for(budget + 1);
    CHECK(n3ds_frame_profile_record(&stats, &sample, frequency));
    CHECK(stats.work_over_budget == 3 && stats.max_over_streak == 2 && stats.over_streak == 1);
    CHECK(stats.wall_over_budget == 3 && stats.max_wall_streak == 2 && stats.wall_streak == 1);
    return 0;
}

static int test_stages_and_vblank_wrap(void)
{
    struct n3ds_frame_profile_stats stats = {0};
    struct n3ds_frame_profile_sample sample = {
        .start = 1000, .stages = {1010, 1030, 1060, 1100, 1150},
        .pace_start = 1180, .pace_end = 1200, .end = 1210, .gpu_wait = 50,
        .vblank_start = UINT32_MAX - 1, .vblank_end = 1,
        .gpu_begins = 1, .gpu_submissions = 1, .previous_queue_ms = 12.5f, .previous_queue_available = 1
    };
    CHECK(n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(stats.stage_ticks[0] == 10 && stats.stage_ticks[1] == 20 && stats.stage_ticks[2] == 30);
    CHECK(stats.stage_ticks[3] == 40 && stats.stage_ticks[4] == 50);
    CHECK(stats.tail_ticks == 30 && stats.pacing_ticks == 20 && stats.gap_ticks == 10 && stats.gpu_wait_total_ticks == 50);
    CHECK(stats.work_ticks[0] == 180 && stats.wall_ticks[0] == 210);
    CHECK(stats.vblank_total == 3 && stats.max_vblank_span == 3);
    CHECK(stats.queue_count == 1 && stats.queue_total_ms == 12.5 && stats.queue_ms[0] == 12.5f);
    return 0;
}

static int test_gpu_percentiles(void)
{
    struct n3ds_frame_profile_stats stats = {0};
    struct n3ds_frame_profile_summary summary;
    for (unsigned int i = 1; i <= 100; ++i) {
        struct n3ds_frame_profile_sample sample = sample_for(i + 100);
        sample.gpu_wait = i;
        sample.previous_queue_available = 1;
        sample.previous_queue_ms = i * 0.5f;
        CHECK(n3ds_frame_profile_record(&stats, &sample, 3000));
    }
    CHECK(n3ds_frame_profile_summarize(&stats, &summary));
    CHECK(stats.gpu_wait_count == 100 && summary.wait_p50 == 50 && summary.wait_p95 == 95);
    CHECK(summary.wait_p99 == 99 && summary.wait_max == 100);
    CHECK(stats.queue_count == 100 && summary.queue_p50 == 25.0f && summary.queue_p95 == 47.5f);
    CHECK(summary.queue_p99 == 49.5f && summary.queue_max == 50.0f);
    return 0;
}

static int test_invalid_and_capacity(void)
{
    struct n3ds_frame_profile_stats stats = {0};
    struct n3ds_frame_profile_sample sample = sample_for(101);
    CHECK(n3ds_frame_profile_record(&stats, &sample, 3000) && stats.over_streak == 1);
    sample.stages[2] = sample.start - 1;
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(stats.invalid == 1 && !stats.over_streak && stats.count == 1);
    sample = sample_for(101);
    sample.gpu_wait = 102;
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 0));
    sample = sample_for(10);
    sample.pace_end = sample.end + 1;
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 3000));
    sample = sample_for(10);
    sample.previous_queue_available = 1;
    sample.previous_queue_ms = NAN;
    CHECK(n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(stats.invalid == 4 && stats.queue_invalid == 1 && stats.count == 2);

    memset(&stats, 0, sizeof(stats));
    for (unsigned int i = 0; i < N3DS_FRAME_PROFILE_CAPACITY; ++i) {
        sample = sample_for((i * 173) % N3DS_FRAME_PROFILE_CAPACITY + 1);
        CHECK(n3ds_frame_profile_record(&stats, &sample, 3000));
    }
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(stats.count == N3DS_FRAME_PROFILE_CAPACITY && stats.dropped == 1);
    struct n3ds_frame_profile_summary summary;
    CHECK(n3ds_frame_profile_summarize(&stats, &summary));
    CHECK(summary.work_p50 == 150 && summary.work_p95 == 285 && summary.work_p99 == 297 && summary.work_max == 300);

    memset(&stats, 0, sizeof(stats));
    stats.stage_ticks[0] = ULLONG_MAX;
    sample = sample_for(10);
    ++sample.stages[0];
    for (unsigned int i = 1; i < N3DS_FRAME_PROFILE_STAGES - 1; ++i) sample.stages[i] = sample.stages[0];
    CHECK(!n3ds_frame_profile_record(&stats, &sample, 3000));
    CHECK(stats.invalid == 1 && !stats.count && stats.stage_ticks[0] == ULLONG_MAX);
    return 0;
}

static const char *find_log(const char *text)
{
    for (unsigned int i = 0; i < log_count; ++i)
        if (strstr(logs[i], text)) return logs[i];
    return NULL;
}

static void submit_frame_as(long long frame, int scene, int stereo, int quality,
    const char *map, unsigned int loading)
{
    long long start = (frame - 1) * 110;
    struct n3ds_frame_profile_sample sample = sample_for(101);
    struct n3ds_frame_profile_context context = {0};
    sample.start = start;
    for (unsigned int i = 0; i < N3DS_FRAME_PROFILE_STAGES; ++i) sample.stages[i] = start;
    sample.stages[N3DS_FRAME_PROFILE_STAGES - 1] = start + 101;
    sample.pace_start = start + 101;
    sample.pace_end = start + 105;
    sample.vblank_start = (uint32_t)(frame * 2);
    sample.gpu_wait = 2;
    sample.previous_queue_available = 1;
    sample.previous_queue_ms = 8.0f;
    context.frame = frame;
    context.scene = scene;
    context.original = 1;
    context.stereo = stereo;
    context.quality = quality;
    snprintf(context.map, sizeof(context.map), "%s", map);
    context.loading_submissions = loading;
    n3ds_frame_profile_begin(start, sample.vblank_start);
    n3ds_frame_profile_end(&sample, &context);
}

static void submit_frame(long long frame, int scene, unsigned int loading)
{
    submit_frame_as(frame, scene, 0, 0, "a10", loading);
}

static int test_session_windows_and_contexts(void)
{
    log_count = 0;
    tick_frequency = 3000;
    n3ds_frame_profile_start();
    for (long long frame = 1; frame <= 301; ++frame)
        submit_frame(frame, N3DS_PROFILE_GAMEPLAY, 0);
    CHECK(frame_profile.windows == 1 && find_log("work_max_streak=300"));
    CHECK(find_log("work_ms_p50=33.667 work_ms_p95=33.667 work_ms_p99=33.667 work_ms_max=33.667"));
    CHECK(find_log("wall_ms_p50=36.667 wall_ms_p95=36.667 wall_ms_p99=36.667 wall_ms_max=36.667"));
    CHECK(find_log("gpu_begins=300 gpu_begins_anomalous=0 gpu_submissions=300 gpu_submissions_anomalous=0"));
    CHECK(find_log("FRAME PROFILE GPU WAIT: id=1 samples=300 mean_ms=0.667 p50_ms=0.667 p95_ms=0.667 p99_ms=0.667 max_ms=0.667"));
    CHECK(find_log("FRAME PROFILE GPU: id=1 samples=299 previous_queue_mean_ms=8.000 p50_ms=8.000 p95_ms=8.000 p99_ms=8.000 max_ms=8.000"));
    submit_frame(302, N3DS_PROFILE_CINEMATIC, 0);
    submit_frame(303, N3DS_PROFILE_CINEMATIC, 2);
    CHECK(frame_profile.windows == 2 && find_log("work_max_streak=301"));
    CHECK(frame_profile.stats.count == 1 && frame_profile.stats.over_streak == 1);
    submit_frame(304, N3DS_PROFILE_CINEMATIC, 0);
    CHECK(frame_profile.loading_overlaps == 1 && find_log("loading_submissions=2"));
    CHECK(!frame_profile.have_context && !frame_profile.stats.over_streak);
    n3ds_frame_profile_finish(304 * 110, 610);
    CHECK(frame_profile.windows == 4 && frame_profile.incomplete_windows == 3 && !frame_profile.errors && !frame_profile.aborted);
    CHECK(find_log("scene=gameplay") && find_log("scene=cinematic"));
    CHECK(find_log("queue_missing=1") && find_log("presentation=unverified"));
    CHECK(find_log("FRAME PROFILE END: windows=4 incomplete_windows=3 errors=0 aborted=0 user_stops=0 loading_overlaps=1"));
    return 0;
}

static int test_context_change_after_full_window(void)
{
    log_count = 0;
    n3ds_frame_profile_start();
    for (long long frame = 1; frame <= 300; ++frame)
        submit_frame(frame, N3DS_PROFILE_GAMEPLAY, 0);
    submit_frame(301, N3DS_PROFILE_CINEMATIC, 0);
    submit_frame(302, N3DS_PROFILE_CINEMATIC, 0);
    CHECK(frame_profile.windows == 1);
    CHECK(frame_profile.stats.count == 1 && frame_profile.stats.over_streak == 1);
    n3ds_frame_profile_finish(302 * 110, 606);
    CHECK(frame_profile.windows == 2 && !frame_profile.errors);
    return 0;
}

static int test_missing_and_aborted_frames(void)
{
    struct n3ds_frame_profile_sample sample = sample_for(10);
    struct n3ds_frame_profile_context context = {0};
    log_count = 0;
    tick_frequency = 0;
    n3ds_frame_profile_start();
    context.frame = 1;
    context.scene = N3DS_PROFILE_MENU;
    memcpy(context.map, "ui", 3);
    n3ds_frame_profile_begin(1000, 0);
    n3ds_frame_profile_end(&sample, &context);
    n3ds_frame_profile_finish(1010, 1);
    CHECK(find_log("ERROR: invalid tick frequency") && find_log("samples=0 invalid=1"));
    CHECK(find_log("no valid samples") && frame_profile.errors == 1);

    log_count = 0;
    tick_frequency = 3000;
    n3ds_frame_profile_start();
    n3ds_frame_profile_begin(0, 0);
    n3ds_frame_profile_finish(10, 0);
    CHECK(frame_profile.aborted == 1 && find_log("FRAME PROFILE ABORTED"));
    CHECK(find_log("FRAME PROFILE END: windows=0 incomplete_windows=0 errors=0 aborted=1"));

    log_count = 0;
    n3ds_frame_profile_start();
    n3ds_frame_profile_begin(0, 0);
    n3ds_frame_profile_user_stop();
    n3ds_frame_profile_finish(10, 0);
    CHECK(frame_profile.user_stops == 1 && !frame_profile.aborted && !frame_profile.errors);
    CHECK(find_log("FRAME PROFILE STOP:") && find_log("aborted=0 user_stops=1"));

    log_count = 0;
    n3ds_frame_profile_start();
    submit_frame(1, N3DS_PROFILE_GAMEPLAY, 0);
    n3ds_frame_profile_begin(110, 4);
    n3ds_frame_profile_user_stop();
    n3ds_frame_profile_finish(120, 4);
    CHECK(frame_profile.windows == 1 && frame_profile.user_stops == 1 && !frame_profile.aborted);
    CHECK(find_log("first=1 last=1 samples=1"));

    log_count = 0;
    n3ds_frame_profile_start();
    sample = sample_for(10);
    sample.gpu_begins = 0;
    sample.gpu_submissions = 0;
    n3ds_frame_profile_begin(1000, 0);
    n3ds_frame_profile_end(&sample, &context);
    n3ds_frame_profile_finish(1011, 1);
    CHECK(find_log("gpu_begins_anomalous=1") && find_log("queue_missing=1"));
    CHECK(find_log("gpu_submissions_anomalous=1"));
    CHECK(find_log("FRAME PROFILE GPU WAIT: id=1 unavailable") && find_log("previous_queue=unavailable"));
    return 0;
}

static int test_context_metadata(void)
{
    log_count = 0;
    n3ds_frame_profile_start();
    submit_frame_as(1, N3DS_PROFILE_GAMEPLAY, 0, 0, "a10", 0);
    submit_frame_as(2, N3DS_PROFILE_GAMEPLAY, 1, 0, "a10", 0);
    submit_frame_as(3, N3DS_PROFILE_GAMEPLAY, 1, 1, "a10", 0);
    submit_frame_as(4, N3DS_PROFILE_GAMEPLAY, 1, 1, "a30", 0);
    n3ds_frame_profile_finish(440, 10);
    CHECK(frame_profile.windows == 4 && !frame_profile.errors);
    CHECK(find_log("map=a10 model=original stereo=0 quality=0"));
    CHECK(find_log("map=a10 model=original stereo=1 quality=0"));
    CHECK(find_log("map=a10 model=original stereo=1 quality=1"));
    CHECK(find_log("map=a30 model=original stereo=1 quality=1"));

    log_count = 0;
    n3ds_frame_profile_start();
    submit_frame_as(1, N3DS_PROFILE_GAMEPLAY, 0, 0, "none", 0);
    n3ds_frame_profile_finish(110, 4);
    CHECK(frame_profile.incomplete_windows == 1 && find_log("metadata_missing=1"));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        log_count = 0;
        tick_frequency = 3000;
        if (!strcmp(argv[1], "--emit-fixture")) {
            n3ds_frame_profile_start();
            for (long long frame = 1; frame <= 301; ++frame)
                submit_frame(frame, N3DS_PROFILE_GAMEPLAY, 0);
            n3ds_frame_profile_finish(301 * 110, 604);
        } else if (!strcmp(argv[1], "--emit-context-fixture")) {
            if (test_session_windows_and_contexts()) return 1;
        } else if (!strcmp(argv[1], "--emit-anomaly-fixture")) {
            if (test_missing_and_aborted_frames()) return 1;
        } else if (!strcmp(argv[1], "--emit-aborted-fixture") ||
                   !strcmp(argv[1], "--emit-stop-fixture")) {
            n3ds_frame_profile_start();
            submit_frame(1, N3DS_PROFILE_GAMEPLAY, 0);
            n3ds_frame_profile_begin(110, 4);
            if (!strcmp(argv[1], "--emit-stop-fixture")) n3ds_frame_profile_user_stop();
            n3ds_frame_profile_finish(120, 4);
        } else if (!strcmp(argv[1], "--emit-invalid-fixture")) {
            struct n3ds_frame_profile_sample sample = sample_for(10);
            struct n3ds_frame_profile_context context = {0};
            sample.stages[0] = sample.start - 1;
            context.frame = 1;
            context.scene = N3DS_PROFILE_MENU;
            context.original = 1;
            memcpy(context.map, "ui", 3);
            n3ds_frame_profile_start();
            n3ds_frame_profile_begin(sample.start, 0);
            n3ds_frame_profile_end(&sample, &context);
            n3ds_frame_profile_finish(1011, 1);
        } else {
            fprintf(stderr, "unknown fixture: %s\n", argv[1]);
            return 2;
        }
        for (unsigned int i = 0; i < log_count; ++i) puts(logs[i]);
        puts("LOG QUEUE: dropped=0 io_errors=0; routine writes ran off the frame thread");
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--emit-*-fixture|--emit-fixture]\n", argv[0]);
        return 2;
    }
    if (test_percentiles() || test_deadline_and_streaks() ||
        test_stages_and_vblank_wrap() || test_gpu_percentiles() || test_invalid_and_capacity() ||
        test_session_windows_and_contexts() || test_context_change_after_full_window() ||
        test_missing_and_aborted_frames() || test_context_metadata()) return 1;
    puts("PASS: frame profile statistics, context transitions, loading exclusions and failure reporting");
    return 0;
}
