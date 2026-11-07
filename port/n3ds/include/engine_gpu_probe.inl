/* Owner-side paused-view diagnostic. Only copied camera values and counters
 * cross frames. The Citro3D adapter supplies scalar completion timings. */
enum { GPU_PROBE_WARMUP = 3, GPU_PROBE_SAMPLES = 12, GPU_PROBE_RUNS = 3, GPU_PROBE_MISSING_LIMIT = 5 };
enum { PROBE_DRAWS, PROBE_ELEMENTS, PROBE_TERRAIN_DRAWS, PROBE_TERRAIN_ELEMENTS,
#ifdef HALO_N3DS_FRAMEBUFFER_REUSE
    PROBE_FB_BINDS, PROBE_FB_REUSES,
#endif
#ifdef HALO_N3DS_OPAQUE_COLOR_READS
    PROBE_READ_ELIGIBLE, PROBE_READ_ELIDED, PROBE_READ_MASK_WRITES, PROBE_READ_EXTRA_FLUSHES,
#endif
    PROBE_COUNTERS };
#ifdef HALO_N3DS_OPAQUE_COLOR_READS
enum { PROBE_OPAQUE_COLOR_MODE = 3 };
static const unsigned char probe_modes[] = {0, PROBE_OPAQUE_COLOR_MODE, 0, PROBE_OPAQUE_COLOR_MODE, 0};
static const char *const probe_mode_names[] = {"reference", "quarter-pixels", "no-effects", "opaque-color-reads"};
#elif defined(HALO_N3DS_FRAMEBUFFER_REUSE)
enum { PROBE_REUSE_MODE = 3 };
static const unsigned char probe_modes[] = {0, PROBE_REUSE_MODE, 0, PROBE_REUSE_MODE, 0};
static const char *const probe_mode_names[] = {"reference", "quarter-pixels", "no-effects", "framebuffer-reuse"};
#else
static const unsigned char probe_modes[] = {0, 1, 0, 2, 0};
static const char *const probe_mode_names[] = {"normal", "quarter-pixels", "no-effects"};
#endif
static struct {
    struct native_gpu_probe_view view;
    u64 started;
    u32 run, phase, warmup, samples, missing, pending_submission;
    u32 queue_ticks[GPU_PROBE_SAMPLES];
    u64 wait_ticks;
    unsigned int draws[PROBE_COUNTERS], minimum[PROBE_COUNTERS], maximum[PROBE_COUNTERS];
    int chord_held, active, selected, recording, terrain;
} gpu_probe;

#ifdef HALO_N3DS_FRAMEBUFFER_REUSE
static int gpu_probe_framebuffer_reuse(void)
{
    return !gpu_probe.recording || probe_modes[gpu_probe.phase] == PROBE_REUSE_MODE;
}

static void gpu_probe_framebuffer_bind(int reused)
{
    if (gpu_probe.recording)
        ++gpu_probe.draws[reused ? PROBE_FB_REUSES : PROBE_FB_BINDS];
}
#endif

u32 n3ds_c3d_gpu_probe_submission(void);
int n3ds_c3d_gpu_probe_sample(u32 submission, u32 *ticks);

static void gpu_probe_abort(const char *reason)
{
    if (gpu_probe.active) {
        char message[160];
        snprintf(message, sizeof(message), "GPU PROBE ABORT: run=%u phase=%u samples=%u reason=%s",
            gpu_probe.run, gpu_probe.phase + 1, gpu_probe.samples, reason);
        n3ds_log(message);
    }
    gpu_probe.active = gpu_probe.selected = gpu_probe.recording = gpu_probe.terrain = 0;
    gpu_probe.pending_submission = 0;
}

void n3ds_gpu_probe_view(const struct native_gpu_probe_view *view, int chord_held)
{
    int fresh_chord = chord_held && !gpu_probe.chord_held;
    gpu_probe.chord_held = chord_held;
    if (gpu_probe.active) {
        if (!view) gpu_probe_abort("pause-left");
        else if (view->tick != gpu_probe.view.tick || view->bsp != gpu_probe.view.bsp ||
            view->generation != gpu_probe.view.generation ||
            memcmp(view->camera, gpu_probe.view.camera, sizeof(view->camera)) ||
            memcmp(view->bounds, gpu_probe.view.bounds, sizeof(view->bounds)))
            gpu_probe_abort("view-changed");
        else if (svcGetSystemTick() - gpu_probe.started >= 30ULL * SYSCLOCK_ARM11)
            gpu_probe_abort("time-limit");
    } else if (view && fresh_chord && !diagnostic_mode && gpu_probe.run < GPU_PROBE_RUNS) {
        gpu_probe.view = *view;
        gpu_probe.started = svcGetSystemTick();
        ++gpu_probe.run;
        gpu_probe.phase = gpu_probe.warmup = gpu_probe.samples = gpu_probe.missing = 0;
        gpu_probe.wait_ticks = 0;
        gpu_probe.active = 1;
        char message[192];
        snprintf(message, sizeof(message),
            "GPU PROBE START: run=%u tick=%d generation=%u bsp=%d camera=%.3f,%.3f,%.3f",
            gpu_probe.run, view->tick, view->generation, view->bsp,
            view->camera[0][0], view->camera[0][1], view->camera[0][2]);
        n3ds_log(message);
    }
    gpu_probe.selected = gpu_probe.active;
}

void n3ds_gpu_probe_terrain(int terrain)
{
    gpu_probe.terrain = terrain;
}

static void gpu_probe_count(unsigned int elements)
{
    if (!gpu_probe.recording) return;
    ++gpu_probe.draws[PROBE_DRAWS];
    gpu_probe.draws[PROBE_ELEMENTS] += elements;
    if (gpu_probe.terrain) {
        ++gpu_probe.draws[PROBE_TERRAIN_DRAWS];
        gpu_probe.draws[PROBE_TERRAIN_ELEMENTS] += elements;
    }
}

static int probe_compare_ticks(const void *left, const void *right)
{
    u32 a = *(const u32 *)left, b = *(const u32 *)right;
    return (a > b) - (a < b);
}

static void gpu_probe_report_phase(void)
{
    const double tick_to_ms = 1000.0 / SYSCLOCK_ARM11;
    qsort(gpu_probe.queue_ticks, GPU_PROBE_SAMPLES, sizeof(gpu_probe.queue_ticks[0]), probe_compare_ticks);
    char message[320];
    snprintf(message, sizeof(message),
        "GPU PROBE PHASE: run=%u phase=%u mode=%s samples=%u queue_min_ms=%.3f queue_median_ms=%.3f queue_max_ms=%.3f begin_wait_mean_ms=%.3f",
        gpu_probe.run, gpu_probe.phase + 1, probe_mode_names[probe_modes[gpu_probe.phase]], gpu_probe.samples,
        gpu_probe.queue_ticks[0] * tick_to_ms,
        (gpu_probe.queue_ticks[GPU_PROBE_SAMPLES / 2 - 1] * .5 +
            gpu_probe.queue_ticks[GPU_PROBE_SAMPLES / 2] * .5) * tick_to_ms,
        gpu_probe.queue_ticks[GPU_PROBE_SAMPLES - 1] * tick_to_ms,
        gpu_probe.wait_ticks * tick_to_ms / GPU_PROBE_SAMPLES);
    n3ds_log(message);
#ifdef HALO_N3DS_FRAMEBUFFER_REUSE
    snprintf(message, sizeof(message),
        "GPU PROBE FRAMEBUFFER: run=%u phase=%u target_binds=%u..%u redundant_rebinds_skipped=%u..%u",
        gpu_probe.run, gpu_probe.phase + 1,
        gpu_probe.minimum[PROBE_FB_BINDS], gpu_probe.maximum[PROBE_FB_BINDS],
        gpu_probe.minimum[PROBE_FB_REUSES], gpu_probe.maximum[PROBE_FB_REUSES]);
    n3ds_log(message);
#endif
#ifdef HALO_N3DS_OPAQUE_COLOR_READS
    snprintf(message, sizeof(message),
        "GPU PROBE COLOR READS: run=%u phase=%u eligible_draws=%u..%u reads_elided_draws=%u..%u mask_writes=%u..%u extra_flushes=%u..%u",
        gpu_probe.run, gpu_probe.phase + 1,
        gpu_probe.minimum[PROBE_READ_ELIGIBLE], gpu_probe.maximum[PROBE_READ_ELIGIBLE],
        gpu_probe.minimum[PROBE_READ_ELIDED], gpu_probe.maximum[PROBE_READ_ELIDED],
        gpu_probe.minimum[PROBE_READ_MASK_WRITES], gpu_probe.maximum[PROBE_READ_MASK_WRITES],
        gpu_probe.minimum[PROBE_READ_EXTRA_FLUSHES], gpu_probe.maximum[PROBE_READ_EXTRA_FLUSHES]);
    n3ds_log(message);
#endif
    snprintf(message, sizeof(message),
        "GPU PROBE COUNTS: run=%u phase=%u renderer_draws=%u..%u renderer_elements=%u..%u opaque_bsp_draws=%u..%u opaque_bsp_elements=%u..%u",
        gpu_probe.run, gpu_probe.phase + 1,
        gpu_probe.minimum[PROBE_DRAWS], gpu_probe.maximum[PROBE_DRAWS],
        gpu_probe.minimum[PROBE_ELEMENTS], gpu_probe.maximum[PROBE_ELEMENTS],
        gpu_probe.minimum[PROBE_TERRAIN_DRAWS], gpu_probe.maximum[PROBE_TERRAIN_DRAWS],
        gpu_probe.minimum[PROBE_TERRAIN_ELEMENTS], gpu_probe.maximum[PROBE_TERRAIN_ELEMENTS]);
    n3ds_log(message);
}

static void gpu_probe_begin(int midpoint, u64 waited)
{
    if (!gpu_probe.active) return;
    if (!gpu_probe.selected || midpoint) {
        gpu_probe_abort("non-mission-frame");
        return;
    }
    gpu_probe.selected = 0;
    if (gpu_probe.pending_submission) {
        u32 ticks;
        int ready = n3ds_c3d_gpu_probe_sample(gpu_probe.pending_submission, &ticks);
        gpu_probe.pending_submission = 0;
        if (!ready) {
            if (++gpu_probe.missing > GPU_PROBE_MISSING_LIMIT) {
                gpu_probe_abort("queue-samples-unavailable");
                return;
            }
        } else if (gpu_probe.warmup < GPU_PROBE_WARMUP) {
            ++gpu_probe.warmup;
        } else {
            for (unsigned int i = 0; i < PROBE_COUNTERS; ++i) {
                unsigned int value = gpu_probe.draws[i];
                if (!gpu_probe.samples || value < gpu_probe.minimum[i]) gpu_probe.minimum[i] = value;
                if (!gpu_probe.samples || value > gpu_probe.maximum[i]) gpu_probe.maximum[i] = value;
            }
            gpu_probe.wait_ticks += waited;
            gpu_probe.queue_ticks[gpu_probe.samples++] = ticks;
            if (gpu_probe.samples == GPU_PROBE_SAMPLES) {
                gpu_probe_report_phase();
                if (++gpu_probe.phase == sizeof(probe_modes)) {
                    char message[112];
                    snprintf(message, sizeof(message),
                        "GPU PROBE COMPLETE: run=%u missing=%u rendering=%s", gpu_probe.run, gpu_probe.missing,
#ifdef HALO_N3DS_OPAQUE_COLOR_READS
                        "opaque-color-reads"
#elif defined(HALO_N3DS_FRAMEBUFFER_REUSE)
                        "framebuffer-reuse"
#else
                        "normal"
#endif
                    );
                    n3ds_log(message);
                    gpu_probe.active = 0;
                    return;
                }
                gpu_probe.warmup = gpu_probe.samples = 0;
                gpu_probe.wait_ticks = 0;
            }
        }
    }
    memset(gpu_probe.draws, 0, sizeof(gpu_probe.draws));
    gpu_probe.recording = 1;
}

static void gpu_probe_submit(u32 prior_submission)
{
    gpu_probe.recording = 0;
    gpu_probe.terrain = 0;
    u32 submission = n3ds_c3d_gpu_probe_submission();
    if (!submission || submission - prior_submission != 1) {
        gpu_probe_abort("queue-identity");
        return;
    }
    gpu_probe.pending_submission = submission;
}
