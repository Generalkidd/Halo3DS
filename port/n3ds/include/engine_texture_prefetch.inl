/* The render thread owns cache slots, map pointers and unpublished C3D
 * resources. The worker uses only copied plans and private pixel pointers. */
#if (defined(HALO_N3DS_TEXTURE_PREFETCH) || defined(HALO_N3DS_PRESENT_INTERPOLATION)) && !defined(HALO_N3DS_ARCHIVE_VERIFY)
enum { TEXTURE_HINT_LIMIT=24, TEXTURE_STAGE_LIMIT=64*1024, TEXTURE_QUEUE_STAGE_LIMIT=256*1024,
    TEXTURE_CUBE_STAGE_LIMIT=6*64*64*4 };
static const struct bitmap_data *texture_hints[TEXTURE_HINT_LIMIT];
static const struct bitmap_data *texture_midpoint_miss;
static unsigned int texture_hint_count;
static struct texture_prefetch_job {
    const struct bitmap_data *bitmap;
    struct bitmap_plan plan;
    struct native_cube_layout cube;
    int cube_format;
    byte *source;
    void *resource;
    unsigned int *images[6];
    unsigned short *distance;
    unsigned int generation, archive_hash, pending_slot;
    enum texture_lane lane;
    int archived;
} texture_job;

static int texture_prefetch_enabled(void)
{
    return initialized && opened && !n3ds_input_platform_original_model() && !menu_cache();
}

static int texture_queue_async_enabled(void)
{
#if defined(HALO_N3DS_RENDERER_TESTS) || HALO_N3DS_MISSION_TICKS
    /* The existing diagnostic fixture services its queue without a frontend kick. */
    return 0;
#else
    return texture_prefetch_enabled();
#endif
}

static void texture_prefetch_note_miss(const struct bitmap_data *bitmap)
{
    if(!texture_midpoint_miss) texture_midpoint_miss=bitmap;
}

static void texture_prefetch_prioritize_pending(const struct bitmap_data *bitmap,unsigned int slot)
{
    if(texture_job.bitmap==bitmap && texture_job.lane==TEXTURE_PREFETCH && texture_queue_async_enabled()) {
        texture_job.lane=TEXTURE_QUEUED;
        texture_job.pending_slot=slot;
    }
}

static void texture_hint_remove(unsigned int index)
{
    --texture_hint_count;
    memmove(texture_hints+index,texture_hints+index+1,
        (texture_hint_count-index)*sizeof(texture_hints[0]));
}

static void texture_prefetch_forget(const struct bitmap_data *bitmap)
{
    if(texture_job.bitmap==bitmap) {
        texture_job.bitmap=NULL;
        n3ds_texture_reader_cancel();
    }
    if(texture_midpoint_miss==bitmap) texture_midpoint_miss=NULL;
    for(unsigned int i=0;i<texture_hint_count;++i) {
        if(texture_hints[i]==bitmap) {texture_hint_remove(i);break;}
    }
}

static void texture_prefetch_clear(int shutdown)
{
    texture_hint_count=0;
    texture_midpoint_miss=NULL;
    texture_job.bitmap=NULL;
    n3ds_texture_reader_cancel();
    if(shutdown) {
        n3ds_texture_reader_shutdown();
        free(texture_job.source);
        n3ds_gpu_texture_abandon(texture_job.resource);
        n3ds_algorithm_profile_staging_release(texture_job.plan.native.scratch_bytes+texture_job.plan.native.resident_bytes);
        memset(&texture_job,0,sizeof(texture_job));
    }
}

int n3ds_engine_texture_prefetch(const struct bitmap_data *bitmap)
{
    if(!texture_prefetch_enabled() || texture_hint_count>=TEXTURE_HINT_LIMIT || bitmap==texture_job.bitmap) return 0;
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    if(!bitmap || n3ds_cache_resolve(view,bitmap,sizeof(*bitmap))!=bitmap || (bitmap->type!=0 && bitmap->type!=2) ||
            bitmap->hardware_format) return 0;
    for(unsigned int i=0;i<texture_hint_count;++i) if(texture_hints[i]==bitmap) return 0;
    texture_hints[texture_hint_count++]=bitmap;
    return 1;
}

int n3ds_engine_texture_prewarm(const struct bitmap_data *bitmap)
{
    if(!texture_prefetch_enabled() || n3ds_gpu_frame_active() || !owns_bitmap(bitmap) ||
            bitmap->type!=0 || bitmap->hardware_format) return 0;
    struct bitmap_plan plan;
    if(!bitmap_plan(bitmap,&plan) || plan.native.source_bytes>TEXTURE_STAGE_LIMIT || plan.native.resident_bytes>TEXTURE_STAGE_LIMIT) return 0;
    unsigned int before=texture_count;
    load_bitmap_now((struct bitmap_data *)bitmap,TEXTURE_PREFETCH,0);
    return texture_count>before;
}

static int texture_prepare_pixels(void *context)
{
    const struct texture_prefetch_job *job=context;
    if(job->cube.side) {
        for(unsigned int face=0;face<6;++face)
            if(n3ds_texture_reader_cancelled() || !cube_decode_face(&job->cube,job->cube_format,
                job->source+face*job->cube.bytes,job->images[face])) return 0;
        return 1;
    }
    if(job->archived) return texture_hash(job->images[0],job->plan.native.face[0].bytes)==job->archive_hash;
    return bitmap_decode_sampled(&job->plan,job->source,job->images[0],job->distance);
}

static int texture_prefetch_submit(const struct bitmap_data *bitmap,enum texture_lane lane,unsigned int pending_slot)
{
    struct bitmap_plan plan={0};struct native_cube_layout cube={0};
    int queued=lane==TEXTURE_QUEUED;
    if(!owns_bitmap(bitmap) || bitmap->hardware_format) return 0;
    int is_cube=bitmap->type==2;
    if(is_cube) {
        if(!cube_layout(bitmap,MIN(texture_resolution,64),&cube)) return 0;
        plan.target_width=plan.target_height=cube.target;
        if(!n3ds_texture_descriptor_rgba8(&plan.native,cube.target,cube.target,6,cube.level,
                cube.bytes*6,(cube.bytes*6+3)&~3u)) return 0;
    } else if(!bitmap_plan(bitmap,&plan) || plan.detail_levels>1 || plan.detail_rgb8) {
        /* Authored world details use the bounded synchronous loader. Do not
         * publish a single RGBA8 level ahead of the requested representation. */
        return 0;
    }
    unsigned int stage_limit=queued?TEXTURE_QUEUE_STAGE_LIMIT:is_cube?TEXTURE_CUBE_STAGE_LIMIT:TEXTURE_STAGE_LIMIT;
    /* One bounded unpublished image sits outside residency accounting. Never
     * evict to reserve it, and preserve 4 MiB of linear headroom for demand. */
    if(plan.native.resident_bytes>stage_limit || !texture_can_stage(plan.native.resident_bytes)) return 0;
    struct texture_archive_selection entry={0};
    int archived=!is_cube && texture_archive_select(bitmap,plan.resolution,
        plan.source_offset,plan.native.source_bytes,plan.target_width,plan.target_height,plan.lightmap,
        plan.level,(unsigned int)plan.format,plan.width,plan.height,1,&entry);
    if(archived && (entry.format!=NATIVE_TEXTURE_FORMAT_RGBA8 || entry.codec!=TEXTURE_ARCHIVE_RAW)) return 0;
    char path[128];
    struct native_texture_read_range ranges[NATIVE_TEXTURE_READ_RANGES]={0};
    unsigned int range_count=is_cube?6:1,read_bytes;
    unsigned int read_limit=queued?NATIVE_TEXTURE_READ_LIMIT:is_cube?TEXTURE_CUBE_STAGE_LIMIT:TEXTURE_STAGE_LIMIT;
    if(is_cube) {
        if(cube.bytes>read_limit/6) return 0;
        read_bytes=cube.bytes*6;
        for(unsigned int face=0;face<6;++face) {
            unsigned int offset=bitmap->pixels_offset+face*cube.stride+cube.skip;
            if(!n3ds_engine_cache_read_path(offset,cube.bytes,path)) return 0;
            ranges[face].offset=offset;ranges[face].bytes=cube.bytes;
        }
    } else read_bytes=archived?entry.bytes:plan.native.source_bytes;
    if(archived) {
        if(!texture_data_path(n3ds_engine_cache_view(0),entry.kind==TEXTURE_ARCHIVE_KIND_CANDIDATE?"ntx4":"ntx",path)) return 0;
    } else if(!is_cube && (plan.native.source_bytes>read_limit ||
            !n3ds_engine_cache_read_path(plan.source_offset,plan.native.source_bytes,path))) return 0;
    unsigned int distance_bytes=!archived && plan.lightmap?plan.native.resident_bytes/2:0;
    unsigned int source_bytes=archived?0:((read_bytes+3)&~3u);
    /* This selected lane's actual scratch is separate from canonical source
     * and GPU residency bytes; archive workers need no second image buffer. */
    plan.native.scratch_bytes=source_bytes+distance_bytes;
    byte *source=source_bytes?(malloc)(source_bytes+distance_bytes):NULL;
    if(source_bytes && !source) {TEXTURE_PROFILE(++texture_profile.allocation_failures);return 0;}
    if(source)n3ds_algorithm_profile_staging_acquire(plan.native.scratch_bytes);
    struct native_texture_span images[6];
    void *resource=n3ds_gpu_texture_reserve_descriptor(&plan.native,images);
    if(!resource) {
        if(source)n3ds_algorithm_profile_staging_release(plan.native.scratch_bytes);
        free(source);TEXTURE_PROFILE(++texture_profile.allocation_failures);return 0;
    }
    n3ds_algorithm_profile_staging_acquire(plan.native.resident_bytes);
    if(is_cube) for(unsigned int face=0;face<6;++face) ranges[face].destination=source+face*cube.bytes;
    else ranges[0]=(struct native_texture_read_range){archived?entry.offset:plan.source_offset,read_bytes,archived?images[0].data:source};
    texture_job=(struct texture_prefetch_job){.bitmap=bitmap,.plan=plan,.cube=cube,.cube_format=bitmap->format,
        .source=source,.resource=resource,.distance=distance_bytes?(unsigned short *)(source+source_bytes):NULL,
        .generation=n3ds_engine_cache_generation(),.archive_hash=archived?entry.hash:0,.pending_slot=pending_slot,
        .lane=lane,.archived=archived?(int)entry.kind:0};
    for(unsigned int face=0;face<6;++face) texture_job.images[face]=images[face].data;
    if(!n3ds_texture_reader_submit_ranges(path,ranges,range_count,texture_prepare_pixels,&texture_job)) {
        free(source);n3ds_gpu_texture_abandon(resource);
        n3ds_algorithm_profile_staging_release(plan.native.scratch_bytes+plan.native.resident_bytes);
        memset(&texture_job,0,sizeof(texture_job));
        return 0;
    }
    TEXTURE_PROFILE(++texture_profile.prefetch_submitted);
    TEXTURE_PROFILE(++texture_load_profile(lane)->attempts);
    return 1;
}

static int texture_prefetch_collect(enum texture_lane lane)
{
    if(!texture_job.resource) return 0;
    struct native_texture_read_result stats;
    int ready=n3ds_texture_reader_poll(&stats);
    if(!ready) return 0;
    /* Worker totals and the submitting lane below describe the same work;
     * they must not be added together as frame latency. Charge background
     * work when collected, even if it began before the last profile reset.
     * Publication below is charged to the collecting lane, which may be a
     * demand draw consuming an earlier queued/prefetched result. */
    TEXTURE_PROFILE(texture_profile.worker_read_ticks+=stats.read_ticks);
    TEXTURE_PROFILE(texture_profile.worker_prepare_ticks+=stats.prepare_ticks);
    TEXTURE_PROFILE(texture_profile.worker_read_bytes+=stats.bytes);
    TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->read_ticks+=stats.read_ticks);
    TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->decode_ticks+=stats.prepare_ticks);
    TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->read_bytes+=stats.bytes);
    struct bitmap_data *bitmap=(struct bitmap_data *)texture_job.bitmap;
    const struct bitmap_plan *plan=&texture_job.plan;
    unsigned int slot=texture_job.pending_slot;
    int pending_job=bitmap && slot<pending_limit && pending[slot]==bitmap;
    if(texture_job.generation!=n3ds_engine_cache_generation() || !owns_bitmap(bitmap) || bitmap->hardware_format ||
            (lane==TEXTURE_QUEUED && !pending_job)) {
        if(pending_job) texture_pending_remove(slot);
        TEXTURE_PROFILE(++texture_profile.prefetch_discarded);
    } else if(ready<0) {
        TEXTURE_PROFILE(++texture_load_profile(texture_job.lane)->failures);
        if(texture_job.archived) texture_archive_reject_payload((unsigned int)texture_job.archived);
        else if(pending_job) pending_synchronous[slot]=1;
    } else if(!(lane==TEXTURE_PREFETCH?texture_can_stage_allocated(plan->native.resident_bytes,1):texture_make_room_allocated(plan->native.resident_bytes,1))) {
        TEXTURE_PROFILE(++texture_profile.prefetch_discarded);
    } else {
#if HALO_N3DS_TEXTURE_PROFILE
        long long start=n3ds_engine_ticks();
#endif
        void *resource=texture_job.cube.side?texture_publish(bitmap,texture_job.resource,&plan->native,lane==TEXTURE_PREFETCH):
            bitmap_publish(bitmap,plan,texture_job.resource,lane==TEXTURE_PREFETCH);
        if(resource) texture_job.resource=NULL;
        TEXTURE_PROFILE(texture_load_profile(lane)->upload_ticks+=n3ds_engine_ticks()-start);
        TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->archive_hits+=texture_job.archived!=0);
        TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->loads+=resource!=NULL);
        TEXTURE_PROFILE(texture_load_profile(texture_job.lane)->failures+=resource==NULL);
        if(texture_job.archived) texture_archive_record_hit((unsigned int)texture_job.archived);
        if(pending_job && resource) texture_pending_remove(slot);
    }
    free(texture_job.source);
    n3ds_gpu_texture_abandon(texture_job.resource);
    n3ds_algorithm_profile_staging_release(plan->native.scratch_bytes+plan->native.resident_bytes);
    memset(&texture_job,0,sizeof(texture_job));
    return 1;
}

static int texture_prefetch_finish_for(const struct bitmap_data *bitmap,enum texture_lane lane)
{
    if(texture_job.bitmap!=bitmap) return 0;
    if(texture_prefetch_collect(lane)) return 1;
    /* A required draw takes the normal path immediately if the job is late.
     * Stop further worker reads between SD chunks to limit contention. */
    TEXTURE_PROFILE(++texture_profile.prefetch_late);
    texture_job.bitmap=NULL;
    n3ds_texture_reader_cancel();
    return 0;
}

static void texture_prefetch_service(int queued_only)
{
    /* A canceled job has no upload work left. Retire its finished scratch
     * even when regular compatibility loads consume the optional budget. */
    if(!queued_only || texture_job.lane==TEXTURE_QUEUED || !texture_job.bitmap)
        texture_prefetch_collect(texture_job.lane);
}

void n3ds_engine_texture_prefetch_kick(int speculative)
{
    if(!texture_prefetch_enabled()) return;
    int async=texture_queue_async_enabled();
    if(texture_job.resource) {
        /* A required queue outranks unrelated read-ahead. Same-bitmap jobs
         * are already promoted by texture_prefetch_prioritize_pending. */
        if(texture_job.bitmap && texture_job.lane==TEXTURE_PREFETCH && async &&
                texture_pending_oldest(-1)!=NATIVE_TEXTURE_SLOTS) {
            texture_job.bitmap=NULL;
            n3ds_texture_reader_cancel();
        }
        if(!texture_job.bitmap) texture_prefetch_collect(texture_job.lane);
        if(texture_job.resource) return;
    }
    if(!pending_limit && (!speculative || (!texture_midpoint_miss && !texture_hint_count))) return;
#if HALO_N3DS_TEXTURE_PROFILE
    long long start=n3ds_engine_ticks();
#endif
    if(async) {
        for(unsigned int tries=0;tries<4;++tries) {
            unsigned int slot=texture_pending_oldest(0);
            if(slot==NATIVE_TEXTURE_SLOTS) break;
            if(texture_prefetch_submit(pending[slot],TEXTURE_QUEUED,slot)) goto done;
            pending_synchronous[slot]=1;
        }
    }
    /* Compatibility reads also have priority over starting fresh read-ahead. */
    if(speculative && (!async || !pending_limit)) {
        for(unsigned int tries=0;tries<4 && (texture_midpoint_miss || texture_hint_count);++tries) {
            const struct bitmap_data *bitmap=texture_midpoint_miss;
            if(bitmap) texture_midpoint_miss=NULL;
            else {bitmap=texture_hints[0];texture_hint_remove(0);}
            if(texture_prefetch_submit(bitmap,TEXTURE_PREFETCH,NATIVE_TEXTURE_SLOTS)) break;
        }
    }
done:
    TEXTURE_PROFILE(texture_profile.maintenance_ticks+=n3ds_engine_ticks()-start);
}
#else
int n3ds_engine_texture_prefetch(const struct bitmap_data *bitmap) {(void)bitmap;return 0;}
int n3ds_engine_texture_prewarm(const struct bitmap_data *bitmap) {(void)bitmap;return 0;}
void n3ds_engine_texture_prefetch_kick(int speculative) {(void)speculative;}
static int texture_queue_async_enabled(void) {return 0;}
static void texture_prefetch_note_miss(const struct bitmap_data *bitmap) {(void)bitmap;}
static void texture_prefetch_prioritize_pending(const struct bitmap_data *bitmap,unsigned int slot) {(void)bitmap;(void)slot;}
static void texture_prefetch_clear(int shutdown) {(void)shutdown;}
static void texture_prefetch_forget(const struct bitmap_data *bitmap) {(void)bitmap;}
static int texture_prefetch_finish_for(const struct bitmap_data *bitmap,enum texture_lane lane) {(void)bitmap;(void)lane;return 0;}
static void texture_prefetch_service(int queued_only) {(void)queued_only;}
#endif
