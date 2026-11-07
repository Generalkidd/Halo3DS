#include "cseries.h"
#include "engine_files.h"
#include "engine_graphics_options.h"
#include "engine_input.h"
#include <limits.h>

void n3ds_log(const char *message);

enum { GRAPHICS_SETTINGS_MAGIC=0x33584647, GRAPHICS_SETTINGS_VERSION=2 };
enum graphics_read_result { GRAPHICS_READ_ERROR, GRAPHICS_INVALID, GRAPHICS_OK };

struct graphics_settings_file_v1 {
    unsigned int magic,version,texture,checksum;
};
struct graphics_settings_file {
    unsigned int magic,version,texture,resolution,checksum;
};
_Static_assert(sizeof(struct graphics_settings_file_v1)==16,"Graphics settings v1 file ABI");
_Static_assert(sizeof(struct graphics_settings_file)==20,"Graphics settings file ABI");

struct graphics_settings {
    enum n3ds_world_texture_mode texture;
    enum n3ds_world_resolution resolution;
};

static const char path[]="t:\\graphics-settings.bin";
static const char staging_path[]="t:\\graphics-settings.tmp";
static const char backup[]="t:\\graphics-settings.bak";
static struct graphics_settings saved={N3DS_WORLD_TEXTURE_32_MIPS,N3DS_WORLD_RESOLUTION_AUTO},draft;
static enum native_graphics_tab tab;
static int initialized,save_error;

static unsigned int checksum(const void *file,unsigned int size)
{
    const unsigned char *bytes=(const unsigned char *)file;
    unsigned int hash=2166136261u;
    for(unsigned int i=0;i<size;++i)
        hash=(hash^bytes[i])*16777619u;
    return hash;
}

static enum graphics_read_result read_settings(const char *name,struct graphics_settings *settings)
{
    unsigned int handle=n3ds_file_open(name,1);
    if(!handle) return GRAPHICS_READ_ERROR;
    unsigned int size=n3ds_file_size(handle);
    struct graphics_settings_file file;
    struct graphics_settings_file_v1 legacy;
    int read_ok=size==sizeof(file)?n3ds_file_read(handle,&file,sizeof(file)):
        size==sizeof(legacy)?n3ds_file_read(handle,&legacy,sizeof(legacy)):1;
    if(!n3ds_file_close(handle) || !read_ok || size==UINT_MAX) return GRAPHICS_READ_ERROR;
    if(size==sizeof(legacy)) {
        if(legacy.magic!=GRAPHICS_SETTINGS_MAGIC || legacy.version!=1 ||
            legacy.texture>=NATIVE_GRAPHICS_MODE_COUNT ||
            legacy.checksum!=checksum(&legacy,offsetof(struct graphics_settings_file_v1,checksum))) return GRAPHICS_INVALID;
        *settings=(struct graphics_settings){legacy.texture,N3DS_WORLD_RESOLUTION_AUTO};
    } else {
        if(size!=sizeof(file) || file.magic!=GRAPHICS_SETTINGS_MAGIC || file.version!=GRAPHICS_SETTINGS_VERSION ||
            file.texture>=NATIVE_GRAPHICS_MODE_COUNT || file.resolution>=N3DS_WORLD_RESOLUTION_COUNT ||
            file.checksum!=checksum(&file,offsetof(struct graphics_settings_file,checksum))) return GRAPHICS_INVALID;
        *settings=(struct graphics_settings){file.texture,file.resolution};
    }
    return GRAPHICS_OK;
}

static int settings_match(struct graphics_settings a,struct graphics_settings b)
{
    return a.texture==b.texture && a.resolution==b.resolution;
}

static int write_settings(struct graphics_settings settings)
{
    struct graphics_settings_file file={GRAPHICS_SETTINGS_MAGIC,GRAPHICS_SETTINGS_VERSION,
        settings.texture,settings.resolution,0};
    file.checksum=checksum(&file,offsetof(struct graphics_settings_file,checksum));
    if(!n3ds_file_create(staging_path,0)) return 0;
    unsigned int handle=n3ds_file_open(staging_path,3);
    if(!handle) return 0;
    int ok=n3ds_file_write(handle,&file,sizeof(file));
    ok=n3ds_file_close(handle) && ok;
    struct graphics_settings verified;
    if(!ok || read_settings(staging_path,&verified)!=GRAPHICS_OK || !settings_match(verified,settings)) return 0;

    struct native_file_info info;
    if(n3ds_file_stat(path,&info)) {
        enum graphics_read_result result=read_settings(path,&verified);
        if(result==GRAPHICS_READ_ERROR) return 0;
        if(result==GRAPHICS_OK) {
            if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0)) return 0;
            if(!n3ds_file_rename(path,backup)) return 0;
        } else if(!n3ds_file_delete(path,0)) return 0;
    }
    return n3ds_file_rename(staging_path,path);
}

int n3ds_graphics_options_available(void)
{
    return !n3ds_input_platform_original_model() || n3ds_gpu_world_resolution_available();
}

void n3ds_graphics_options_initialize(void)
{
    if(initialized) return;
    initialized=1;
    if(!n3ds_graphics_options_available()) return;
    const char *source="default";
    if(read_settings(path,&saved)==GRAPHICS_OK) source="primary";
    else if(read_settings(backup,&saved)==GRAPHICS_OK) source="backup";
    if(!n3ds_input_platform_original_model()) n3ds_engine_world_texture_request(saved.texture);
    if(n3ds_gpu_world_resolution_available()) n3ds_gpu_world_resolution_request(saved.resolution);
    char message[112];
    snprintf(message,sizeof(message),"GRAPHICS OPTIONS: loaded mode=%u source=%s resolution=%u",
        (unsigned int)saved.texture,source,(unsigned int)saved.resolution);
    n3ds_log(message);
}

void n3ds_graphics_options_open(void)
{
    draft=saved;
    tab=n3ds_gpu_world_resolution_available()?NATIVE_GRAPHICS_RESOLUTION_TAB:NATIVE_GRAPHICS_TEXTURE_TAB;
    save_error=0;
    n3ds_log("GRAPHICS OPTIONS: opened");
}

static int save(void)
{
    if(!settings_match(draft,saved)) {
        if(!write_settings(draft)) {
            save_error=1;
            n3ds_log("GRAPHICS OPTIONS: could not save preferences; retry or cancel");
            return 0;
        }
        if(draft.texture!=saved.texture) n3ds_engine_world_texture_request(draft.texture);
        if(draft.resolution!=saved.resolution) n3ds_gpu_world_resolution_request(draft.resolution);
        saved=draft;
    }
    char message[112];
    snprintf(message,sizeof(message),"GRAPHICS OPTIONS: saved mode=%u resolution=%u; texture applies on next map load",
        (unsigned int)saved.texture,(unsigned int)saved.resolution);
    n3ds_log(message);
    return 1;
}

static int show_tabs(void)
{
    return !n3ds_input_platform_original_model() && n3ds_gpu_world_resolution_available();
}

int n3ds_graphics_options_input(unsigned int fresh,unsigned int x,unsigned int y)
{
    int touch=!!(fresh&NATIVE_KEY_TOUCH);
    int footer=touch && y>=NATIVE_OPTIONS_ENTRY_Y && y<NATIVE_OPTIONS_ENTRY_Y+NATIVE_OPTIONS_ENTRY_HEIGHT;
    if((fresh&NATIVE_KEY_B) || (footer && x>=NATIVE_GRAPHICS_CANCEL_X && x<NATIVE_GRAPHICS_CANCEL_X+NATIVE_GRAPHICS_BUTTON_WIDTH)) {
        n3ds_log("GRAPHICS OPTIONS: cancelled");
        return 1;
    }
    if((fresh&(NATIVE_KEY_A|NATIVE_KEY_START)) ||
        (footer && x>=NATIVE_GRAPHICS_SAVE_X && x<NATIVE_GRAPHICS_SAVE_X+NATIVE_GRAPHICS_BUTTON_WIDTH)) return save();
    if(show_tabs()) {
        if(fresh&NATIVE_KEY_DLEFT) {tab=NATIVE_GRAPHICS_RESOLUTION_TAB;return 0;}
        if(fresh&NATIVE_KEY_DRIGHT) {tab=NATIVE_GRAPHICS_TEXTURE_TAB;return 0;}
        if(touch && y>=NATIVE_GRAPHICS_TAB_Y && y<NATIVE_GRAPHICS_TAB_Y+NATIVE_GRAPHICS_TAB_HEIGHT) {
            if(x>=NATIVE_GRAPHICS_RESOLUTION_X && x<NATIVE_GRAPHICS_RESOLUTION_X+NATIVE_GRAPHICS_TAB_WIDTH)
                tab=NATIVE_GRAPHICS_RESOLUTION_TAB;
            if(x>=NATIVE_GRAPHICS_TEXTURE_X && x<NATIVE_GRAPHICS_TEXTURE_X+NATIVE_GRAPHICS_TAB_WIDTH)
                tab=NATIVE_GRAPHICS_TEXTURE_TAB;
            return 0;
        }
    }
    int resolution=tab==NATIVE_GRAPHICS_RESOLUTION_TAB;
    unsigned int count=resolution?N3DS_WORLD_RESOLUTION_COUNT:NATIVE_GRAPHICS_MODE_COUNT;
    unsigned int row=resolution?(unsigned int)draft.resolution:NATIVE_GRAPHICS_MODE_COUNT-1-draft.texture;
    if(fresh&NATIVE_KEY_DUP) row=(row+count-1)%count;
    else if(fresh&NATIVE_KEY_DDOWN) row=(row+1)%count;
    else if(touch && x>=NATIVE_GRAPHICS_ROW_X && x<NATIVE_GRAPHICS_ROW_X+NATIVE_GRAPHICS_ROW_WIDTH) {
        unsigned int row_y=resolution?NATIVE_RESOLUTION_ROW_Y:NATIVE_GRAPHICS_ROW_Y;
        unsigned int stride=resolution?NATIVE_RESOLUTION_ROW_STRIDE:NATIVE_GRAPHICS_ROW_STRIDE;
        unsigned int height=resolution?NATIVE_RESOLUTION_ROW_HEIGHT:NATIVE_GRAPHICS_ROW_HEIGHT;
        if(y>=row_y && (y-row_y)/stride<count && (y-row_y)%stride<height) row=(y-row_y)/stride;
    }
    if(resolution) draft.resolution=(enum n3ds_world_resolution)row;
    else draft.texture=(enum n3ds_world_texture_mode)(NATIVE_GRAPHICS_MODE_COUNT-1-row);
    return 0;
}

void n3ds_graphics_options_draw(void)
{
    const struct native_graphics_options_view view={
        .draft_texture=draft.texture,.saved_texture=saved.texture,
        .draft_resolution=draft.resolution,.saved_resolution=saved.resolution,
        .tab=tab,.show_tabs=show_tabs(),.save_error=save_error
    };
    n3ds_gpu_graphics_options_draw(&view);
}
