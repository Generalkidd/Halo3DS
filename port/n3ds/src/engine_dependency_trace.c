#include "engine_dependency_trace.h"

#ifdef HALO_N3DS_DEPENDENCY_TRACE
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void n3ds_log(const char *message);

/* The owner alone appends to the retained journal. GPU and VBlank callbacks
 * each have a separate SPSC mailbox. No callbacks allocate, log, touch game
 * state, or use the aggregate profiler. Worker timestamps arrive after join. */
enum { TRACE_OWNER, TRACE_GPU, TRACE_VBLANK, TRACE_JOINED_JOB };
struct trace_record {
    uint64_t timestamp;
    struct n3ds_trace_context context;
    uint32_t sequence, arg0, arg1;
    uint16_t event;
    uint8_t producer, flags;
};
typedef char trace_record_size[(sizeof(struct trace_record) == 64) ? 1 : -1];
struct trace_ring {
    struct trace_record records[N3DS_TRACE_CALLBACK_CAPACITY];
    uint32_t write, read, dropped, sequence;
};
struct trace_queue_token {
    struct n3ds_trace_context context;
    uint32_t captured;
};
static struct trace_record journal[N3DS_TRACE_CAPACITY];
static struct trace_ring gpu_events, blank_events;
/* Every observed queue run gets a token, even before capture, so a late
 * pre-window callback cannot steal the first captured queue's identity. */
static struct trace_queue_token queue_tokens[N3DS_TRACE_QUEUE_CAPACITY];
static uint32_t queue_write, queue_read, identity_lost;
static uint32_t capture_active, posted_sequence, blank_sequence;
static uint32_t callback_version;
static struct n3ds_trace_context current, pending_top;
static uint32_t count, owner_sequence, owner_dropped, attempt_serial, view_serial;
static uint32_t queue_serial, job_serial, loops, pending_top_sequence;
static uint32_t start_pending, stop_pending, stop_posted, stop_blank, stop_reason;
static uint64_t run_id, start_tick, stop_tick;
static int initialized, started, stopped, finished, have_tick;

static uint32_t load32(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void store32(uint32_t *p, uint32_t value) { __atomic_store_n(p, value, __ATOMIC_RELEASE); }
static void increment_saturated(uint32_t *p)
{
    /* Only one writer owns each counter. Readers use acquire loads. */
    uint32_t value = load32(p);
    if (value != UINT32_MAX) store32(p, value + 1);
}
static uint64_t clock_now(void) { return svcGetSystemTick(); }

static struct trace_record record_make(const struct n3ds_trace_context *context,
    unsigned int event, uint64_t timestamp, unsigned int producer,
    uint32_t sequence, uint32_t arg0, uint32_t arg1)
{
    struct trace_record record = {0};
    record.timestamp = timestamp;
    if (context) record.context = *context;
    record.sequence = sequence; record.arg0 = arg0; record.arg1 = arg1;
    record.event = (uint16_t)event; record.producer = (uint8_t)producer;
    return record;
}
static void retain(const struct trace_record *record)
{
    if (count < N3DS_TRACE_CAPACITY) journal[count++] = *record;
    else if (owner_dropped != UINT32_MAX) ++owner_dropped;
}
static void ring_publish(struct trace_ring *ring, struct trace_record *record)
{
    uint32_t write = load32(&ring->write), read = load32(&ring->read);
    record->sequence = ++ring->sequence;
    if (write - read >= N3DS_TRACE_CALLBACK_CAPACITY) {
        increment_saturated(&ring->dropped);
        return;
    }
    ring->records[write % N3DS_TRACE_CALLBACK_CAPACITY] = *record;
    store32(&ring->write, write + 1);
}
static void emit_context(const struct n3ds_trace_context *context, unsigned int event,
    uint64_t timestamp, unsigned int producer, uint32_t arg0, uint32_t arg1)
{
    struct trace_record record = record_make(context, event, timestamp, producer,
        ++owner_sequence, arg0, arg1);
    retain(&record);
}
void n3ds_trace_event(enum n3ds_trace_event event, unsigned int arg0, unsigned int arg1)
{
    if (load32(&capture_active))
        emit_context(&current, event, clock_now(), TRACE_OWNER, arg0, arg1);
}
void n3ds_trace_event_at(const struct n3ds_trace_context *context, enum n3ds_trace_event event,
    unsigned long long timestamp, unsigned int arg0, unsigned int arg1)
{
    if (load32(&capture_active) && timestamp)
        emit_context(context, event, timestamp, TRACE_JOINED_JOB, arg0, arg1);
}
unsigned long long n3ds_trace_clock(void)
{ return load32(&capture_active) ? clock_now() : 0; }
void n3ds_trace_snapshot(struct n3ds_trace_context *out) { *out = current; }
unsigned int n3ds_trace_job_begin(void)
{ return load32(&capture_active) ? ++job_serial : 0; }

static void drain(struct trace_ring *ring)
{
    /* A finite cursor snapshot bounds work even if a producer keeps running. */
    uint32_t read = load32(&ring->read), write = load32(&ring->write);
    while (read != write) {
        struct trace_record record = ring->records[read % N3DS_TRACE_CALLBACK_CAPACITY];
        ++read;
        if (stopped && record.timestamp > stop_tick) continue;
        if (record.event == N3DS_TRACE_TOP_SWAP_POSTED) {
            if (pending_top_sequence) {
                /* An unobserved earlier swap cannot be counted as displayed. */
                emit_context(&pending_top, N3DS_TRACE_TOP_ACK_UNOBSERVED,
                    record.timestamp, TRACE_OWNER, pending_top_sequence, record.arg0);
            }
            pending_top = record.context;
            pending_top_sequence = record.arg0;
        }
        retain(&record);
    }
    store32(&ring->read, read);
}
void n3ds_trace_gpu_poll(void)
{
    if (!load32(&capture_active)) return;
    drain(&gpu_events); drain(&blank_events);
    /* One nonblocking observation. A callback racing the sample invalidates
     * this observation; it does not induce a retry or change admission. */
    uint32_t version = load32(&callback_version);
    uint32_t before = load32(&posted_sequence);
    if (!load32(&identity_lost) && pending_top_sequence &&
        !(version & 1) && before == pending_top_sequence && !gspIsPresentPending(GFX_TOP)) {
        uint64_t observed = clock_now();
        /* ARM11 is weakly ordered. Keep the pending-bit sample before the
         * final version reads; an acquire load alone does not do that. */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint32_t after = load32(&posted_sequence);
        if (before == after && version == load32(&callback_version) && !load32(&identity_lost)) {
            emit_context(&pending_top, N3DS_TRACE_TOP_PENDING_CLEAR, observed,
                TRACE_OWNER, before, load32(&blank_sequence));
            pending_top_sequence = 0;
        }
    }
}

void n3ds_trace_queue_run(void)
{
    uint32_t write = load32(&queue_write), read = load32(&queue_read);
    if (load32(&identity_lost)) return;
    if (write - read >= N3DS_TRACE_QUEUE_CAPACITY || queue_serial == UINT32_MAX) {
        store32(&identity_lost, 1); return;
    }
    struct trace_queue_token token = {0};
    token.context = current;
    token.context.submission = ++queue_serial;
    token.captured = load32(&capture_active);
    current.submission = token.context.submission;
    queue_tokens[write % N3DS_TRACE_QUEUE_CAPACITY] = token;
    store32(&queue_write, write + 1);
    if (token.captured)
        emit_context(&token.context, N3DS_TRACE_QUEUE_RUN, clock_now(), TRACE_OWNER, 0, 0);
}
void n3ds_trace_queue_complete_begin(void)
{
    /* Publish BEFORE stock onQueueFinish can post/replace a top swap. A
     * passive odd/even version prevents the owner acknowledging the old
     * identity during that bookkeeping interval; the owner never waits. */
    /* ACQ_REL also prevents subsequent stock swap writes moving before the
     * odd version. A release-only store would not provide that direction. */
    __atomic_add_fetch(&callback_version, 1, __ATOMIC_ACQ_REL);
}
void n3ds_trace_queue_complete(int top_swap)
{
    /* Called after stock Citro3D completion bookkeeping, not at the precise
     * hardware completion instant. No shared owner context is read here. */
    uint32_t read = load32(&queue_read), write = load32(&queue_write);
    if (load32(&identity_lost)) goto done;
    if (read == write) { store32(&identity_lost, 1); goto done; }
    struct trace_queue_token token = queue_tokens[read % N3DS_TRACE_QUEUE_CAPACITY];
    store32(&queue_read, read + 1);
    uint32_t posted = load32(&posted_sequence);
    if (top_swap) { ++posted; store32(&posted_sequence, posted); }
    if (!token.captured || !load32(&capture_active)) goto done;
    struct trace_record record = record_make(&token.context, N3DS_TRACE_QUEUE_COMPLETE,
        clock_now(), TRACE_GPU, 0, !!top_swap, 0);
    ring_publish(&gpu_events, &record);
    if (top_swap) {
        record.event = N3DS_TRACE_TOP_SWAP_POSTED;
        record.timestamp = clock_now(); record.arg0 = posted;
        record.arg1 = load32(&blank_sequence);
        ring_publish(&gpu_events, &record);
    }
done:
    store32(&callback_version, load32(&callback_version) + 1);
}
void n3ds_trace_vblank(void)
{
    uint32_t sequence = load32(&blank_sequence) + 1;
    store32(&blank_sequence, sequence);
    if (!load32(&capture_active)) return;
    struct trace_record record = record_make(NULL, N3DS_TRACE_VBLANK, clock_now(),
        TRACE_VBLANK, 0, sequence, 0);
    ring_publish(&blank_events, &record);
}

void n3ds_trace_tick(long tick)
{
    if (have_tick && tick < current.tick) {
        ++current.epoch;
        current.tick = (int32_t)tick;
        n3ds_trace_event(N3DS_TRACE_RESTORE, 1, 0); /* implicit rewind observed */
    }
    current.tick = (int32_t)tick; have_tick = 1;
}
void n3ds_trace_restore(long tick)
{
    ++current.epoch; current.tick = (int32_t)tick; have_tick = 1;
    n3ds_trace_event(N3DS_TRACE_RESTORE, 0, 0); /* successful restore seam */
}
void n3ds_trace_location(unsigned int generation, int bsp)
{
    current.generation = generation; current.bsp = bsp;
    n3ds_trace_event(N3DS_TRACE_LOCATION, 0, 0);
}
void n3ds_trace_attempt_begin(int kind, long tick, unsigned int generation, int bsp)
{
    n3ds_trace_tick(tick);
    current.generation = generation; current.bsp = bsp;
    current.kind = (uint8_t)kind; current.view = current.submission = current.job = 0;
    current.attempt = load32(&capture_active) ? ++attempt_serial : 0;
    n3ds_trace_event(N3DS_TRACE_ATTEMPT_BEGIN, 0, 0);
}
void n3ds_trace_attempt_end(unsigned int reason)
{
    n3ds_trace_event(N3DS_TRACE_ATTEMPT_END, reason, 0);
    current.kind = 0; current.attempt = current.view = current.submission = current.job = 0;
}
void n3ds_trace_view_begin(unsigned int eye)
{
    current.view = load32(&capture_active) ? ++view_serial : 0;
    n3ds_trace_event(N3DS_TRACE_VIEW_BEGIN, eye, 0);
}
void n3ds_trace_view_end(void)
{
    n3ds_trace_event(N3DS_TRACE_VIEW_END, 0, 0); current.view = 0;
}
void n3ds_trace_begin_loop(unsigned int loop, long tick, unsigned int generation, int bsp, int gameplay)
{
    current.loop = loop; current.generation = generation; current.bsp = bsp;
    current.kind = 0; current.attempt = current.view = current.submission = current.job = 0;
    n3ds_trace_tick(tick);
    if (initialized && !started && gameplay) {
        started = 1; run_id = start_tick = clock_now();
        start_pending = load32(&queue_write) - load32(&queue_read);
        store32(&capture_active, 1);
    }
    n3ds_trace_event(N3DS_TRACE_LOOP_BEGIN, !!gameplay, 0);
    n3ds_trace_gpu_poll();
}
static void capture_stop(unsigned int reason)
{
    if (!started || stopped) return;
    n3ds_trace_gpu_poll();
    store32(&capture_active, 0);
    stop_tick = clock_now(); stopped = 1; stop_reason = reason;
    stop_pending = load32(&queue_write) - load32(&queue_read);
    stop_posted = load32(&posted_sequence); stop_blank = load32(&blank_sequence);
    /* Do not wait for callbacks. Their last published cursor is a bounded
     * observation; in-progress writes and pending work remain unclaimed. */
    drain(&gpu_events); drain(&blank_events);
}
void n3ds_trace_end_loop(void)
{
    if (!load32(&capture_active)) return;
    n3ds_trace_gpu_poll(); n3ds_trace_event(N3DS_TRACE_LOOP_END, 0, 0);
    if (++loops >= HALO_N3DS_DEPENDENCY_TRACE_LOOPS) capture_stop(1);
}
void n3ds_trace_stop(void) { capture_stop(2); }

static void put32(unsigned char *out, uint32_t value)
{ for (unsigned int i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (8*i)); }
static void put64(unsigned char *out, uint64_t value)
{ put32(out, (uint32_t)value); put32(out+4, (uint32_t)(value>>32)); }
static uint32_t checksum(uint32_t hash, const unsigned char *data, unsigned int bytes)
{ for (unsigned int i = 0; i < bytes; ++i) { hash ^= data[i]; hash *= 16777619u; } return hash; }
static void encode_record(unsigned char out[64], const struct trace_record *record)
{
    const struct n3ds_trace_context *c = &record->context;
    put64(out, record->timestamp); put32(out+8, record->sequence);
    put32(out+12,c->loop); put32(out+16,c->epoch); put32(out+20,(uint32_t)c->tick);
    put32(out+24,c->generation); put32(out+28,(uint32_t)c->bsp);
    put32(out+32,c->attempt); put32(out+36,c->view); put32(out+40,c->submission);
    put32(out+44,c->job); out[48]=(unsigned char)record->event; out[49]=(unsigned char)(record->event>>8);
    out[50]=c->kind; out[51]=record->producer; put32(out+52,record->flags);
    put32(out+56,record->arg0); put32(out+60,record->arg1);
}
void n3ds_trace_finish(void)
{
    if (!initialized || finished) return;
    finished = 1; capture_stop(2);
    if (!started) { n3ds_log("DEPENDENCY TRACE: no gameplay capture; no trace file written"); return; }
    unsigned char header[128] = {0}, encoded[64], footer[16] = {0};
    memcpy(header,"H2DSTRC1",8); put32(header+8,N3DS_TRACE_SCHEMA); put32(header+12,sizeof(header));
    put32(header+16,64); put32(header+20,N3DS_TRACE_CAPACITY); put32(header+24,count);
    put32(header+28,HALO_N3DS_DEPENDENCY_TRACE_LOOPS); put64(header+32,run_id);
    put64(header+40,SYSCLOCK_ARM11); put64(header+48,start_tick); put64(header+56,stop_tick);
    put32(header+64,owner_dropped); put32(header+68,load32(&gpu_events.dropped));
    put32(header+72,load32(&blank_events.dropped)); put32(header+76,load32(&identity_lost));
    put32(header+80,start_pending); put32(header+84,stop_pending);
    put32(header+88,pending_top_sequence); put32(header+92,stop_reason);
    put32(header+96,loops); put32(header+100,stop_posted); put32(header+104,stop_blank);
    put32(header+108,sizeof(journal)+sizeof(gpu_events)+sizeof(blank_events)+sizeof(queue_tokens));
    /* Observation may end with a callback already running: this schema never
     * claims a fully drained GPU/scanout tail, even when pending counters are 0. */
    put32(header+112,1);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    put32(header+116,1);
#endif
    put32(header+120,queue_serial); put32(header+124,job_serial);
    uint32_t hash = checksum(2166136261u, header, sizeof(header));
    char path[96], message[256];
    snprintf(path,sizeof(path),"sdmc:/halo-trace-%016llx.bin",(unsigned long long)run_id);
    FILE *file = fopen(path,"wb");
    int ok = file && fwrite(header,1,sizeof(header),file)==sizeof(header);
    for (unsigned int i=0; ok && i<count; ++i) {
        encode_record(encoded,&journal[i]); hash=checksum(hash,encoded,sizeof(encoded));
        ok=fwrite(encoded,1,sizeof(encoded),file)==sizeof(encoded);
    }
    memcpy(footer,"H2DEND01",8); put32(footer+8,count); put32(footer+12,hash);
    if (ok) ok=fwrite(footer,1,sizeof(footer),file)==sizeof(footer);
    if (file && fclose(file)) ok=0;
    snprintf(message,sizeof(message),"DEPENDENCY TRACE: export=%s path=%s records=%lu dropped=%lu/%lu/%lu identity_lost=%lu; tail unconfirmed, no FPS claim",
        ok?"complete":"failed",path,(unsigned long)count,(unsigned long)owner_dropped,
        (unsigned long)load32(&gpu_events.dropped),(unsigned long)load32(&blank_events.dropped),
        (unsigned long)load32(&identity_lost));
    n3ds_log(message);
}
void n3ds_trace_initialize(void)
{
    if (initialized) return;
    initialized = 1; current.bsp = -1;
    if (atexit(n3ds_trace_finish)) n3ds_log("DEPENDENCY TRACE: exit handler unavailable; normal shutdown export remains");
    char message[256];
    snprintf(message,sizeof(message),"DEPENDENCY TRACE: enabled=1 schema=%u records=%u buffers_bytes=%u loops=%u start=first-gameplay export=shutdown; overhead unmeasured",
        N3DS_TRACE_SCHEMA,N3DS_TRACE_CAPACITY,
        (unsigned int)(sizeof(journal)+sizeof(gpu_events)+sizeof(blank_events)+sizeof(queue_tokens)),
        (unsigned int)HALO_N3DS_DEPENDENCY_TRACE_LOOPS);
    n3ds_log(message);
}
#endif
