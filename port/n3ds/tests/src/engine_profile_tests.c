#include "cseries.h"
#include "tag_files/files.h"
#include "saved games/saved_game_files.h"
#include "saved games/player_profile.h"
#include "engine_signatures.h"
#include "engine_files.h"
#include "engine_threads.h"
void n3ds_log(const char *message);
_Static_assert(sizeof(struct player_profile)==48,"Original default profile ABI");
int halo_engine_profile_tests(void)
{
    char path[256]; struct file_reference file;
    struct player_profile profile,untouched;
    unsigned char block[512],corrupt=0x7b;
#define CHECK(x) do { if(!(x)) { n3ds_log("PROFILE ENGINE FAIL: " #x); return 1; } } while(0)
    CHECK(!n3ds_mutex_count() && !n3ds_signature_count());
    for(unsigned int cycle=0;cycle<2;++cycle) {
        saved_game_files_initialize();
        CHECK(n3ds_mutex_count()==2 && saved_game_files_take_mutex()); saved_game_files_release_mutex();
        for(unsigned int i=0;i<NUMBER_OF_DEFAULT_PROFILES;++i) {
            snprintf(path,sizeof(path),"z:\\saved\\player_profiles\\default_profile\\%02u.sav",i);
            CHECK(player_profile_get_from_path(path,&profile));
            CHECK(profile.primary_color_index==NONE && profile.flags==(1|(i<<8)));
            CHECK(profile.last_single_player_map_played==0 && profile.controller_settings.look_sensitivity==3);
            CHECK(profile.controller_settings.invert_look==i && !profile.controller_settings.button_preset && !profile.controller_settings.joystick_preset);
            file_reference_create_from_path(&file,path,FALSE);
            CHECK(file_open(&file,3) && file_get_eof(&file)==sizeof(block) && file_read(&file,sizeof(block),block));
            CHECK(!memcmp(block,&profile,sizeof(profile)));
            CHECK(file_write_to_position(&file,0,1,&corrupt) && file_close(&file));
            memset(&untouched,0xcc,sizeof(untouched)); profile=untouched;
            int rejected=!player_profile_get_from_path(path,&profile) && !memcmp(&profile,&untouched,sizeof(profile));
            /* Restore the generated cache file before evaluating rejection. */
            CHECK(file_open(&file,3) && file_write_to_position(&file,0,sizeof(block),block) && file_close(&file));
            CHECK(rejected && player_profile_get_from_path(path,&profile));
        }
        CHECK(!n3ds_signature_count() && !n3ds_files_open_count());
        saved_game_files_dispose();
        CHECK(!n3ds_mutex_count() && !n3ds_thread_count());
    }
    n3ds_log("PASS: original save/player/playlist initialization creates both signed default profiles; original reads reject damage without changing output; repeated disposal reclaims mutexes");
    return 0;
}
