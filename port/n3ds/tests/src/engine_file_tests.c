#include "cseries.h"
#include "tag_files/files.h"
#include "engine_files.h"
void n3ds_log(const char *message);

int halo_engine_file_tests(void)
{
    struct file_reference root,sub,file,copy,entry,map;
    struct file_last_modification_date date,again;
    struct native_file_info info;
    unsigned long size,signature;
    unsigned int handles[64],old_handle;
    char path[256],native[512],data[24],name[256];
    long i,count,dirs;
    const char payload[]="Halo native IO";
#define CHECK(expr) do { if(!(expr)) { n3ds_log("FILE FAIL: " #expr); return 1; } } while(0)
    CHECK(!n3ds_files_open_count() && !n3ds_files_directory_count());
    CHECK(n3ds_file_resolve("D:\\maps\\a10.map",native,sizeof(native),0));
    CHECK(!strcmp(native,"sdmc:/halo-source/a10.map"));
    CHECK(n3ds_file_resolve("z:\\saved\\profile.sav",native,sizeof(native),1));
    CHECK(!strcmp(native,"sdmc:/halo-source/state/cache/saved/profile.sav"));
    CHECK(!n3ds_file_resolve("z:\\..\\ui.map",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("z:\\saved/../../ui.map",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("sdmc:/halo-source/ui.map",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("c:\\x",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("d:\\x",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("z:\\x ",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("z:\\x:y",native,sizeof(native),1));
    CHECK(!n3ds_file_resolve("z:\\x",native,8,1));
    file_reference_create_from_path(&map,"d:\\maps\\ui.map",FALSE);
    CHECK(file_exists(&map) && file_read_only(&map) && file_get_size(&map,&size) && size==33582080);
    CHECK(file_open(&map,1) && file_read(&map,sizeof(signature),&signature) && signature==0x68656164);
    CHECK(file_close(&map) && !file_open(&map,2));
    CHECK(!n3ds_file_create("d:\\__native_io_must_not_create__.tmp",0));
    n3ds_log("PASS: native SD volume confinement and original file API reads actual ui map header");

    file_reference_create_from_path(&root,"z:\\native-io-regression",TRUE);
    CHECK(!file_exists(&root) && file_create(&root));
    file_reference_create_from_path(&sub,"z:\\native-io-regression\\nested",TRUE);
    CHECK(file_create(&sub));
    file_reference_create_from_path(&file,"z:\\native-io-regression\\test.bin",FALSE);
    CHECK(!file_open(&file,3)); /* OPEN_EXISTING must never create. */
    size=0x12345678; CHECK(!file_get_size(&file,&size) && size==0x12345678);
    CHECK(file_create(&file) && file_open(&file,3));
    CHECK(!file_open(&file,3)); /* Opening an already open reference cannot leak it. */
    CHECK(file_write(&file,sizeof(payload),payload) && file_get_position(&file)==sizeof(payload));
    CHECK(file_get_eof(&file)==sizeof(payload));
    memset(data,0xcc,sizeof(data));
    CHECK(file_read_from_position(&file,0,sizeof(payload),data) && !memcmp(data,payload,sizeof(payload)));
    CHECK((unsigned char)data[sizeof(payload)]==0xcc);
    CHECK(!file_read(&file,1,data)); /* EOF is failure, not a full successful read. */
    CHECK(!file_set_position(&file,0x80000000UL));
    CHECK(file_set_eof(&file,5) && file_get_eof(&file)==5 && file_get_position(&file)==5);
    CHECK(file_set_eof(&file,20) && file_get_eof(&file)==20 && file_get_position(&file)==20);
    CHECK(file_read_from_position(&file,0,5,data) && !memcmp(data,payload,5));
    file_reference_create_from_path(&copy,"Z:\\NATIVE-IO-REGRESSION\\TEST.BIN",FALSE);
    CHECK(!file_open(&copy,1)); /* Case-insensitive exclusive share contract. */
    CHECK(!file_delete(&file) && !file_rename(&file,"renamed.bin"));
    CHECK(file_close(&file));
    memset(&copy,0xff,sizeof(copy)); file_reference_copy(&copy,&file);
    memcpy(&old_handle,copy.data+264,sizeof(old_handle)); CHECK(!n3ds_file_valid(old_handle));
    CHECK(file_open(&copy,6) && file_get_position(&copy)==20);
    CHECK(!file_read(&copy,1,data)); /* Write-only handle. */
    CHECK(file_write(&copy,1,"Z") && file_get_eof(&copy)==21);
    CHECK(file_write_to_position(&copy,0,1,"X") && file_get_eof(&copy)==21);
    CHECK(file_close(&copy) && file_open(&copy,1));
    CHECK(!file_write(&copy,1,"Q") && !file_set_eof(&copy,0));
    CHECK(file_read(&copy,1,data) && data[0]=='X' && file_close(&copy));
    CHECK(file_get_last_modification_date(&file,&date));
    CHECK(file_get_last_modification_date(&file,&again) && !file_compare_last_modification_dates(&date,&again));
    CHECK(file_rename(&file,"renamed.bin"));
    CHECK(!file_exists(&copy) && file_exists(&file));
    file_reference_get_name(&file,FLAG(_name_filename_bit)|FLAG(_name_extension_bit),name);
    CHECK(!strcmp(name,"renamed.bin"));
    file_reference_create_from_path(&entry,"z:\\native-io-regression\\nested\\child.bin",FALSE);
    CHECK(file_create(&entry));
    CHECK(!file_rename(&entry,"child.bin")); /* MoveFile must not overwrite. */
    find_files_start(0,&root); count=0;
    while(find_files_next(&copy,&again)) { ++count; CHECK(file_exists(&copy)); }
    CHECK(count==1 && !n3ds_files_directory_count());
    find_files_start(1,&root); count=0;
    while(find_files_next(&copy,NULL)) { ++count; CHECK(file_exists(&copy)); }
    CHECK(count==2 && !n3ds_files_directory_count());
    find_files_start(3,&root); dirs=0;
    while(find_files_next(&copy,NULL)) { ++dirs; CHECK(!(file_reference_get_info(&copy)->flags&1)); }
    CHECK(dirs==1 && !n3ds_files_directory_count());
    find_files_start(1,&root); CHECK(find_files_next(&copy,NULL));
    find_files_start(0,&sub); CHECK(find_files_next(&copy,NULL) && !find_files_next(&copy,NULL));
    CHECK(!n3ds_files_directory_count());
    CHECK(file_delete(&entry) && file_delete(&sub) && file_delete(&file));
    n3ds_log("PASS: original SD file references, exclusive opens, seek/read/write/append/resize/rename and recursive enumeration");

    for(i=0;i<64;++i) {
        snprintf(path,sizeof(path),"z:\\native-io-regression\\slot%02ld.bin",i);
        CHECK(n3ds_file_create(path,0)); handles[i]=n3ds_file_open(path,3); CHECK(handles[i]);
    }
    CHECK(n3ds_files_open_count()==64 && !n3ds_file_open("d:\\ui.map",1));
    old_handle=handles[0]; CHECK(n3ds_file_close(old_handle));
    handles[0]=n3ds_file_open("z:\\native-io-regression\\slot00.bin",3);
    CHECK(handles[0] && handles[0]!=old_handle);
    CHECK(!n3ds_file_close(old_handle) && !n3ds_file_read(old_handle,data,1) && !n3ds_file_write(old_handle,"!",1));
    CHECK(n3ds_file_write(handles[0],"!",1));
    for(i=0;i<64;++i) {
        CHECK(n3ds_file_close(handles[i]));
        snprintf(path,sizeof(path),"z:\\native-io-regression\\slot%02ld.bin",i);
        CHECK(n3ds_file_delete(path,0));
    }
    CHECK(!n3ds_files_open_count() && !n3ds_files_directory_count());
    CHECK(file_delete(&root) && !file_exists(&root));
    CHECK(n3ds_file_stat("d:\\ui.map",&info) && info.size==33582080 && info.readonly);
    n3ds_log("PASS: 64-slot native file handle budget, stale-token rejection and complete test-file cleanup");
    return 0;
#undef CHECK
}
