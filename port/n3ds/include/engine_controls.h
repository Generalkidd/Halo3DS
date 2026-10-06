#ifndef HALO_N3DS_ENGINE_CONTROLS_H
#define HALO_N3DS_ENGINE_CONTROLS_H
#include "engine_input.h"
struct native_control_settings { unsigned int cstick, touch, xbox_buttons, swap_shoulders; };
void n3ds_controls_initialize(void);
void n3ds_controls_open(int from_main);
int n3ds_controls_active(void);
void n3ds_controls_filter(struct native_input_sample *sample);
unsigned int n3ds_controls_buttons(unsigned int held,int original,int gameplay);
float n3ds_controls_sensitivity(int touch);
void n3ds_controls_draw(void);
/* Tests restore runtime preferences without writing the user's file. */
void n3ds_controls_get(struct native_control_settings *settings);
void n3ds_controls_set(const struct native_control_settings *settings);
int n3ds_controls_tests(void);
int n3ds_ui_pause_menu_active(void);
void n3ds_gpu_controls_draw(const struct native_control_settings *settings,int selected,int original,int save_error);
void n3ds_gpu_controls_entry(void);
#endif
