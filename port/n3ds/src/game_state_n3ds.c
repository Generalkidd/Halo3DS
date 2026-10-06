#include "cseries.h"
#include "game_state.h"
#include "crc.h"
#include "engine_checkpoint.h"

void n3ds_log(const char *message);
void *n3ds_state_allocate(unsigned int bytes);
void n3ds_state_release(void *pointer);
void n3ds_state_flush(void *pointer, unsigned int bytes);
int n3ds_state_make_directory(void);
long long n3ds_engine_ticks(void);
static unsigned long long state_generation;

static struct {
    void *arena;
    unsigned long bytes;
    FILE *checkpoint;
    void *snapshot;
    unsigned long checksum;
    boolean valid;
} state;
const void *n3ds_game_state_snapshot(void) {return state.valid?state.snapshot:NULL;}
unsigned int n3ds_save_memory_crc(const void *a,unsigned int n,const void *b,unsigned int m)
{unsigned long crc;crc_new(&crc);crc_checksum_buffer(&crc,a,n);if(m)crc_checksum_buffer(&crc,b,m);return crc;}

void *n3ds_game_state_identity(unsigned long long *generation)
{
    *generation=state_generation;
    return state.arena;
}

void *game_state_allocate_buffer(unsigned long xbox_address, unsigned long cpu_size, unsigned long gpu_size)
{
    /* Real native memory replaces the Xbox fixed virtual address. The original
     * game-state allocator already computes its pointers relative to this base. */
    assert(!state.arena && xbox_address && cpu_size && gpu_size);
    assert(!(cpu_size & 4095) && !(gpu_size & 4095));
    assert(cpu_size <= 4*1024*1024 && gpu_size <= 4*1024*1024 - cpu_size);
    state.bytes = cpu_size + gpu_size;
    state.arena = n3ds_state_allocate(state.bytes);
    assert(state.arena);
    state_generation=(unsigned long long)n3ds_engine_ticks();
    memset(state.arena, 0, state.bytes);
    return state.arena;
}

void game_state_free_buffer(void)
{
    n3ds_checkpoint_finish();
    assert(state.arena);
    n3ds_state_release(state.arena);
    state.arena = NULL;
    state.bytes = 0;
    state.valid = FALSE;
}

void game_state_create_or_open_file(void)
{
    assert(state.arena && !state.checkpoint && !state.snapshot);
    assert(!n3ds_state_make_directory());
    /* Xbox snapshots contain live pointers. A session checkpoint is safe at
     * this arena base; cross-launch saves need explicit relocation/versioning.
     * Never interpret a prior process's raw pointers as this process's state. */
    /* Both models retain a bounded compressed snapshot in ordinary RAM,
     * leaving linear GPU memory untouched. Allocation failure falls back to disk. */
    n3ds_log("CHECKPOINT: bounded compressed RAM snapshots on both model families; disk fallback on allocation failure");
    state.valid = FALSE;
}

void game_state_close_file(void)
{
    n3ds_checkpoint_finish();
    if(state.checkpoint) {assert(!fclose(state.checkpoint));}
    n3ds_save_snapshot_release(state.snapshot);state.snapshot=NULL;
    state.checkpoint = NULL;
    state.valid = FALSE;
}

boolean game_state_write_to_file(void)
{
    /* A disk worker retains its own immutable snapshot. Capturing a newer
     * checkpoint never waits for that older SD write to finish. */
    assert(state.arena);
    long long start=n3ds_engine_ticks();
    state.valid = FALSE;
    void *snapshot=n3ds_save_snapshot_create(state.arena,state.bytes);
    if(snapshot) {
        n3ds_save_snapshot_release(state.snapshot);state.snapshot=snapshot;state.valid=TRUE;
        extern long long n3ds_engine_tick_frequency(void);char message[128];
        snprintf(message,sizeof(message),"CHECKPOINT SNAPSHOT: compressed_bytes=%u main_thread_ms=%.3f",n3ds_save_snapshot_size(snapshot),(n3ds_engine_ticks()-start)*1000./n3ds_engine_tick_frequency());n3ds_log(message);return TRUE;
    }
    n3ds_save_snapshot_release(state.snapshot);state.snapshot=NULL;
    if(!state.checkpoint)state.checkpoint=fopen("sdmc:/halo-source/state/engine-session.bin","w+b");
    if(!state.checkpoint)return FALSE;
    crc_new(&state.checksum);crc_checksum_buffer(&state.checksum,state.arena,state.bytes);
    n3ds_log("CHECKPOINT: compressed snapshot allocation failed; bounded disk fallback");
    if (fseek(state.checkpoint, 0, SEEK_SET) ||
        fwrite(state.arena, 1, state.bytes, state.checkpoint) != state.bytes || fflush(state.checkpoint)) return FALSE;
    state.valid = TRUE;
    return TRUE;
}

boolean game_state_read_from_file(void)
{
    byte *block;
    unsigned long checksum, offset;
    assert(state.arena && (state.checkpoint || state.snapshot) && state.valid);
    /* Transient cinematic filters must not survive reverting to gameplay. */
    extern void rasterizer_screen_effect_stop(void);
    rasterizer_screen_effect_stop();
    if(state.snapshot) {
        /* Validate the compact image before touching live state. The SD job
         * owns a separate reference and only reads, so reverting need not wait. */
        if(!n3ds_save_snapshot_validate(state.snapshot))return FALSE;
        block=(malloc)(32768);if(!block)return FALSE;
        for(offset=0;offset<state.bytes;offset+=32768){unsigned int n=MIN(32768u,state.bytes-offset);
            assert(n3ds_save_snapshot_read(state.snapshot,offset,block,n));memcpy((byte *)state.arena+offset,block,n);}
        free(block);n3ds_state_flush(state.arena,state.bytes);return TRUE;
    }
    /* Verify the whole file before modifying live engine state. */
    if (fseek(state.checkpoint, 0, SEEK_END) || ftell(state.checkpoint) != (long)state.bytes ||
        fseek(state.checkpoint, 0, SEEK_SET)) return FALSE;
    crc_new(&checksum);
    byte disk_block[4096];
    for (offset = 0; offset < state.bytes; offset += sizeof(disk_block)) {
        unsigned bytes = MIN(sizeof(disk_block), state.bytes - offset);
        if (fread(disk_block, 1, bytes, state.checkpoint) != bytes) return FALSE;
        crc_checksum_buffer(&checksum, disk_block, bytes);
    }
    if (checksum != state.checksum) { n3ds_log("Checkpoint checksum rejected before restore"); return FALSE; }
    if (fseek(state.checkpoint, 0, SEEK_SET) || fread(state.arena, 1, state.bytes, state.checkpoint) != state.bytes) {
        /* A device failure after validation may have partially overwritten the
         * arena. Stop; continuing would expose inconsistent engine state. */
        vhalt("Native checkpoint read failed during restore");
    }
    n3ds_state_flush(state.arena, state.bytes);
    return TRUE;
}
