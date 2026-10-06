/* Profile/empty-slot tests run in the bootstrap UI context. Real mission
 * save/reload/restart checks are in engine_checkpoint_test.inl. */
#include "cseries.h"
#include "game_state.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "engine_files.h"
#include "text/unicode.h"
long long n3ds_engine_ticks(void);
void n3ds_log(const char *message);
int halo_engine_persistent_state_tests(void)
{
    char name[12],directory[256],path[256];wchar_t wide_name[12];
    struct game_state_header header,untouched;
    struct player_profile profile;struct native_file_info info;
    unsigned int handle;boolean corrupted;long index;
#define CHECK(x) do {if(!(x)){n3ds_log("CAMPAIGN STORAGE FAIL: " #x);return 1;}}while(0)
    snprintf(name,sizeof(name),"N3%08lx",(unsigned long)n3ds_engine_ticks());
    CHECK(ascii_to_wide(name,wide_name,sizeof(wide_name))==wide_name);
    player_ui_initialize();
    index=player_profile_new(0,wide_name);
    CHECK(index!=NONE && player_profile_get(index,&profile));
    CHECK(!ustrcmp(profile.player_name,wide_name) && !profile.flags);
    player_ui_set_active_player_profile(0,index,&profile);
    CHECK(player_ui_get_path_to_local_player_profile_directory(0,directory));
    snprintf(path,sizeof(path),"%s%s",directory,game_state_get_persistent_storage_filename());
    CHECK(n3ds_file_stat(path,&info) && !info.size && !info.directory);
    memset(&untouched,0xa5,sizeof(untouched));header=untouched;corrupted=TRUE;
    CHECK(!game_state_read_header_from_persistent_storage(&header,&header.checksum,sizeof(header),0x345000,&corrupted));
    CHECK(!corrupted && !memcmp(&header,&untouched,sizeof(header)));
    /* A partial first write must never look like a usable checkpoint. */
    handle=n3ds_file_open(path,3);CHECK(handle);
    CHECK(n3ds_file_write(handle,"partial",7) && n3ds_file_close(handle));
    game_state_create_persistent_storage(directory);
    CHECK(n3ds_file_stat(path,&info) && info.size==7);
    corrupted=FALSE;
    CHECK(!game_state_read_header_from_persistent_storage(&header,&header.checksum,sizeof(header),0x345000,&corrupted));
    CHECK(corrupted && !memcmp(&header,&untouched,sizeof(header)));
    player_ui_initialize();player_profile_delete(index);player_ui_dispose();
    CHECK(!n3ds_file_stat(path,&info) && !n3ds_files_open_count());
    n3ds_log("PASS: Campaign profile creation/deletion, empty slot, partial save rejection and caller preservation");
    return 0;
}
