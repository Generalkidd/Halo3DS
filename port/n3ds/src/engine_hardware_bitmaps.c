/* Mutable CPU bitmaps (including Halo's original font atlas). Each upload is
 * immutable to queued draws. Old versions survive until a GPU fence succeeds. */
#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "engine_bitmaps.h"
#include "engine_textures.h"
#include "engine_input.h"
#include "engine_diagnostics.h"

enum { BITMAP_SLOTS=64, RETIRED_SLOTS=256, BITMAP_BUDGET=4*1024*1024 };
struct native_bitmap { struct bitmap_data *owner; void *resource; unsigned int bytes; short width,height,format; };
struct retired_bitmap { void *resource; unsigned int bytes; };
static struct native_bitmap bitmaps[BITMAP_SLOTS];
static struct retired_bitmap retired[RETIRED_SLOTS];
static unsigned int live_count,retired_count,total_bytes;
void texture_cache_bitmap_delete(struct bitmap_data *bitmap);
void n3ds_log(const char *);
static int update_failed(const char *reason,const struct native_bitmap *entry)
{
    struct native_memory_snapshot memory;char message[256];
    n3ds_engine_memory_snapshot(&memory);
    snprintf(message,sizeof(message),"DYNAMIC BITMAP UPDATE: reason=%s bytes=%u live=%u retired=%u total=%u heap_used=%u heap_free=%u linear_free=%u",
        reason,entry?entry->bytes:0,live_count,retired_count,total_bytes,memory.heap_used,memory.heap_free,memory.linear_free);
    n3ds_log(message);return 0;
}

unsigned int n3ds_dynamic_bitmap_count(void) { return live_count; }
unsigned int n3ds_dynamic_bitmap_bytes(void) { return total_bytes; }
static struct native_bitmap *find_bitmap(const struct bitmap_data *bitmap)
{
    for(unsigned int i=0;i<BITMAP_SLOTS;++i) if(bitmaps[i].owner==bitmap) return &bitmaps[i];
    return NULL;
}
void *n3ds_dynamic_bitmap_resource(const struct bitmap_data *bitmap)
{
    struct native_bitmap *entry=bitmap ? find_bitmap(bitmap) : NULL;
    return entry && bitmap->hardware_format==entry->resource ? entry->resource : NULL;
}
int n3ds_dynamic_bitmaps_collect(void)
{
    if(!retired_count) return 1;
    if(!n3ds_gpu_texture_barrier()) return 0;
    for(unsigned int i=0;i<retired_count;++i) {
        n3ds_gpu_texture_destroy(retired[i].resource);
        total_bytes-=retired[i].bytes;
    }
    memset(retired,0,sizeof(retired)); retired_count=0;
    return 1;
}
static int valid_bitmap(const struct bitmap_data *bitmap)
{
    if(!bitmap || bitmap->signature!='bitm' || bitmap->type || bitmap->depth!=1 ||
       !(bitmap->flags&0x40) || (bitmap->flags&(0x80|8|4|2)) || bitmap->mipmap_count ||
       bitmap->width<8 || bitmap->height<8 || bitmap->width>128 || bitmap->height>128 ||
       (bitmap->width&(bitmap->width-1)) || (bitmap->height&(bitmap->height-1))) return 0;
    return bitmap->format==0 || bitmap->format==1 || bitmap->format==2 || bitmap->format==3 ||
           bitmap->format==6 || bitmap->format==8 || bitmap->format==9 || bitmap->format==10 || bitmap->format==11;
}
boolean rasterizer_bitmap_new(struct bitmap_data *bitmap)
{
    struct native_bitmap *entry=NULL; unsigned int *pixels,bytes; void *resource;
    if(!valid_bitmap(bitmap) || !bitmap->base_address || bitmap->hardware_format || find_bitmap(bitmap)) return FALSE;
    n3ds_dynamic_bitmaps_collect(); /* May be inside a frame; retained versions stay live. */
    int packed=bitmap->format==9 && n3ds_input_platform_original_model();
    bytes=(unsigned int)bitmap->width*bitmap->height*(packed?2:4);
    if(bytes>BITMAP_BUDGET-total_bytes) return FALSE;
    for(unsigned int i=0;i<BITMAP_SLOTS;++i) if(!bitmaps[i].owner) { entry=&bitmaps[i]; break; }
    if(!entry) return FALSE;
    pixels=(calloc)(1,bytes); if(!pixels) return FALSE;
    resource=packed?n3ds_gpu_texture_create_rgba4(pixels,bitmap->width,bitmap->height):
        n3ds_gpu_texture_create(pixels,bitmap->width,bitmap->height); free(pixels);
    if(!resource) return FALSE;
    *entry=(struct native_bitmap){bitmap,resource,bytes,bitmap->width,bitmap->height,bitmap->format};
    total_bytes+=bytes; ++live_count; bitmap->hardware_format=resource;
    return TRUE;
}
unsigned int n3ds_bitmap_decode_pixel(const byte *p,int format)
{
    unsigned int a=255,r=255,g=255,b=255,v;
    switch(format) {
    case 0: a=p[0]; break;
    case 1: r=g=b=p[0]; break;
    case 2: a=r=g=b=p[0]; break;
    case 3: r=g=b=p[0]; a=p[1]; break;
    case 6:
        v=p[0]|(unsigned int)p[1]<<8;
        r=(v>>11)&31; r=(r<<3)|(r>>2);
        g=(v>>5)&63; g=(g<<2)|(g>>4);
        b=v&31; b=(b<<3)|(b>>2); break;
    case 8:
        v=p[0]|(unsigned int)p[1]<<8; a=(v&32768)?255:0;
        r=(v>>10)&31; r=(r<<3)|(r>>2);
        g=(v>>5)&31; g=(g<<3)|(g>>2);
        b=v&31; b=(b<<3)|(b>>2); break;
    case 9:
        v=p[0]|(unsigned int)p[1]<<8;
        a=((v>>12)&15)*17; r=((v>>8)&15)*17; g=((v>>4)&15)*17; b=(v&15)*17; break;
    case 10: case 11:
        b=p[0]; g=p[1]; r=p[2]; if(format==11) a=p[3]; break;
    default: abort();
    }
    return r<<24|g<<16|b<<8|a;
}
int n3ds_dynamic_bitmap_update(struct bitmap_data *bitmap)
{
    struct native_bitmap *entry=bitmap ? find_bitmap(bitmap) : NULL;
    unsigned int *pixels,width,height,stride; void *resource,*old;
    if(!entry || !valid_bitmap(bitmap) || !bitmap->base_address ||
       bitmap->hardware_format!=entry->resource || bitmap->width!=entry->width ||
       bitmap->height!=entry->height || bitmap->format!=entry->format) return update_failed("metadata",entry);
    n3ds_dynamic_bitmaps_collect();
    if(retired_count==RETIRED_SLOTS || entry->bytes>BITMAP_BUDGET-total_bytes) return update_failed("retained-budget",entry);
    pixels=(malloc)(entry->bytes); if(!pixels) return update_failed("staging-heap",entry);
    width=entry->width; height=entry->height;
    int packed=entry->bytes==width*height*2;
    stride=entry->format<=2 ? 1 : entry->format>=10 ? 4 : 2;
    for(unsigned int y=0;y<height;++y) for(unsigned int x=0;x<width;++x) {
        static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
        unsigned int ny=height-1-y;
        unsigned int offset=((ny/8)*(width/8)+x/8)*64+spread[x&7]+2*spread[ny&7];
        const byte *source=(const byte *)bitmap->base_address+(y*width+x)*stride;
        if(packed) {
            /* Preserve the font's exact four-bit channels, without expansion. */
            unsigned int v=source[0]|(unsigned int)source[1]<<8;
            ((unsigned short *)pixels)[offset]=(v<<4)|(v>>12);
        } else pixels[offset]=n3ds_bitmap_decode_pixel(source,entry->format);
    }
    resource=packed?n3ds_gpu_texture_create_rgba4(pixels,width,height):
        n3ds_gpu_texture_create(pixels,width,height); free(pixels);
    if(!resource) return update_failed("gpu-texture",entry);
    old=entry->resource;
    retired[retired_count++]=(struct retired_bitmap){old,entry->bytes};
    total_bytes+=entry->bytes; entry->resource=resource; bitmap->hardware_format=resource;
    /* Original text code binds once, then modifies its atlas while drawing. */
    for(unsigned int stage=0;stage<3;++stage)
        if(n3ds_gpu_texture_bound(stage)==old) n3ds_gpu_texture_bind(stage,resource);
    return 1;
}
void rasterizer_bitmap_changed(struct bitmap_data *bitmap)
{
    assert(n3ds_dynamic_bitmap_update(bitmap));
}
void rasterizer_bitmap_delete(struct bitmap_data *bitmap)
{
    struct native_bitmap *entry;
    if(!bitmap) return;
    entry=find_bitmap(bitmap);
    if(!entry) { texture_cache_bitmap_delete(bitmap); return; }
    n3ds_dynamic_bitmaps_collect();
    assert(retired_count<RETIRED_SLOTS);
    for(unsigned int stage=0;stage<3;++stage)
        if(n3ds_gpu_texture_bound(stage)==entry->resource) n3ds_gpu_texture_bind(stage,NULL);
    retired[retired_count++]=(struct retired_bitmap){entry->resource,entry->bytes};
    bitmap->hardware_format=NULL; memset(entry,0,sizeof(*entry)); --live_count;
    /* CPU bitmap storage may now be freed; retired versions hold no owner pointer. */
}
