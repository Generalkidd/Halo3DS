#ifndef HALO_N3DS_ENGINE_FRAME_PROFILE_STATS_H
#define HALO_N3DS_ENGINE_FRAME_PROFILE_STATS_H

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

enum { N3DS_FRAME_PROFILE_CAPACITY = 300, N3DS_FRAME_PROFILE_STAGES = 5 };

struct n3ds_frame_profile_sample {
    long long start, stages[N3DS_FRAME_PROFILE_STAGES], pace_start, pace_end, end, gpu_wait;
    uint32_t vblank_start, vblank_end;
    unsigned long long gpu_begins, gpu_submissions;
    float previous_queue_ms;
    int previous_queue_available;
};

struct n3ds_frame_profile_stats {
    unsigned int count, invalid, dropped, work_over_budget, over_streak, max_over_streak;
    unsigned int wall_over_budget, wall_streak, max_wall_streak;
    unsigned int queue_count, queue_missing, queue_invalid, gpu_wait_count, gpu_begin_anomalies, gpu_submission_anomalies, max_vblank_span;
    unsigned long long stage_ticks[N3DS_FRAME_PROFILE_STAGES], tail_ticks, pacing_ticks, gap_ticks, gpu_wait_total_ticks;
    unsigned long long vblank_total, gpu_begins_total, gpu_submissions_total;
    unsigned long long work_ticks[N3DS_FRAME_PROFILE_CAPACITY], wall_ticks[N3DS_FRAME_PROFILE_CAPACITY];
    unsigned long long gpu_wait_ticks[N3DS_FRAME_PROFILE_CAPACITY];
    float queue_ms[N3DS_FRAME_PROFILE_CAPACITY];
    double queue_total_ms;
};

struct n3ds_frame_profile_summary {
    unsigned long long work_p50, work_p95, work_p99, work_max;
    unsigned long long wall_p50, wall_p95, wall_p99, wall_max;
    unsigned long long wait_p50, wait_p95, wait_p99, wait_max;
    float queue_p50, queue_p95, queue_p99, queue_max;
};

static void n3ds_frame_profile_increment(unsigned int *value)
{
    if (*value != UINT_MAX) ++*value;
}

static int n3ds_frame_profile_record(struct n3ds_frame_profile_stats *stats,
    const struct n3ds_frame_profile_sample *sample, unsigned long long frequency)
{
    unsigned long long stages[N3DS_FRAME_PROFILE_STAGES], work, wall, tail, pacing, gap, wait;
    long long previous;
    unsigned int i;
    uint32_t vblanks;
    if (!stats) return 0;
    if (!sample || !frequency || sample->start < 0 || sample->gpu_wait < 0 ||
        (sample->gpu_begins != 1 && (sample->gpu_wait || sample->previous_queue_available))) goto invalid;
    if (stats->count >= N3DS_FRAME_PROFILE_CAPACITY) {
        n3ds_frame_profile_increment(&stats->dropped);
        stats->over_streak = stats->wall_streak = 0;
        return 0;
    }
    previous = sample->start;
    for (i = 0; i < N3DS_FRAME_PROFILE_STAGES; ++i) {
        if (sample->stages[i] < previous) goto invalid;
        stages[i] = (unsigned long long)(sample->stages[i] - previous);
        previous = sample->stages[i];
    }
    if (sample->pace_start < previous || sample->pace_end < sample->pace_start || sample->end < sample->pace_end) goto invalid;
    work = (unsigned long long)(sample->pace_start - sample->start);
    wall = (unsigned long long)(sample->end - sample->start);
    tail = (unsigned long long)(sample->pace_start - previous);
    pacing = (unsigned long long)(sample->pace_end - sample->pace_start);
    gap = (unsigned long long)(sample->end - sample->pace_end);
    wait = (unsigned long long)sample->gpu_wait;
    if (wait > work) goto invalid;
    for (i = 0; i < N3DS_FRAME_PROFILE_STAGES; ++i)
        if (stages[i] > ULLONG_MAX - stats->stage_ticks[i]) goto invalid;
    if (tail > ULLONG_MAX - stats->tail_ticks || pacing > ULLONG_MAX - stats->pacing_ticks ||
        gap > ULLONG_MAX - stats->gap_ticks || wait > ULLONG_MAX - stats->gpu_wait_total_ticks) goto invalid;
    vblanks = sample->vblank_end - sample->vblank_start;
    if ((unsigned long long)vblanks > ULLONG_MAX - stats->vblank_total ||
        sample->gpu_begins > ULLONG_MAX - stats->gpu_begins_total ||
        sample->gpu_submissions > ULLONG_MAX - stats->gpu_submissions_total) goto invalid;

    for (i = 0; i < N3DS_FRAME_PROFILE_STAGES; ++i) stats->stage_ticks[i] += stages[i];
    stats->tail_ticks += tail;
    stats->pacing_ticks += pacing;
    stats->gap_ticks += gap;
    stats->gpu_wait_total_ticks += wait;
    stats->vblank_total += vblanks;
    stats->gpu_begins_total += sample->gpu_begins;
    stats->gpu_submissions_total += sample->gpu_submissions;
    if (vblanks > stats->max_vblank_span) stats->max_vblank_span = vblanks;
    stats->work_ticks[stats->count] = work;
    stats->wall_ticks[stats->count] = wall;
    ++stats->count;
    if (sample->gpu_begins != 1) n3ds_frame_profile_increment(&stats->gpu_begin_anomalies);
    else stats->gpu_wait_ticks[stats->gpu_wait_count++] = wait;
    if (sample->gpu_submissions != 1) n3ds_frame_profile_increment(&stats->gpu_submission_anomalies);
    if (work > frequency / 30) {
        n3ds_frame_profile_increment(&stats->work_over_budget);
        n3ds_frame_profile_increment(&stats->over_streak);
        if (stats->over_streak > stats->max_over_streak) stats->max_over_streak = stats->over_streak;
    } else stats->over_streak = 0;
    if (wall > frequency / 30) {
        n3ds_frame_profile_increment(&stats->wall_over_budget);
        n3ds_frame_profile_increment(&stats->wall_streak);
        if (stats->wall_streak > stats->max_wall_streak) stats->max_wall_streak = stats->wall_streak;
    } else stats->wall_streak = 0;

    if (!sample->previous_queue_available) n3ds_frame_profile_increment(&stats->queue_missing);
    else if (!isfinite(sample->previous_queue_ms) || sample->previous_queue_ms < 0)
        n3ds_frame_profile_increment(&stats->queue_invalid);
    else {
        stats->queue_ms[stats->queue_count++] = sample->previous_queue_ms;
        stats->queue_total_ms += sample->previous_queue_ms;
    }
    return 1;

invalid:
    n3ds_frame_profile_increment(&stats->invalid);
    stats->over_streak = stats->wall_streak = 0;
    return 0;
}

static int n3ds_frame_profile_compare_ticks(const void *left, const void *right)
{
    unsigned long long a = *(const unsigned long long *)left;
    unsigned long long b = *(const unsigned long long *)right;
    return (a > b) - (a < b);
}

static int n3ds_frame_profile_compare_queue(const void *left, const void *right)
{
    float a = *(const float *)left;
    float b = *(const float *)right;
    return (a > b) - (a < b);
}

static unsigned int n3ds_frame_profile_percentile_index(unsigned int count, unsigned int percentile)
{
    return (count * percentile + 99) / 100 - 1;
}

static unsigned long long n3ds_frame_profile_percentile(const unsigned long long *sorted,
    unsigned int count, unsigned int percentile)
{
    return sorted[n3ds_frame_profile_percentile_index(count, percentile)];
}

static int n3ds_frame_profile_summarize(struct n3ds_frame_profile_stats *stats,
    struct n3ds_frame_profile_summary *summary)
{
    if (!stats || !summary || !stats->count) return 0;
    qsort(stats->work_ticks, stats->count, sizeof(stats->work_ticks[0]), n3ds_frame_profile_compare_ticks);
    qsort(stats->wall_ticks, stats->count, sizeof(stats->wall_ticks[0]), n3ds_frame_profile_compare_ticks);
    summary->work_p50 = n3ds_frame_profile_percentile(stats->work_ticks, stats->count, 50);
    summary->work_p95 = n3ds_frame_profile_percentile(stats->work_ticks, stats->count, 95);
    summary->work_p99 = n3ds_frame_profile_percentile(stats->work_ticks, stats->count, 99);
    summary->work_max = stats->work_ticks[stats->count - 1];
    summary->wall_p50 = n3ds_frame_profile_percentile(stats->wall_ticks, stats->count, 50);
    summary->wall_p95 = n3ds_frame_profile_percentile(stats->wall_ticks, stats->count, 95);
    summary->wall_p99 = n3ds_frame_profile_percentile(stats->wall_ticks, stats->count, 99);
    summary->wall_max = stats->wall_ticks[stats->count - 1];
    if (stats->gpu_wait_count) {
        qsort(stats->gpu_wait_ticks, stats->gpu_wait_count, sizeof(stats->gpu_wait_ticks[0]), n3ds_frame_profile_compare_ticks);
        summary->wait_p50 = n3ds_frame_profile_percentile(stats->gpu_wait_ticks, stats->gpu_wait_count, 50);
        summary->wait_p95 = n3ds_frame_profile_percentile(stats->gpu_wait_ticks, stats->gpu_wait_count, 95);
        summary->wait_p99 = n3ds_frame_profile_percentile(stats->gpu_wait_ticks, stats->gpu_wait_count, 99);
        summary->wait_max = stats->gpu_wait_ticks[stats->gpu_wait_count - 1];
    }
    if (stats->queue_count) {
        qsort(stats->queue_ms, stats->queue_count, sizeof(stats->queue_ms[0]), n3ds_frame_profile_compare_queue);
        summary->queue_p50 = stats->queue_ms[n3ds_frame_profile_percentile_index(stats->queue_count, 50)];
        summary->queue_p95 = stats->queue_ms[n3ds_frame_profile_percentile_index(stats->queue_count, 95)];
        summary->queue_p99 = stats->queue_ms[n3ds_frame_profile_percentile_index(stats->queue_count, 99)];
        summary->queue_max = stats->queue_ms[stats->queue_count - 1];
    }
    return 1;
}

#endif
