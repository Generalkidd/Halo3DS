#ifndef HALO_N3DS_ENGINE_CONTROLS_H
#define HALO_N3DS_ENGINE_CONTROLS_H
#include "engine_input.h"

enum native_control_action {
    NATIVE_ACTION_NONE, NATIVE_ACTION_FIRE, NATIVE_ACTION_GRENADE,
    NATIVE_ACTION_JUMP, NATIVE_ACTION_MELEE, NATIVE_ACTION_RELOAD,
    NATIVE_ACTION_SWITCH_WEAPON, NATIVE_ACTION_CROUCH, NATIVE_ACTION_ZOOM,
    NATIVE_ACTION_SWITCH_GRENADE, NATIVE_ACTION_FLASHLIGHT,
    NATIVE_ACTION_MOVE_UP, NATIVE_ACTION_MOVE_DOWN, NATIVE_ACTION_MOVE_LEFT,
    NATIVE_ACTION_MOVE_RIGHT, NATIVE_ACTION_COUNT
};
enum { NATIVE_BIND_COUNT=12 };
struct native_control_settings {
    unsigned int cstick, touch, xbox_buttons;
    unsigned int cstick_deadzone, invert_cstick_y, invert_touch_y, touch_aim;
    unsigned char bindings[2][NATIVE_BIND_COUNT]; /* New, then Original */
};

enum { NATIVE_CONTROLS_LAYOUT, NATIVE_CONTROLS_BUTTONS, NATIVE_CONTROLS_AIM, NATIVE_CONTROLS_TABS };
enum {
    NATIVE_CONTROLS_VISIBLE_ROWS=6,
    NATIVE_CONTROLS_ROW_Y=42, NATIVE_CONTROLS_ROW_STRIDE=23, NATIVE_CONTROLS_ROW_HEIGHT=21,
    NATIVE_CONTROLS_PAGE_Y=179, NATIVE_CONTROLS_PAGE_END=201
};
struct native_controls_view {
    int tab, selected, first, count, save_error, original;
    int picking, dirty, preset_pending;
    const char *labels[NATIVE_CONTROLS_VISIBLE_ROWS];
    char values[NATIVE_CONTROLS_VISIBLE_ROWS][32];
    const char *fire, *grenade;
    int fire_bindings, grenade_bindings;
    const char *focus_label, *focus_value, *help[2];
};

void n3ds_controls_initialize(void);
void n3ds_controls_open(int from_main);
int n3ds_controls_active(void);
void n3ds_controls_widget_deleted(const void *widget);
void n3ds_controls_filter(struct native_input_sample *sample);
unsigned int n3ds_controls_menu_buttons(unsigned int held);
unsigned int n3ds_controls_gameplay_buttons(unsigned int held,int original);
const char *n3ds_controls_action_button_name(int action,int original);
float n3ds_controls_sensitivity(int touch);
short n3ds_controls_cstick_deadzone(void);
int n3ds_controls_invert_y(int touch);
int n3ds_controls_touch_aim_enabled(int original);
void n3ds_controls_draw(void);
/* Tests restore runtime preferences without writing the user's file. */
void n3ds_controls_defaults(struct native_control_settings *settings);
void n3ds_controls_get(struct native_control_settings *settings);
void n3ds_controls_set(const struct native_control_settings *settings);
const void *n3ds_ui_controls_root(void);
int n3ds_ui_controls_shortcut_active(void);
void n3ds_gpu_controls_draw(const struct native_controls_view *view);
void n3ds_gpu_controls_entry(void);
#endif
