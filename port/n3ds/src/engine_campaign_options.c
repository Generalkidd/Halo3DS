/* New3DS integration additions by Kevin Boggs. */
#include "engine_campaign_options.h"
#include "engine_files.h"
#include <string.h>

void n3ds_log(const char *message);

static const char path[]="t:\\campaign-options.bin";
static const char temporary[]="t:\\campaign-options.tmp";
static const char backup[]="t:\\campaign-options.bak";
static int initialized, enabled=1, save_error, previous_touch, swallow_touch;

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
    if(n3ds_file_stat(temporary,&info) && !n3ds_file_delete(temporary,0))return 0;
    if(!n3ds_file_create(temporary,0))return 0;
    unsigned int handle=n3ds_file_open(temporary,3);
    if(!handle)return 0;
    int ok=n3ds_file_resize(handle,0) && n3ds_file_write(handle,&file,sizeof(file));
    ok=n3ds_file_close(handle) && ok;
    int verified=-1;
    if(!ok || !read_options(temporary,&verified) || verified!=value)return 0;
    if(n3ds_file_stat(path,&info)) {
        if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0))return 0;
        if(!n3ds_file_rename(path,backup))return 0;
    }
    if(!n3ds_file_rename(temporary,path))return 0;
    return 1;
}

/* Kevin Boggs: n3ds_campaign_extras_enabled - New3DS integration. */
int n3ds_campaign_extras_enabled(void)
{
    initialize();
    return enabled;
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
    int touch=(sample->held&NATIVE_KEY_TOUCH)!=0;
    int fresh=touch && !previous_touch;
    previous_touch=touch;
    if(swallow_touch) {
        sample->held&=~NATIVE_KEY_TOUCH;
        sample->touch_x=sample->touch_y=0;
        if(!touch)swallow_touch=0;
        return;
    }
    if(!n3ds_ui_difficulty_menu_active())return;
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
