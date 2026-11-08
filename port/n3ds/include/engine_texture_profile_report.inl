/* Sample after the last eye and HUD. Demand loads in draw calls would be
 * missed by the texture-idle sample at the start of rasterizer submission. */
static void texture_profile_add(struct native_texture_load_profile *sum,
    const struct native_texture_load_profile *frame)
{
    sum->read_ticks += frame->read_ticks;
    sum->decode_ticks += frame->decode_ticks;
    sum->tile_ticks += frame->tile_ticks;
    sum->upload_ticks += frame->upload_ticks;
    sum->attempts += frame->attempts;
    sum->loads += frame->loads;
    sum->failures += frame->failures;
    sum->read_bytes += frame->read_bytes;
    sum->archive_hits += frame->archive_hits;
}

static void texture_profile_log_loads(const char *kind, long time, unsigned int frames,
    const struct native_texture_load_profile *p, double per_frame_ms)
{
    char message[300];
    snprintf(message, sizeof(message),
        "TEXTURE LOAD PROFILE: time=%ld frames=%u kind=%s attempts=%u loads=%u failures=%u read_bytes=%u archive_hits=%u mean_read_ms=%.3f mean_decode_ms=%.3f mean_tile_ms=%.3f mean_upload_ms=%.3f",
        time, frames, kind, p->attempts, p->loads, p->failures, p->read_bytes, p->archive_hits,
        p->read_ticks * per_frame_ms, p->decode_ticks * per_frame_ms,
        p->tile_ticks * per_frame_ms, p->upload_ticks * per_frame_ms);
    n3ds_log(message);
}

static void texture_profile_observe(long time, int report, unsigned int frames)
{
    static struct native_texture_profile sum;
    static long long peak_demand_ticks;
    static long peak_demand_frame;
    struct native_texture_profile frame;
    n3ds_engine_texture_profile_read(&frame);
    texture_profile_add(&sum.demand, &frame.demand);
    texture_profile_add(&sum.queued, &frame.queued);
    texture_profile_add(&sum.prefetch, &frame.prefetch);
    long long demand_ticks = frame.demand.read_ticks + frame.demand.decode_ticks +
        frame.demand.tile_ticks + frame.demand.upload_ticks;
    if (demand_ticks > peak_demand_ticks) {
        peak_demand_ticks = demand_ticks;
        peak_demand_frame = render.frame_index;
    }
    sum.maintenance_ticks += frame.maintenance_ticks;
    sum.fence_ticks += frame.fence_ticks;
    sum.worker_read_ticks += frame.worker_read_ticks;
    sum.worker_prepare_ticks += frame.worker_prepare_ticks;
    sum.worker_read_bytes += frame.worker_read_bytes;
    sum.demand_hits += frame.demand_hits;
    sum.demand_evictions += frame.demand_evictions;
    sum.maintenance_evictions += frame.maintenance_evictions;
    sum.slot_evictions += frame.slot_evictions;
    sum.byte_evictions += frame.byte_evictions;
    sum.headroom_evictions += frame.headroom_evictions;
    sum.allocation_failures += frame.allocation_failures;
    sum.prefetch_submitted += frame.prefetch_submitted;
    sum.prefetch_late += frame.prefetch_late;
    sum.prefetch_discarded += frame.prefetch_discarded;
    if (!report)
        return;

    double ms = 1000.0 / n3ds_engine_tick_frequency();
    double per_frame_ms = ms / frames;
    texture_profile_log_loads("demand", time, frames, &sum.demand, per_frame_ms);
    texture_profile_log_loads("queued", time, frames, &sum.queued, per_frame_ms);
    texture_profile_log_loads("prefetch", time, frames, &sum.prefetch, per_frame_ms);
    char message[384];
    snprintf(message, sizeof(message),
        "TEXTURE CACHE PROFILE: time=%ld frames=%u bytes=%u slots=%u budget=%u linear_free=%u demand_hits=%u demand_evictions=%u maintenance_evictions=%u slot_pressure=%u byte_pressure=%u headroom_pressure=%u alloc_fail=%u mean_maintenance_ms=%.3f mean_fence_ms=%.3f peak_demand_ms=%.3f peak_frame=%ld",
        time, frames, frame.resident_bytes, frame.resident_slots, frame.budget_bytes, frame.linear_free,
        sum.demand_hits, sum.demand_evictions, sum.maintenance_evictions,
        sum.slot_evictions, sum.byte_evictions, sum.headroom_evictions, sum.allocation_failures,
        sum.maintenance_ticks * per_frame_ms, sum.fence_ticks * per_frame_ms,
        peak_demand_ticks * ms, peak_demand_frame);
    n3ds_log(message);
    snprintf(message, sizeof(message),
        "TEXTURE WORKER PROFILE: time=%ld frames=%u submitted=%u late=%u discarded=%u read_bytes=%u worker_read_ms=%.3f worker_prepare_ms=%.3f",
        time, frames, sum.prefetch_submitted, sum.prefetch_late, sum.prefetch_discarded,
        sum.worker_read_bytes, sum.worker_read_ticks * ms, sum.worker_prepare_ticks * ms);
    n3ds_log(message);
    memset(&sum, 0, sizeof(sum));
    peak_demand_ticks = peak_demand_frame = 0;
}
