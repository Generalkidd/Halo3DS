#include "cseries.h"
#include <xtl.h>
#include "tag_files/files.h"
#include "text/unicode.h"
#include "saved games/saved_game_files.h"
#include "bungie_net/common/thread.h"
#include "engine_saves.h"
#include "engine_files.h"
#include "engine_threads.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
static unsigned int count_saves(void)
{
    XGAME_FIND_DATA data; unsigned int count=0;
    HANDLE handle=XFindFirstSaveGame("u:\\",&data);
    if(handle!=INVALID_HANDLE_VALUE) {
        do { ++count; } while(XFindNextSaveGame(handle,&data));
        if(!XFindClose(handle)) return ~0U;
    }
    return count;
}
static unsigned long __stdcall startup_check(void *argument)
{
    short *result=argument;
    *result=saved_game_perform_file_system_checks();
    return 0;
}
int halo_engine_save_tests(void)
{
    const wchar_t *name=L"__native_save_\u00e9_\u03a9__",*second=L"__NATIVE_SAVE_SECOND__";
    char path[260],again[260],child[260];
    struct file_reference file,nested,metadata;
    XGAME_FIND_DATA data; HANDLE handles[8],stale;
    unsigned char original[272],corrupt=0xff;
    ULARGE_INTEGER available,total,free_bytes;
    struct thread_reference *thread; short result=-1;
    unsigned int baseline;
#define CHECK(x) do { if(!(x)) { n3ds_log("SAVE ENGINE FAIL: " #x); return 1; } } while(0)
    CHECK(GetDiskFreeSpaceExA("u:\\",&available,&total,&free_bytes));
    CHECK(total.QuadPart && available.QuadPart<=free_bytes.QuadPart && free_bytes.QuadPart<=total.QuadPart);
    available.QuadPart=0x1122334455667788ULL;
    CHECK(!GetDiskFreeSpaceExA("u:\\..\\",&available,NULL,NULL) && available.QuadPart==0x1122334455667788ULL);
    baseline=count_saves(); CHECK(baseline<90 && !n3ds_save_find_count());
    CHECK(XCreateSaveGame("d:\\",name,CREATE_NEW,0,path,sizeof(path))==ERROR_INVALID_PARAMETER);
    CHECK(XCreateSaveGame("u:\\",L"",CREATE_NEW,0,path,sizeof(path))==ERROR_INVALID_PARAMETER);
    CHECK(XCreateSaveGame("u:\\",name,CREATE_NEW,0,path,17)==ERROR_INVALID_PARAMETER);
    CHECK(XCreateSaveGame("u:\\",name,OPEN_EXISTING,0,path,sizeof(path))==ERROR_FILE_NOT_FOUND);
    CHECK(XCreateSaveGame("u:\\",name,CREATE_NEW,0,path,sizeof(path))==ERROR_SUCCESS);
    CHECK(XCreateSaveGame("U:/",name,CREATE_NEW,0,again,sizeof(again))==ERROR_ALREADY_EXISTS);
    CHECK(XCreateSaveGame("u:\\",name,OPEN_EXISTING,0,again,sizeof(again))==ERROR_SUCCESS && !strcmp(path,again));
    CHECK(XCreateSaveGame("u:\\",second,OPEN_ALWAYS,XSAVEGAME_NOCOPY,again,sizeof(again))==ERROR_SUCCESS);
    CHECK(XCreateSaveGame("u:\\",L"__native_save_second__",OPEN_ALWAYS,0,child,sizeof(child))==ERROR_SUCCESS && !strcmp(child,again));
    CHECK(count_saves()==baseline+2);
    int found=0; HANDLE walk=XFindFirstSaveGame("u:\\",&data);
    CHECK(walk!=INVALID_HANDLE_VALUE);
    do {
        if(!strcmp(data.szSaveGameDirectory,path)) {
            CHECK(!memcmp(data.szSaveGameName,name,(ustrlen(name)+1)*2));
            CHECK(data.wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY); ++found;
            CHECK(!strchr(data.wfd.cFileName,'\\') && !strncmp(data.wfd.cFileName,"n3ds-",5));
        }
    } while(XFindNextSaveGame(walk,&data));
    CHECK(found==1 && XFindClose(walk));
    /* The original filesystem startup check, called from a real native worker. */
    CHECK(create_thread(0,startup_check,&result,&thread));
    long long deadline=n3ds_engine_ticks()+15*n3ds_engine_tick_frequency();
    while(!thread_has_exited(thread)) { CHECK(n3ds_engine_ticks()<deadline); n3ds_engine_sleep_milliseconds(1); }
    dispose_thread(thread); CHECK(result==0 && !n3ds_save_find_count());
    n3ds_log("PASS: original saved-game filesystem startup check runs on native worker with real SD space and persistent UTF-16 save enumeration");
    for(unsigned int i=0;i<8;++i) { handles[i]=XFindFirstSaveGame("u:\\",&data); CHECK(handles[i]!=INVALID_HANDLE_VALUE); }
    CHECK(n3ds_save_find_count()==8 && XFindFirstSaveGame("u:\\",&data)==INVALID_HANDLE_VALUE);
    stale=handles[0]; CHECK(XFindClose(stale)); handles[0]=XFindFirstSaveGame("u:\\",&data);
    CHECK(handles[0]!=INVALID_HANDLE_VALUE && handles[0]!=stale && !XFindNextSaveGame(stale,&data) && !XFindClose(stale));
    for(unsigned int i=0;i<8;++i) CHECK(XFindClose(handles[i]));
    CHECK(!XFindClose(INVALID_HANDLE_VALUE));
    snprintf(child,sizeof(child),"%sname.h3s",path); file_reference_create_from_path(&metadata,child,FALSE);
    CHECK(file_open(&metadata,3) && file_read(&metadata,sizeof(original),original));
    CHECK(file_write_to_position(&metadata,16,1,&corrupt) && file_close(&metadata));
    CHECK(count_saves()==baseline+1); /* Invalid checksum cannot publish changed metadata. */
    CHECK(file_open(&metadata,3) && file_write_to_position(&metadata,0,sizeof(original),original) && file_close(&metadata));
    CHECK(count_saves()==baseline+2);
    snprintf(child,sizeof(child),"%sblam.sav",path); file_reference_create_from_path(&file,child,FALSE);
    CHECK(file_create(&file) && file_open(&file,3) && file_write(&file,4,"test"));
    CHECK(XDeleteSaveGame("u:\\",name)==ERROR_SHARING_VIOLATION);
    CHECK(file_exists(&metadata) && file_get_eof(&file)==4 && file_close(&file));
    snprintf(child,sizeof(child),"%snested",path); file_reference_create_from_path(&nested,child,TRUE); CHECK(file_create(&nested));
    snprintf(child,sizeof(child),"%snested\\marker.bin",path); file_reference_create_from_path(&file,child,FALSE); CHECK(file_create(&file));
    CHECK(XDeleteSaveGame("u:\\",name)==ERROR_SUCCESS && !file_exists(&metadata) && !file_exists(&file));
    CHECK(XDeleteSaveGame("u:\\",second)==ERROR_SUCCESS && XDeleteSaveGame("u:\\",second)==ERROR_FILE_NOT_FOUND);
    CHECK(count_saves()==baseline && !n3ds_save_find_count() && !n3ds_files_open_count() && !n3ds_thread_count());
    n3ds_log("PASS: native save create/open dispositions, 8 iterator slots, stale handles, corrupt metadata rejection and busy/nested deletion with complete cleanup");
    return 0;
}
