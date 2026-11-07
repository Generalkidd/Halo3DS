/* Native encoded-sample cache, preserving original ready/reference semantics.
 * The optional reader can stage bytes, but only this thread publishes them. */
#include "cseries.h"
#include "cache/sound_cache.h"
#include "sound/sound_definitions.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "engine_sounds.h"
#include "engine_algorithm_profile.h"
#ifdef HALO_N3DS_ALGORITHM_PROFILE
long long n3ds_engine_ticks(void);
#define SOUND_LOAD_RETURN(success) do { \
    n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_LOAD_TICKS,n3ds_engine_ticks()-profile_start); \
    n3ds_algorithm_profile_add((success)?N3DS_ALG_IO_SOUND_LOADS:N3DS_ALG_IO_SOUND_LOAD_FAILS,1); \
    if(success)n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_LOAD_BYTES,size); \
    return (success); \
} while(0)
#else
#define SOUND_LOAD_RETURN(success) return (success)
#endif

enum { SOUND_SLOTS=512, SOUND_BUDGET=4*1024*1024 };
struct sound_entry {
    struct sound_permutation *sound;
    byte *data;
    unsigned int bytes,age,generation,map_generation;
    unsigned short software,hardware;
};
static struct sound_entry entries[SOUND_SLOTS];
static unsigned int used_bytes,used_slots,sequence,generation;
static unsigned int first_pending=SOUND_SLOTS;
static boolean initialized,opened,read_ahead;
unsigned int n3ds_engine_sound_count(void) { return used_slots; }
unsigned int n3ds_engine_sound_bytes(void) { return used_bytes; }

static boolean sound_in_map(const struct n3ds_cache_view *view,const struct sound_permutation *sound)
{
    return sound && (unsigned long)sound%__alignof__(struct sound_permutation)==0 &&
        n3ds_cache_resolve(view,sound,sizeof(*sound))==sound;
}
static boolean owns_sound(const struct n3ds_cache_view *view,const struct sound_permutation *sound)
{
    struct cache_file_tag_instance *tag;
    struct sound_definition *definition;
    struct sound_pitch_range *ranges;
    long i;
    if (!sound_in_map(view,sound)) return FALSE;
    tag=n3ds_cache_tag(view,sound->unknown3,'snd!');
    if (!tag) return FALSE;
    definition=n3ds_cache_resolve(view,tag->base_address,sizeof(*definition));
    if (!definition || definition->pitch_ranges.count<0 || definition->pitch_ranges.count>8) return FALSE;
    ranges=n3ds_cache_resolve(view,definition->pitch_ranges.address,definition->pitch_ranges.count*sizeof(*ranges));
    if (!ranges) return FALSE;
    for (i=0;i<definition->pitch_ranges.count;++i) {
        struct sound_permutation *permutations;
        if (ranges[i].permutations.count<0 || ranges[i].permutations.count>256) return FALSE;
        permutations=n3ds_cache_resolve(view,ranges[i].permutations.address,ranges[i].permutations.count*sizeof(*sound));
        if (!permutations) return FALSE;
        /* All permutations are a validated contiguous tag block. Membership
         * needs one bounds/alignment check, not a scan on every voice update. */
        unsigned long address=(unsigned long)sound,base=(unsigned long)permutations;
        unsigned long bytes=ranges[i].permutations.count*sizeof(*sound);
        if(address>=base && address-base<bytes && (address-base)%sizeof(*sound)==0) return TRUE;
    }
    return FALSE;
}
static long handle(const struct sound_entry *entry)
{
    return (entry->generation<<9)|(entry-entries);
}
static struct sound_entry *lookup(const struct sound_permutation *sound)
{
    struct sound_entry *entry;
    if (!sound_in_map(n3ds_engine_cache_view(0),sound) || sound->unknown0<0) return NULL;
    entry=entries+(sound->unknown0&(SOUND_SLOTS-1));
    /* Insertion proved tag-block membership. The exact owner, handle and
     * current tag lifetime retain that proof for warm queries. */
    return entry->sound==sound && handle(entry)==sound->unknown0 &&
        entry->map_generation==n3ds_engine_tag_generation() ? entry : NULL;
}
static struct n3ds_sound_read_key read_key(const struct sound_entry *entry)
{
    return (struct n3ds_sound_read_key){entry->map_generation,handle(entry),
        entry->sound->samples.file_offset,entry->sound->samples.size};
}
static void advance_pending(const struct sound_entry *entry)
{
    if (entry!=entries+first_pending) return;
    /* Legacy idle order is lowest occupied slot, rather than arrival order. */
    while (++first_pending<SOUND_SLOTS)
        if (entries[first_pending].sound && !entries[first_pending].data) break;
}
static void stage_first_pending(void)
{
    char path[128];
    if (!read_ahead || first_pending==SOUND_SLOTS) return;
    struct n3ds_sound_read_key key=read_key(entries+first_pending);
    if (!key.bytes || key.bytes>N3DS_SOUND_READ_LIMIT ||
        !n3ds_engine_cache_read_path(key.offset,key.bytes,path)) {
        n3ds_sound_reader_cancel();
        return;
    }
    n3ds_sound_reader_offer(path,&key);
}
static void release(struct sound_entry *entry)
{
    assert(entry->sound && !entry->software && !entry->hardware);
    if (read_ahead && !entry->data) n3ds_sound_reader_discard(entry->map_generation,handle(entry));
    entry->sound->unknown0=NONE; entry->sound->unknown1=0;
    if (entry->data) { used_bytes-=entry->bytes; free(entry->data); }
    memset(entry,0,sizeof(*entry)); --used_slots;
    advance_pending(entry);
}
void sound_cache_new(void) { assert(!initialized && !used_slots); initialized=TRUE; }
void sound_cache_open(void)
{
    assert(initialized && !opened);
    read_ahead=n3ds_sound_reader_supported();
    opened=TRUE;
}
void sound_cache_flush(void)
{
    unsigned int i;
    if (read_ahead) n3ds_sound_reader_cancel();
    for (i=0;i<SOUND_SLOTS;++i) if (entries[i].sound && !entries[i].software && !entries[i].hardware) release(entries+i);
}
void n3ds_engine_sounds_unload(void)
{
    if (read_ahead) n3ds_sound_reader_shutdown();
    sound_cache_flush();
    /* The sound device must stop/release voices before their tag memory dies. */
    assert(!used_slots && !used_bytes && first_pending==SOUND_SLOTS);
}
void sound_cache_close(void) { n3ds_engine_sounds_unload(); opened=read_ahead=FALSE; }
void sound_cache_delete(void) { sound_cache_close(); initialized=FALSE; }
void sound_cache_sound_new(long tag_index,struct sound_permutation *sound)
{
    assert(owns_sound(n3ds_engine_cache_view(0),sound) && !lookup(sound) && !sound->unknown1 && tag_index!=0);
    sound->unknown0=NONE; sound->unknown1=0; sound->unknown2=tag_index;
}
void sound_cache_sound_delete(struct sound_permutation *sound)
{
    struct sound_entry *entry=lookup(sound);
    assert(owns_sound(n3ds_engine_cache_view(0),sound));
    if (entry) release(entry);
    else { assert(sound->unknown0==NONE && !sound->unknown1); }
}
static struct sound_entry *oldest_unused(void)
{
    struct sound_entry *oldest=NULL;
    n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SCAN_SLOTS,SOUND_SLOTS);
    for(unsigned int i=0;i<SOUND_SLOTS;++i)
        if(entries[i].data && !entries[i].software && !entries[i].hardware &&
           (!oldest || (unsigned int)(sequence-entries[i].age)>(unsigned int)(sequence-oldest->age)))
            oldest=entries+i;
    return oldest;
}
static boolean load(struct sound_entry *entry)
{
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    long long profile_start=n3ds_engine_ticks();
#endif
    byte *data;
    struct n3ds_sound_read_key key=read_key(entry);
    unsigned int size=key.bytes;
    if (size>SOUND_BUDGET-used_bytes) {
        unsigned int reclaim=0;
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SCAN_SLOTS,SOUND_SLOTS);
        for (unsigned int i=0;i<SOUND_SLOTS;++i)
            if (entries[i].data && !entries[i].software && !entries[i].hardware) reclaim+=entries[i].bytes;
        if (size>SOUND_BUDGET-used_bytes+reclaim) {
            n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_BUDGET_FAILS,1);
            release(entry);SOUND_LOAD_RETURN(FALSE);
        }
    }
    while (size>SOUND_BUDGET-used_bytes) {
        struct sound_entry *oldest=oldest_unused();
        assert(oldest);
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_BUDGET_EVICTIONS,1);
        release(oldest);
    }
    /* Reclaim idle samples before growing the cache. The cseries malloc macro
     * is fatal: use libc so memory pressure can decline this optional sound.
     * Never evict software/hardware-referenced samples, including during retry. */
    while(!(data=(malloc)(size))) {
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_ALLOC_ATTEMPT_FAILS,1);
        struct sound_entry *oldest=oldest_unused();
        if(!oldest) {
            n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_ALLOC_FAILS,1);
            release(entry);SOUND_LOAD_RETURN(FALSE);
        }
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_ALLOC_EVICTIONS,1);
        release(oldest);
    }
    /* Same admission and fallback as before. A late or failed read cannot
     * delay or promote an entry, and an altered range won't match the ticket. */
    int staged=0;
    if(read_ahead) {
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        long long start=n3ds_engine_ticks();
#endif
        staged=n3ds_sound_reader_copy(&key,data);
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_STAGE_COPY_TICKS,n3ds_engine_ticks()-start);
        n3ds_algorithm_profile_add(staged?N3DS_ALG_IO_SOUND_STAGE_HITS:N3DS_ALG_IO_SOUND_STAGE_MISSES,1);
        if(staged)n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_STAGE_BYTES,size);
    }
    if(!staged) {
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        long long start=n3ds_engine_ticks();
#endif
        int read=n3ds_engine_cache_read(key.offset,size,data);
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_READ_TICKS,n3ds_engine_ticks()-start);
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_READS,1);
        if(!read) {
            n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_READ_FAILS,1);
            free(data);release(entry);SOUND_LOAD_RETURN(FALSE);
        }
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_READ_BYTES,size);
    }
    entry->data=data; entry->bytes=size; used_bytes+=size;
    entry->sound->unknown1=(unsigned long)data;
    n3ds_algorithm_profile_max(N3DS_ALG_IO_SOUND_PEAK_BYTES,used_bytes);
    advance_pending(entry);
    SOUND_LOAD_RETURN(TRUE);
}
void sound_cache_idle(void)
{
    assert(initialized);
    /* Seed each observation window even when a warm cache needs no loads. */
    n3ds_algorithm_profile_max(N3DS_ALG_IO_SOUND_PEAK_BYTES,used_bytes);
    n3ds_algorithm_profile_max(N3DS_ALG_IO_SOUND_PEAK_SLOTS,used_slots);
    if (first_pending<SOUND_SLOTS) {
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_IDLE_LOADS,1);
        load(entries+first_pending);
        stage_first_pending();
    }
}
boolean _sound_cache_sound_request(struct sound_permutation *sound, boolean block, boolean fetch, boolean reference)
{
    struct sound_entry *entry;
    unsigned int i;
    assert(fetch || (!block && !reference));
    if (!initialized || !opened) return FALSE;
    n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_REQUESTS,1);
    entry=lookup(sound);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_READY_HITS,entry && entry->data);
    if (!entry) {
        const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
        if (!fetch || !owns_sound(view,sound) || sound->unknown0!=NONE) return FALSE;
        unsigned long offset=sound->samples.file_offset;
        long size=sound->samples.size;
        if (size<=0 || size>SOUND_BUDGET || offset<2048 || offset>(unsigned long)view->header.file_length ||
            (unsigned long)size>(unsigned long)view->header.file_length-offset) return FALSE;
        for (i=0;i<SOUND_SLOTS;++i) if (!entries[i].sound) break;
        n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SCAN_SLOTS,i<SOUND_SLOTS?i+1:SOUND_SLOTS);
        if (i==SOUND_SLOTS) {
            struct sound_entry *oldest=NULL;
            n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SCAN_SLOTS,SOUND_SLOTS);
            for (i=0;i<SOUND_SLOTS;++i) if (!entries[i].software && !entries[i].hardware &&
                (!oldest || (unsigned int)(sequence-entries[i].age)>(unsigned int)(sequence-oldest->age))) oldest=entries+i;
            if (!oldest) {n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SLOT_FAILS,1);return FALSE;}
            n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_SLOT_EVICTIONS,1);
            i=oldest-entries; release(oldest);
        }
        entry=entries+i; entry->sound=sound;
        generation=(generation+1)&0x3fffff; if (!generation) generation=1;
        entry->generation=generation;
        entry->map_generation=n3ds_engine_tag_generation();
        sound->unknown0=handle(entry); sound->unknown1=0; ++used_slots;
        n3ds_algorithm_profile_max(N3DS_ALG_IO_SOUND_PEAK_SLOTS,used_slots);
        if (i<first_pending) first_pending=i;
        if (!block && i==first_pending) stage_first_pending();
    }
    entry->age=++sequence;
    n3ds_algorithm_profile_add(N3DS_ALG_IO_SOUND_BLOCK_LOADS,block && !entry->data);
    if (!entry->data && (!block || !load(entry))) return FALSE;
    if (reference) { assert(entry->software<255); ++entry->software; }
    return TRUE;
}
void sound_cache_sound_finished(struct sound_permutation *sound)
{
    struct sound_entry *entry=lookup(sound); assert(entry && entry->data && entry->software); --entry->software;
}
void sound_cache_sound_hardware_lock(struct sound_permutation *sound)
{
    struct sound_entry *entry=lookup(sound); assert(entry && entry->data && entry->hardware<255); ++entry->hardware;
}
void sound_cache_sound_hardware_unlock(struct sound_permutation *sound)
{
    struct sound_entry *entry=lookup(sound); assert(entry && entry->hardware); --entry->hardware;
}
