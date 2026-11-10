/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_INPUT_H
#define HALO_N3DS_ENGINE_INPUT_H
float n3ds_input_touch_look_scale(int component);
/* Fixed-width boundary between the original engine and libctru. These are HID
 * bit positions, checked against libctru when compiling the platform adapter. */
enum {
    NATIVE_KEY_A=1u<<0, NATIVE_KEY_B=1u<<1, NATIVE_KEY_SELECT=1u<<2,
    NATIVE_KEY_START=1u<<3, NATIVE_KEY_DRIGHT=1u<<4, NATIVE_KEY_DLEFT=1u<<5,
    NATIVE_KEY_DUP=1u<<6, NATIVE_KEY_DDOWN=1u<<7, NATIVE_KEY_R=1u<<8,
    NATIVE_KEY_L=1u<<9, NATIVE_KEY_X=1u<<10, NATIVE_KEY_Y=1u<<11,
    NATIVE_KEY_ZL=1u<<14, NATIVE_KEY_ZR=1u<<15, NATIVE_KEY_TOUCH=1u<<20
};
struct native_input_sample {
    unsigned int held;
    short circle_x, circle_y, cstick_x, cstick_y;
    unsigned short touch_x, touch_y;
};
_Static_assert(sizeof(struct native_input_sample)==16, "Native input sample ABI");
int n3ds_input_platform_initialize(void);
int n3ds_input_platform_original_model(void);
/* Menus retain D-pad navigation; gameplay may consume Up/Down for actions. */
void n3ds_input_gameplay_controls(int enabled);
void n3ds_input_platform_dispose(void);
void n3ds_input_platform_poll(struct native_input_sample *sample);
/* Optional infrared right stick. No service work in poll. */
void n3ds_cpp_poll(struct native_input_sample *sample, int enabled);
void n3ds_cpp_dispose(void);
/* Used by the hardware polling path and deterministic input sequence tests. */
void n3ds_engine_input_sample(const struct native_input_sample *sample);
enum { NATIVE_TOUCH_GRENADE=1, NATIVE_TOUCH_FLASHLIGHT=2, NATIVE_TOUCH_AIM=4,
       NATIVE_TOUCH_SCOREBOARD=8,NATIVE_TOUCH_STUB_1=16,NATIVE_TOUCH_STUB_2=32,NATIVE_TOUCH_STUB_3=64,NATIVE_TOUCH_STUB_4=128,
       NATIVE_TOUCH_AIM_BOTTOM=176 };
/* The handler chooses an action at press time. Once AIM captures a drag,
 * only the side-effect-free live gameplay gate is checked; every pixel of
 * the bottom screen remains available until release or gameplay cancellation. */
void n3ds_input_touch_handler(unsigned int (*handler)(unsigned int,unsigned int), int (*aim_available)(void));
int n3ds_input_touch_panel_pressed(void);
int n3ds_input_touch_scoreboard_active(void);
int halo_engine_input_tests(void);
int halo_engine_touch_aim_tests(void);
int halo_engine_model_controls_tests(void);
#endif
