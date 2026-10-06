#include "cseries.h"
#include "cache/cache_files.h"
#include "cache/sound_cache.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "engine_sounds.h"
void n3ds_log(const char *message);
static unsigned int sample_hash(const struct sound_permutation *sound)
{
    const byte *data=(const void *)sound->unknown1;
    unsigned int hash=2166136261u; long i;
    for (i=0;i<sound->samples.size;++i) hash=(hash^data[i])*16777619u;
    return hash;
}
int halo_engine_sound_tests(void)
{
    struct tag_iterator iterator;
    struct sound_permutation *first=NULL,*pressure=NULL,*pinned[512];
    unsigned int count=0,before_bytes,before_count;
    long tag,old_handle; boolean full=FALSE;
    char message[160];
#define CHECK(x) do { if (!(x)) { n3ds_log("SOUND CACHE FAIL: " #x); return 1; } } while (0)
    sound_cache_new(); sound_cache_open();
    CHECK(scenario_tags_load("a10")!=NONE);
    tag_iterator_new(&iterator,'snd!');
    while ((tag=tag_iterator_next(&iterator))!=NONE && !first) {
        struct sound_definition *sound=tag_get('snd!',tag);
        if (sound->pitch_ranges.count) {
            struct sound_pitch_range *range=TAG_BLOCK_GET_ELEMENT(&sound->pitch_ranges,0,struct sound_pitch_range);
            if (range->permutations.count) first=TAG_BLOCK_GET_ELEMENT(&range->permutations,0,struct sound_permutation);
        }
    }
    CHECK(first && !strcmp(tag_get_name(first->unknown3),"sound\\sfx\\impulse\\impacts\\metal_chips"));
    CHECK(first->samples.size==2484 && first->samples.file_offset==106365952);
    {
        struct sound_permutation foreign=*first;
        long offset=first->samples.file_offset;
        CHECK(!_sound_cache_sound_request(&foreign,TRUE,TRUE,FALSE));
        first->samples.file_offset=n3ds_engine_cache_view(0)->header.file_length-1;
        CHECK(!_sound_cache_sound_request(first,TRUE,TRUE,FALSE) && !n3ds_engine_sound_count());
        first->samples.file_offset=offset;
    }
    CHECK(!_sound_cache_sound_request(first,FALSE,FALSE,FALSE));
    CHECK(!_sound_cache_sound_request(first,FALSE,TRUE,TRUE));
    CHECK(!_sound_cache_sound_request(first,FALSE,TRUE,TRUE));
    CHECK(n3ds_engine_sound_count()==1 && !n3ds_engine_sound_bytes() && !first->unknown1);
    sound_cache_idle();
    CHECK(_sound_cache_sound_request(first,FALSE,FALSE,FALSE));
    CHECK(first->unknown1 && n3ds_engine_sound_bytes()==2484 && sample_hash(first)==0x970ba9f7u);
    CHECK(_sound_cache_sound_request(first,TRUE,TRUE,TRUE));
    sound_cache_sound_hardware_lock(first);
    sound_cache_flush(); CHECK(n3ds_engine_sound_count()==1);
    sound_cache_sound_finished(first);
    sound_cache_flush(); CHECK(n3ds_engine_sound_count()==1);
    sound_cache_sound_hardware_unlock(first);
    old_handle=first->unknown0;
    sound_cache_flush(); CHECK(!n3ds_engine_sound_count() && !first->unknown1 && first->unknown0==NONE);
    CHECK(_sound_cache_sound_request(first,TRUE,TRUE,TRUE));
    CHECK(first->unknown0!=old_handle && sample_hash(first)==0x970ba9f7u);
    {
        long live_handle=first->unknown0;
        first->unknown0=old_handle;
        CHECK(!_sound_cache_sound_request(first,FALSE,FALSE,FALSE));
        CHECK(!_sound_cache_sound_request(first,TRUE,TRUE,FALSE) && n3ds_engine_sound_count()==1);
        first->unknown0=live_handle;
    }
    pinned[count++]=first;
    tag_iterator_new(&iterator,'snd!');
    while (!full && (tag=tag_iterator_next(&iterator))!=NONE) {
        struct sound_definition *sound=tag_get('snd!',tag);
        long r,p;
        for (r=0;r<sound->pitch_ranges.count && !full;++r) {
            struct sound_pitch_range *range=TAG_BLOCK_GET_ELEMENT(&sound->pitch_ranges,r,struct sound_pitch_range);
            for (p=0;p<range->permutations.count;++p) {
                struct sound_permutation *permutation=TAG_BLOCK_GET_ELEMENT(&range->permutations,p,struct sound_permutation);
                if (permutation==first || permutation->samples.size<=0 || permutation->samples.size>4*1024*1024) continue;
                before_bytes=n3ds_engine_sound_bytes(); before_count=n3ds_engine_sound_count();
                if (!_sound_cache_sound_request(permutation,TRUE,TRUE,TRUE)) {
                    CHECK(before_count==512 || (unsigned long)permutation->samples.size>4*1024*1024-before_bytes);
                    CHECK(n3ds_engine_sound_bytes()==before_bytes && n3ds_engine_sound_count()==before_count);
                    CHECK(sample_hash(first)==0x970ba9f7u); pressure=permutation; full=TRUE; break;
                }
                CHECK(count<512); pinned[count++]=permutation;
            }
        }
    }
    CHECK(full && count>1 && n3ds_engine_sound_bytes()<=4*1024*1024);
    snprintf(message,sizeof(message),"PASS: native sound cache pins %u real samples, %u bytes; budget failure preserves active data",count,n3ds_engine_sound_bytes()); n3ds_log(message);
    /* Release all but the first; a future request may reclaim only those. */
    for (unsigned int i=1;i<count;++i) sound_cache_sound_finished(pinned[i]);
    CHECK(pressure && _sound_cache_sound_request(pressure,TRUE,TRUE,FALSE));
    CHECK(n3ds_engine_sound_bytes()<=4*1024*1024 && sample_hash(first)==0x970ba9f7u);
    n3ds_log("PASS: sound cache reclaims unreferenced samples under pressure while preserving pinned data");
    sound_cache_flush(); CHECK(n3ds_engine_sound_count()==1 && n3ds_engine_sound_bytes()==2484);
    sound_cache_sound_finished(first);
    {
        long offset=pinned[1]->samples.file_offset;
        CHECK(!_sound_cache_sound_request(pinned[1],FALSE,TRUE,FALSE));
        pinned[1]->samples.file_offset=n3ds_engine_cache_view(0)->header.file_length-1;
        sound_cache_idle();
        CHECK(n3ds_engine_sound_count()==1 && !pinned[1]->unknown1 && pinned[1]->unknown0==NONE);
        CHECK(sample_hash(first)==0x970ba9f7u);
        pinned[1]->samples.file_offset=offset;
    }
    /* Both a queued request and ready storage must die before map tags. */
    CHECK(!_sound_cache_sound_request(pinned[1],FALSE,TRUE,FALSE));
    scenario_tags_unload(); CHECK(!n3ds_engine_sound_count() && !n3ds_engine_sound_bytes());
    sound_cache_close(); sound_cache_delete();
    n3ds_log("PASS: sound cache exact sample hash, queued deduplication, software/hardware pins, stale handles, unload cancellation; no audio playback");
    return 0;
#undef CHECK
}
