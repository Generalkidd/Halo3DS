/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_CONTROLS_H
#define HALO_N3DS_ENGINE_CONTROLS_H
#include "engine_input.h"
struct native_control_settings { unsigned int cstick, touch, xbox_buttons, swap_shoulders, high_textures, swap_xy, show_fps, decals, swap_ab; };
void n3ds_controls_initialize(void);
void n3ds_controls_open(int from_main);
int n3ds_controls_active(void);
void n3ds_controls_filter(struct native_input_sample *sample);
unsigned int n3ds_controls_buttons(unsigned int held,int original,int gameplay);
float n3ds_controls_sensitivity(int touch);
int n3ds_controls_high_textures(void);
int n3ds_controls_show_fps(void);
int n3ds_controls_decals(void);
void n3ds_controls_draw(void);
/* Tests restore runtime preferences without writing the user's file. */
void n3ds_controls_get(struct native_control_settings *settings);
void n3ds_controls_set(const struct native_control_settings *settings);
int n3ds_controls_tests(void);
int n3ds_ui_pause_menu_active(void);
void n3ds_gpu_controls_draw(const struct native_control_settings *settings,int selected,int original,int save_error);
void n3ds_gpu_controls_entry(void);
struct native_touch_meter { float fill; int state; };
void n3ds_gpu_match_touch_overlay(int fps,int show_fps,int show_score,int pressed,
    const struct native_touch_meter *meters,int meter_count,int loaded,int reserve);
struct native_score_row { char name[16]; long score; short kills,deaths; int local; };
void n3ds_gpu_match_scoreboard(const struct native_score_row *rows,int count);
void n3ds_gpu_match_postgame_lobby(void);
#ifdef HALO_N3DS_PC_LOCAL_MENU
void n3ds_pc_local_menu_filter(struct native_input_sample *sample);
void n3ds_pc_local_menu_draw(void);
void n3ds_pc_local_menu_queue_reopen(void);
int n3ds_pc_local_menu_active(void);
void n3ds_gpu_pc_local_menu(int map,int mode,int bots,int difficulty,int weapon_set,int vehicle_choice,int page,int field,int error,int high_textures,int original);
#endif
#endif
