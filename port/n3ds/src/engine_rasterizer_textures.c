/* Native owners of the original rasterizer's bitmap binding entrypoints.
 * Nonblocking APIs preserve Halo's TRUE-means-pending convention. */
#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_runtime.h"
#include "cache/texture_cache.h"
#include "game/game_globals.h"
#include "rasterizer/common/rasterizer_common.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "engine_textures.h"
#include "tag_files/tag_groups.h"

#include "rasterizer/rasterizer_debug_options.h"
_Static_assert(offsetof(struct rasterizer_debug_options_definition, bump_mapping) == 0x29, "Original bump option offset");
static point2d bitmap_dimensions;

static struct bitmap_data *select_bitmap(long group_index, short index)
{
    struct bitmap_group *group;
    if (group_index == NONE || index < 0) return NULL;
    group = bitmap_group_get(group_index);
    if (!group || group->bitmaps.count <= 0) return NULL;
    /* The original helper repeats bitmap_group_get/tag_get. Keep its checked
     * block access, using the group we have already resolved above. */
    return TAG_BLOCK_GET_ELEMENT(&group->bitmaps,index % group->bitmaps.count,struct bitmap_data);
}
static struct bitmap_data *select_with_default(short type, short usage, long group_index, short index)
{
    struct bitmap_data *bitmap = NULL;
    assert(type >= 0 && type < 3 && usage >= 0 && usage < 4);
    if (rasterizer_debug_options.bump_mapping || usage != 3)
        bitmap = select_bitmap(group_index, index);
    if (bitmap && bitmap->type != type) bitmap = NULL;
    if (!bitmap && global_rasterizer_data && global_rasterizer_data->default_textures[type].index != NONE)
        bitmap = bitmap_group_try_and_get_bitmap(global_rasterizer_data->default_textures[type].index, usage);
    return bitmap;
}
boolean rasterizer_set_texture_bitmap_data(short stage, const struct bitmap_data *bitmap)
{
    void *resource;
    assert(stage >= 0 && stage < 3); /* Native materials have three PICA units. */
    if (!bitmap) return FALSE;
    /* The tag data remains mutable; const describes callers' image metadata.
     * Cache bookkeeping is owned by the native map's bitmap resource cache. */
    resource = texture_cache_bitmap_load((struct bitmap_data *)bitmap);
    return resource && n3ds_gpu_texture_bind(stage, resource);
}
void *n3ds_engine_bitmap_resource(int group_index,int index)
{
    if(index<0 || index>32767) return NULL;
    struct bitmap_data *bitmap=select_bitmap(group_index,(short)index);
    return bitmap?texture_cache_bitmap_load(bitmap):NULL;
}
const struct bitmap_data *n3ds_engine_bitmap_metadata(int group_index,int index)
{
    return index<0 || index>32767?NULL:select_bitmap(group_index,(short)index);
}
int n3ds_engine_bitmap_bind_sampler(unsigned int stage,const struct bitmap_data *bitmap,unsigned int flags)
{
    if(!bitmap || stage>=3 || (flags&~31u) || (stage && (flags&NATIVE_SAMPLER_PROJECTIVE))) return 0;
    void *resource=texture_cache_bitmap_load((struct bitmap_data *)bitmap);
    return resource && n3ds_gpu_texture_bind_sampler(stage,resource,flags);
}
int n3ds_engine_bitmap_dimensions(int group_index,int index,int size[2])
{
    if(!size || index<0 || index>32767) return 0;
    struct bitmap_data *bitmap=select_bitmap(group_index,(short)index);
    if(!bitmap || bitmap->type || bitmap->width<=0 || bitmap->height<=0) return 0;
    size[0]=bitmap->width;size[1]=bitmap->height;return 1;
}
boolean rasterizer_set_texture_direct(short stage, long group_index, short index)
{
    return rasterizer_set_texture_bitmap_data(stage, select_bitmap(group_index, index));
}
int n3ds_engine_bitmap_resource_tests(void)
{
    extern void n3ds_log(const char *);
    if(!global_rasterizer_data || global_rasterizer_data->default_textures[0].index==NONE) return 1;
    long tag=global_rasterizer_data->default_textures[0].index;
    struct bitmap_group *group=bitmap_group_get(tag);
    if(!group || group->bitmaps.count<=0) return 1;
    const short selection[8]={0,1,2,3,7,31,255,32767};
    for(unsigned int i=0;i<8;++i)
        if(select_bitmap(tag,selection[i])!=bitmap_group_try_and_get_bitmap(tag,selection[i]%group->bitmaps.count)) return 1;
    if(select_bitmap(NONE,0) || select_bitmap(tag,-1)) return 1;
    for(unsigned int i=0;i<8;++i) {
        struct bitmap_data *bitmap=bitmap_group_try_and_get_bitmap(tag,selection[i]%group->bitmaps.count);
        int size[2]={0};
        if(n3ds_engine_bitmap_metadata(tag,selection[i])!=bitmap ||
           !n3ds_engine_bitmap_dimensions(tag,selection[i],size) || size[0]!=bitmap->width || size[1]!=bitmap->height) return 1;
    }
    if(n3ds_engine_bitmap_metadata(NONE,0) || n3ds_engine_bitmap_metadata(tag,-1) || n3ds_engine_bitmap_metadata(tag,32768)) return 1;
    n3ds_log("PASS: bitmap metadata ownership:8 wrapped selections and invalid indices match the original selector");
    int invalid_size[2]={123,456};
    if(n3ds_engine_bitmap_dimensions(NONE,0,invalid_size) || n3ds_engine_bitmap_dimensions(tag,-1,invalid_size) ||
       n3ds_engine_bitmap_dimensions(tag,32768,invalid_size) || n3ds_engine_bitmap_dimensions(tag,0,NULL) ||
       invalid_size[0]!=123 || invalid_size[1]!=456) return 1;
    n3ds_log("PASS: authored bitmap dimensions:8 wrapped selections; invalid metadata leaves output unchanged");
    n3ds_log("PASS: bitmap selection:8 wrapped indices match original checked helper; invalid selections preserved; one group lookup");
    void *saved[3],*resource=NULL;
    for(unsigned int i=0;i<3;++i) saved[i]=n3ds_gpu_texture_bound(i);
    for(unsigned int tries=0;tries<512 && !resource;++tries) {
        resource=n3ds_engine_bitmap_resource(tag,0);
        if(!resource) texture_cache_idle();
    }
    if(!resource || n3ds_engine_bitmap_resource(tag,0)!=resource ||
       n3ds_engine_bitmap_resource(NONE,0) || n3ds_engine_bitmap_resource(tag,-1) ||
       n3ds_engine_bitmap_resource(tag,32768)) return 1;
    for(unsigned int i=0;i<3;++i) if(n3ds_gpu_texture_bound(i)!=saved[i]) return 1;
    n3ds_log("PASS: native bitmap resource lookup: real map upload/reuse, invalid selection, all sampler bindings preserved");
    return 0;
}
boolean rasterizer_set_texture_direct_non_blocking(short stage, long group_index, short index)
{
    struct bitmap_data *bitmap;
    void *resource;
    assert(stage >= 0 && stage < 3);
    bitmap = select_bitmap(group_index, index);
    if (!bitmap) return TRUE; /* There is no usable binding. */
    resource = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);
    if (!resource) return TRUE;
    return !n3ds_gpu_texture_bind(stage, resource);
}
boolean rasterizer_set_texture_non_blocking(short stage, short type, short usage, long group_index, short index)
{
    struct bitmap_data *bitmap;
    void *resource;
    assert(stage >= 0 && stage < 3);
    bitmap = select_with_default(type, usage, group_index, index);
    if (!bitmap) return TRUE;
    resource = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);
    if (!resource) return TRUE;
    return !n3ds_gpu_texture_bind(stage, resource);
}
point2d *rasterizer_set_texture(short stage, short type, short usage, long group_index, short index)
{
    struct bitmap_data *bitmap = select_with_default(type, usage, group_index, index);
    if (!rasterizer_set_texture_bitmap_data(stage, bitmap)) return NULL;
    bitmap_dimensions.x = bitmap->width; bitmap_dimensions.y = bitmap->height;
    return &bitmap_dimensions;
}
