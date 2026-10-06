#include "cseries.h"
#include "engine_diagnostics.h"
#include "engine_files.h"
#include "engine_textures.h"
#include "cache/texture_cache.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/tiff_file.h"
#include "tag_files/files.h"
extern byte debug_texture_cache[];
extern byte texture_cache_debug_options[];
void n3ds_log(const char *message);
unsigned long n3ds_engine_available_memory(void);
#define CHECK(e) do { if(!(e)) { n3ds_log("DIAGNOSTICS FAIL: " #e); return 1; } } while(0)
int halo_engine_diagnostics_tests(void)
{
    struct native_memory_snapshot before,during,after;
    struct file_reference file;
    struct bitmap_data bitmap={0};
    struct native_file_info info;
    const char *path="z:\\native-diagnostics-export.tif";
    unsigned int handle;
    byte bytes[4]={0xde,0xad,0xbe,0xef},readback[4];
    byte *allocation;
    debug_dump_memory(); debug_dump_memory_for_file(NULL);
    debug_dump_memory_for_file("source/game/game.c"); debug_dump_memory_by_file();
    n3ds_engine_memory_snapshot(&before);
    allocation=malloc(65536); CHECK(allocation);
    allocation[0]=0x42; allocation[65535]=0xa5;
    n3ds_engine_memory_snapshot(&during);
    CHECK(during.heap_used>=before.heap_used+65536);
    CHECK(allocation[0]==0x42 && allocation[65535]==0xa5);
    free(allocation); n3ds_engine_memory_snapshot(&after);
    CHECK(after.heap_used==before.heap_used && after.linear_free==before.linear_free);
    CHECK(after.application_free==before.application_free);
    CHECK(after.application_free_valid==before.application_free_valid);
    CHECK(after.application_free_valid || after.application_free==0);
    CHECK(n3ds_engine_available_memory()>0 && n3ds_engine_available_memory()<256*1024*1024);
    CHECK(!n3ds_file_stat(path,&info));
    CHECK(file_reference_create_from_path(&file,path,FALSE));
    bitmap.width=8; bitmap.height=8; bitmap.format=11;
    CHECK(tiff_export(&file,&bitmap)!=NULL && !n3ds_file_stat(path,&info));
    CHECK(n3ds_file_create(path,0)); handle=n3ds_file_open(path,3); CHECK(handle);
    CHECK(n3ds_file_write(handle,bytes,sizeof(bytes)) && n3ds_file_close(handle));
    CHECK(tiff_export(&file,&bitmap)!=NULL);
    handle=n3ds_file_open(path,1); CHECK(handle && n3ds_file_size(handle)==sizeof(bytes));
    CHECK(n3ds_file_read(handle,readback,sizeof(readback)) && !memcmp(bytes,readback,sizeof(bytes)));
    CHECK(n3ds_file_close(handle) && n3ds_file_delete(path,0));
    n3ds_log("PASS: native memory snapshots track allocation/free, source attribution reports unavailable, and unsupported TIFF export neither creates nor truncates files");
    return 0;
}
int halo_engine_texture_diagnostics_tests(void)
{
    unsigned int count=n3ds_engine_texture_count(),bytes=n3ds_engine_texture_bytes();
    CHECK(count>0 && bytes>0);
    CHECK(!debug_texture_cache[0] && !texture_cache_debug_options[0] && !texture_cache_debug_options[1]);
    texture_cache_debug_render();
    debug_texture_cache[0]=TRUE; texture_cache_debug_options[0]=TRUE; texture_cache_debug_options[1]=TRUE;
    texture_cache_debug_render();
    debug_texture_cache[0]=FALSE; texture_cache_debug_options[0]=FALSE; texture_cache_debug_options[1]=FALSE;
    CHECK(n3ds_engine_texture_count()==count && n3ds_engine_texture_bytes()==bytes);
    n3ds_log("PASS: native texture diagnostics list resident entries without changing cache ownership; graphical diagnostics explicitly unavailable");
    return 0;
}
