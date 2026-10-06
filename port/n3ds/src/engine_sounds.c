/* Native encoded-sample cache, preserving original ready/reference semantics.
 * Playback and codec conversion belong to the future native sound device. */
#include "cseries.h"
#include "cache/sound_cache.h"
#include "sound/sound_definitions.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "engine_sounds.h"

enum { SOUND_SLOTS=512, SOUND_BUDGET=4*1024*1024 };
struct sound_entry {
    struct sound_permutation *sound;
    byte *data;
    unsigned int bytes,age,generation;
    unsigned short software,hardware;
};
static struct sound_entry entries[SOUND_SLOTS];
static unsigned int used_bytes,used_slots,sequence,generation;
static boolean initialized,opened;
unsigned int n3ds_engine_sound_count(void) { return used_slots; }
unsigned int n3ds_engine_sound_bytes(void) { return used_bytes; }

static boolean owns_sound(const struct sound_permutation *sound)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    struct cache_file_tag_instance *tag;
    struct sound_definition *definition;
    struct sound_pitch_range *ranges;
    long i;
    if (!view->tags || !sound || n3ds_cache_resolve(view,sound,sizeof(*sound))!=sound) return FALSE;
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
    if (!owns_sound(sound) || sound->unknown0<0) return NULL;
    entry=entries+(sound->unknown0&511);
    return entry->sound==sound && handle(entry)==sound->unknown0 ? entry : NULL;
}
static void release(struct sound_entry *entry)
{
    assert(entry->sound && !entry->software && !entry->hardware);
    entry->sound->unknown0=NONE; entry->sound->unknown1=0;
    if (entry->data) { used_bytes-=entry->bytes; free(entry->data); }
    memset(entry,0,sizeof(*entry)); --used_slots;
}
void sound_cache_new(void) { assert(!initialized && !used_slots); initialized=TRUE; }
void sound_cache_open(void) { assert(initialized && !opened); opened=TRUE; }
void sound_cache_flush(void)
{
    unsigned int i;
    for (i=0;i<SOUND_SLOTS;++i) if (entries[i].sound && !entries[i].software && !entries[i].hardware) release(entries+i);
}
void n3ds_engine_sounds_unload(void)
{
    sound_cache_flush();
    /* The sound device must stop/release voices before their tag memory dies. */
    assert(!used_slots && !used_bytes);
}
void sound_cache_close(void) { n3ds_engine_sounds_unload(); opened=FALSE; }
void sound_cache_delete(void) { sound_cache_close(); initialized=FALSE; }
void sound_cache_sound_new(long tag_index,struct sound_permutation *sound)
{
    assert(owns_sound(sound) && !lookup(sound) && !sound->unknown1 && tag_index!=0);
    sound->unknown0=NONE; sound->unknown1=0; sound->unknown2=tag_index;
}
void sound_cache_sound_delete(struct sound_permutation *sound)
{
    struct sound_entry *entry=lookup(sound);
    assert(owns_sound(sound));
    if (entry) release(entry);
    else { assert(sound->unknown0==NONE && !sound->unknown1); }
}
static struct sound_entry *oldest_unused(void)
{
    struct sound_entry *oldest=NULL;
    for(unsigned int i=0;i<SOUND_SLOTS;++i)
        if(entries[i].data && !entries[i].software && !entries[i].hardware &&
           (!oldest || (unsigned int)(sequence-entries[i].age)>(unsigned int)(sequence-oldest->age)))
            oldest=entries+i;
    return oldest;
}
static boolean load(struct sound_entry *entry)
{
    byte *data;
    unsigned int size=entry->sound->samples.size;
    unsigned int reclaim=0,i;
    for (i=0;i<SOUND_SLOTS;++i) if (entries[i].data && !entries[i].software && !entries[i].hardware) reclaim+=entries[i].bytes;
    if (size>SOUND_BUDGET-used_bytes+reclaim) { release(entry); return FALSE; }
    while (size>SOUND_BUDGET-used_bytes) {
        struct sound_entry *oldest=oldest_unused();
        assert(oldest); release(oldest);
    }
    /* Reclaim idle samples before growing the cache. The cseries malloc macro
     * is fatal: use libc so memory pressure can decline this optional sound.
     * Never evict software/hardware-referenced samples, including during retry. */
    while(!(data=(malloc)(size))) {
        struct sound_entry *oldest=oldest_unused();
        if(!oldest) { release(entry); return FALSE; }
        release(oldest);
    }
    if (!n3ds_engine_cache_read(entry->sound->samples.file_offset,size,data)) { free(data); release(entry); return FALSE; }
    entry->data=data; entry->bytes=size; used_bytes+=size;
    entry->sound->unknown1=(unsigned long)data;
    return TRUE;
}
void sound_cache_idle(void)
{
    unsigned int i;
    assert(initialized);
    for (i=0;i<SOUND_SLOTS;++i) if (entries[i].sound && !entries[i].data) { load(entries+i); break; }
}
boolean _sound_cache_sound_request(struct sound_permutation *sound, boolean block, boolean fetch, boolean reference)
{
    struct sound_entry *entry;
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    unsigned int i;
    assert(fetch || (!block && !reference));
    if (!initialized || !opened) return FALSE;
    entry=lookup(sound);
    if (!entry && (!owns_sound(sound) || sound->unknown0!=NONE)) return FALSE;
    if (!entry) {
        unsigned long offset=sound->samples.file_offset;
        long size=sound->samples.size;
        if (!fetch || size<=0 || size>SOUND_BUDGET || offset<2048 || offset>(unsigned long)view->header.file_length ||
            (unsigned long)size>(unsigned long)view->header.file_length-offset) return FALSE;
        for (i=0;i<SOUND_SLOTS;++i) if (!entries[i].sound) break;
        if (i==SOUND_SLOTS) {
            struct sound_entry *oldest=NULL;
            for (i=0;i<SOUND_SLOTS;++i) if (!entries[i].software && !entries[i].hardware &&
                (!oldest || (unsigned int)(sequence-entries[i].age)>(unsigned int)(sequence-oldest->age))) oldest=entries+i;
            if (!oldest) return FALSE;
            i=oldest-entries; release(oldest);
        }
        entry=entries+i; entry->sound=sound;
        generation=(generation+1)&0x3fffff; if (!generation) generation=1;
        entry->generation=generation;
        sound->unknown0=handle(entry); sound->unknown1=0; ++used_slots;
    }
    entry->age=++sequence;
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
