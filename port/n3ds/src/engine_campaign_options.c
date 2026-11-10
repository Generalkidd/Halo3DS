/* New3DS integration additions by Kevin Boggs. */
#include "cseries.h"
#include "main/main.h"
#include "saved games/game_state.h"
#include "interface/player_ui.h"
#include "engine_cache.h"
#include "engine_campaign_options.h"
#include "engine_files.h"
#include "engine_network.h"
#include "engine_map_format.h"
int n3ds_campaign_network_blocked(void);
#include <string.h>

void n3ds_log(const char *message);

static const char path[]="t:\\campaign-options.bin";
static const char options_temporary[]="t:\\campaign-options.tmp";
static const char backup[]="t:\\campaign-options.bak";
static int initialized, enabled=1, save_error, previous_touch, swallow_touch;
static int checkpoint_menu, checkpoint_available;
static long checkpoint_owner=NONE;
static short checkpoint_difficulty;
static char checkpoint_map[256], checkpoint_label[96];

/* Kevin Boggs: n3ds_campaign_checkpoint_info */
const char *n3ds_campaign_checkpoint_info(int *available)
{
    *available=checkpoint_available;
    return checkpoint_label;
}

/* Kevin Boggs: n3ds_campaign_checkpoint_resume */
int n3ds_campaign_checkpoint_resume(void)
{
    if(!checkpoint_menu || !checkpoint_available || n3ds_campaign_network_blocked())return 0;
    main_set_map_name(checkpoint_map);
    main_set_difficulty(checkpoint_difficulty);
    main_menu_switch_to_single_player();
    checkpoint_available=0;
    return 1;
}

/* Kevin Boggs: campaign_checkpoint_refresh */
static void campaign_checkpoint_refresh(void)
{
    extern int n3ds_controls_active(void);
    int menu=n3ds_ui_campaign_menu_active() && !n3ds_campaign_network_blocked() && !n3ds_controls_active();
#ifdef HALO_N3DS_PC_LOCAL_MENU
    extern int n3ds_pc_local_menu_active(void);
    if(n3ds_pc_local_menu_active())menu=0;
#endif
    long owner=menu?player_ui_get_active_player_profile_index(0):NONE;
    if(menu && (!checkpoint_menu || owner!=checkpoint_owner)) {
        boolean damaged=FALSE;
        checkpoint_available=owner!=NONE && game_state_test_persistent_storage(checkpoint_map,&checkpoint_difficulty,&damaged);
        if(checkpoint_available && !n3ds_engine_map_available(checkpoint_map))checkpoint_available=0;
        static const char *codes[]={"a10","a30","a50","b30","b40","c10","c20","c40","d20","d40"};
        static const char *titles[]={"The Pillar of Autumn","Halo","The Truth and Reconciliation","The Silent Cartographer","Assault on the Control Room","343 Guilty Spark","The Library","Two Betrayals","Keyes","The Maw"};
        static const char *difficulties[]={"Easy","Normal","Heroic","Legendary"};
        const char *title="Campaign";
        if(checkpoint_available) {
            const char *leaf=strrchr(checkpoint_map,'\\');leaf=leaf?leaf+1:checkpoint_map;
            for(int i=0;i<10;++i)if(!strcmp(leaf,codes[i]))title=titles[i];
            if(checkpoint_difficulty<0 || checkpoint_difficulty>3)checkpoint_available=0;
        }
        if(checkpoint_available)snprintf(checkpoint_label,sizeof(checkpoint_label),"%s\n%s",title,difficulties[checkpoint_difficulty]);
        else snprintf(checkpoint_label,sizeof(checkpoint_label),"%s",owner==NONE?"Select a profile to see its checkpoint.":damaged?"Checkpoint unavailable for this build.":"No resumable checkpoint.");
    }
    checkpoint_menu=menu;checkpoint_owner=owner;
}


struct campaign_options_file {
    unsigned int magic, version, extras, checksum;
};

/* Kevin Boggs: checksum - New3DS integration. */
static unsigned int checksum(const struct campaign_options_file *file)
{
    const unsigned char *bytes=(const unsigned char *)file;
    unsigned int value=2166136261u;
    for(unsigned int i=0;i<sizeof(*file)-4;++i)value=(value^bytes[i])*16777619u;
    return value;
}

/* Kevin Boggs: read_options - New3DS integration. */
static int read_options(const char *name,int *value)
{
    struct campaign_options_file file;
    unsigned int handle=n3ds_file_open(name,1);
    if(!handle)return 0;
    int ok=n3ds_file_size(handle)==sizeof(file) && n3ds_file_read(handle,&file,sizeof(file));
    ok=n3ds_file_close(handle) && ok;
    if(!ok || file.magic!=0x4e33434fu || file.version!=1 || file.extras>1 || file.checksum!=checksum(&file))return 0;
    *value=(int)file.extras;
    return 1;
}

/* Kevin Boggs: initialize - New3DS integration. */
static void initialize(void)
{
    if(initialized)return;
    initialized=1;
    if(!read_options(path,&enabled) && !read_options(backup,&enabled))enabled=1;
    n3ds_log(enabled?"CAMPAIGN OPTIONS: PC additions on":"CAMPAIGN OPTIONS: PC additions off");
}

/* Kevin Boggs: save_options - New3DS integration. */
static int save_options(int value)
{
    struct campaign_options_file file={0x4e33434fu,1,(unsigned int)value,0};
    file.checksum=checksum(&file);
    struct native_file_info info;
    if(n3ds_file_stat(options_temporary,&info) && !n3ds_file_delete(options_temporary,0))return 0;
    if(!n3ds_file_create(options_temporary,0))return 0;
    unsigned int handle=n3ds_file_open(options_temporary,3);
    if(!handle)return 0;
    int ok=n3ds_file_resize(handle,0) && n3ds_file_write(handle,&file,sizeof(file));
    ok=n3ds_file_close(handle) && ok;
    int verified=-1;
    if(!ok || !read_options(options_temporary,&verified) || verified!=value)return 0;
    if(n3ds_file_stat(path,&info)) {
        if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0))return 0;
        if(!n3ds_file_rename(path,backup))return 0;
    }
    if(!n3ds_file_rename(options_temporary,path))return 0;
    return 1;
}

/* Kevin Boggs: n3ds_campaign_extras_enabled - New3DS integration. */
int n3ds_campaign_extras_enabled(void)
{
    initialize();
    return enabled && n3ds_pc_maps_present() && !n3ds_campaign_network_blocked();
}

/* Kevin Boggs: n3ds_campaign_options_save_error - New3DS integration. */
int n3ds_campaign_options_save_error(void)
{
    return save_error;
}

/* Kevin Boggs: n3ds_campaign_options_filter - New3DS integration. */
void n3ds_campaign_options_filter(struct native_input_sample *sample)
{
    initialize();
    campaign_checkpoint_refresh();
    int touch=(sample->held&NATIVE_KEY_TOUCH)!=0;
    int fresh=touch && !previous_touch;
    previous_touch=touch;
    if(swallow_touch) {
        sample->held&=~NATIVE_KEY_TOUCH;
        sample->touch_x=sample->touch_y=0;
        if(!touch)swallow_touch=0;
        return;
    }
    if(checkpoint_menu && fresh && sample->touch_x>=16 && sample->touch_x<304 &&
       sample->touch_y>=80 && sample->touch_y<158 && n3ds_campaign_checkpoint_resume()) {
        swallow_touch=1;sample->held&=~NATIVE_KEY_TOUCH;sample->touch_x=sample->touch_y=0;return;
    }
    if(!n3ds_ui_difficulty_menu_active() || !n3ds_pc_maps_present() || n3ds_campaign_network_blocked())return;
    if(fresh && sample->touch_x>=16 && sample->touch_x<304 &&
       sample->touch_y>=94 && sample->touch_y<158) {
        int next=!enabled;
        if(save_options(next)) {
            enabled=next;
            save_error=0;
            n3ds_log(enabled?"CAMPAIGN OPTIONS: PC additions enabled and saved":"CAMPAIGN OPTIONS: PC additions disabled and saved");
        } else {
            save_error=1;
            n3ds_log("CAMPAIGN OPTIONS: could not save choice; previous value kept");
        }
        swallow_touch=1;
        sample->held&=~NATIVE_KEY_TOUCH;
        sample->touch_x=sample->touch_y=0;
    }
}
