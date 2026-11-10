/* New3DS integration additions by Kevin Boggs. */
#include "engine_campaign_overlay.h"
#include "engine_campaign_options.h"
#include "cseries/cseries.h"
#include "cache_reader.h"
#include "../pc/engine_pc_runtime.h"
#include "cache/cache_file_format.h"
#include "bitmaps/bitmap_group.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void n3ds_log(const char *message);

#ifdef HALO_N3DS_UNIFIED_UI

struct overlay_key { long group; const char *name; };
static const struct overlay_key imports[]={
    {'vehi',"vehicles\\rwarthog\\rwarthog"},
    {'mode',"vehicles\\rwarthog\\rwarthog"},
    {'soso',"vehicles\\rwarthog\\shaders\\rwarthog_tires"},
    {'bitm',"vehicles\\rwarthog\\bitmaps\\rwarthog_tires"},
    {'soso',"vehicles\\rwarthog\\shaders\\rwarthog_hull"},
    {'bitm',"vehicles\\rwarthog\\bitmaps\\rwarthog"},
    {'soso',"vehicles\\rwarthog\\shaders\\rwarthog_rl"},
    {'bitm',"vehicles\\rwarthog\\bitmaps\\rwarthog_rl"},
    {'bitm',"vehicles\\rwarthog\\bitmaps\\rwarthog_rl_multipurpose"},
    {'coll',"vehicles\\rwarthog\\rwarthog"},
    {'weap',"vehicles\\rwarthog\\rwarthog_gun"},
    {'wphi',"vehicles\\rwarthog\\rwarthog_gun"},
    {'wphi',"ui\\hud\\master rwarthog"},
    {'snd!',"sound\\sfx\\weapons\\rocket launcher\\reload"},
    {'effe',"weapons\\rocket launcher\\effects\\fire"},
    {'jpt!',"vehicles\\rwarthog\\effects\\trigger"}
};
enum { IMPORT_COUNT=sizeof(imports)/sizeof(imports[0]), MAX_PATCHES=1024 };
struct overlay_patch { uint32_t *word, replacement; };

static struct n3ds_cache_view pc_view;
static struct cache_file_tag_instance *extended;
static int active;

/* Kevin Boggs: same_tag - New3DS integration. */
static int same_tag(const struct cache_file_tag_instance *tag,
    long group, const char *name)
{
    return tag->group_tag==group && tag->name && !_stricmp(tag->name,name);
}

/* Kevin Boggs: find_tag - New3DS integration. */
static int find_tag(const struct n3ds_cache_view *view,long group,const char *name)
{
    for(long i=0;i<view->tag_header->tag_count;++i)
        if(same_tag(&view->instances[i],group,name))return (int)i;
    return -1;
}

/* Kevin Boggs: pc_pointer - New3DS integration. */
static int pc_pointer(const void *pointer,unsigned long bytes)
{
    uintptr_t address=(uintptr_t)pointer,base=(uintptr_t)pc_view.tags;
    return address>=base && address-base<=pc_view.tag_bytes &&
        bytes<=pc_view.tag_bytes-(address-base);
}

/* Kevin Boggs: mapped_datum - New3DS integration. */
static uint32_t mapped_datum(const struct n3ds_cache_view *xbox,
    const unsigned short *pc_indices,uint32_t word)
{
    unsigned int index=word&0xffffu;
    if(index>=(unsigned int)pc_view.tag_header->tag_count ||
       (uint32_t)pc_view.instances[index].tag_index!=word)return word;
    for(unsigned int i=0;i<IMPORT_COUNT;++i)
        if(pc_indices[i]==index)
            return (uint32_t)extended[xbox->tag_header->tag_count+i].tag_index;
    const struct cache_file_tag_instance *source=&pc_view.instances[index];
    int existing=find_tag(xbox,source->group_tag,source->name);
    return existing<0 ? UINT32_MAX : (uint32_t)xbox->instances[existing].tag_index;
}

int n3ds_campaign_overlay_open(struct n3ds_cache_view *xbox,const char *map_name)
{
    unsigned short pc_indices[IMPORT_COUNT];
    int pc_reticles=-1,xbox_reticles=-1;
    struct overlay_patch *patches=NULL;
    unsigned int patch_count=0;
    char message[160];
    if(!xbox || !map_name || strcmp(map_name,"b30") || !n3ds_campaign_extras_enabled())return 0;
    if(active || extended || pc_view.tags || !xbox->tag_header || !xbox->instances)return 0;
    if(xbox->tag_header->tag_count<=0 || xbox->tag_header->tag_count+IMPORT_COUNT>=32768)return 0;
    if(n3ds_pc_tags_open("bloodgulch",&pc_view)) {
        n3ds_log("CAMPAIGN VEHICLE: PC Blood Gulch assets unavailable; Xbox mission remains playable");
        return 0;
    }
    if(!pc_view.tag_header || !pc_view.instances ||
       pc_view.tag_header->tag_count<=0 || pc_view.tag_header->tag_count>=32768)goto fail;
    for(unsigned int i=0;i<IMPORT_COUNT;++i) {
        int found=find_tag(&pc_view,imports[i].group,imports[i].name);
        if(found<0 || find_tag(xbox,imports[i].group,imports[i].name)>=0 ||
           !pc_pointer(pc_view.instances[found].base_address,4))goto fail;
        pc_indices[i]=(unsigned short)found;
    }
    /* The retail PC Rocket Warthog HUD uses reticle sequence 16. The Xbox
     * bitmap of the same name has only 15 sequences, while the PC bitmap has
     * 17. Replace this one b30 tag instance for the overlay's lifetime so
     * both its new WPHI and existing Xbox references resolve consistently. */
    static const char *reticles="ui\\hud\\bitmaps\\combined\\hud_reticles";
    pc_reticles=find_tag(&pc_view,'bitm',reticles);
    xbox_reticles=find_tag(xbox,'bitm',reticles);
    if(pc_reticles<0 || xbox_reticles<0 ||
       !pc_pointer(pc_view.instances[pc_reticles].base_address,sizeof(struct bitmap_group)))goto fail;
    const struct bitmap_group *pc_bitmap=(const struct bitmap_group *)pc_view.instances[pc_reticles].base_address;
    const struct bitmap_group *xbox_bitmap=(const struct bitmap_group *)xbox->instances[xbox_reticles].base_address;
    if(!xbox_bitmap || pc_bitmap->sequences.count!=17 || xbox_bitmap->sequences.count!=15 ||
       !pc_pointer(pc_bitmap->sequences.address,17*sizeof(struct bitmap_group_sequence)))goto fail;
    long original_count=xbox->tag_header->tag_count;
    extended=malloc((original_count+IMPORT_COUNT)*sizeof(*extended));
    patches=malloc(MAX_PATCHES*sizeof(*patches));
    if(!extended || !patches)goto fail;
    memcpy(extended,xbox->instances,original_count*sizeof(*extended));
    for(unsigned int i=0;i<IMPORT_COUNT;++i) {
        unsigned int index=(unsigned int)original_count+i;
        extended[index]=pc_view.instances[pc_indices[i]];
        extended[index].tag_index=(long)(((0xe174u+index)<<16)|index);
    }
    for(unsigned int i=0;i<=IMPORT_COUNT;++i) {
        unsigned int pc_index=i==IMPORT_COUNT?(unsigned int)pc_reticles:pc_indices[i];
        uintptr_t start=(uintptr_t)pc_view.instances[pc_index].base_address;
        uintptr_t end=(uintptr_t)pc_view.tags+pc_view.tag_bytes;
        for(long j=0;j<pc_view.tag_header->tag_count;++j) {
            uintptr_t candidate=(uintptr_t)pc_view.instances[j].base_address;
            if(candidate>start && candidate<end && pc_pointer((void *)candidate,1))end=candidate;
        }
        if(end<=start || end-start>131072 || !pc_pointer((void *)start,end-start))goto fail;
        for(uintptr_t at=start;at+4<=end;at+=4) {
            uint32_t word,replacement;
            memcpy(&word,(void *)at,4);
            replacement=mapped_datum(xbox,pc_indices,word);
            if(replacement==word)continue;
            if(replacement==UINT32_MAX || patch_count>=MAX_PATCHES)goto fail;
            patches[patch_count++]=(struct overlay_patch){(uint32_t *)at,replacement};
        }
    }
    if(patch_count<50)goto fail;
    for(unsigned int i=0;i<patch_count;++i)
        *patches[i].word=patches[i].replacement;
    extended[xbox_reticles]=pc_view.instances[pc_reticles];
    extended[xbox_reticles].tag_index=xbox->instances[xbox_reticles].tag_index;
    xbox->instances=extended;
    xbox->tag_header->tag_instances=extended;
    xbox->tag_header->tag_count=original_count+IMPORT_COUNT;
    n3ds_pc_tags_use_overlay();
    active=1;
    snprintf(message,sizeof(message),"CAMPAIGN VEHICLE: imported %u PC tags, remapped %u references, upgraded HUD reticles",
        (unsigned int)IMPORT_COUNT,patch_count);
    n3ds_log(message);
    free(patches);
    return 1;
fail:
    free(patches);
    free(extended);extended=NULL;
    n3ds_cache_close(&pc_view);
    n3ds_pc_tags_close();
    n3ds_log("CAMPAIGN VEHICLE: PC tag import rejected; Xbox mission remains playable");
    return 0;
}

void n3ds_campaign_overlay_close(void)
{
    if(!active && !extended && !pc_view.tags)return;
    active=0;
    free(extended);extended=NULL;
    n3ds_cache_close(&pc_view);
    n3ds_pc_tags_close();
}

int n3ds_campaign_overlay_active(void) {return active;}

#else

/* Kevin Boggs: n3ds_campaign_overlay_open - New3DS integration. */
int n3ds_campaign_overlay_open(struct n3ds_cache_view *xbox,const char *map_name)
{(void)xbox;(void)map_name;return 0;}
/* Kevin Boggs: n3ds_campaign_overlay_close - New3DS integration. */
void n3ds_campaign_overlay_close(void) {}
/* Kevin Boggs: n3ds_campaign_overlay_active - New3DS integration. */
int n3ds_campaign_overlay_active(void) {return 0;}

#endif
