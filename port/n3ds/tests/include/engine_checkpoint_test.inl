/* Opt-in integration fixture; never compiled into the live executable. */
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "interface/hud_messaging.h"
#include "engine_files.h"
#include "text/unicode.h"
boolean n3ds_main_mission_skip_cinematic(void);
void scripted_hud_set_timer_warning_cutoff(short,short);
void scripted_hud_show_timer(boolean);
static int checkpoint_fixture_resume;
static long checkpoint_fixture_profile;
void *n3ds_game_state_identity(unsigned long long *);
static struct game_state_header *checkpoint_fixture_header(void)
{unsigned long long generation;return n3ds_game_state_identity(&generation);}
static int checkpoint_fixture_flip(const char *path)
{
 unsigned char value;unsigned int handle=n3ds_file_open(path,3);if(!handle)return 0;
 int ok=n3ds_file_read_at(handle,80,&value,1);value^=0x40;
 if(ok)ok=n3ds_file_write_at(handle,80,&value,1);
 return n3ds_file_close(handle) && ok;
}
static int checkpoint_fixture_step(long frame)
{
    if(frame==30) {
        long indices[100],index=NONE;word count=100;struct player_profile profile;
        player_profiles_enumerate_available_to_local_player_index(NONE,&count,indices,FALSE);
        for(int i=0;i<count;++i) if(player_profile_get(indices[i],&profile) && !ustrcmp(profile.player_name,L"CPTEST")) {index=indices[i];break;}
        if(index==NONE) index=player_profile_new(0,L"CPTEST");
        CHECK(index!=NONE && player_profile_get(index,&profile));
        checkpoint_fixture_profile=index;
        player_ui_set_active_player_profile(0,index,&profile);
        player_ui_set_single_player_local_player_controller(0,0);player_spawn_count=1;
        char map[256];short difficulty;boolean damaged;
        checkpoint_fixture_resume=game_state_test_persistent_storage(map,&difficulty,&damaged);
#ifdef HALO_N3DS_CHECKPOINT_REJECTION_TEST
        CHECK(!checkpoint_fixture_resume && damaged);
        n3ds_log("PASS: damaged checkpoint without a backup was rejected before mission restore");
        return 0;
#else
        CHECK(!damaged);
#endif
        if(!checkpoint_fixture_resume) {strcpy(map,"levels\\a30\\a30");difficulty=1;}
        main_set_difficulty(difficulty);main_set_map_name(map);main_menu_switch_to_single_player();
        n3ds_log(checkpoint_fixture_resume?"CHECKPOINT TEST: launching saved mission after process restart":"CHECKPOINT TEST: launching new campaign fixture");
    }
    if(frame==100 && checkpoint_fixture_resume) {
        CHECK(!main_menu_is_active() && game_time_get()>100);
        if(checkpoint_fixture_header()->unused[0]==0x43505432) {
            CHECK(game_time_get()>=checkpoint_fixture_header()->unused[1]);
            CHECK(global_structure_bsp_index_get()==checkpoint_fixture_header()->unused[2]);
            n3ds_log("PASS: saved simulation time and BSP marker survived process restart");
        }
        n3ds_log("PASS: checkpoint loaded after process restart and simulation continued");
    }
    if(frame==150 && cinematic_can_be_skipped()) CHECK(n3ds_main_mission_skip_cinematic());
    if(frame==400) {
        CHECK(!main_menu_is_active() && !cinematic_in_progress());
        scripted_hud_set_timer_time(0,20);scripted_hud_set_timer_position(10,10,1);
        scripted_hud_set_timer_warning_cutoff(0,10);scripted_hud_show_timer(TRUE);
        n3ds_log("TIMER TEST: original scripted countdown enabled, 20 seconds");
    }
    if(frame==410) {
        CHECK(!n3ds_gpu_screen_test());
        CHECK(scripted_hud_get_timer_ticks()>0 && scripted_hud_get_timer_ticks()<600);
    }
    if(frame>=420 && frame<=422) {
        checkpoint_fixture_header()->unused[3]=frame;
        game_state_save();
        n3ds_log("CHECKPOINT TEST: consecutive immutable saves while writer may be active");
    }
    if(frame==440) {
        game_state_revert();
        CHECK(checkpoint_fixture_header()->unused[3]==422);
        n3ds_log("PASS: newest session checkpoint survived overlapping SD saves");
    }
    if(frame==450) {
#ifdef HALO_N3DS_CHECKPOINT_BSP_TEST
        CHECK(scenario_switch_structure_bsp(1));
#endif
        checkpoint_fixture_header()->unused[0]=0x43505432;
        checkpoint_fixture_header()->unused[1]=game_time_get();
        checkpoint_fixture_header()->unused[2]=global_structure_bsp_index_get();
        game_state_save();
        char map[256];short difficulty;boolean damaged;
        CHECK(game_state_test_persistent_storage(map,&difficulty,&damaged) && !damaged);
        CHECK(!strcmp(map,"levels\\a30\\a30") && difficulty==1);
        game_state_save_to_persistent_storage();main_goto_main_menu();
        n3ds_log("CHECKPOINT TEST: saved checkpoint and requested main menu");
    }
    if(frame==510) {
        CHECK(main_menu_is_active());
        struct player_profile profile;
        CHECK(player_profile_get(checkpoint_fixture_profile,&profile));
        player_ui_set_active_player_profile(0,checkpoint_fixture_profile,&profile);
        player_ui_set_single_player_local_player_controller(0,0);
        char map[256];short difficulty;boolean damaged;
        CHECK(game_state_test_persistent_storage(map,&difficulty,&damaged) && !damaged);
        main_set_difficulty(difficulty);main_set_map_name(map);main_menu_switch_to_single_player();
    }
    if(frame==570) {
        CHECK(!main_menu_is_active() && game_time_get()>100 && !cinematic_in_progress());
        CHECK(checkpoint_fixture_header()->unused[0]==0x43505432);
        CHECK(game_time_get()>=checkpoint_fixture_header()->unused[1]);
        CHECK(global_structure_bsp_index_get()==checkpoint_fixture_header()->unused[2]);
        n3ds_log("PASS: Save and Quit, menu checkpoint query, mission reload and resumed gameplay");
    }
    if(frame==600) {
        char directory[256],path[256],backup[256],map[256];short difficulty;boolean damaged;
        CHECK(player_ui_get_path_to_local_player_profile_directory(0,directory));
        snprintf(path,sizeof(path),"%s\\savegame.n3ds",directory);
        snprintf(backup,sizeof(backup),"%s.bak",path);
        n3ds_checkpoint_finish();
        CHECK(checkpoint_fixture_flip(path));
        CHECK(game_state_test_persistent_storage(map,&difficulty,&damaged) && !damaged);
        n3ds_log("PASS: damaged packed save recovered through validated backup");
        CHECK(checkpoint_fixture_flip(backup));
        CHECK(!game_state_test_persistent_storage(map,&difficulty,&damaged) && damaged);
        CHECK(checkpoint_fixture_flip(path) && checkpoint_fixture_flip(backup));
        CHECK(game_state_test_persistent_storage(map,&difficulty,&damaged) && !damaged);
        n3ds_log("PASS: both damaged saves rejected; restored fixture bytes validate again");
    }
    return 0;
}
