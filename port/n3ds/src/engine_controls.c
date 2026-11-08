#include "cseries.h"
#include "engine_controls.h"
#include "engine_files.h"
#include "engine_graphics_options.h"
#include "input/input.h"
#include <limits.h>

void n3ds_log(const char *message);
void n3ds_ui_controls_back(void);

enum { SETTINGS_MAGIC=0x3343544c, SETTINGS_VERSION=2, CSTICK_DEADZONE_MAX=19660 };
enum { PRESET_DEFAULT, PRESET_RIGHT, PRESET_CLASSIC, PRESET_XBOX, PRESET_COUNT };
enum { BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_A, BIND_B, BIND_X, BIND_Y };

static const unsigned int binding_keys[NATIVE_BIND_COUNT]={
    NATIVE_KEY_L,NATIVE_KEY_R,NATIVE_KEY_ZL,NATIVE_KEY_ZR,
    NATIVE_KEY_A,NATIVE_KEY_B,NATIVE_KEY_X,NATIVE_KEY_Y,
    NATIVE_KEY_DUP,NATIVE_KEY_DDOWN,NATIVE_KEY_DLEFT,NATIVE_KEY_DRIGHT
};
static const char *const binding_names[NATIVE_BIND_COUNT]={
    "L","R","ZL","ZR","A","B","X","Y","D-pad Up","D-pad Down","D-pad Left","D-pad Right"
};
static const char *const action_names[NATIVE_ACTION_COUNT]={
    "Unassigned","Fire","Throw grenade","Jump","Melee","Reload / Interact","Switch weapon",
    "Crouch","Zoom","Cycle grenades","Flashlight","Move forward","Move backward","Strafe left","Strafe right"
};
static const unsigned char gamepad_button[NATIVE_ACTION_COUNT]={
    [NATIVE_ACTION_FIRE]=_gamepad_analog_button_right_trigger,
    [NATIVE_ACTION_GRENADE]=_gamepad_analog_button_left_trigger,
    [NATIVE_ACTION_JUMP]=_gamepad_analog_button_a,
    [NATIVE_ACTION_MELEE]=_gamepad_analog_button_b,
    [NATIVE_ACTION_RELOAD]=_gamepad_analog_button_x,
    [NATIVE_ACTION_SWITCH_WEAPON]=_gamepad_analog_button_y,
    [NATIVE_ACTION_CROUCH]=_gamepad_binary_button_left_thumb,
    [NATIVE_ACTION_ZOOM]=_gamepad_binary_button_right_thumb,
    [NATIVE_ACTION_SWITCH_GRENADE]=_gamepad_analog_button_black,
    [NATIVE_ACTION_FLASHLIGHT]=_gamepad_analog_button_white,
    [NATIVE_ACTION_MOVE_UP]=_gamepad_binary_button_dpad_up,
    [NATIVE_ACTION_MOVE_DOWN]=_gamepad_binary_button_dpad_down,
    [NATIVE_ACTION_MOVE_LEFT]=_gamepad_binary_button_dpad_left,
    [NATIVE_ACTION_MOVE_RIGHT]=_gamepad_binary_button_dpad_right
};
static const unsigned char default_bindings[2][NATIVE_BIND_COUNT]={
    {NATIVE_ACTION_FIRE,NATIVE_ACTION_ZOOM,NATIVE_ACTION_GRENADE,NATIVE_ACTION_CROUCH,
     NATIVE_ACTION_JUMP,NATIVE_ACTION_MELEE,NATIVE_ACTION_RELOAD,NATIVE_ACTION_SWITCH_WEAPON,
     NATIVE_ACTION_MOVE_UP,NATIVE_ACTION_MOVE_DOWN,NATIVE_ACTION_MOVE_LEFT,NATIVE_ACTION_MOVE_RIGHT},
    {NATIVE_ACTION_FIRE,NATIVE_ACTION_GRENADE,NATIVE_ACTION_NONE,NATIVE_ACTION_NONE,
     NATIVE_ACTION_JUMP,NATIVE_ACTION_MELEE,NATIVE_ACTION_RELOAD,NATIVE_ACTION_SWITCH_WEAPON,
     NATIVE_ACTION_ZOOM,NATIVE_ACTION_CROUCH,NATIVE_ACTION_MOVE_LEFT,NATIVE_ACTION_MOVE_RIGHT}
};
static const char *const preset_names[PRESET_COUNT]={
    "Default","Right shoulder","Classic ZR fire","Xbox face buttons"
};
static struct native_control_settings settings,opened_settings;
static const void *modal_owner;
static int initialized,main_route,tab,selected,first,save_error,repeat_ticks,swallow,graphics_open;
static int pending_preset=-1, picker_row=-1, picker_return_first;
static unsigned int previous_keys, repeat_key;
static const char path[]="t:\\controller-settings.bin";
static const char settings_temporary[]="t:\\controller-settings.tmp";
static const char backup[]="t:\\controller-settings.bak";

struct settings_file {
    unsigned int magic,version;
    struct native_control_settings settings;
    unsigned int checksum;
};
struct settings_file_v1 {unsigned int magic,version,cstick,touch,xbox_buttons,swap_shoulders,checksum;};
_Static_assert(sizeof(struct settings_file)==64 && sizeof(struct settings_file_v1)==28,"Controller settings file ABI");

static void swap_binding(unsigned char *bindings,int a,int b)
{
    unsigned char temp=bindings[a];
    bindings[a]=bindings[b];
    bindings[b]=temp;
}

static void preset_bindings(unsigned char *bindings,int original,int preset)
{
    memcpy(bindings,default_bindings[original],NATIVE_BIND_COUNT);
    if(preset==PRESET_RIGHT) {
        swap_binding(bindings,BIND_L,BIND_R);
        swap_binding(bindings,BIND_ZL,BIND_ZR);
    } else if(preset==PRESET_CLASSIC) {
        bindings[BIND_L]=NATIVE_ACTION_CROUCH;
        bindings[BIND_ZR]=NATIVE_ACTION_FIRE;
    } else if(preset==PRESET_XBOX) {
        swap_binding(bindings,BIND_A,BIND_B);
        swap_binding(bindings,BIND_X,BIND_Y);
    }
}

void n3ds_controls_defaults(struct native_control_settings *s)
{
    *s=(struct native_control_settings){.cstick=100,.touch=100,.cstick_deadzone=9000,.touch_aim=1};
    memcpy(s->bindings,default_bindings,sizeof(s->bindings));
}

static int valid_sensitivity(unsigned int value) { return value>=25 && value<=300; }

static int valid(const struct native_control_settings *s)
{
    if(!valid_sensitivity(s->cstick) || !valid_sensitivity(s->touch) || s->xbox_buttons>1 ||
       s->cstick_deadzone>CSTICK_DEADZONE_MAX || s->invert_cstick_y>1 ||
       s->invert_touch_y>1 || s->touch_aim>1) return 0;
    for(int model=0;model<2;++model)
        for(int i=0;i<NATIVE_BIND_COUNT;++i)
            if(s->bindings[model][i]>=NATIVE_ACTION_COUNT ||
               (model && (i==BIND_ZL || i==BIND_ZR) && s->bindings[model][i])) return 0;
    return 1;
}

static unsigned int checksum(const void *data,unsigned int size)
{
    const unsigned char *p=data;
    unsigned int h=2166136261u;
    for(unsigned int i=0;i<size;++i) h=(h^p[i])*16777619u;
    return h;
}

static int migrate_v1(const struct settings_file_v1 *old,struct native_control_settings *s)
{
    if(old->magic!=SETTINGS_MAGIC || old->version!=1 ||
       old->checksum!=checksum(old,offsetof(struct settings_file_v1,checksum)) ||
       !valid_sensitivity(old->cstick) || !valid_sensitivity(old->touch) ||
       old->xbox_buttons>1 || old->swap_shoulders>1) return 0;
    n3ds_controls_defaults(s);
    s->cstick=old->cstick;
    s->touch=old->touch;
    s->xbox_buttons=old->xbox_buttons;
    if(old->swap_shoulders) {
        /* Preserve the explicitly chosen v1 shoulder swap. Untouched v1
         * shoulders get the new default on both models. */
        unsigned char *keys=s->bindings[0];
        preset_bindings(keys,0,PRESET_CLASSIC);
        swap_binding(keys,BIND_L,BIND_ZL);
        swap_binding(keys,BIND_R,BIND_ZR);
    }
    if(old->xbox_buttons)
        for(int i=0;i<2;++i) {
            swap_binding(s->bindings[i],BIND_A,BIND_B);
            swap_binding(s->bindings[i],BIND_X,BIND_Y);
        }
    return 1;
}

enum settings_read_result { SETTINGS_READ_ERROR, SETTINGS_INVALID, SETTINGS_OK };

static enum settings_read_result read_settings(const char *name,struct native_control_settings *s)
{
    union {struct settings_file current;struct settings_file_v1 old;} file;
    unsigned int h=n3ds_file_open(name,1);
    if(!h) return SETTINGS_READ_ERROR;
    unsigned int size=n3ds_file_size(h);
    int known_size=size==sizeof(file.current) || size==sizeof(file.old);
    int read_ok=!known_size || n3ds_file_read(h,&file,size);
    if(!n3ds_file_close(h) || !read_ok || size==UINT_MAX) return SETTINGS_READ_ERROR;
    if(!known_size) return SETTINGS_INVALID;
    if(size==sizeof(file.old)) return migrate_v1(&file.old,s)?SETTINGS_OK:SETTINGS_INVALID;
    const struct settings_file *f=&file.current;
    if(f->magic!=SETTINGS_MAGIC || f->version!=SETTINGS_VERSION ||
       f->checksum!=checksum(f,offsetof(struct settings_file,checksum)) || !valid(&f->settings)) return SETTINGS_INVALID;
    *s=f->settings;
    return SETTINGS_OK;
}

static int save_settings(void)
{
    struct settings_file f={SETTINGS_MAGIC,SETTINGS_VERSION,settings,0};
    f.checksum=checksum(&f,offsetof(struct settings_file,checksum));
    if(!n3ds_file_create(settings_temporary,0)) return 0;
    unsigned int h=n3ds_file_open(settings_temporary,3);
    if(!h) return 0;
    int ok=n3ds_file_resize(h,0) && n3ds_file_write(h,&f,sizeof(f));
    ok=n3ds_file_close(h) && ok;
    struct native_control_settings verified;
    if(!ok || read_settings(settings_temporary,&verified)!=SETTINGS_OK || memcmp(&verified,&settings,sizeof(settings))) return 0;
    struct native_file_info info;
    if(n3ds_file_stat(path,&info)) {
        enum settings_read_result result=read_settings(path,&verified);
        if(result==SETTINGS_READ_ERROR) return 0;
        if(result==SETTINGS_OK) {
            if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0)) return 0;
            if(!n3ds_file_rename(path,backup)) return 0;
        } else if(!n3ds_file_delete(path,0)) return 0; /* Keep a valid backup on recovery. */
    }
    return n3ds_file_rename(settings_temporary,path);
}

void n3ds_controls_initialize(void)
{
    if(initialized) return;
    initialized=1;
    if(read_settings(path,&settings)!=SETTINGS_OK && read_settings(backup,&settings)!=SETTINGS_OK)
        n3ds_controls_defaults(&settings);
}
void n3ds_controls_get(struct native_control_settings *s) { *s=settings; }
void n3ds_controls_set(const struct native_control_settings *s) { if(valid(s)) settings=*s; }
float n3ds_controls_sensitivity(int touch) { return (touch?settings.touch:settings.cstick)*.01f; }
short n3ds_controls_cstick_deadzone(void) { return (short)settings.cstick_deadzone; }
int n3ds_controls_invert_y(int touch) { return touch?settings.invert_touch_y:settings.invert_cstick_y; }
int n3ds_controls_touch_aim_enabled(int original) { return original || settings.touch_aim; }

static unsigned int exchange(unsigned int held,unsigned int a,unsigned int b)
{
    return (held&~(a|b)) | (held&a?b:0) | (held&b?a:0);
}
unsigned int n3ds_controls_menu_buttons(unsigned int held)
{
    if(settings.xbox_buttons) {
        held=exchange(held,NATIVE_KEY_A,NATIVE_KEY_B);
        held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);
    }
    return held;
}

unsigned int n3ds_controls_gameplay_buttons(unsigned int held,int original)
{
    const unsigned char *bindings=settings.bindings[!!original];
    unsigned int pressed=0;
    if(held&NATIVE_KEY_START) pressed|=1u<<_gamepad_binary_button_start;
    if(held&NATIVE_KEY_SELECT) pressed|=1u<<_gamepad_binary_button_back;
    for(int i=0;i<NATIVE_BIND_COUNT;++i)
        if((held&binding_keys[i]) && bindings[i]!=NATIVE_ACTION_NONE)
            pressed|=1u<<gamepad_button[bindings[i]];
    return pressed;
}

static const char *action_button_name(const unsigned char *bindings,int action)
{
    if(action>NATIVE_ACTION_NONE && action<NATIVE_ACTION_COUNT)
        for(int i=0;i<NATIVE_BIND_COUNT;++i)
            if(bindings[i]==action) return binding_names[i];
    return action==NATIVE_ACTION_SWITCH_GRENADE || action==NATIVE_ACTION_FLASHLIGHT?"Touch":"Unbound";
}

const char *n3ds_controls_action_button_name(int action,int original)
{
    return action_button_name(settings.bindings[!!original],action);
}

void n3ds_controls_widget_deleted(const void *widget)
{
    if(modal_owner && modal_owner==widget) {
        modal_owner=NULL;
        swallow=1;
        if(graphics_open) {
            graphics_open=0;
            n3ds_log("GRAPHICS OPTIONS: discarded after menu changed");
        } else {
            settings=opened_settings;
            n3ds_log("CONTROLLER SETTINGS: discarded after menu changed");
        }
    }
}

/* The underlying widget may be replaced by a dialog or a map transition.
 * Never navigate its history or commit the previous screen's draft. */
int n3ds_controls_active(void)
{
    if(modal_owner && modal_owner!=n3ds_ui_controls_root())
        n3ds_controls_widget_deleted(modal_owner);
    return modal_owner!=NULL;
}

void n3ds_controls_open(int from_main)
{
    if(n3ds_controls_active()) return;
    const void *root=n3ds_ui_controls_root();
    if(!root) return;
    n3ds_controls_initialize();
    modal_owner=root;
    main_route=from_main;
    tab=NATIVE_CONTROLS_LAYOUT;
    selected=first=save_error=repeat_ticks=0;
    pending_preset=picker_row=-1;
    repeat_key=0;
    opened_settings=settings;
    swallow=1;
    n3ds_log(from_main?"CONTROLLER SETTINGS: profile menu opened":"CONTROLLER SETTINGS: shortcut opened");
}

static int settings_dirty(void)
{
    return memcmp(&settings,&opened_settings,sizeof(settings))!=0;
}

static void close_settings(int save)
{
    if(save && settings_dirty() && !save_settings()) {
        save_error=1;
        n3ds_log("CONTROLLER SETTINGS: could not save preferences; retry or cancel");
        return;
    }
    if(!save) settings=opened_settings;
    modal_owner=NULL;
    swallow=1;
    if(main_route) n3ds_ui_controls_back();
    n3ds_log(save?"CONTROLLER SETTINGS: saved and returned":"CONTROLLER SETTINGS: cancelled");
}

static int row_count(int original)
{
    if(picker_row>=0) return NATIVE_ACTION_COUNT;
    if(tab==NATIVE_CONTROLS_LAYOUT) return 2;
    if(tab==NATIVE_CONTROLS_BUTTONS) return NATIVE_BIND_COUNT-(original?2:0);
    return original?2:6;
}

static int binding_row(int row,int original) { return row+(original && row>=BIND_ZL?2:0); }

static int current_preset(int original)
{
    for(int i=0;i<PRESET_COUNT;++i) {
        if(i==PRESET_CLASSIC && original) continue;
        unsigned char bindings[NATIVE_BIND_COUNT];
        preset_bindings(bindings,original,i);
        if(!memcmp(bindings,settings.bindings[original],sizeof(bindings))) return i;
    }
    return -1;
}

static void preview_preset(int delta,int original)
{
    int active=current_preset(original);
    int preset=pending_preset>=0?pending_preset:active;
    if(preset<0) preset=delta>0?PRESET_COUNT-1:0;
    do preset=(preset+delta+PRESET_COUNT)%PRESET_COUNT;
    while(original && preset==PRESET_CLASSIC);
    pending_preset=preset==active?-1:preset;
}

static void apply_preset(int original)
{
    if(pending_preset<0) return;
    preset_bindings(settings.bindings[original],original,pending_preset);
    pending_preset=-1;
    save_error=0;
}

static void reset_tab(int original)
{
    struct native_control_settings defaults;
    n3ds_controls_defaults(&defaults);
    if(tab==NATIVE_CONTROLS_AIM) {
        settings.touch=defaults.touch;
        settings.invert_touch_y=defaults.invert_touch_y;
        if(!original) {
            settings.cstick=defaults.cstick;
            settings.cstick_deadzone=defaults.cstick_deadzone;
            settings.invert_cstick_y=defaults.invert_cstick_y;
            settings.touch_aim=defaults.touch_aim;
        }
    } else {
        memcpy(settings.bindings[original],defaults.bindings[original],NATIVE_BIND_COUNT);
        if(tab==NATIVE_CONTROLS_LAYOUT) settings.xbox_buttons=defaults.xbox_buttons;
    }
    pending_preset=-1;
    save_error=0;
}

static unsigned int adjust_range(unsigned int value,int delta,int minimum,int maximum)
{
    int next=(int)value+delta;
    return next<minimum?minimum:next>maximum?maximum:next;
}

enum { AIM_CSTICK, AIM_TOUCH, AIM_DEADZONE, AIM_INVERT_CSTICK, AIM_INVERT_TOUCH, AIM_TOUCH_DRAG };
static int aim_row(int row,int original)
{
    return original?(row?AIM_INVERT_TOUCH:AIM_TOUCH):row;
}

static void adjust(int delta,int original)
{
    if(tab==NATIVE_CONTROLS_LAYOUT) {
        if(selected==0) {
            preview_preset(delta,original);
            return;
        }
        settings.xbox_buttons^=1;
    } else if(tab==NATIVE_CONTROLS_BUTTONS) {
        unsigned char *binding=&settings.bindings[original][binding_row(selected,original)];
        *binding=(*binding+delta+NATIVE_ACTION_COUNT)%NATIVE_ACTION_COUNT;
    } else switch(aim_row(selected,original)) {
        case AIM_CSTICK: settings.cstick=adjust_range(settings.cstick,delta*5,25,300); break;
        case AIM_TOUCH: settings.touch=adjust_range(settings.touch,delta*5,25,300); break;
        case AIM_DEADZONE: settings.cstick_deadzone=adjust_range(settings.cstick_deadzone,delta*1638,0,CSTICK_DEADZONE_MAX); break;
        case AIM_INVERT_CSTICK: settings.invert_cstick_y^=1; break;
        case AIM_INVERT_TOUCH: settings.invert_touch_y^=1; break;
        case AIM_TOUCH_DRAG: settings.touch_aim^=1; break;
    }
    save_error=0;
}

static void select_tab(int next)
{
    if(next==tab) return;
    tab=next;
    selected=first=0;
    pending_preset=-1;
}

static void select_row(int row)
{
    selected=row;
    if(tab==NATIVE_CONTROLS_LAYOUT && row!=0) pending_preset=-1;
    if(selected<first) first=selected;
    if(selected>=first+NATIVE_CONTROLS_VISIBLE_ROWS) first=selected-NATIVE_CONTROLS_VISIBLE_ROWS+1;
}

static void page_rows(int delta,int count)
{
    int next=first+delta*NATIVE_CONTROLS_VISIBLE_ROWS;
    if(next<0) next=0;
    if(next>=count || next==first) return;
    first=selected=next;
}

static void open_picker(int original)
{
    picker_row=selected;
    picker_return_first=first;
    selected=settings.bindings[original][binding_row(picker_row,original)];
    first=(selected/NATIVE_CONTROLS_VISIBLE_ROWS)*NATIVE_CONTROLS_VISIBLE_ROWS;
}

static void close_picker(int assign,int original)
{
    if(assign) {
        settings.bindings[original][binding_row(picker_row,original)]=selected;
        save_error=0;
    }
    selected=picker_row;
    first=picker_return_first;
    picker_row=-1;
}

/* Repeat a single direction. Changing direction starts a new hold and
 * opposing directions do not change a value twice on the same frame. */
static unsigned int direction_press(unsigned int held,int original)
{
    unsigned int vertical=held&(NATIVE_KEY_DUP|NATIVE_KEY_DDOWN);
    unsigned int horizontal=held&(NATIVE_KEY_DLEFT|NATIVE_KEY_DRIGHT);
    unsigned int key=vertical==NATIVE_KEY_DUP || vertical==NATIVE_KEY_DDOWN?vertical:0;
    if(!key && (horizontal==NATIVE_KEY_DLEFT || horizontal==NATIVE_KEY_DRIGHT)) key=horizontal;
    if(key!=repeat_key) {
        repeat_key=key;
        repeat_ticks=0;
        return key;
    }
    if(!key) return 0;
    int repeat_value=picker_row>=0 || tab==NATIVE_CONTROLS_BUTTONS ||
        (tab==NATIVE_CONTROLS_AIM && aim_row(selected,original)<=AIM_DEADZONE);
    if(!vertical && !repeat_value) return 0;
    if(++repeat_ticks<18) return 0;
    repeat_ticks=13;
    return key;
}

static void touch_controls(unsigned int x,unsigned int y,int original)
{
    int count=row_count(original);
    if(y>=NATIVE_CONTROLS_PAGE_Y && y<NATIVE_CONTROLS_PAGE_END) {
        if(x>=12 && x<100 && first>0) page_rows(-1,count);
        if(x>=220 && x<308 && first+NATIVE_CONTROLS_VISIBLE_ROWS<count) page_rows(1,count);
        return;
    }
    if(y>=202 && y<232) {
        if(x>=112 && x<208) {
            if(picker_row>=0) close_picker(0,original);
            else close_settings(0);
        } else if(picker_row<0) {
            if(x>=12 && x<104) {
                if(tab==NATIVE_CONTROLS_LAYOUT && pending_preset>=0) apply_preset(original);
                else reset_tab(original);
            } else if(x>=216 && x<308) close_settings(1);
        }
        return;
    }
    if(picker_row<0 && y>=10 && y<36 && x>=12 && x<308 && (x-12)%100<96) {
        select_tab((x-12)/100);
        return;
    }
    if(y<NATIVE_CONTROLS_ROW_Y || x<12 || x>=308) return;
    unsigned int offset=y-NATIVE_CONTROLS_ROW_Y;
    int slot=offset/NATIVE_CONTROLS_ROW_STRIDE;
    if(slot>=NATIVE_CONTROLS_VISIBLE_ROWS || first+slot>=count ||
       offset%NATIVE_CONTROLS_ROW_STRIDE>=NATIVE_CONTROLS_ROW_HEIGHT) return;
    select_row(first+slot);
    if(picker_row>=0) close_picker(1,original);
    else if(x>=218 && x<256) adjust(-1,original);
    else if(x>=264 && x<302) adjust(1,original);
    else if(x<218 && tab==NATIVE_CONTROLS_BUTTONS) open_picker(original);
    else if(x<218 && tab==NATIVE_CONTROLS_LAYOUT && selected==0) apply_preset(original);
}

void n3ds_controls_filter(struct native_input_sample *s)
{
    /* libctru also sets virtual direction bits for both sticks. Wait only
     * for actual buttons and the stylus so stick drift cannot trap the menu. */
    const unsigned int menu_keys=NATIVE_KEY_A|NATIVE_KEY_B|NATIVE_KEY_X|NATIVE_KEY_Y|
        NATIVE_KEY_DUP|NATIVE_KEY_DDOWN|NATIVE_KEY_DLEFT|NATIVE_KEY_DRIGHT|
        NATIVE_KEY_L|NATIVE_KEY_R|NATIVE_KEY_ZL|NATIVE_KEY_ZR|
        NATIVE_KEY_SELECT|NATIVE_KEY_START|NATIVE_KEY_TOUCH;
    unsigned int held=s->held&menu_keys,fresh=held&~previous_keys;
    previous_keys=held;
    int modal=n3ds_controls_active();
    int entry_touch=(fresh&NATIVE_KEY_TOUCH) && s->touch_y>=NATIVE_OPTIONS_ENTRY_Y &&
        s->touch_y<NATIVE_OPTIONS_ENTRY_Y+NATIVE_OPTIONS_ENTRY_HEIGHT;
    if(!modal && !swallow && ((fresh&(NATIVE_KEY_SELECT|NATIVE_KEY_Y)) || entry_touch) && n3ds_ui_controls_shortcut_active()) {
        int graphics=n3ds_graphics_options_available();
        unsigned int controls_x=graphics?NATIVE_CONTROLS_ENTRY_X:24;
        unsigned int controls_width=graphics?NATIVE_CONTROLS_ENTRY_WIDTH:272;
        if((fresh&NATIVE_KEY_SELECT) || (entry_touch && s->touch_x>=controls_x && s->touch_x<controls_x+controls_width))
            n3ds_controls_open(0);
        else if(graphics && ((fresh&NATIVE_KEY_Y) || (entry_touch && s->touch_x>=NATIVE_GRAPHICS_ENTRY_X &&
            s->touch_x<NATIVE_GRAPHICS_ENTRY_X+NATIVE_GRAPHICS_ENTRY_WIDTH))) {
            modal_owner=n3ds_ui_controls_root();
            if(modal_owner) {
                graphics_open=swallow=1;
                n3ds_graphics_options_open();
            }
        }
    }
    if(swallow) {
        if(!held) swallow=0;
        memset(s,0,sizeof(*s));
        return;
    }
    if(!modal) return;
    if(graphics_open) {
        if(n3ds_graphics_options_input(fresh,s->touch_x,s->touch_y)) {
            modal_owner=NULL;
            graphics_open=0;
            swallow=1;
        }
        memset(s,0,sizeof(*s));
        return;
    }
    int original=!!n3ds_input_platform_original_model();
    unsigned int direction=direction_press(held,original);
    int count=row_count(original);
    if(fresh&NATIVE_KEY_B) {
        if(picker_row>=0) close_picker(0,original);
        else close_settings(0);
    } else if(fresh&NATIVE_KEY_TOUCH) touch_controls(s->touch_x,s->touch_y,original);
    else if(picker_row>=0) {
        if(fresh&NATIVE_KEY_A) close_picker(1,original);
        else if(direction==NATIVE_KEY_DUP) select_row((selected+count-1)%count);
        else if(direction==NATIVE_KEY_DDOWN) select_row((selected+1)%count);
        else if(direction==NATIVE_KEY_DLEFT && first>0) page_rows(-1,count);
        else if(direction==NATIVE_KEY_DRIGHT && first+NATIVE_CONTROLS_VISIBLE_ROWS<count) page_rows(1,count);
    } else if(fresh&NATIVE_KEY_START) close_settings(1);
    else if(fresh&NATIVE_KEY_X) reset_tab(original);
    else if(fresh&NATIVE_KEY_A) {
        if(tab==NATIVE_CONTROLS_BUTTONS) open_picker(original);
        else if(tab==NATIVE_CONTROLS_LAYOUT) apply_preset(original);
    } else if(fresh&NATIVE_KEY_L) select_tab((tab+NATIVE_CONTROLS_TABS-1)%NATIVE_CONTROLS_TABS);
    else if(fresh&NATIVE_KEY_R) select_tab((tab+1)%NATIVE_CONTROLS_TABS);
    else if(direction==NATIVE_KEY_DUP) select_row((selected+count-1)%count);
    else if(direction==NATIVE_KEY_DDOWN) select_row((selected+1)%count);
    else if(direction==NATIVE_KEY_DLEFT) adjust(-1,original);
    else if(direction==NATIVE_KEY_DRIGHT) adjust(1,original);
    memset(s,0,sizeof(*s));
}

static void describe_row(int row,int original,const char **label,char value[32])
{
    if(picker_row>=0) {
        *label=action_names[row];
        if(row==settings.bindings[original][binding_row(picker_row,original)])
            snprintf(value,32,"Assigned");
    } else if(tab==NATIVE_CONTROLS_LAYOUT) {
        if(row==0) {
            int preset=pending_preset>=0?pending_preset:current_preset(original);
            *label="Gameplay preset";
            snprintf(value,32,"%s",preset<0?"Custom":preset_names[preset]);
        } else {
            *label="Menu face buttons";
            snprintf(value,32,"%s",settings.xbox_buttons?"Xbox positions":"Nintendo letters");
        }
    } else if(tab==NATIVE_CONTROLS_BUTTONS) {
        int key=binding_row(row,original);
        *label=binding_names[key];
        snprintf(value,32,"%s",action_names[settings.bindings[original][key]]);
    } else {
        static const char *const labels[]={
            "C-stick sensitivity","Touch sensitivity","C-stick dead zone",
            "Reverse C-stick Y","Reverse touch Y","Touch drag aim"
        };
        int aim=aim_row(row,original);
        *label=labels[aim];
        switch(aim) {
            case AIM_CSTICK: snprintf(value,32,"%u%%",settings.cstick); break;
            case AIM_TOUCH: snprintf(value,32,"%u%%",settings.touch); break;
            case AIM_DEADZONE: snprintf(value,32,"%u%%",(settings.cstick_deadzone*100+16383)/32767); break;
            case AIM_INVERT_CSTICK: snprintf(value,32,"%s",settings.invert_cstick_y?"On":"Off"); break;
            case AIM_INVERT_TOUCH: snprintf(value,32,"%s",settings.invert_touch_y?"On":"Off"); break;
            case AIM_TOUCH_DRAG: snprintf(value,32,"%s",settings.touch_aim?"On":"Off"); break;
        }
    }
}

static void describe_focus(struct native_controls_view *view)
{
    int focus=selected-first;
    view->focus_label=view->labels[focus];
    view->focus_value=view->values[focus];
    if(view->picking) {
        view->focus_label=binding_names[binding_row(picker_row,view->original)];
        view->focus_value=action_names[selected];
        view->help[0]="Tap an action or press A to assign it.";
        view->help[1]="B returns to the button list.";
    } else if(tab==NATIVE_CONTROLS_BUTTONS) {
        view->help[0]="Press A or tap a row to pick an action.";
        view->help[1]="Left/Right steps through the actions.";
    } else if(tab==NATIVE_CONTROLS_LAYOUT) {
        if(selected==0) {
            view->help[0]=view->preset_pending?"Preview only. Press A or tap Apply.":"Left/Right previews another preset.";
            view->help[1]=view->preset_pending?"START saves edits and skips this preview.":
                "Apply replaces this model's bindings.";
        } else {
            view->help[0]="Swap face button positions in menus.";
            view->help[1]="Set gameplay actions in the Buttons tab.";
        }
    } else {
        static const char *const help[][2]={
            {"Set how fast the stick turns the camera.","100% uses the profile's normal speed."},
            {"Set how far a drag turns the camera.","Lower this for finer touch aiming."},
            {"Ignore small movements near the center.","Raise this if the C-stick drifts."},
            {"Reverse vertical C-stick input.","This also reverses an inverted profile."},
            {"Reverse vertical touch drag input.","This also reverses an inverted profile."},
            {"Use the lower screen to turn the camera.","Touch shortcuts work in either mode."}
        };
        int aim=aim_row(selected,view->original);
        view->help[0]=help[aim][0];
        view->help[1]=help[aim][1];
    }
}

void n3ds_controls_draw(void)
{
    if(n3ds_controls_active()) {
        if(graphics_open) {
            n3ds_graphics_options_draw();
            return;
        }
        int original=!!n3ds_input_platform_original_model();
        unsigned char preview[NATIVE_BIND_COUNT];
        const unsigned char *bindings=settings.bindings[original];
        if(pending_preset>=0) {
            preset_bindings(preview,original,pending_preset);
            bindings=preview;
        }
        struct native_controls_view view={
            .tab=tab,.selected=selected,.first=first,.count=row_count(original),
            .save_error=save_error,.original=original,
            .picking=picker_row>=0,.dirty=settings_dirty(),.preset_pending=pending_preset>=0,
            .fire=action_button_name(bindings,NATIVE_ACTION_FIRE),
            .grenade=action_button_name(bindings,NATIVE_ACTION_GRENADE)
        };
        for(int i=0;i<NATIVE_BIND_COUNT;++i) {
            view.fire_bindings+=bindings[i]==NATIVE_ACTION_FIRE;
            view.grenade_bindings+=bindings[i]==NATIVE_ACTION_GRENADE;
        }
        for(int i=0;i<NATIVE_CONTROLS_VISIBLE_ROWS && first+i<view.count;++i)
            describe_row(first+i,original,&view.labels[i],view.values[i]);
        describe_focus(&view);
        n3ds_gpu_controls_draw(&view);
    } else if(n3ds_ui_controls_shortcut_active()) n3ds_gpu_controls_entry();
}
