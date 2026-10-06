#include "cseries.h"
#include "tag_groups.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "text/text_group.h"
#include "text/unicode.h"
#include "saved games/saved_game_files.h"
#include "saved games/player_profile.h"
#include "saved games/playlist_profile.h"
#include "game/game_engine.h"
#include "bungie_net/common/thread.h"
#include "engine_files.h"
#include "engine_threads.h"
#include "engine_saves.h"
#include "engine_signatures.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
unsigned long __stdcall n3ds_ui_filesystem_worker(void *input);
int halo_engine_persistent_state_tests(void);

static int validate_names(long tag,unsigned int minimum)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    if(tag==NONE) return 0;
    struct string_list *list=tag_get(UNICODE_STRING_LIST_TAG,tag);
    if(!n3ds_cache_resolve(view,list,sizeof(*list)) || list->strings.count<(long)minimum || list->strings.count>512) return 0;
    if(!n3ds_cache_resolve(view,list->strings.address,list->strings.count*sizeof(struct string_list_entry))) return 0;
    for(unsigned int i=0;i<minimum;++i) {
        struct string_list_entry *entry=TAG_BLOCK_GET_ELEMENT(&list->strings,i,struct string_list_entry);
        if(entry->string.size<2 || entry->string.size>512 || (entry->string.size&1) ||
           !n3ds_cache_resolve(view,entry->string.address,entry->string.size)) return 0;
        const wchar_t *name=entry->string.address;
        if(!*name || name[entry->string.size/2-1]) return 0;
    }
    return 1;
}
int halo_engine_profile_enumeration_tests(void)
{
    long players=tag_loaded(UNICODE_STRING_LIST_TAG,"ui\\shell\\strings\\default_player_profile_names");
    long playlists=tag_loaded(UNICODE_STRING_LIST_TAG,"ui\\default_multiplayer_game_setting_names");
    long indices[100]; word count;
    struct thread_reference *thread; unsigned int result;
    unsigned int player_mask=0,variant_mask=0; struct player_profile profile; struct game_variant variant;
    char message[192],name[128];
#define CHECK(x) do { if(!(x)) { n3ds_log("PROFILE ENUMERATION FAIL: " #x); return 1; } } while(0)
    CHECK(validate_names(players,2) && validate_names(playlists,26));
    CHECK(!n3ds_mutex_count() && !n3ds_files_open_count());
    saved_game_files_initialize();
    CHECK(create_thread(0,n3ds_ui_filesystem_worker,NULL,&thread));
    long long deadline=n3ds_engine_ticks()+60*n3ds_engine_tick_frequency();
    while(!thread_has_exited(thread)) { CHECK(n3ds_engine_ticks()<deadline); n3ds_engine_sleep_milliseconds(1); }
    CHECK(n3ds_thread_result((unsigned long)thread,&result) && !result); dispose_thread(thread);
    CHECK(playlist_profile_number_of_default_profiles_on_disk()==26);
    n3ds_log("PASS: original UI filesystem worker completes with actual UI map strings, native disk checks, default profile enumeration and last-used profile lookup");
    for(unsigned int pass=0;pass<2;++pass) {
        player_mask=variant_mask=0;
        count=100; player_profiles_enumerate_available_to_local_player_index(NONE,&count,indices,TRUE);
        for(unsigned int i=0;i<count;++i) if((unsigned long)indices[i]&(1UL<<30)) {
            CHECK((unsigned long)indices[i]&(1UL<<31));
            CHECK(player_profile_get(indices[i],&profile));
            unsigned int index=profile.flags>>8;
            CHECK(index<2 && (profile.flags&1) && !(player_mask&(1U<<index)));
            CHECK(!ustrcmp(saved_game_file_get_display_name(indices[i]),unicode_string_list_get_string(players,index)));
            CHECK(profile.controller_settings.invert_look==index && profile.controller_settings.look_sensitivity==3);
            player_mask|=1U<<index;
            if(!pass) {
                wide_to_ascii(unicode_string_list_get_string(players,index),name,sizeof(name));
                snprintf(message,sizeof(message),"Native default player profile %u: %s",index,name); n3ds_log(message);
            }
        }
        CHECK(player_mask==3);
        count=100; playlist_profiles_enumerate_available_to_local_player_index(NONE,&count,indices);
        for(unsigned int i=0;i<count;++i) if((unsigned long)indices[i]&(1UL<<30)) {
            CHECK((unsigned long)indices[i]&(1UL<<31));
            CHECK(playlist_profile_get(indices[i],&variant));
            unsigned int index=variant.flags>>8;
            CHECK(index<26 && (variant.flags&1) && !(variant_mask&(1U<<index)));
            CHECK(!ustrcmp(saved_game_file_get_display_name(indices[i]),unicode_string_list_get_string(playlists,index)));
            wchar_t expected[12]={0}; ustrncpy(expected,unicode_string_list_get_string(playlists,index),11);
            CHECK(!ustrcmp(variant.human_readable_game_description,expected));
            CHECK(variant.game_engine_index>=1 && variant.game_engine_index<=5);
            variant_mask|=1U<<index;
        }
        CHECK(variant_mask==0x03ffffff);
        count=100; player_profiles_enumerate_available_to_local_player_index(NONE,&count,indices,FALSE);
        for(unsigned int i=0;i<count;++i) CHECK(!((unsigned long)indices[i]&(1UL<<30)));
    }
    CHECK(!n3ds_files_open_count() && !n3ds_files_directory_count() && !n3ds_save_find_count() && !n3ds_signature_count());
    CHECK(!halo_engine_persistent_state_tests());
    saved_game_files_dispose();
    CHECK(!n3ds_mutex_count() && !n3ds_thread_count());
    n3ds_log("PASS: original enumeration and indexed reads validate 2 player and 26 playlist defaults, localized names, validity/read-only flags, repeat calls and resource cleanup");
    return 0;
}
