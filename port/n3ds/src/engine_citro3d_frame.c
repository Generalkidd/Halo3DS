/* Extend the pinned Citro3D render queue without modifying the SDK checkout.
 * This object supplies renderqueue.c's symbols ahead of the stock archive. */
#if defined(HALO_N3DS_PRESENT_INTERPOLATION) || defined(HALO_N3DS_DEPENDENCY_TRACE) || defined(HALO_N3DS_GPU_PROBE)
#include <3ds.h>
#include <c3d/uniforms.h>
#include "engine_dependency_trace.h"
static void halo_c3d_set_event_callback(GSPGPU_Event, ThreadFunc, void *, bool);
static void halo_c3d_set_queue_callback(gxCmdQueue_s *, void (*)(gxCmdQueue_s *), void *);
static void halo_c3d_run_queue(gxCmdQueue_s *);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
static bool halo_c3d_wait_queue(gxCmdQueue_s *, s64);
#define gxCmdQueueWait halo_c3d_wait_queue
#endif
#define gspSetEventCallback halo_c3d_set_event_callback
#define gxCmdQueueSetCallback halo_c3d_set_queue_callback
#define gxCmdQueueRun halo_c3d_run_queue
#include "renderqueue.c"
#undef gspSetEventCallback
#undef gxCmdQueueSetCallback
#undef gxCmdQueueRun
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
#undef gxCmdQueueWait

static u32 top_vblank_tick;
static unsigned int queue_callback_pending;
void n3ds_gpu_top_swap_posted(void);
#endif

#ifdef HALO_N3DS_GPU_PROBE
/* Include every actual GX queue, even safe transfers. Completion callbacks
 * run in submission order and can lag the queue's idle flag. A missing
 * identity rejects the sample instead of extending the ordinary GPU wait. */
enum { GPU_PROBE_QUEUE_SLOTS = 8 };
static u32 probe_queue_start[GPU_PROBE_QUEUE_SLOTS], probe_queue_ticks[GPU_PROBE_QUEUE_SLOTS];
static u32 probe_queue_issued, probe_queue_retired, probe_queue_invalid;

u32 n3ds_c3d_gpu_probe_submission(void)
{
    return __atomic_load_n(&probe_queue_issued, __ATOMIC_ACQUIRE);
}

int n3ds_c3d_gpu_probe_sample(u32 submission, u32 *ticks)
{
    u32 issued = n3ds_c3d_gpu_probe_submission();
    u32 retired = __atomic_load_n(&probe_queue_retired, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&probe_queue_invalid, __ATOMIC_ACQUIRE) || !submission ||
        submission > retired || issued - submission >= GPU_PROBE_QUEUE_SLOTS)
        return 0;
    *ticks = probe_queue_ticks[submission % GPU_PROBE_QUEUE_SLOTS];
    return *ticks != 0;
}

static void probe_queue_run(void)
{
    u32 issued = n3ds_c3d_gpu_probe_submission();
    u32 retired = __atomic_load_n(&probe_queue_retired, __ATOMIC_ACQUIRE);
    if (issued - retired >= GPU_PROBE_QUEUE_SLOTS || issued == UINT32_MAX) {
        __atomic_store_n(&probe_queue_invalid, 1, __ATOMIC_RELEASE);
        return;
    }
    probe_queue_start[(issued + 1) % GPU_PROBE_QUEUE_SLOTS] = (u32)svcGetSystemTick();
    __atomic_store_n(&probe_queue_issued, issued + 1, __ATOMIC_RELEASE);
}

static void probe_queue_complete(void)
{
    u32 retired = __atomic_load_n(&probe_queue_retired, __ATOMIC_RELAXED);
    u32 now = (u32)svcGetSystemTick();
    if (retired == n3ds_c3d_gpu_probe_submission()) {
        __atomic_store_n(&probe_queue_invalid, 1, __ATOMIC_RELEASE);
        return;
    }
    u32 slot = (retired + 1) % GPU_PROBE_QUEUE_SLOTS;
    /* The existing five-second queue watchdog is shorter than one tick wrap. */
    probe_queue_ticks[slot] = now - probe_queue_start[slot];
    __atomic_store_n(&probe_queue_retired, retired + 1, __ATOMIC_RELEASE);
}
#endif

static void halo_c3d_queue_finish(gxCmdQueue_s *queue)
{
#ifdef HALO_N3DS_GPU_PROBE
    probe_queue_complete();
#endif
    /* Cover stock swap posting as well as trace publication: an owner poll
     * must not associate a replaced pending swap with the earlier queue. */
    n3ds_trace_queue_complete_begin();
    bool top_swap = !inSafeTransfer && needSwapTop;
    onQueueFinish(queue);
    /* Queue completion and swap posting are not exact physical scanout.
     * Trace correlation is independent of optional midpoint scheduling. */
    n3ds_trace_queue_complete(top_swap);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if (top_swap)
        n3ds_gpu_top_swap_posted();
    __atomic_store_n(&queue_callback_pending, 0, __ATOMIC_RELEASE);
#endif
}

static void halo_c3d_set_queue_callback(gxCmdQueue_s *queue, void (*callback)(gxCmdQueue_s *), void *user)
{
    gxCmdQueueSetCallback(queue, callback == onQueueFinish ? halo_c3d_queue_finish : callback, user);
}

static void halo_c3d_run_queue(gxCmdQueue_s *queue)
{
    if (queue->lastEntry < queue->numEntries) {
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
        __atomic_store_n(&queue_callback_pending, 1, __ATOMIC_RELEASE);
#endif
        /* Publish correlation before the callback can run, including queues
         * outside an active capture window. This does not start extra work. */
        n3ds_trace_queue_run();
#ifdef HALO_N3DS_GPU_PROBE
        probe_queue_run();
#endif
    }
    gxCmdQueueRun(queue);
}

#ifdef HALO_N3DS_PRESENT_INTERPOLATION
static bool halo_c3d_wait_queue(gxCmdQueue_s *queue, s64 timeout_ticks)
{
    /* Pinned libctru gxqueue.c adds timeout directly to system ticks despite
     * gx.h documenting nanoseconds. Match its actual units for both waits. */
    u64 started = timeout_ticks > 0 ? svcGetSystemTick() : 0;
    if (!gxCmdQueueWait(queue, timeout_ticks)) return false;
    /* libctru marks the GX commands idle before invoking the callback. The
     * next frame and APT suspend must also wait for its framebuffer bookkeeping. */
    while (__atomic_load_n(&queue_callback_pending, __ATOMIC_ACQUIRE)) {
        if (timeout_ticks >= 0 && (!timeout_ticks || svcGetSystemTick() - started >= (u64)timeout_ticks)) return false;
        svcSleepThread(10000);
    }
    return true;
}
#endif

static void halo_c3d_top_vblank(void *data)
{
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    __atomic_store_n(&top_vblank_tick, (u32)svcGetSystemTick(), __ATOMIC_RELEASE);
#endif
    onVBlank0(data);
    n3ds_trace_vblank();
}

static void halo_c3d_set_event_callback(GSPGPU_Event event, ThreadFunc callback, void *data, bool once)
{
    if (event == GSPGPU_EVENT_VBlank0) {
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
        __atomic_store_n(&top_vblank_tick, 0, __ATOMIC_RELEASE);
#endif
        if (callback == onVBlank0)
            callback = halo_c3d_top_vblank;
    }
    gspSetEventCallback(event, callback, data, once);
}

#ifdef HALO_N3DS_PRESENT_INTERPOLATION
u32 n3ds_c3d_top_vblank_tick(void)
{
    return __atomic_load_n(&top_vblank_tick, __ATOMIC_ACQUIRE);
}

int n3ds_c3d_wait_idle(void)
{
    return !inFrame && C3Di_WaitAndClearQueue(-1);
}

#if HALO_N3DS_MIDPOINT_CAPTURE_TICK
int n3ds_c3d_capture_wait_idle(void)
{
    if(inFrame) return 0;
    u64 start=svcGetSystemTick();
    while(!C3Di_WaitAndClearQueue(0)) {
        if(aptShouldClose() || !gspHasGpuRight() || svcGetSystemTick()-start>=5ULL*SYSCLOCK_ARM11)
            return 0;
        svcSleepThread(250000);
    }
    return 1;
}

/* Return -1 only after DMA has been dispatched without proven retirement. */
int n3ds_c3d_capture_transfer(u32 *source,u32 *destination,u32 dimension,u32 bytes,int depth)
{
    if(inFrame || inSafeTransfer || aptShouldClose() || !gspHasGpuRight() ||
       !C3Di_WaitAndClearQueue(0)) return 0;
    gxCmdQueue_s *queue=&C3Di_GetContext()->gxQueue;
    /* Citro3D's VRAM texture loader uses zero input/output gaps and flag 8
     * for a contiguous raw copy. Preserve native depth/stencil tiling. */
    Result queued=depth ? GX_TextureCopy(source,0,destination,0,bytes,8) :
        GX_DisplayTransfer(source,dimension,destination,dimension,
            GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(R_FAILED(queued) || queue->numEntries!=1) {
        gxCmdQueueClear(queue);
        return 0;
    }
    inSafeTransfer=true;
    halo_c3d_run_queue(queue);
    /* Waiting on generic PPF can consume the preceding screen transfer's
     * sticky event. The queue and its completion callback own this DMA. */
    return n3ds_c3d_capture_wait_idle()?1:-1;
}
#endif

void C3Di_LightEnvDirty(C3D_LightEnv* env);
void C3Di_ProcTexDirty(C3D_Context* ctx);

void n3ds_c3d_discard_frame(void)
{
    C3D_Context* ctx = C3Di_GetContext();
    gxCmdQueue_s* queue = &ctx->gxQueue;
    /* FrameBegin stopped the queue. Draws, splits and target clears can record
     * into it but none of those commands may already have reached GSP. */
    if (!inFrame || inSafeTransfer || !gxCmdQueueWait(queue, 0) || queue->curEntry || queue->lastEntry)
        abort();

    gxCmdQueueStop(queue);
    gxCmdQueueClear(queue);
    for (C3D_RenderTarget* target = firstTarget; target; target = target->next)
        target->used = false;
    GPUCMD_SetBuffer(NULL, 0, 0);
    ctx->cmdBufUsage = 0;
    osTickCounterUpdate(&cpuTime);
    inFrame = false;

    /* The canceled draws consumed dirty bits while recording. Reinstall the
     * shadow on the next real draw, using Citro3D's APT restore as the model. */
    ctx->flags &= ~C3DiF_DrawUsed;
    ctx->flags |= C3DiF_AttrInfo | C3DiF_BufInfo | C3DiF_Effect | C3DiF_FrameBuf |
        C3DiF_Viewport | C3DiF_Scissor | C3DiF_Program | C3DiF_VshCode | C3DiF_GshCode |
        C3DiF_TexAll | C3DiF_TexStatus | C3DiF_TexEnvBuf | C3DiF_TexEnvAll |
        C3DiF_LightEnv | C3DiF_Gas;
    C3Di_DirtyUniforms(GPU_VERTEX_SHADER);
    C3Di_DirtyUniforms(GPU_GEOMETRY_SHADER);
    /* The pinned library clears its integer-uniform history on upload. */
    for (int i = 0; i < C3D_IVUNIF_COUNT; ++i) {
        C3D_IVUnifDirty[GPU_VERTEX_SHADER][i] = true;
        C3D_IVUnifDirty[GPU_GEOMETRY_SHADER][i] = true;
    }
    ctx->fixedAttribDirty |= ctx->fixedAttribEverDirty;
    ctx->gasFlags |= C3DiG_BeginAcc | C3DiG_AccStage | C3DiG_SetAccMax | C3DiG_RenderStage;
    if (ctx->fogLut)
        ctx->flags |= C3DiF_FogLut;
    if (ctx->gasLut)
        ctx->flags |= C3DiF_GasLut;
    if (ctx->lightEnv)
        C3Di_LightEnvDirty(ctx->lightEnv);
    C3Di_ProcTexDirty(ctx);
}
#endif
#endif
