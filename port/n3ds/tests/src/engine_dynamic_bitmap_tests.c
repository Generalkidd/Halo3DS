#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "rasterizer/rasterizer_text.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "cache/texture_cache.h"
#include "engine_bitmaps.h"
#include "engine_textures.h"

boolean rasterizer_set_texture_bitmap_data(short stage,const struct bitmap_data *bitmap);
void rasterizer_text_cache_dispose(void);
int n3ds_gpu_texture_mutation_test(void (*mutate)(int),const unsigned int colors[2]);
void n3ds_log(const char *message);
static struct bitmap_data *mutation_bitmap;
static void mutate_texture(int phase)
{
    if(!phase) {
        for(unsigned int i=0;i<(unsigned int)mutation_bitmap->width*mutation_bitmap->height;++i)
            ((unsigned short *)mutation_bitmap->base_address)[i]=0xfa53;
        rasterizer_bitmap_changed(mutation_bitmap);
    } else { bitmap_delete(mutation_bitmap); mutation_bitmap=NULL; }
}
int halo_engine_dynamic_bitmap_tests(void)
{
    /* Independent packed pixels / expected RGBA. Covers every supported format
     * and alpha semantics, including Xbox AL8's shared intensity/alpha. */
    static const struct { short format; unsigned int pixel,rgba; } cases[]={
        {0,0x45,0xffffff45},{1,0x72,0x727272ff},{2,0x3a,0x3a3a3a3a},
        {3,0x9371,0x71717193},{6,0xf81f,0xff00ffff},
        {8,0x03e0,0x00ff0000},{8,0xfc00,0xff0000ff},
        {9,0x8421,0x44221188},{10,0x1267abcd,0x67abcdff},{11,0x1267abcd,0x67abcd12}};
    struct bitmap_data *bitmap=NULL,*held[64]={0};
    unsigned int i,bytes;
#define CHECK(x) do { if(!(x)) { n3ds_log("DYNAMIC BITMAP FAIL: " #x); return 1; } } while(0)
    CHECK(!n3ds_dynamic_bitmap_count() && !n3ds_dynamic_bitmap_bytes());
    CHECK(rasterizer_text_cache_initialize());
    bitmap=hardware_character_cache_get_bitmap();
    CHECK(bitmap && bitmap->width==128 && bitmap->height==128 && bitmap->format==9);
    CHECK(n3ds_dynamic_bitmap_resource(bitmap));
    rasterizer_text_cache_dispose(); bitmap=NULL;
    CHECK(n3ds_dynamic_bitmaps_collect() && !n3ds_dynamic_bitmap_bytes());
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        unsigned int colors[4]={cases[i].rgba,cases[i].rgba,cases[i].rgba,cases[i].rgba};
        short format=cases[i].format;
        unsigned int stride=format<=2 ? 1 : format>=10 ? 4 : 2;
        bitmap=bitmap_2d_new(8,8,0,format); CHECK(bitmap && bitmap->base_address);
        for(unsigned int pixel=0;pixel<64;++pixel)
            for(unsigned int byte_index=0;byte_index<stride;++byte_index)
                ((byte *)bitmap->base_address)[pixel*stride+byte_index]=cases[i].pixel>>(8*byte_index);
        CHECK(rasterizer_bitmap_new(bitmap));
        rasterizer_bitmap_changed(bitmap);
        CHECK(rasterizer_set_texture_bitmap_data(i%3,bitmap));
        CHECK(!n3ds_gpu_texture_sampling_test(i%3,colors));
        bitmap_delete(bitmap); bitmap=NULL;
        CHECK(n3ds_dynamic_bitmaps_collect() && !n3ds_dynamic_bitmap_bytes());
    }
    n3ds_log("PASS: original font atlas lifecycle and all 9 native mutable bitmap formats sampled on GPU");
    for(unsigned int side=8;side<=128;side*=16) {
    mutation_bitmap=bitmap_2d_new(side,side,0,9); CHECK(mutation_bitmap);
    for(i=0;i<side*side;++i) ((unsigned short *)mutation_bitmap->base_address)[i]=0x8421;
    CHECK(rasterizer_bitmap_new(mutation_bitmap));
    rasterizer_bitmap_changed(mutation_bitmap);
    CHECK(n3ds_dynamic_bitmaps_collect());
    CHECK(rasterizer_set_texture_bitmap_data(0,mutation_bitmap));
    {
        static const unsigned int colors[2]={0x44221188,0xaa5533ff};
        CHECK(!n3ds_gpu_texture_mutation_test(mutate_texture,colors));
    }
    CHECK(!mutation_bitmap && !n3ds_dynamic_bitmap_count());
    CHECK(n3ds_dynamic_bitmaps_collect() && !n3ds_dynamic_bitmap_bytes());
    }
    n3ds_log("PASS: queued GPU draws retain old and updated 8x8/128x128 atlas pixels after CPU bitmap deletion");
    /* Six live font images exceed the four-image reserve. Fallback storage
     * must stay distinct, and collecting every owner must release each lease. */
    for(unsigned int round=0;round<2;++round) {
        for(i=0;i<6;++i) {
            held[i]=bitmap_2d_new(128,128,0,9);CHECK(held[i] && rasterizer_bitmap_new(held[i]));
            for(unsigned int other=0;other<i;++other)CHECK(held[i]->hardware_format!=held[other]->hardware_format);
        }
        for(i=0;i<6;++i){bitmap_delete(held[i]);held[i]=NULL;}
        CHECK(n3ds_dynamic_bitmaps_collect() && !n3ds_dynamic_bitmap_bytes() && !n3ds_dynamic_bitmap_count());
    }
    n3ds_log("PASS: six simultaneous font images retain distinct owners across reserve overflow and repeated reclamation");
    bitmap=bitmap_2d_new(8,8,0,9); CHECK(bitmap);
    CHECK(rasterizer_bitmap_new(bitmap));
    {
        struct bitmap_data foreign=*bitmap;
        void *saved=bitmap->hardware_format;
        CHECK(!n3ds_dynamic_bitmap_resource(&foreign));
        CHECK(!n3ds_dynamic_bitmap_update(&foreign));
        bitmap->width=16; CHECK(!n3ds_dynamic_bitmap_update(bitmap)); bitmap->width=8;
        CHECK(bitmap->hardware_format==saved);
        bitmap->mipmap_count=1; CHECK(!n3ds_dynamic_bitmap_update(bitmap)); bitmap->mipmap_count=0;
    }
    bitmap_delete(bitmap); bitmap=NULL; CHECK(n3ds_dynamic_bitmaps_collect());
    for(i=0;i<64;++i) {
        held[i]=bitmap_2d_new(128,128,0,11); CHECK(held[i]);
        CHECK(rasterizer_bitmap_new(held[i]));
    }
    bytes=n3ds_dynamic_bitmap_bytes(); CHECK(bytes==4*1024*1024);
    bitmap=bitmap_2d_new(8,8,0,9); CHECK(bitmap);
    CHECK(!rasterizer_bitmap_new(bitmap) && !bitmap->hardware_format);
    CHECK(!n3ds_dynamic_bitmap_update(held[0]));
    CHECK(!rasterizer_text_cache_initialize());
    CHECK(!hardware_character_cache_get_bitmap());
    CHECK(n3ds_dynamic_bitmap_bytes()==bytes && n3ds_dynamic_bitmap_count()==64);
    bitmap_delete(bitmap); bitmap=NULL;
    for(i=0;i<64;++i) bitmap_delete(held[i]);
    CHECK(n3ds_dynamic_bitmaps_collect() && !n3ds_dynamic_bitmap_bytes() && !n3ds_dynamic_bitmap_count());
    n3ds_log("PASS: mutable bitmap ownership, metadata rejection, 4 MiB budget and complete reclamation");
    return 0;
}
