/* Engine-owned map bitmaps converted to bounded PICA resources on demand.
 * Supports cached 2D DXT1/3/5 and swizzled color/luminance/alpha images. */
#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps_internal.h"
#include "cache/texture_cache.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "engine_textures.h"
#include "engine_renderer_features.h"
#include "engine_presentation.h"
#include "engine_texture_prefetch.h"
#include "engine_bitmaps.h"
#include "engine_input.h"
#include "engine_files.h"
#include "s3tc.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "engine_lightmap_filter.inl"
#include "engine_display_textures.inl"

enum { NATIVE_GAME_TEXTURE_SLOTS = 256, NATIVE_TEXTURE_SLOTS = 512,
    NATIVE_TEXTURE_BUDGET = 4*1024*1024, NATIVE_MENU_TEXTURE_BUDGET = 8*1024*1024,
    NATIVE_LARGE_TEXTURE_BUDGET = 8*1024*1024, NATIVE_LARGE_TEXTURE_HEADROOM = 4*1024*1024 };
#ifndef HALO_N3DS_TEXTURE_CACHE_MIB
#define HALO_N3DS_TEXTURE_CACHE_MIB 4
#endif
_Static_assert(HALO_N3DS_TEXTURE_CACHE_MIB==4 || HALO_N3DS_TEXTURE_CACHE_MIB==8,"Texture cache budget must be 4 or 8 MiB");
struct native_texture_entry { struct bitmap_data *bitmap; void *resource; unsigned int bytes,last_used; };
static struct native_texture_entry entries[NATIVE_TEXTURE_SLOTS];
static unsigned int texture_count, texture_bytes,texture_frame;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
static unsigned int presentation_texture_revision;
unsigned int n3ds_engine_texture_presentation_revision(void)
{
    return presentation_texture_revision;
}
#endif
static unsigned int texture_resolution = 128;
static boolean initialized, opened;
/* Layout is also addressed byte-by-byte by the original script globals. */
boolean debug_texture_cache;
struct { boolean graph, list; } texture_cache_debug_options;
_Static_assert(sizeof(texture_cache_debug_options)==2,"Script texture debug flag layout");
void n3ds_log(const char *message);
#include "engine_lightmap_filter_tests.inl"
static struct bitmap_data *pending[NATIVE_TEXTURE_SLOTS];
static unsigned int pending_order[NATIVE_TEXTURE_SLOTS],pending_seen[NATIVE_TEXTURE_SLOTS],request_sequence,pending_limit;
static byte pending_synchronous[NATIVE_TEXTURE_SLOTS];
enum texture_lane { TEXTURE_DEMAND, TEXTURE_QUEUED, TEXTURE_PREFETCH };
static void *load_bitmap_now(struct bitmap_data *bitmap,enum texture_lane lane,int allow_eviction);
static void texture_prefetch_service(int queued_only);
static void texture_prefetch_clear(int shutdown);
static void texture_prefetch_forget(const struct bitmap_data *bitmap);
static int texture_prefetch_finish_for(const struct bitmap_data *bitmap,enum texture_lane lane);
static int texture_queue_async_enabled(void);
static void texture_prefetch_note_miss(const struct bitmap_data *bitmap);
static void texture_prefetch_prioritize_pending(const struct bitmap_data *bitmap,unsigned int slot);
int n3ds_gpu_frame_active(void);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
static long long idle_ticks[6];
void n3ds_engine_texture_idle_ticks(long long result[6]) { memcpy(result,idle_ticks,sizeof(idle_ticks)); }
#if HALO_N3DS_TEXTURE_PROFILE
static struct native_texture_profile texture_profile;
#define TEXTURE_PROFILE(statement) do { statement; } while(0)
static struct native_texture_load_profile *texture_load_profile(enum texture_lane lane)
{
    switch(lane) {
        case TEXTURE_DEMAND: return &texture_profile.demand;
        case TEXTURE_QUEUED: return &texture_profile.queued;
        case TEXTURE_PREFETCH: return &texture_profile.prefetch;
    }
    return &texture_profile.demand;
}
#else
#define TEXTURE_PROFILE(statement) ((void)0)
#endif
void *n3ds_cache_linear_allocate(unsigned long bytes);
void n3ds_cache_linear_release(void *pointer);
#include "engine_texture_archive.inl"
static void *texture_scratch(unsigned int bytes,boolean *linear)
{
    /* The cache already retries failed uploads, so an optional decode must
     * not call cseries' fatal malloc wrapper. Original-model normal heaps
     * can be full while the linear heap still has temporary working space. */
    *linear=FALSE;
    void *result=(malloc)(bytes);
    if(!result && n3ds_input_platform_original_model()) {
        result=n3ds_cache_linear_allocate(bytes);
        if(result) {
            *linear=TRUE;
            static unsigned int reported;
            if(reported++<3) {char message[96];snprintf(message,sizeof(message),"TEXTURE SCRATCH: linear fallback bytes=%u",bytes);n3ds_log(message);}
        }
    }
    if(!result) TEXTURE_PROFILE(++texture_profile.allocation_failures);
    return result;
}
static void texture_scratch_free(void *pointer,boolean linear)
{
    if(linear) n3ds_cache_linear_release(pointer);else free(pointer);
}

unsigned int n3ds_engine_texture_count(void) { return texture_count; }
unsigned int n3ds_engine_texture_bytes(void) { return texture_bytes; }
static boolean menu_cache(void)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    return view && view->tags && !strcmp(view->header.name,"ui");
}
static int texture_large_budget(void)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    return HALO_N3DS_TEXTURE_CACHE_MIB==8 && view && view->tags && !menu_cache() &&
        !n3ds_input_platform_original_model();
}
static unsigned int texture_total_budget(void)
{
    if(menu_cache()) return NATIVE_MENU_TEXTURE_BUDGET;
    return texture_large_budget()?NATIVE_LARGE_TEXTURE_BUDGET:NATIVE_TEXTURE_BUDGET;
}
static unsigned int texture_budget(void)
{
    unsigned int total=texture_total_budget();
    unsigned int variants=n3ds_gpu_texture_variant_bytes();
    return variants<total?total-variants:0;
}
void n3ds_engine_texture_profile_read(struct native_texture_profile *result)
{
#if HALO_N3DS_TEXTURE_PROFILE
    texture_profile.resident_bytes=texture_bytes;
    texture_profile.resident_slots=texture_count;
    texture_profile.budget_bytes=texture_budget();
    texture_profile.linear_free=n3ds_gpu_texture_memory_free();
    *result=texture_profile;
    memset(&texture_profile,0,sizeof(texture_profile));
#else
    memset(result,0,sizeof(*result));
#endif
}
static unsigned int texture_slot_limit(void)
{
    return menu_cache() ? NATIVE_TEXTURE_SLOTS : NATIVE_GAME_TEXTURE_SLOTS;
}
int n3ds_engine_texture_resolution(unsigned int limit)
{
    if(texture_count || (limit!=32 && limit!=64 && limit!=128)) return 0;
    if(limit!=texture_resolution) texture_prefetch_clear(0);
    texture_resolution=limit;
    return 1;
}
void texture_cache_debug_render(void)
{
    unsigned int i;
    char message[160];
    if(!debug_texture_cache && !texture_cache_debug_options.graph && !texture_cache_debug_options.list) return;
    snprintf(message,sizeof(message),"NATIVE TEXTURE CACHE: count=%u slots=%u bytes=%u budget=%u",texture_count,texture_slot_limit(),texture_bytes,texture_budget());
    n3ds_log(message);
    if(texture_cache_debug_options.graph)
        n3ds_log("Native texture cache graph unavailable; numeric occupancy reported above");
    if(texture_cache_debug_options.list)
        for(i=0;i<NATIVE_TEXTURE_SLOTS;++i) if(entries[i].bitmap) {
            snprintf(message,sizeof(message),"NATIVE TEXTURE ENTRY: slot=%u width=%d height=%d format=%d bytes=%u",
                i,entries[i].bitmap->width,entries[i].bitmap->height,entries[i].bitmap->format,entries[i].bytes);
            n3ds_log(message);
        }
}
static boolean owns_bitmap(const struct bitmap_data *bitmap)
{
    const struct n3ds_cache_view *view = n3ds_engine_cache_view(0);
    struct cache_file_tag_instance *tag;
    struct bitmap_group *group;
    struct bitmap_data *bitmaps;
    if (!view->tags || !bitmap || n3ds_cache_resolve(view, bitmap, sizeof(*bitmap)) != bitmap) return FALSE;
    tag = n3ds_cache_tag(view, bitmap->tag_index, BITMAP_GROUP_TAG);
    if (!tag) return FALSE;
    group = n3ds_cache_resolve(view, tag->base_address, sizeof(*group));
    if (!group || group->bitmaps.count <= 0 || group->bitmaps.count > view->tag_bytes/sizeof(*bitmap)) return FALSE;
    bitmaps = n3ds_cache_resolve(view, group->bitmaps.address, group->bitmaps.count*sizeof(*bitmap));
    if (!bitmaps) return FALSE;
    /* The validated tag block is contiguous. Preserve exact element
     * membership without revisiting every animation frame in the atlas. */
    unsigned long address=(unsigned long)bitmap,base=(unsigned long)bitmaps;
    unsigned long bytes=group->bitmaps.count*sizeof(*bitmap);
    return address>=base && address-base<bytes && (address-base)%sizeof(*bitmap)==0;
}
static void *cached_bitmap_resource(struct bitmap_data *bitmap)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    /* Validate the map address before reading the slot. The slot is only a
     * hint: the live owner and resource must also agree. Unload/eviction clear
     * entries before tag storage is released, so a reused address cannot hit. */
    if(!view || !view->tags || !bitmap || n3ds_cache_resolve(view,bitmap,sizeof(*bitmap))!=bitmap) return NULL;
    unsigned int slot=(unsigned int)bitmap->cache_block_index;
    if(slot>=NATIVE_TEXTURE_SLOTS || entries[slot].bitmap!=bitmap ||
       entries[slot].resource!=bitmap->hardware_format) return NULL;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(!n3ds_presentation_midpoint_active())
#endif
        entries[slot].last_used=texture_frame;
    return entries[slot].resource;
}
static unsigned int texture_oldest_unbound(void)
{
    unsigned int oldest=NATIVE_TEXTURE_SLOTS;
    void *bound[3]={n3ds_gpu_texture_bound(0),n3ds_gpu_texture_bound(1),n3ds_gpu_texture_bound(2)};
    for(unsigned int slot=0;slot<NATIVE_TEXTURE_SLOTS;++slot) {
        struct native_texture_entry *entry=entries+slot;
        /* Protect this and the preceding frame, plus every still-bound sampler.
         * Multi-map materials can retain earlier stages while loading later ones. */
        if(!entry->bitmap || texture_frame-entry->last_used<=2 ||
           entry->resource==bound[0] || entry->resource==bound[1] || entry->resource==bound[2]) continue;
        if(oldest==NATIVE_TEXTURE_SLOTS || texture_frame-entry->last_used>texture_frame-entries[oldest].last_used) oldest=slot;
    }
    return oldest;
}
static void texture_evict(unsigned int slot)
{
    struct native_texture_entry *entry=entries+slot;
    entry->bitmap->hardware_format=NULL;entry->bitmap->base_address=NULL;entry->bitmap->cache_block_index=NONE;
    n3ds_gpu_texture_destroy(entry->resource);texture_bytes-=entry->bytes;--texture_count;
    memset(entry,0,sizeof(*entry));
}
static int texture_fence(void)
{
#if HALO_N3DS_TEXTURE_PROFILE
    long long start=n3ds_engine_ticks();
#endif
    int ready=n3ds_gpu_texture_barrier();
    TEXTURE_PROFILE(texture_profile.fence_ticks+=n3ds_engine_ticks()-start);
    return ready;
}
static int texture_can_stage_allocated(unsigned int bytes,int allocated)
{
    unsigned int budget=texture_budget();
    /* Speculative uploads must fit under the next frame's reclaim threshold. */
    return bytes<=budget && texture_bytes<=budget-bytes && budget-bytes-texture_bytes>=128*1024 &&
        texture_count<texture_slot_limit()*7/8 && n3ds_gpu_texture_memory_free()>=(allocated?0:bytes)+NATIVE_LARGE_TEXTURE_HEADROOM;
}
static int texture_can_stage(unsigned int bytes) {return texture_can_stage_allocated(bytes,0);}
static int texture_make_room_allocated(unsigned int bytes,int allocated)
{
    unsigned int budget=texture_budget(),evicted=0;
    if(bytes>budget) return 0;
    if(!allocated && n3ds_gpu_texture_memory_free()<NATIVE_LARGE_TEXTURE_HEADROOM+bytes) {
        /* Demand outranks an unpublished worker image. Cancel without waiting;
         * a finished job can release its allocation immediately, an active
         * job keeps it until acknowledgement. Actual free space includes it. */
        texture_prefetch_clear(0);texture_prefetch_service(1);
    }
    int fenced=n3ds_gpu_frame_active();
    int large=texture_large_budget();
    for(;;) {
        int slots=texture_count>=texture_slot_limit(), full=texture_bytes>budget-bytes;
        int pressure=large && texture_bytes+bytes>NATIVE_TEXTURE_BUDGET &&
            n3ds_gpu_texture_memory_free()<NATIVE_LARGE_TEXTURE_HEADROOM+(allocated?0:bytes);
        if(!slots && !full && !pressure) break;
        unsigned int slot=texture_oldest_unbound();
        if(slot==NATIVE_TEXTURE_SLOTS) {
            /* A required texture still gets an allocation attempt when only
             * the extra headroom is tight and every resident is in use. */
            if(slots || full) return 0;
            break;
        }
        /* FrameBegin already waits for all previous GPU work. An aged, unbound
         * image cannot be in this frame's command list. Outside a frame, fence
         * once before reclaiming; never begin a nested frame from a draw. */
        if(!fenced) {if(!texture_fence()) return 0;fenced=1;}
        TEXTURE_PROFILE(texture_profile.slot_evictions+=slots);
        TEXTURE_PROFILE(texture_profile.byte_evictions+=full);
        TEXTURE_PROFILE(texture_profile.headroom_evictions+=pressure);
        TEXTURE_PROFILE(++texture_profile.demand_evictions);
        texture_evict(slot);++evicted;
    }
    if(evicted) {char text[128];snprintf(text,sizeof(text),"TEXTURE CACHE: demand reclaimed=%u requested=%u remaining=%u",evicted,bytes,texture_bytes);n3ds_log(text);}
    return 1;
}
static int texture_make_room(unsigned int bytes) {return texture_make_room_allocated(bytes,0);}
static void *texture_publish(struct bitmap_data *bitmap,void *resource,unsigned int bytes,int speculative)
{
    unsigned int budget=texture_budget();
    if(!resource || bytes>budget || texture_bytes>budget-bytes || texture_count>=texture_slot_limit()) return NULL;
    unsigned int slot;
    for(slot=0;slot<NATIVE_TEXTURE_SLOTS && entries[slot].bitmap;++slot) {}
    if(slot==NATIVE_TEXTURE_SLOTS || !n3ds_gpu_texture_finalize(resource)) return NULL;
    n3ds_gpu_texture_publish_immutable(resource);
    entries[slot]=(struct native_texture_entry){bitmap,resource,bytes,speculative?texture_frame-3u:texture_frame};
    bitmap->hardware_format=resource;bitmap->cache_block_index=slot;bitmap->base_address=NULL;
    texture_bytes+=bytes;++texture_count;
    return resource;
}
void texture_cache_flush(void)
{
    unsigned int i;
    texture_prefetch_clear(1);
    texture_archive_close();
    memset(pending, 0, sizeof(pending));
    pending_limit=0;
    if (!texture_count) return;
    /* Map unload happens between frames, before its bitmap structures are freed. */
    {
        int ready = texture_fence();
        if (!ready) { assert(ready); abort(); }
    }
    for (i = 0; i < NATIVE_TEXTURE_SLOTS; ++i) if (entries[i].bitmap) {
        entries[i].bitmap->hardware_format = NULL;
        entries[i].bitmap->base_address = NULL;
        entries[i].bitmap->cache_block_index = NONE;
        n3ds_gpu_texture_destroy(entries[i].resource);
    }
    memset(entries, 0, sizeof(entries));
    texture_count = texture_bytes = 0;
}
void n3ds_engine_texture_bsp_transition(void)
{
    if(n3ds_input_platform_original_model()) {texture_cache_flush();return;}
    texture_prefetch_clear(0);
    /* Only map-tag bitmaps enter this cache; their owners survive a BSP swap.
     * Whole-map unload still fences and clears every resource before free. */
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    for(unsigned int i=0;i<NATIVE_TEXTURE_SLOTS;++i) if(entries[i].bitmap)
        assert(n3ds_cache_resolve(view,entries[i].bitmap,sizeof(*entries[i].bitmap))==entries[i].bitmap);
    memset(pending,0,sizeof(pending));pending_limit=0;texture_frame+=3;
    char message[128];snprintf(message,sizeof(message),"BSP TEXTURES: retained=%u bytes=%u; normal demand eviction remains active",texture_count,texture_bytes);n3ds_log(message);
}
void texture_cache_new(void) { assert(!initialized); initialized = TRUE; }
void texture_cache_open(void) { assert(initialized && !opened); opened = TRUE; }
void texture_cache_close(void) { texture_cache_flush(); opened = FALSE; }
void texture_cache_delete(void) { texture_cache_close(); initialized = FALSE; }
static int texture_idle_pressure(unsigned int headroom,int large)
{
    return headroom && (!large || texture_bytes>NATIVE_TEXTURE_BUDGET) &&
        n3ds_gpu_texture_memory_free()<headroom;
}
static void texture_pending_remove(unsigned int slot)
{
    pending[slot]=NULL;
    while(pending_limit && !pending[pending_limit-1]) --pending_limit;
}
static unsigned int texture_pending_oldest(int synchronous)
{
    unsigned int oldest=NATIVE_TEXTURE_SLOTS;
    for(unsigned int i=0;i<pending_limit;++i) if(pending[i]) {
        if(texture_frame-pending_seen[i]>30) {texture_pending_remove(i);continue;}
        if(synchronous>=0 && pending_synchronous[i]!=synchronous) continue;
        if(oldest==NATIVE_TEXTURE_SLOTS || request_sequence-pending_order[i]>request_sequence-pending_order[oldest]) oldest=i;
    }
    return oldest;
}
/* Service ordinary queued textures before optional read-ahead. */
void texture_cache_idle(void)
{
    assert(initialized);
    ++texture_frame;
#if HALO_N3DS_TEXTURE_PROFILE
    long long maintenance_start=n3ds_engine_ticks();
#endif
    /* Prepare the bounded index once per map before asynchronous requests
     * need it. This regular-frame boundary precedes GPU frame acquisition;
     * optional midpoint frames never open files or allocate an index. */
    if(opened && !n3ds_gpu_frame_active()) {
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
        if(!n3ds_presentation_midpoint_active())
#endif
            texture_archive_open();
    }
    /* Reclaim old working sets only between frames and after the GPU fence.
     * Never evict a texture referenced by this or the preceding frame. */
    unsigned int budget=texture_budget();
    /* Gameplay images are at most128x128 RGBA8 (64KiB); a64px cube is96KiB.
     * Reserve128KiB for the next queued upload, not a quarter of this4MiB
     * cache. Keeping recently visible masks/cubes avoids needless SD reloads.
     * Larger menu artwork retains its previous allowance and watermarks. */
    int menu=menu_cache();
    unsigned int high=menu?budget*7/8:budget-128*1024;
    unsigned int low=menu?budget*3/4:budget-256*1024;
    /* Map textures share linear memory with geometry and mutable HUD images.
     * A cache below its own 4 MiB cap can still crowd those out on original
     * hardware. Reclaim only old, fenced images until 2 MiB is available.
     * Texture resolution and the New-model working set are unchanged. */
    int large=texture_large_budget();
    unsigned int headroom=large?NATIVE_LARGE_TEXTURE_HEADROOM:n3ds_input_platform_original_model()?2u*1024*1024:0;
    if(texture_count>texture_slot_limit()*7/8 || texture_bytes>high || texture_idle_pressure(headroom,large)) {
        int waited=0;
        for(;;) {
            int slots=texture_count>texture_slot_limit()*3/4, full=texture_bytes>low;
            int pressure=texture_idle_pressure(headroom,large);
            if(!slots && !full && !pressure) break;
            unsigned int oldest=texture_oldest_unbound();
            if(oldest==NATIVE_TEXTURE_SLOTS) break;
            if(!waited) {assert(texture_fence());waited=1;}
            TEXTURE_PROFILE(texture_profile.slot_evictions+=slots);
            TEXTURE_PROFILE(texture_profile.byte_evictions+=full);
            TEXTURE_PROFILE(texture_profile.headroom_evictions+=pressure);
            TEXTURE_PROFILE(++texture_profile.maintenance_evictions);
            texture_evict(oldest);
        }
        if(waited) n3ds_log("TEXTURE CACHE: reclaimed inactive textures at frame boundary");
    }
    memset(idle_ticks,0,sizeof(idle_ticks));
    long long start=n3ds_engine_ticks();
    assert(n3ds_dynamic_bitmaps_collect());
    long long collected=n3ds_engine_ticks();idle_ticks[0]=collected-start;
    TEXTURE_PROFILE(texture_profile.maintenance_ticks+=collected-maintenance_start);
    /* On New hardware, only completed jobs publish here. Unsupported queued
     * inputs keep a single compatibility load per boundary so they can still
     * appear. Blocking draws retain the ordinary demand path. */
    int async=texture_queue_async_enabled();
    unsigned int maximum=n3ds_input_platform_original_model()?2:4;
    long long budget_ticks=n3ds_engine_tick_frequency()/500; /* 2 ms */
    if(async) {
        texture_prefetch_service(1);
        maximum=1;
    }
    for(unsigned int serviced=0;serviced<maximum;++serviced) {
        unsigned int oldest=texture_pending_oldest(async?1:-1);
        if(oldest==NATIVE_TEXTURE_SLOTS) break;
        if(async && n3ds_engine_ticks()-collected>=budget_ticks) break;
        struct bitmap_data *bitmap=pending[oldest];texture_pending_remove(oldest);
        load_bitmap_now(bitmap,TEXTURE_QUEUED,1);
        if(n3ds_engine_ticks()-collected>=budget_ticks) break;
    }
    idle_ticks[1]=n3ds_engine_ticks()-collected;
    if(idle_ticks[1]<budget_ticks) texture_prefetch_service(0);
}

/* Xbox swizzle separates the two coordinate contributions even for rectangular
 * images. Sample only texels which survive the existing nearest reduction and
 * write them directly into the same vertically flipped PICA layout. */
static int sampled_raw_texture(const byte *source, unsigned int source_bytes,
    int format, unsigned int width, unsigned int height,
    unsigned int target_width, unsigned int target_height,
    unsigned int *output, unsigned int output_bytes)
{
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    unsigned int xs[512],ys[512],x,y,stride;
    if(!source || !output || !((format>=0 && format<=3) || format==6 || (format>=8 && format<=11) || format==17)) return 0;
    if(width<4 || height<4 || width>1024 || height>1024 || (width&(width-1)) || (height&(height-1)) ||
       target_width<8 || target_height<8 || target_width>512 || target_height>512 ||
       target_width>MAX(width,8) || target_height>MAX(height,8) || (target_width&(target_width-1)) || (target_height&(target_height-1))) return 0;
    stride=format<=2 || format==17?1:format>=10?4:2;
    if(source_bytes<width*height*stride || output_bytes<target_width*target_height*4) return 0;
    for(x=0;x<target_width;++x) {
        long swizzled[2]; bitmap_swizzle_vector2d(width,height,x*width/target_width,0,swizzled);
        xs[x]=swizzled[0];
    }
    for(y=0;y<target_height;++y) {
        long swizzled[2]; bitmap_swizzle_vector2d(width,height,0,y*height/target_height,swizzled);
        ys[y]=swizzled[1];
    }
    for(y=0;y<target_height;++y) for(x=0;x<target_width;++x) {
        unsigned int native_y=target_height-1-y;
        unsigned int offset=((native_y/8)*(target_width/8)+x/8)*64+spread[x&7]+2*spread[native_y&7];
        if(format==17) {
            unsigned int argb=global_vector_palette[source[xs[x]|ys[y]]];
            output[offset]=(argb<<8)|(argb>>24);
        } else output[offset]=n3ds_bitmap_decode_pixel(source+((xs[x]|ys[y])*stride),format);
    }
    return 1;
}

/* Compare with the former full-image decode, including discarded pixels, on
 * deterministic varied input. This runs before gameplay timing begins. */
int n3ds_engine_sampled_texture_tests(void)
{
    static const int formats[]={0,1,2,3,6,8,9,10,11,17};
    static const unsigned int dimensions[][2]={{8,8},{16,32},{32,16},{128,128},{256,64},{64,256},{1024,8},{8,1024},{4,4},{4,512},{512,4},{512,64},{64,512}};
    static const unsigned int limits[]={32,64,128,256,512};
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    enum { TEST_BYTES=131072 };
    byte *source=malloc(TEST_BYTES),*rgba=malloc(TEST_BYTES);
    unsigned int *expected=malloc(TEST_BYTES),*actual=malloc(TEST_BYTES+8);
    unsigned int state=0x128abc73U,f,d,l,x,y,cases=0,rejected=0;
    int result=1;
    if(!source || !rgba || !expected || !actual) goto done;
    for(f=0;f<TEST_BYTES;++f) {state^=state<<13;state^=state>>17;state^=state<<5;source[f]=state;}
    for(f=0;f<sizeof(formats)/sizeof(*formats);++f) for(d=0;d<sizeof(dimensions)/sizeof(*dimensions);++d) {
        unsigned int width=dimensions[d][0],height=dimensions[d][1],stride=formats[f]<=2 || formats[f]==17?1:formats[f]>=10?4:2;
        /* Retained reference: decode every source pixel, then reduce and tile. */
        for(y=0;y<height;++y) for(x=0;x<width;++x) {
            long swizzled[2];bitmap_swizzle_vector2d(width,height,x,y,swizzled);
            unsigned int color;
            if(formats[f]==17) {
                unsigned int argb=bitmap_format_to_a8r8g8b8(17,source,swizzled[0]|swizzled[1]);
                color=((argb>>16)&255)<<24|((argb>>8)&255)<<16|(argb&255)<<8|(argb>>24);
            } else color=n3ds_bitmap_decode_pixel(source+(swizzled[0]|swizzled[1])*stride,formats[f]);
            byte *p=rgba+(y*width+x)*4;p[0]=color>>24;p[1]=color>>16;p[2]=color>>8;p[3]=color;
        }
        for(l=0;l<sizeof(limits)/sizeof(*limits);++l) {
            unsigned int tw=MAX(8,MIN(width,limits[l])),th=MAX(8,MIN(height,limits[l]));
            for(y=0;y<th;++y) for(x=0;x<tw;++x) {
                unsigned int ny=th-1-y,offset=((ny/8)*(tw/8)+x/8)*64+spread[x&7]+2*spread[ny&7];
                const byte *p=rgba+((y*height/th)*width+x*width/tw)*4;
                expected[offset]=(unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];
            }
            memset(actual,0x5a,TEST_BYTES+8);
            if(!sampled_raw_texture(source,width*height*stride,formats[f],width,height,tw,th,actual+1,tw*th*4) ||
               memcmp(actual+1,expected,tw*th*4) || actual[0]!=0x5a5a5a5aU || actual[tw*th+1]!=0x5a5a5a5aU) goto done;
            ++cases;
        }
    }
    memset(actual,0x5a,TEST_BYTES+8);
#define REJECT(s,b,f,w,h,tw,th,o,ob) do { if(sampled_raw_texture(s,b,f,w,h,tw,th,o,ob)) goto done; ++rejected; } while(0)
    REJECT(NULL,256,11,8,8,8,8,actual,256);
    REJECT(source,256,11,8,8,8,8,NULL,256);
    REJECT(source,256,7,8,8,8,8,actual,256);
    REJECT(source,255,11,8,8,8,8,actual,256);
    REJECT(source,256,11,8,8,8,8,actual,255);
    REJECT(source,65536,11,12,8,8,8,actual,256);
    REJECT(source,65536,11,8,8,16,8,actual,512);
    REJECT(source,TEST_BYTES,11,1024,8,1024,8,actual,TEST_BYTES);
    REJECT(source,65536,11,2048,8,128,8,actual,4096);
    REJECT(source,256,11,8,8,4,8,actual,256);
#undef REJECT
    for(x=0;x<(TEST_BYTES+8)/4;++x) if(actual[x]!=0x5a5a5a5aU) goto done;
    {char message[160];snprintf(message,sizeof(message),"PASS: sampled raw textures: %u exact full-decode comparisons across 10 formats (including Xbox P8 bump), 13 dimensions, 5 limits; %u rejected inputs",cases,rejected);n3ds_log(message);}
    result=0;
done:
    free(source);free(rgba);free(expected);free(actual);return result;
}

/* Decode only blocks that contribute to the output. A single 4x4 tile replaces
 * the full-size RGBA scratch image, including at unchanged texture dimensions. */
static int sampled_dxt_texture(const byte *source,unsigned int source_bytes,int format,
    unsigned int width,unsigned int height,unsigned int tw,unsigned int th,unsigned int *out)
{
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    if(!source || !out || format<14 || format>16 || width<4 || height<4 || width>1024 || height>1024 ||
       (width&(width-1)) || (height&(height-1)) || tw<8 || th<8 || tw>512 || th>512 ||
       tw>MAX(width,8) || th>MAX(height,8) || (tw&(tw-1)) || (th&(th-1))) return 0;
    unsigned int block_bytes=format==14?8:16;
    unsigned int step_x=MAX(1,(4*tw)>>__builtin_ctz(width)),step_y=MAX(1,(4*th)>>__builtin_ctz(height));
    unsigned int x_shift=__builtin_ctz(tw),y_shift=__builtin_ctz(th),blocks_wide=(width+3)/4;
    /* Coordinate selection and PICA addressing are separable. Prepare each
     * axis once instead of repeating variable divisions for every pixel and
     * block. These bounded tables use 5 KiB of the worker's 24 KiB stack. */
    unsigned short source_x[512],source_y[512],target_x[512];
    unsigned int target_y[512];
    for(unsigned int x=0;x<tw;++x) {
        source_x[x]=(x*width)>>x_shift;
        target_x[x]=(x/8)*64+spread[x&7];
    }
    for(unsigned int y=0;y<th;++y) {
        unsigned int ny=th-1-y;
        source_y[y]=(y*height)>>y_shift;
        target_y[y]=(ny/8)*(tw/8)*64+2*spread[ny&7];
    }
    byte rgba[64];
    for(unsigned int y=0;y<th;y+=step_y) for(unsigned int x=0;x<tw;x+=step_x) {
        unsigned int offset=((source_y[y]/4)*blocks_wide+source_x[x]/4)*block_bytes;
        if(offset>source_bytes || block_bytes>source_bytes-offset || halo_s3tc_decode(format==14?0x0c:format==15?0x0e:0x0f,
            source+offset,block_bytes,4,4,rgba,sizeof(rgba),16)) return 0;
        for(unsigned int yy=y;yy<MIN(y+step_y,th);++yy) for(unsigned int xx=x;xx<MIN(x+step_x,tw);++xx) {
            const byte *p=rgba+(((source_y[yy]&3)*4)+(source_x[xx]&3))*4;
            out[target_y[yy]+target_x[xx]]=(unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];
        }
    }
    return 1;
}
int n3ds_sampled_dxt_tests(void)
{
    const unsigned int sizes[][4]={{128,128,32,32},{64,256,32,64},{4,512,8,64},{512,4,64,8},{8,8,8,8},{4,4,8,8},{1024,8,32,8}};
    byte *source=malloc(65536),*reference=malloc(65536);unsigned int *actual=malloc(32768),state=17297,cases=0;
    if(!source || !reference || !actual) {free(source);free(reference);free(actual);return 3;}
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    for(unsigned int i=0;i<65536;++i) {state=state*1664525u+1013904223u;source[i]=state>>24;}
    for(int f=14;f<=16;++f) for(unsigned int c=0;c<NUMBEROF(sizes);++c) {
        unsigned int w=sizes[c][0],h=sizes[c][1],tw=sizes[c][2],th=sizes[c][3],bytes=((w+3)/4)*((h+3)/4)*(f==14?8:16);
        /* Larger synthetic inputs are generated with a smaller valid extent. */
        if(bytes>65536 || w*h*4>65536) continue;
        if(halo_s3tc_decode(f==14?0x0c:f==15?0x0e:0x0f,source,bytes,w,h,reference,65536,w*4) ||
           !sampled_dxt_texture(source,bytes,f,w,h,tw,th,actual)) return 1;
        for(unsigned int y=0;y<th;++y) for(unsigned int x=0;x<tw;++x) {
            unsigned int ny=th-1-y,index=((ny/8)*(tw/8)+x/8)*64+spread[x&7]+2*spread[ny&7];
            const byte *p=reference+((y*h/th)*w+x*w/tw)*4;
            if(actual[index]!=((unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3])) return 2;
        }
        ++cases;
    }
    char message[128];snprintf(message,sizeof(message),"PASS: sampled DXT: %u exact reduced/rectangular/upsampled comparisons",cases);n3ds_log(message);free(source);free(reference);free(actual);return 0;
}
struct native_cube_layout {unsigned int side,level,skip,bytes,stride,total,target;};
static int cube_layout(const struct bitmap_data *b,unsigned int limit,struct native_cube_layout *out)
{
    int raw=b && ((b->format>=0 && b->format<=3) || b->format==6 || (b->format>=8 && b->format<=11));
    if(!b || !out || b->signature!='bitm' || b->type!=2 || b->depth!=1 ||
       b->width<4 || b->width>1024 || b->height!=b->width || (b->width&(b->width-1)) ||
       !(b->flags&0x80) || !(b->flags&1) || (b->flags&16) ||
       (raw?(!(b->flags&8) || (b->flags&2)):((b->flags&8) || b->format<14 || b->format>16)) ||
       b->mipmap_count<0 || b->mipmap_count>10 || b->pixels_size<=0 || b->pixels_offset<2048 ||
       (limit!=32 && limit!=64)) return 0;
    struct native_cube_layout r={0};
    unsigned int side=b->width,skip=0,level=0;
    unsigned int pixel=raw?(b->format<=2?1:b->format>=10?4:2):0;
    unsigned int block=b->format==14?8:16;
    for(;;) {
        unsigned int bytes=raw?side*side*pixel:((side+3)/4)*((side+3)/4)*block;
        if(!r.side || (r.side>limit && side<r.side)) {
            r.side=side;r.level=level;r.skip=skip;r.bytes=bytes;
        }
        skip+=bytes;
        /* Xbox compressed chains stop at4x4; raw chains stop at1x1. */
        if(level==(unsigned int)b->mipmap_count || side==(raw?1u:4u)) break;
        side>>=1;++level;
    }
    r.stride=(skip+127)&~127u;r.total=r.stride*6;r.target=MAX(8,MIN(r.side,limit));
    if(r.total>(unsigned int)b->pixels_size || (unsigned int)b->pixels_offset>0x7fffffffu-r.total) return 0;
    *out=r;return 1;
}
static int cube_decode_face(const struct native_cube_layout *layout,int format,const byte *source,unsigned int *output)
{
    if(format<14) return sampled_raw_texture(source,layout->bytes,format,layout->side,layout->side,
        layout->target,layout->target,output,layout->target*layout->target*4);
    return sampled_dxt_texture(source,layout->bytes,format,layout->side,layout->side,
        layout->target,layout->target,output);
}
static void *load_cube_now(struct bitmap_data *bitmap,enum texture_lane lane)
{
    (void)lane;
    struct native_cube_layout layout;
    if(!cube_layout(bitmap,MIN(texture_resolution,64),&layout)) return NULL;
    struct native_texture_descriptor native;
    if(!n3ds_texture_descriptor_rgba8(&native,layout.target,layout.target,6,layout.level,
            layout.bytes*6,layout.bytes)) return NULL;
    unsigned int output_bytes=native.resident_bytes;
    if(!texture_make_room(output_bytes)) return NULL;
    boolean source_linear=FALSE;
    byte *source=NULL;struct native_texture_span images[6];
    void *resource=NULL,*reserved=n3ds_gpu_texture_reserve_descriptor(&native,images);
    if(!reserved) {TEXTURE_PROFILE(++texture_profile.allocation_failures);return NULL;}
    source=texture_scratch(layout.bytes,&source_linear);
    if(!source) goto done;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(lane!=TEXTURE_PREFETCH) ++presentation_texture_revision;
#endif
    for(unsigned int face=0;face<6;++face) {
        /* Cached Xbox payload is already hardware +X,-X,+Y,-Y,+Z,-Z order.
         * Each face has a full mip chain, padded independently to128bytes. */
#if HALO_N3DS_TEXTURE_PROFILE
        long long start=n3ds_engine_ticks();
#endif
        int read=n3ds_engine_cache_read(bitmap->pixels_offset+face*layout.stride+layout.skip,layout.bytes,source);
        TEXTURE_PROFILE(texture_load_profile(lane)->read_ticks+=n3ds_engine_ticks()-start);
        if(!read) goto done;
        TEXTURE_PROFILE(texture_load_profile(lane)->read_bytes+=layout.bytes);
        TEXTURE_PROFILE(start=n3ds_engine_ticks());
        if(!cube_decode_face(&layout,bitmap->format,source,images[face].data)) goto done;
        TEXTURE_PROFILE(texture_load_profile(lane)->decode_ticks+=n3ds_engine_ticks()-start);
    }
    texture_scratch_free(source,source_linear);source=NULL;source_linear=FALSE;
#if HALO_N3DS_TEXTURE_PROFILE
    long long start=n3ds_engine_ticks();
#endif
    resource=texture_publish(bitmap,reserved,output_bytes,lane==TEXTURE_PREFETCH);
    TEXTURE_PROFILE(texture_load_profile(lane)->upload_ticks+=n3ds_engine_ticks()-start);
    if(!resource) goto done;
    reserved=NULL;
    {char message[192];snprintf(message,sizeof(message),"NATIVE CUBE TEXTURE: tag=%08lx format=%d side=%u mip=%u face_stride=%u native=%u bytes=%u",bitmap->tag_index,bitmap->format,layout.side,layout.level,layout.stride,layout.target,output_bytes);n3ds_log(message);}
done:
    texture_scratch_free(source,source_linear);n3ds_gpu_texture_abandon(reserved);return resource;
}
#ifdef HALO_N3DS_TEXTURE_PRESSURE_TEST
static int texture_pressure_tests(void)
{
    extern int n3ds_gpu_frame_begin(void);
    extern void n3ds_gpu_present(void);
    struct bitmap_data bitmaps[64]={0};
    enum { image_bytes=128*128*4 };
    unsigned int *pixels=(malloc)(image_bytes),saved_frame=texture_frame;
    void *saved[3];int active=0,result=1;
    if(texture_count || texture_bytes || !pixels) {free(pixels);return 1;}
    for(unsigned int i=0;i<3;++i) saved[i]=n3ds_gpu_texture_bound(i);
    texture_frame=100;
#define PRESSURE_CHECK(x) do {if(!(x)) {n3ds_log("TEXTURE PRESSURE FAIL: " #x);goto done;} } while(0)
    for(unsigned int i=0;i<64;++i) {
        for(unsigned int p=0;p<128*128;++p) pixels[p]=0x112233ffu+i*0x01000000u;
        void *resource=n3ds_gpu_texture_create(pixels,128,128);PRESSURE_CHECK(resource);
        entries[i]=(struct native_texture_entry){bitmaps+i,resource,image_bytes,90};
        bitmaps[i].hardware_format=resource;bitmaps[i].cache_block_index=i;
        ++texture_count;texture_bytes+=image_bytes;
    }
    for(unsigned int i=0;i<3;++i) PRESSURE_CHECK(n3ds_gpu_texture_bind(i,entries[i].resource));
    entries[3].last_used=100;entries[4].last_used=99;entries[5].last_used=98;
    PRESSURE_CHECK(n3ds_gpu_frame_begin());active=1;
    PRESSURE_CHECK(texture_make_room(image_bytes));
    PRESSURE_CHECK(texture_count==63 && texture_bytes==63*image_bytes && !entries[6].bitmap);
    PRESSURE_CHECK(!bitmaps[6].hardware_format && bitmaps[6].cache_block_index==NONE);
    for(unsigned int i=0;i<6;++i) PRESSURE_CHECK(entries[i].resource==bitmaps[i].hardware_format);
    for(unsigned int i=0;i<64;++i) if(entries[i].bitmap) entries[i].last_used=texture_frame;
    PRESSURE_CHECK(!texture_make_room(2*image_bytes) && texture_count==63);
    n3ds_gpu_present();active=0;
    entries[7].last_used=90;
    PRESSURE_CHECK(texture_make_room(2*image_bytes) && texture_count==62 && !entries[7].bitmap);
    result=0;
done:
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    for(unsigned int i=0;i<3;++i) n3ds_gpu_texture_bind(i,saved[i]);
    texture_cache_flush();texture_frame=saved_frame;free(pixels);
    if(!result) n3ds_log("PASS: texture pressure: 4 MiB full cache, synchronous admission, bound/recent protection, active-frame rejection and fenced out-of-frame eviction");
#undef PRESSURE_CHECK
    return result;
}
#endif
int n3ds_engine_cube_layout_tests(void)
{
#ifdef HALO_N3DS_TEXTURE_PRESSURE_TEST
    if(texture_pressure_tests()) return 1;
#endif
    struct bitmap_data b={.signature='bitm',.width=64,.height=64,.depth=1,.type=2,.format=14,
        .flags=0x83,.mipmap_count=6,.pixels_offset=2048,.pixels_size=16896};
    struct native_cube_layout r;
    if(!cube_layout(&b,32,&r) || r.side!=32 || r.level!=1 || r.skip!=2048 || r.bytes!=512 || r.stride!=2816 || r.total!=16896) return 1;
    b.format=11;b.flags=0x89;b.pixels_size=131328;
    if(!cube_layout(&b,32,&r) || r.side!=32 || r.skip!=16384 || r.bytes!=4096 || r.stride!=21888 || r.total!=131328) return 1;
    --b.pixels_size;if(cube_layout(&b,32,&r)) return 1;++b.pixels_size;
    b.height=32;if(cube_layout(&b,32,&r)) return 1;b.height=64;
    b.type=0;if(cube_layout(&b,32,&r)) return 1;b.type=2;
    b.pixels_offset=0x7fffffff;if(cube_layout(&b,32,&r)) return 1;
    n3ds_log("PASS: native cube mip layout: compressed4x4/raw1x1 tails,128byte face alignment, truncated/range/type rejection");return 0;
}

struct bitmap_plan {
    unsigned int source_offset, bytes, width, height, target_width, target_height;
    unsigned int resolution, display_resolution, level;
    int format, raw, lightmap;
    struct native_texture_descriptor native;
};

static int bitmap_plan(const struct bitmap_data *bitmap,struct bitmap_plan *plan)
{
    unsigned int width,height,target_width,target_height,level,skip,bytes,block_bytes,output_bytes;
    unsigned int resolution=texture_resolution,display_resolution=0;
    int lightmap=0;
    int raw=(bitmap->format>=0 && bitmap->format<=3) || bitmap->format==6 ||
        (bitmap->format>=8 && bitmap->format<=11) || bitmap->format==17;
    unsigned int pixel_stride=(bitmap->format<=2 || bitmap->format==17)?1:bitmap->format>=10?4:2;
#ifndef HALO_N3DS_MENU_TEXTURE_BASELINE
    /* UI-map assets have their own 8 MiB cache. Improve their source texels
     * without changing world resolution, gameplay assets, or render passes. */
    extern int n3ds_input_platform_original_model(void);
    if(menu_cache()) resolution=MAX(resolution,n3ds_input_platform_original_model()?64:128);
#endif
    /* Interface artwork controls legibility independently of world detail.
     * Only menu headings/logo need the larger bound; other assets retain
     * their existing reductions and all remain within the cache budget. */
    {
        const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
        struct cache_file_tag_instance *tag=n3ds_cache_tag(view,bitmap->tag_index,BITMAP_GROUP_TAG);
        const struct bitmap_group *group=n3ds_cache_resolve(view,tag->base_address,sizeof(*group));
        lightmap=group->usage==4 && bitmap->format==6 && bitmap->type==0;
#ifndef HALO_N3DS_DISPLAY_TEXTURE_BASELINE
        if(bitmap->type==0 && group->type==0 && group->usage!=2 && !lightmap)
            display_resolution=display_texture_resolution(tag->name,n3ds_input_platform_original_model());
        resolution=MAX(resolution,display_resolution);
#endif
        /* HUD requests are deferred to the next frame, outside HUD begin/end.
         * The original asset namespace identifies those atlases persistently. */
        /* World reductions must not shrink menu labels, buttons or pause UI. */
        if(!strncmp(tag->name,"ui\\",3) && resolution<64) resolution=64;
#ifndef HALO_N3DS_MENU_TEXTURE_BASELINE
        if(!strncmp(tag->name,"ui\\shell\\",9) && resolution<128) resolution=128;
#endif
        if(group->type==4 || !strncmp(tag->name,"ui\\hud\\",7)) resolution=128;
        if(menu_cache() && !strncmp(tag->name,"ui\\shell\\",9)) {
            /* Match the256pixel composited heading width rather than keeping
             * twice as many horizontal samples for every retained page. */
            if(strstr(tag->name,"\\header_") ||
               !strncmp(tag->name,"ui\\shell\\main_menu\\menu_",24)) {
                resolution=256;
#ifndef HALO_N3DS_MENU_TEXTURE_BASELINE
                if(!n3ds_input_platform_original_model())resolution=512;
#endif
            }
            else if(!strcmp(tag->name,"ui\\shell\\main_menu\\halo_logo")) resolution=512;
        }
    }
    if (bitmap->signature != 'bitm' || !(bitmap->flags & 0x80) ||
        (!!(bitmap->flags&4)!=(bitmap->format==17)) ||
        (raw ? (!(bitmap->flags&8) || (bitmap->flags&(2|16))) : ((bitmap->flags&8) || bitmap->format<14 || bitmap->format>16)) ||
        bitmap->type != 0 || bitmap->depth != 1 ||
        bitmap->width < 4 || bitmap->height < 4 || bitmap->width > 2048 || bitmap->height > 2048 ||
        bitmap->mipmap_count < 0 || bitmap->mipmap_count > 10 || bitmap->pixels_size <= 0) return 0;
    width = bitmap->width; height = bitmap->height;
    if ((width & (width-1)) || (height & (height-1))) return 0;
    block_bytes = bitmap->format == 14 ? 8 : 16;
    level = skip = 0;
    while ((width > resolution || height > resolution) && width >= 16 && height >= 16 && level < (unsigned)bitmap->mipmap_count) {
        skip += raw ? width*height*pixel_stride : ((width+3)/4)*((height+3)/4)*block_bytes;
        width >>= 1; height >>= 1; ++level;
    }
    /* Retail Halo's inner-ring image is 2048x128, with smaller mip levels.
     * Apply the CPU decode bound after selecting a mip, not to the original
     * image dimensions. No larger PICA texture or decode buffer is needed. */
    if(width>1024 || height>1024) return 0;
    /* Original menu solid fills are 4x4; its gradient is 4x512. Expand
     * only the short axis to PICA's eight-texel minimum, preserving UVs. */
    target_width = MAX(8,MIN(width,resolution));
    target_height = MAX(8,MIN(height,resolution));
    output_bytes = target_width*target_height*4;
    bytes = raw ? width*height*pixel_stride : ((width+3)/4)*((height+3)/4)*block_bytes;
    if (skip > (unsigned)bitmap->pixels_size || bytes > (unsigned)bitmap->pixels_size-skip ||
        bitmap->pixels_offset < 2048 || (unsigned)bitmap->pixels_offset > 0x7fffffffU-skip) return 0;
    *plan=(struct bitmap_plan){.source_offset=(unsigned int)bitmap->pixels_offset+skip,.bytes=bytes,
        .width=width,.height=height,.target_width=target_width,.target_height=target_height,
        .resolution=resolution,.display_resolution=display_resolution,.level=level,
        .format=bitmap->format,.raw=raw,.lightmap=lightmap};
    /* Descriptor records the exact selected source and the bounded fallback
     * scratch. Archives write straight into the same reserved RGBA8 image. */
    return n3ds_texture_descriptor_rgba8(&plan->native,target_width,target_height,1,level,bytes,
        ((bytes+3)&~3u)+(lightmap?output_bytes/2:0));
}

static int bitmap_decode_sampled(const struct bitmap_plan *plan,const byte *source,
    unsigned int *tiled,unsigned short *distance)
{
    if(plan->lightmap) return lightmap_reduce(source,plan->bytes,plan->width,plan->height,
        plan->target_width,plan->target_height,tiled,distance);
    if(plan->raw) return sampled_raw_texture(source,plan->bytes,plan->format,plan->width,
        plan->height,plan->target_width,plan->target_height,tiled,plan->native.resident_bytes);
    return sampled_dxt_texture(source,plan->bytes,plan->format,plan->width,plan->height,
        plan->target_width,plan->target_height,tiled);
}

static void *bitmap_publish(struct bitmap_data *bitmap,const struct bitmap_plan *plan,void *reserved,
    int speculative)
{
    void *resource=texture_publish(bitmap,reserved,plan->native.resident_bytes,speculative);
    if(!resource) return NULL;
    if(plan->display_resolution) {
        const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
        struct cache_file_tag_instance *tag=n3ds_cache_tag(view,bitmap->tag_index,BITMAP_GROUP_TAG);
        char message[200];
        snprintf(message,sizeof(message),"DISPLAY TEXTURE: %.130s native=%ux%u cache_bytes=%u",
            tag->name,plan->target_width,plan->target_height,texture_bytes);
        n3ds_log(message);
    }
    return resource;
}

static void *load_bitmap_2d(struct bitmap_data *bitmap,enum texture_lane lane,int allow_eviction)
{
    struct bitmap_plan plan;
    if(!bitmap_plan(bitmap,&plan) ||
            !(allow_eviction?texture_make_room(plan.native.resident_bytes):texture_can_stage(plan.native.resident_bytes))) return NULL;
    unsigned int target_width=plan.target_width,target_height=plan.target_height;
    unsigned int bytes=plan.native.source_bytes,output_bytes=plan.native.resident_bytes;
    byte *compressed=NULL;
    struct native_texture_span images[6];unsigned int *tiled;
    boolean compressed_linear=FALSE;
    void *resource=NULL,*reserved=NULL;
    (void)lane;
#ifdef HALO_N3DS_ARCHIVE_VERIFY
    unsigned int *archive_reference=NULL;
    boolean archive_reference_linear=FALSE;
#endif
    reserved=n3ds_gpu_texture_reserve_descriptor(&plan.native,images);
    if(!reserved) {TEXTURE_PROFILE(++texture_profile.allocation_failures);return NULL;}
    tiled=images[0].data;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(lane!=TEXTURE_PREFETCH) ++presentation_texture_revision;
#endif
    long long stamp=n3ds_engine_ticks(),next;
    /* v1 lightmaps used nearest sampling of atlas padding. v2 and later have
     * the exact checked filter output and can replace the console decode. */
    unsigned int read_bytes=0;
    int archived=texture_archive_read(bitmap,plan.resolution,plan.source_offset,
        bytes,target_width,target_height,plan.lightmap,plan.level,(unsigned int)plan.format,
        plan.width,plan.height,tiled,&read_bytes);
    next=n3ds_engine_ticks();idle_ticks[2]+=next-stamp;
    TEXTURE_PROFILE(texture_load_profile(lane)->read_ticks+=next-stamp);
    TEXTURE_PROFILE(texture_load_profile(lane)->read_bytes+=read_bytes);
    TEXTURE_PROFILE(texture_load_profile(lane)->archive_hits+=archived);
#ifdef HALO_N3DS_ARCHIVE_VERIFY
    if(archived) {
        archive_reference=texture_scratch(output_bytes,&archive_reference_linear);
        if(!archive_reference) goto done;
        memcpy(archive_reference,tiled,output_bytes);
        archived=0;
    }
#endif
    if(!archived) {
        compressed=texture_scratch(bytes,&compressed_linear);
        if(!compressed) goto done;
        stamp=n3ds_engine_ticks();
        int read=n3ds_engine_cache_read(plan.source_offset,bytes,compressed);
        next=n3ds_engine_ticks();idle_ticks[2]+=next-stamp;
        TEXTURE_PROFILE(texture_load_profile(lane)->read_ticks+=next-stamp);
        if(!read) goto done;
        TEXTURE_PROFILE(texture_load_profile(lane)->read_bytes+=bytes);
        stamp=next;
        boolean distance_linear=FALSE;
        unsigned short *distance=plan.lightmap?texture_scratch(output_bytes/2,&distance_linear):NULL;
        /* The same block decoder used by the worker also covers full-sized
         * DXT images. Each independent block writes its exact final texels. */
        int decoded=(!plan.lightmap || distance) && bitmap_decode_sampled(&plan,compressed,tiled,distance);
        texture_scratch_free(distance,distance_linear);
        next=n3ds_engine_ticks();idle_ticks[3]+=next-stamp;
        TEXTURE_PROFILE(texture_load_profile(lane)->decode_ticks+=next-stamp);
        if(!decoded) goto done;
    }
#ifdef HALO_N3DS_ARCHIVE_VERIFY
    if(archive_reference) assert(!memcmp(archive_reference,tiled,output_bytes));
#endif
    /* The decoder has written the unpublished final image. Release its inputs
     * before flushing/publishing; no second tiled image or image copy exists. */
    texture_scratch_free(compressed,compressed_linear);compressed=NULL;compressed_linear=FALSE;
    stamp=n3ds_engine_ticks();
    resource=bitmap_publish(bitmap,&plan,reserved,!allow_eviction);
    if(resource) reserved=NULL;
    next=n3ds_engine_ticks();idle_ticks[5]+=next-stamp;
    TEXTURE_PROFILE(texture_load_profile(lane)->upload_ticks+=next-stamp);
done:
#ifdef HALO_N3DS_ARCHIVE_VERIFY
    texture_scratch_free(archive_reference,archive_reference_linear);
#endif
    n3ds_gpu_texture_abandon(reserved);texture_scratch_free(compressed,compressed_linear);
    return resource;
}

static void *cached_bitmap_slow(struct bitmap_data *bitmap)
{
    for(unsigned int i=0;i<NATIVE_TEXTURE_SLOTS;++i) if(entries[i].bitmap==bitmap) {
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
        if(!n3ds_presentation_midpoint_active())
#endif
            entries[i].last_used=texture_frame;
        return entries[i].resource;
    }
    return NULL;
}

static void *load_bitmap_now(struct bitmap_data *bitmap,enum texture_lane lane,int allow_eviction)
{
    if(!initialized || !opened) return NULL;
    void *resource=cached_bitmap_resource(bitmap);
    if(resource) return resource;
    if(!owns_bitmap(bitmap)) return NULL;
    resource=cached_bitmap_slow(bitmap);
    if(resource) return resource;
    if(lane!=TEXTURE_PREFETCH && texture_prefetch_finish_for(bitmap,lane)) {
        resource=cached_bitmap_resource(bitmap);
        if(resource) return resource;
    }
    TEXTURE_PROFILE(++texture_load_profile(lane)->attempts);
    if(bitmap->type==2) {
        if(allow_eviction) resource=load_cube_now(bitmap,lane);
    } else resource=load_bitmap_2d(bitmap,lane,allow_eviction);
    TEXTURE_PROFILE(texture_load_profile(lane)->loads+=resource!=NULL);
    TEXTURE_PROFILE(texture_load_profile(lane)->failures+=resource==NULL);
    return resource;
}

#include "engine_texture_prefetch.inl"

void *_texture_cache_bitmap_get_hardware_format(struct bitmap_data *bitmap, boolean block, boolean load)
{
    unsigned int i;
    void *resource;
    assert(load || !block);
    /* Warm map textures already carry an owner-checked native slot. Resolve
     * that O(1) hit before searching the unrelated64-slot mutable/font atlas
     * table. Dynamic bitmaps still work while the map cache is closed. */
    resource=initialized && opened?cached_bitmap_resource(bitmap):NULL;
    if(resource) {TEXTURE_PROFILE(texture_profile.demand_hits+=block);return resource;}
    resource=n3ds_dynamic_bitmap_resource(bitmap);
    if(resource) return resource;
    if (!initialized || !opened) return NULL;
    if(!owns_bitmap(bitmap)) return NULL;
    resource = cached_bitmap_slow(bitmap);
    if (resource || !load) return resource;
    /* The regular frame owns demand loading and changes to the pending queue. */
    if (n3ds_presentation_reject_texture()) {
        texture_prefetch_note_miss(bitmap);
        return NULL;
    }
    for (i = 0; i < pending_limit; ++i) if (pending[i] == bitmap) {
        if (!block) {pending_seen[i]=texture_frame;return NULL;}
        texture_pending_remove(i);
        break;
    }
    if(block) return load_bitmap_now(bitmap,TEXTURE_DEMAND,1);
    for (i = 0; i < NATIVE_TEXTURE_SLOTS; ++i) if (!pending[i]) {
        pending[i] = bitmap;pending_seen[i]=texture_frame;pending_order[i]=++request_sequence;
        pending_synchronous[i]=0;
        pending_limit=MAX(pending_limit,i+1);
        texture_prefetch_prioritize_pending(bitmap,i);
        break;
    }
    return NULL;
}

void texture_cache_bitmap_delete(struct bitmap_data *bitmap)
{
    unsigned int i;
    if(!bitmap) return;
    texture_prefetch_forget(bitmap);
    for(i=0;i<pending_limit;++i) if(pending[i]==bitmap) {texture_pending_remove(i);break;}
    for(i=0;i<NATIVE_TEXTURE_SLOTS;++i) if(entries[i].bitmap==bitmap) {
        assert(texture_fence());
        n3ds_gpu_texture_destroy(entries[i].resource);
        texture_bytes-=entries[i].bytes; --texture_count;
        memset(&entries[i],0,sizeof(entries[i]));
        bitmap->hardware_format=NULL;
        break;
    }
    if(bitmap->flags&0x80) {
        bitmap->flags&=~0x80; bitmap->cache_block_index=NONE; bitmap->base_address=NULL;
    }
}

int n3ds_engine_bitmap_bind_plasma(unsigned int stage,const struct bitmap_data *bitmap,unsigned int flags)
{
    if(stage!=2 || !bitmap || (flags&~15u)) return 0;
    void *source=texture_cache_bitmap_load((struct bitmap_data *)bitmap);if(!source) return 0;
    unsigned int total=texture_total_budget();
    if(texture_bytes>total) return 0;
    void *variant=n3ds_gpu_texture_plasma(source,total-texture_bytes);
    return variant && n3ds_gpu_texture_bind_sampler(stage,variant,flags);
}
