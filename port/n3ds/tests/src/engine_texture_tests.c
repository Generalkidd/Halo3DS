#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "cache/cache_files.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "engine_textures.h"
#include "engine_texture_fixture.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

void n3ds_log(const char *message);
int halo_engine_texture_tests(void)
{
    struct tag_iterator iterator;
    struct bitmap_data *first = NULL;
    unsigned int loaded = 0, initial_hash = 0, bytes;
    long handle;
    short first_index = NONE;
    char message[160];
    const struct n3ds_cache_view *view = n3ds_engine_cache_view(0);
#define CHECK(expr) do { if (!(expr)) { n3ds_log("NATIVE TEXTURE FAIL: " #expr); goto fail; } } while (0)
    texture_cache_new(); texture_cache_open();
    CHECK(scenario_tags_load("a10") != NONE);
    tag_iterator_new(&iterator, 'bitm');
    while ((handle = tag_iterator_next(&iterator)) != NONE && loaded < 24) {
        struct bitmap_group *group = tag_get('bitm', handle);
        long i;
        for (i = 0; i < group->bitmaps.count && loaded < 24; ++i) {
            struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, i, struct bitmap_data);
            void *resource;
            if (bitmap->type != 0 || bitmap->depth != 1 || bitmap->format < 14 || bitmap->format > 16 ||
                (bitmap->flags & 8) || bitmap->width < 8 || bitmap->height < 8) continue;
            CHECK(!_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, FALSE));
            if (!first) {
                struct bitmap_data foreign = *bitmap;
                long saved_offset = bitmap->pixels_offset;
                CHECK(!_texture_cache_bitmap_get_hardware_format(&foreign, TRUE, TRUE));
                bitmap->pixels_offset = view->header.file_length-1;
                CHECK(!_texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE));
                CHECK(!n3ds_engine_texture_count() && !bitmap->hardware_format);
                bitmap->pixels_offset = saved_offset;
                CHECK(!_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE));
                CHECK(!_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE));
                CHECK(!n3ds_engine_texture_count());
                texture_cache_idle();
                resource = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, FALSE);
            } else resource = texture_cache_bitmap_load(bitmap);
            CHECK(resource && bitmap->hardware_format == resource && !bitmap->base_address);
            CHECK(_texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE) == resource);
            CHECK(n3ds_engine_texture_count() == ++loaded);
            if (!first) { first = bitmap; first_index = i; initial_hash = n3ds_gpu_texture_checksum(resource); }
        }
    }
    CHECK(loaded == 24 && first && initial_hash);
    CHECK(first_index == 0 && !strcmp(tag_get_name(first->tag_index), "sky\\space\\bitmaps\\star mask"));
    for (unsigned int stage = 0; stage < 3; ++stage) {
        CHECK(rasterizer_set_texture_bitmap_data(stage, first));
        CHECK(n3ds_gpu_texture_bound(stage) == first->hardware_format);
        CHECK(!n3ds_gpu_texture_sampling_test(stage, a10_texture_corners));
    }
    CHECK(!n3ds_gpu_texture_bind(3, first->hardware_format));
    n3ds_log("PASS: original rasterizer binding samples mission texture through all three PICA units");
    bytes = n3ds_engine_texture_bytes();
    CHECK(bytes > 0 && bytes <= 2*1024*1024);
    /* Drive the real mission working set to the budget, then prove a failed
     * allocation preserves every previously published resource. */
    {
        boolean exhausted = FALSE;
        tag_iterator_new(&iterator, 'bitm');
        while (!exhausted && (handle = tag_iterator_next(&iterator)) != NONE) {
            struct bitmap_group *group = tag_get('bitm', handle);
            long i;
            for (i = 0; i < group->bitmaps.count; ++i) {
                struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, i, struct bitmap_data);
                unsigned int before_count = n3ds_engine_texture_count(), before_bytes = n3ds_engine_texture_bytes();
                if (bitmap->hardware_format || bitmap->type != 0 || bitmap->depth != 1 ||
                    bitmap->format < 14 || bitmap->format > 16 || (bitmap->flags & 8) ||
                    bitmap->width < 8 || bitmap->height < 8) continue;
                if (!texture_cache_bitmap_load(bitmap)) {
                    CHECK(before_bytes > 2*1024*1024-128*128*4);
                    CHECK(!bitmap->hardware_format && before_count == n3ds_engine_texture_count());
                    CHECK(before_bytes == n3ds_engine_texture_bytes());
                    exhausted = TRUE;
                    break;
                }
                CHECK(n3ds_engine_texture_bytes() <= 2*1024*1024);
            }
        }
        CHECK(exhausted && n3ds_gpu_texture_checksum(first->hardware_format) == initial_hash);
        snprintf(message, sizeof(message), "PASS: native texture budget refuses excess: %u resources, %u bytes; live resources preserved",
                 n3ds_engine_texture_count(), n3ds_engine_texture_bytes());
        n3ds_log(message);
    }
    texture_cache_flush();
    CHECK(!first->hardware_format && first->cache_block_index == NONE);
    CHECK(!n3ds_engine_texture_count() && !n3ds_engine_texture_bytes());
    CHECK(!n3ds_gpu_texture_bound(0) && !n3ds_gpu_texture_bound(1) && !n3ds_gpu_texture_bound(2));
    CHECK(rasterizer_set_texture_direct_non_blocking(0, first->tag_index, first_index));
    CHECK(!n3ds_gpu_texture_bound(0));
    texture_cache_idle();
    CHECK(!rasterizer_set_texture_direct_non_blocking(0, first->tag_index, first_index));
    CHECK(n3ds_gpu_texture_bound(0));
    CHECK(!rasterizer_set_texture_non_blocking(1, first->type, 0, first->tag_index, first_index));
    CHECK(n3ds_gpu_texture_bound(1) == first->hardware_format);
    {
        point2d *dimensions = rasterizer_set_texture(2, first->type, 0, first->tag_index, first_index);
        CHECK(dimensions && dimensions->x == first->width && dimensions->y == first->height);
        CHECK(n3ds_gpu_texture_bound(2) == first->hardware_format);
    }
    CHECK(rasterizer_set_texture_direct(0, first->tag_index, first_index));
    CHECK(!rasterizer_set_texture_bitmap_data(0, NULL));
    CHECK(n3ds_gpu_texture_bound(0) == first->hardware_format);
    {
        void *saved[3];for(unsigned int i=0;i<3;++i) saved[i]=n3ds_gpu_texture_bound(i);
        CHECK(n3ds_engine_bitmap_resource(first->tag_index,first_index)==first->hardware_format);
        CHECK(!n3ds_engine_bitmap_resource(NONE,0));
        CHECK(!n3ds_engine_bitmap_resource(first->tag_index,-1));
        CHECK(!n3ds_engine_bitmap_resource(first->tag_index,32768));
        for(unsigned int i=0;i<3;++i) CHECK(n3ds_gpu_texture_bound(i)==saved[i]);
        n3ds_log("PASS: native bitmap resource lookup preserves all sampler bindings and rejects invalid selection");
    }
    CHECK(n3ds_engine_texture_count() == 1);
    CHECK(texture_cache_bitmap_load(first));
    CHECK(n3ds_gpu_texture_checksum(first->hardware_format) == initial_hash);
    snprintf(message, sizeof(message), "PASS: native PICA texture cache: %u mission bitmaps, %u bytes; queued load, reuse and reload", loaded, bytes);
    n3ds_log(message);
    scenario_tags_unload();
    CHECK(!n3ds_engine_texture_count() && !n3ds_engine_texture_bytes());
    CHECK(!n3ds_engine_cache_read(2048, 1, &bytes));
    CHECK(!n3ds_gpu_texture_bound(0) && !n3ds_gpu_texture_bound(1) && !n3ds_gpu_texture_bound(2));
    texture_cache_delete();
    n3ds_log("PASS: map unload releases native texture resources before freeing tags");
    return 0;
fail:
    scenario_tags_unload(); texture_cache_delete(); return 1;
#undef CHECK
}
