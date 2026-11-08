/* Presentation snapshots are outside the saved game. The regular draw still
 * consumes the live state, including the same effects, material and HUD paths. */
struct presentation_snapshot {
    struct render_window window;
    unsigned int tick, generation;
    long long tick_start;
    long unit_index;
    short zoom;
    director_perspective perspective;
    unsigned int cluster_mode;
    short cluster_index;
    boolean cluster_known;
};

enum { MIDPOINT_SAMPLE_MAX_AGE = 30 };
static struct {
    struct presentation_snapshot previous;
    long long key_prepare_ticks, midpoint_prepare_ticks, midpoint_submit_deadline;
    long long queue_ticks[2];
    long long rejected_ticks;
    unsigned int stable_ticks, cooldown, midpoint_sample_age;
    unsigned int submitted, latched, budget_skips, context_skips, busy_skips, texture_skips, deadline_skips;
    unsigned int last_key_latches;
    int has_previous, midpoint;
} presentation;
/* Set only while recording a midpoint, cleared before regular rendering. */
int n3ds_presentation_reject_reason;

int n3ds_presentation_midpoint_active(void)
{
    return presentation.midpoint;
}

int n3ds_presentation_reject_texture(void)
{
    if (!presentation.midpoint)
        return 0;
    if (!n3ds_presentation_reject_reason)
        n3ds_presentation_reject_reason = NATIVE_MIDPOINT_TEXTURE;
    return 1;
}

int n3ds_presentation_check_midpoint(void)
{
    if (!presentation.midpoint) return 0;
    if (!n3ds_presentation_reject_reason && n3ds_engine_ticks() >= presentation.midpoint_submit_deadline)
        n3ds_presentation_reject_reason = NATIVE_MIDPOINT_DEADLINE;
    return n3ds_presentation_reject_reason != 0;
}

static void presentation_forget_history(void)
{
    presentation.has_previous = 0;
    n3ds_presentation_poses_reset();
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
    n3ds_presentation_particles_reset();
#endif
    n3ds_presentation_weapon_reset();
}

void n3ds_presentation_reset(void)
{
    presentation_forget_history();
    n3ds_presentation_reject_reason = 0;
    presentation.key_prepare_ticks = presentation.midpoint_prepare_ticks = 0;
    presentation.queue_ticks[0] = presentation.queue_ticks[1] = 0;
    presentation.stable_ticks = presentation.cooldown = 0;
    presentation.midpoint_sample_age = MIDPOINT_SAMPLE_MAX_AGE;
}

static int presentation_camera_midpoint(const struct render_camera *a,
    const struct render_camera *b, struct render_camera *result)
{
    if (a->mirrored || b->mirrored || a->z_near != b->z_near || a->z_far != b->z_far ||
        memcmp(&a->viewport_bounds, &b->viewport_bounds, sizeof(a->viewport_bounds)) ||
        memcmp(&a->window_bounds, &b->window_bounds, sizeof(a->window_bounds)) ||
        distance_squared3d(&a->position, &b->position) > 2.f * 2.f ||
        dot_product3d(&a->forward, &b->forward) < .82f ||
        dot_product3d(&a->up, &b->up) < .82f ||
        fabsf(a->vertical_field_of_view - b->vertical_field_of_view) > .12f)
        return 0;

    *result = *b;
    for (int axis = 0; axis < 3; ++axis) {
        result->position.n[axis] = .5f * (a->position.n[axis] + b->position.n[axis]);
        result->forward.n[axis] = a->forward.n[axis] + b->forward.n[axis];
        result->up.n[axis] = a->up.n[axis] + b->up.n[axis];
    }
    real_vector3d right;
    normalize3d(&result->forward);
    cross_product3d(&result->forward, &result->up, &right);
    if (normalize3d(&right) == 0.f)
        return 0;
    cross_product3d(&right, &result->forward, &result->up);
    result->vertical_field_of_view = .5f * (a->vertical_field_of_view + b->vertical_field_of_view);
    return 1;
}

static int presentation_window_midpoint(struct presentation_snapshot *current,
    struct render_window *midpoint)
{
    const struct presentation_snapshot *previous = &presentation.previous;
    if (!presentation.has_previous || current->tick - previous->tick != 1u ||
        current->tick_start - previous->tick_start > n3ds_engine_tick_frequency() / 10 ||
        current->generation != previous->generation || current->unit_index != previous->unit_index ||
        current->zoom != previous->zoom || current->perspective != previous->perspective)
        return 0;

    *midpoint = current->window;
    if (!presentation_camera_midpoint(&previous->window.render_camera,
            &current->window.render_camera, &midpoint->render_camera) ||
        !presentation_camera_midpoint(&previous->window.rasterizer_camera,
            &current->window.rasterizer_camera, &midpoint->rasterizer_camera))
        return 0;

    /* The generation check above fixes the BSP. Reuse the previous result
     * only under the same floating-point mode used to classify its point. */
    const unsigned int mode = __builtin_arm_get_fpscr() & 0x03ffff00u;
    const real_point3d *from_point = &previous->window.render_camera.position;
    const real_point3d *to_point = &current->window.render_camera.position;
    struct location location;
    short from_cluster;
    if (previous->cluster_known && previous->cluster_mode == mode) {
        from_cluster = previous->cluster_index;
    } else {
        scenario_location_from_point(&location, from_point);
        from_cluster = location.cluster_index;
    }
    current->cluster_mode = mode;
    current->cluster_known = TRUE;
    current->cluster_index = from_cluster;
    if (memcmp(from_point, to_point, sizeof(*to_point))) {
        scenario_location_from_point(&location, to_point);
        current->cluster_index = location.cluster_index;
    }

    /* Leave portal and fog transitions to the regular frame. */
    if (from_cluster == NONE || from_cluster != current->cluster_index)
        return 0;
    if (!memcmp(&midpoint->render_camera.position, to_point, sizeof(*to_point)))
        return 1;
    scenario_location_from_point(&location, &midpoint->render_camera.position);
    return location.cluster_index == from_cluster;
}

static int presentation_budget_fits(long long tick_start)
{
    if (presentation.stable_ticks < 4 || presentation.cooldown) return 0;
    long long key_gpu = MAX(presentation.queue_ticks[0], n3ds_gpu_present_cost_ticks(0));
    long long midpoint_gpu = presentation.queue_ticks[1];
    long long refresh = n3ds_gpu_top_refresh_ticks();
    if (!key_gpu || key_gpu >= refresh) return 0;
    if (!midpoint_gpu) midpoint_gpu = key_gpu;
    long long now = n3ds_engine_ticks();
    long long first_blank = n3ds_gpu_next_top_blank(now);
    if (!first_blank) return 0;
    long long frequency = n3ds_engine_tick_frequency();
    long long guard = frequency / 500;
    long long key_finish = MIN(tick_start + frequency / 30, first_blank + refresh);
    /* The next buffer cannot transfer until the midpoint has latched. CPU
     * preparation of the key can run between midpoint retirement and VBlank. */
    if (first_blank + key_gpu + guard >= key_finish) return 0;
    long long latest_midpoint_done = MIN(first_blank, key_finish - presentation.key_prepare_ticks - key_gpu);
    presentation.midpoint_submit_deadline = latest_midpoint_done - midpoint_gpu - guard;
    long long midpoint_cpu = presentation.midpoint_prepare_ticks;
    if (!midpoint_cpu) midpoint_cpu = presentation.key_prepare_ticks;
    return now + midpoint_cpu < presentation.midpoint_submit_deadline;
}

static void presentation_observe(long long tick_start, long long key_begin,
    unsigned int texture_revision, int submitted, int rejection)
{
    long long now = n3ds_engine_ticks();
    long long prepare = MAX(1LL, now - key_begin - n3ds_gpu_frame_wait_ticks());
    /* Keep spikes in the estimate and let them decay over subsequent frames. */
    presentation.key_prepare_ticks = MAX(prepare,
        presentation.key_prepare_ticks - presentation.key_prepare_ticks / 8);
    long long key_peak = presentation.queue_ticks[0];
    presentation.queue_ticks[0] = MAX(n3ds_gpu_present_cost_ticks(0), key_peak - key_peak / 8);
    if (submitted) presentation.midpoint_sample_age = 0;
    else if (presentation.midpoint_sample_age < MIDPOINT_SAMPLE_MAX_AGE) ++presentation.midpoint_sample_age;
    if (presentation.midpoint_sample_age < MIDPOINT_SAMPLE_MAX_AGE) {
        long long mid_peak = presentation.queue_ticks[1];
        presentation.queue_ticks[1] = MAX(n3ds_gpu_present_cost_ticks(1), mid_peak - mid_peak / 8);
    } else {
        /* A single expensive midpoint must not disable all future attempts. */
        presentation.queue_ticks[1] = presentation.midpoint_prepare_ticks = 0;
    }

    int latched = submitted && n3ds_gpu_midpoint_latched();
    presentation.latched += latched;
    if ((submitted && !latched) || now - tick_start + n3ds_gpu_present_cost_ticks(0) >= n3ds_engine_tick_frequency() / 30 ||
        texture_revision != n3ds_engine_texture_presentation_revision()) {
        presentation.cooldown = 8;
        presentation.stable_ticks = 0;
    } else if (rejection) {
        /* A surface visible only at the midpoint should not repeatedly
         * spend the optional slot discovering the same cold texture. */
        presentation.cooldown = 3;
    } else if (presentation.cooldown) {
        --presentation.cooldown;
    } else if (presentation.stable_ticks < 4) {
        ++presentation.stable_ticks;
    }
}

static void presentation_report(long time, int eligible)
{
    static unsigned int frames, eligible_frames;
    static long long previous_report;
    eligible_frames += !!eligible;
    if (++frames < 30)
        return;
    const long long now = n3ds_engine_ticks();
    double ms = 1000.0 / n3ds_engine_tick_frequency();
    const double wall_ms = previous_report ? (now - previous_report) * ms : 0.0;
    previous_report = now;
    char message[448];
    unsigned int key_latches = n3ds_gpu_keyframe_latches();
    snprintf(message, sizeof(message),
        "MIDPOINT PRESENT: time=%ld frames=%u eligible=%u wall_ms=%.2f submitted=%u latched=%u key_latched=%u budget=%u context=%u busy=%u texture=%u deadline=%u key_cpu_ms=%.2f key_queue_ms=%.2f mid_cpu_ms=%.2f mid_queue_ms=%.2f discarded_cpu_ms=%.2f",
        time, frames, eligible_frames, wall_ms,
        presentation.submitted, presentation.latched, key_latches-presentation.last_key_latches, presentation.budget_skips,
        presentation.context_skips, presentation.busy_skips, presentation.texture_skips, presentation.deadline_skips,
        presentation.key_prepare_ticks*ms, presentation.queue_ticks[0]*ms,
        presentation.midpoint_prepare_ticks*ms, presentation.queue_ticks[1]*ms, presentation.rejected_ticks*ms);
    n3ds_log(message);
    frames = eligible_frames = 0;
    presentation.last_key_latches = key_latches;
    presentation.submitted = presentation.latched = presentation.budget_skips =
        presentation.context_skips = presentation.busy_skips = presentation.texture_skips = presentation.deadline_skips = 0;
    presentation.rejected_ticks = 0;
}
