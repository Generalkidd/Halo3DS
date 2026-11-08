/* Built-in 3DS-family controller, exposed through Halo's original gamepad ABI.
 * There are no detachable controllers, Xbox memory units, keyboard, mouse, or
 * rumble motors. No Xbox service shims or input worker thread are necessary. */
#include "cseries.h"
#include "input/input.h"
#include "engine_input.h"
#include "engine_controls.h"

static struct gamepad_state gamepad, neutral;
static struct native_input_sample last_sample;
#ifdef HALO_N3DS_GPU_PROBE
unsigned int n3ds_input_last_held(void) { return last_sample.held; }
#endif
static boolean initialized, active, suppressed;
static boolean touch_was_down;
static boolean touch_aiming;
static boolean touch_reload;
static struct {
    long long down_at,first_release;
    unsigned short x,y,first_x,first_y;
    boolean tracking,pending,second;
} touch_jump;
static long long tap_hold_limit,tap_gap_limit;
long long n3ds_engine_ticks(void),n3ds_engine_tick_frequency(void);
static int original_model,gameplay_controls,sampled_gameplay_controls;
static unsigned int previous_buttons,blocked_buttons;
static const unsigned int remappable_buttons=NATIVE_KEY_A|NATIVE_KEY_B|NATIVE_KEY_X|NATIVE_KEY_Y|
    NATIVE_KEY_L|NATIVE_KEY_R|NATIVE_KEY_ZL|NATIVE_KEY_ZR|
    NATIVE_KEY_DUP|NATIVE_KEY_DDOWN|NATIVE_KEY_DLEFT|NATIVE_KEY_DRIGHT;
void n3ds_log(const char *message);
void n3ds_input_cancel_touch_actions(void)
{
    touch_jump.tracking=touch_jump.pending=FALSE;
    touch_reload=FALSE;
}
void n3ds_input_gameplay_controls(int enabled)
{
    if(!enabled && gameplay_controls) n3ds_input_cancel_touch_actions();
    gameplay_controls=!!enabled;
}
int n3ds_input_gameplay_controls_active(void) { return gameplay_controls; }
int n3ds_input_touch_reload_pressed(void) { return touch_reload && active && !suppressed && gameplay_controls; }
static unsigned short touch_last_x,touch_last_y;
/* Small drags respond sooner; player_control separately lifts the stick-rate
 * ceiling for touch only, before zoom/stun scaling. */
enum { TOUCH_AIM_GAIN=8192, DEFAULT_STICK_DEADZONE=9000, TAP_MOTION_PIXELS=8, TAP_GAP_PIXELS=24 };
static point2d cstick_default, cstick_aim, touch_look, combined_aim;
static unsigned int touch_motion, cstick_motion;
void n3ds_input_resolve_aim_sources(unsigned int cstick_look_axes)
{
    cstick_motion=((cstick_aim.x?NATIVE_AIM_X:0)|(cstick_aim.y?NATIVE_AIM_Y:0)) & cstick_look_axes;
    touch_motion=((touch_look.x?NATIVE_AIM_X:0)|(touch_look.y?NATIVE_AIM_Y:0)) & ~cstick_motion;
}
float n3ds_input_touch_look_scale(int component)
{
    if(component<0 || component>1 || !active || suppressed)return 1.f;
    unsigned int axis=1u<<component;
    return touch_aiming && (touch_motion&axis) ? 1.65f*n3ds_controls_sensitivity(1) :
        (cstick_motion&axis)?n3ds_controls_sensitivity(0):1.f;
}
void n3ds_input_player_aim(unsigned int cstick_look_axes,point2d *right,point2d *look,point2d *touch)
{
    *right=*look=*touch=(point2d){0};
    if(!active || suppressed)return;
    n3ds_input_resolve_aim_sources(cstick_look_axes);
    if(cstick_look_axes==NATIVE_AIM_BOTH) *right=*look=combined_aim;
    else {
        *right=*look=cstick_default;
        if(cstick_look_axes&NATIVE_AIM_X) look->x=cstick_aim.x;
        if(cstick_look_axes&NATIVE_AIM_Y) look->y=cstick_aim.y;
        *touch=touch_look;
    }
}
static unsigned int (*touch_handler)(unsigned int,unsigned int);
void n3ds_input_touch_handler(unsigned int (*handler)(unsigned int,unsigned int))
{
    if(handler!=touch_handler) {
        touch_aiming=FALSE;
        n3ds_input_cancel_touch_actions();
    }
    touch_handler=handler;
}
static int touch_near(int x,int y,int origin_x,int origin_y,int radius)
{
    int dx=x-origin_x,dy=y-origin_y;
    return dx*dx+dy*dy<=radius*radius;
}
static void touch_jump_begin(unsigned short x,unsigned short y)
{
    touch_jump.down_at=n3ds_engine_ticks();
    touch_jump.second=touch_jump.pending && touch_jump.down_at-touch_jump.first_release<=tap_gap_limit &&
        touch_near(x,y,touch_jump.first_x,touch_jump.first_y,TAP_GAP_PIXELS);
    touch_jump.pending=FALSE;
    touch_jump.tracking=TRUE;
    touch_jump.x=x;touch_jump.y=y;
}
static int touch_jump_release(void)
{
    touch_jump.tracking=FALSE;
    long long now=n3ds_engine_ticks();
    if(now-touch_jump.down_at>tap_hold_limit ||
       !(touch_handler(touch_jump.x,touch_jump.y)&NATIVE_TOUCH_JUMP)) return 0;
    if(touch_jump.second) return 1;
    touch_jump.first_release=now;
    touch_jump.first_x=touch_jump.x;touch_jump.first_y=touch_jump.y;
    touch_jump.pending=TRUE;
    return 0;
}
_Static_assert(sizeof(struct gamepad_state)==40, "Original gamepad ABI");

short fix_dead_zone(short value, short dead_range)
{
    assert(dead_range>=0 && dead_range<32767);
    if (value>dead_range) return (short)(((long)value-dead_range)*32767/(32767-dead_range));
    if (value<-dead_range) return (short)(((long)value+dead_range)*-32768/(-32768+dead_range));
    return 0;
}
void update_ticks(byte *ticks, boolean down)
{
    assert(ticks);
    *ticks=down ? (*ticks<255 ? *ticks+1 : 255) : 0;
}
static short axis(short raw,short deadzone)
{
    /* Initial calibration policy, adjustable after hardware testing. Apply
     * Halo's dead zone once, after scaling to its signed 16-bit stick range. */
    long value=raw;
    if (value>156) value=156;
    if (value<-156) value=-156;
    value=value*(value<0 ? 32768 : 32767)/156;
    return fix_dead_zone((short)value,deadzone);
}
static short clamp_stick(long value)
{ return value>32767?32767:value<-32768?-32768:(short)value; }
static unsigned int menu_button_mask(unsigned int held)
{
    static const unsigned int keys[NUMBER_OF_GAMEPAD_BUTTONS]={
        NATIVE_KEY_A,NATIVE_KEY_B,NATIVE_KEY_X,NATIVE_KEY_Y,0,0,
        NATIVE_KEY_ZL,NATIVE_KEY_ZR,NATIVE_KEY_DUP,NATIVE_KEY_DDOWN,
        NATIVE_KEY_DLEFT,NATIVE_KEY_DRIGHT,NATIVE_KEY_START,NATIVE_KEY_SELECT,
        NATIVE_KEY_L,NATIVE_KEY_R
    };
    unsigned int pressed=0;
    held=n3ds_controls_menu_buttons(held);
    for(unsigned int i=0;i<NUMBER_OF_GAMEPAD_BUTTONS;++i)
        if(held&keys[i]) pressed|=1u<<i;
    return pressed;
}
int halo_engine_model_controls_tests(void)
{
    const unsigned int fire=1u<<_gamepad_analog_button_right_trigger;
    const unsigned int grenade=1u<<_gamepad_analog_button_left_trigger;
    const unsigned int crouch=1u<<_gamepad_binary_button_left_thumb;
    const unsigned int zoom=1u<<_gamepad_binary_button_right_thumb;
    struct native_control_settings saved,defaults;
    n3ds_controls_initialize();
    n3ds_controls_get(&saved);
    n3ds_controls_defaults(&defaults);
    n3ds_controls_set(&defaults);
    int ok=n3ds_controls_gameplay_buttons(NATIVE_KEY_L,1)==fire &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_R,1)==grenade &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_DUP,1)==zoom &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_DDOWN,1)==crouch &&
           !n3ds_controls_gameplay_buttons(NATIVE_KEY_ZL|NATIVE_KEY_ZR,1) &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_L,0)==fire &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_ZL,0)==grenade &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_R,0)==zoom &&
           n3ds_controls_gameplay_buttons(NATIVE_KEY_ZR,0)==crouch;
    n3ds_controls_set(&saved);
    if(!ok) return 1;
    n3ds_log("PASS: default New L fire/ZL grenade and Original L fire/R grenade, with model-specific zoom and crouch");
    return 0;
}
void n3ds_engine_input_sample(const struct native_input_sample *sample)
{
    unsigned int i, pressed=0;
    assert(initialized && sample);
    last_sample=*sample;
    unsigned int physical_buttons=sample->held&remappable_buttons;
    struct native_input_sample filtered=*sample;n3ds_controls_filter(&filtered);sample=&filtered;
    touch_motion=cstick_motion=0;
    boolean touching=(sample->held&NATIVE_KEY_TOUCH)!=0;
    boolean touch_pressed=touching && !touch_was_down;
    touch_was_down=touching;
    touch_reload=FALSE;
    if (!active) {
        touch_aiming=FALSE;
        n3ds_input_cancel_touch_actions();
        memset(&gamepad,0,sizeof(gamepad));
        return;
    }
    if(gameplay_controls!=sampled_gameplay_controls) {
        /* A button held to leave a menu must not trigger its gameplay action.
         * Also start a fresh virtual hold for a different physical button that
         * presses the same slot on this frame. START and SELECT stay reserved. */
        blocked_buttons|=previous_buttons;
        for(i=0;i<NUMBER_OF_GAMEPAD_BUTTONS;++i)
            if(i!=_gamepad_binary_button_start && i!=_gamepad_binary_button_back)
                gamepad.buttons[i]=0;
        sampled_gameplay_controls=gameplay_controls;
    }
    blocked_buttons&=physical_buttons;
    previous_buttons=physical_buttons;
    unsigned int held=sample->held&~blocked_buttons;
    pressed=gameplay_controls?n3ds_controls_gameplay_buttons(held,original_model):menu_button_mask(held);
    /* A press must start on a visible, enabled control. Holding or dragging
     * never repeats a toggle, including while input is deactivated. */
    long touch_dx=0,touch_dy=0;
    int touch_aim_enabled=n3ds_controls_touch_aim_enabled(original_model);
    if(!touching || suppressed || !touch_aim_enabled) touch_aiming=FALSE;
    if(touch_pressed) {
        unsigned int action=!suppressed && touch_handler && sample->touch_x<320 && sample->touch_y<240 ?
            touch_handler(sample->touch_x,sample->touch_y):0;
        if(action&NATIVE_TOUCH_GRENADE) pressed|=1u<<_gamepad_analog_button_black;
        if(action&NATIVE_TOUCH_FLASHLIGHT) pressed|=1u<<_gamepad_analog_button_white;
        if(gameplay_controls && (action&NATIVE_TOUCH_JUMP)) touch_jump_begin(sample->touch_x,sample->touch_y);
        else n3ds_input_cancel_touch_actions();
        touch_reload=gameplay_controls && (action&NATIVE_TOUCH_RELOAD);
        touch_aiming=touch_aim_enabled && (action&NATIVE_TOUCH_AIM) && sample->touch_y<NATIVE_TOUCH_AIM_BOTTOM;
        touch_last_x=sample->touch_x;touch_last_y=sample->touch_y;
    } else if(touching && (touch_aiming || touch_jump.tracking)) {
        /* Cancel until release on leaving the aim area or entering a menu.
         * Crossing a button cannot trigger it or resume aiming. */
        unsigned int action=sample->touch_x<320 && sample->touch_y<NATIVE_TOUCH_AIM_BOTTOM ?
            touch_handler(sample->touch_x,sample->touch_y):0;
        if(!(action&NATIVE_TOUCH_JUMP) ||
           !touch_near(sample->touch_x,sample->touch_y,touch_jump.x,touch_jump.y,TAP_MOTION_PIXELS))
            touch_jump.tracking=FALSE;
        if(!(action&NATIVE_TOUCH_AIM)) touch_aiming=FALSE;
        if(touch_aiming) {
            touch_dx=((long)sample->touch_x-touch_last_x)*TOUCH_AIM_GAIN;
            touch_dy=((long)touch_last_y-sample->touch_y)*TOUCH_AIM_GAIN;
            if(n3ds_controls_invert_y(1)) touch_dy=-touch_dy;
            touch_last_x=sample->touch_x;touch_last_y=sample->touch_y;
        }
    } else if(!touching && touch_jump.tracking && touch_jump_release())
        pressed|=1u<<_gamepad_analog_button_a;
    for (i=0;i<NUMBER_OF_GAMEPAD_BUTTONS;++i) {
        boolean down=(pressed&(1u<<i))!=0;
        update_ticks(&gamepad.buttons[i],down);
        if (i<NUMBER_OF_GAMEPAD_ANALOG_BUTTONS) {
            gamepad.analog_buttons[i]=down ? 255 : 0;
            /* Digital hardware only: original hysteresis settles at 223 while
             * held and 64 after release; these thresholds retain that ABI. */
            if (down) gamepad.analog_button_thresholds[i]=223;
            else if (gamepad.analog_button_thresholds[i]>64) gamepad.analog_button_thresholds[i]=64;
        }
    }
    short deadzone=n3ds_controls_cstick_deadzone();
    short cx=axis(sample->cstick_x,deadzone),cy=axis(sample->cstick_y,deadzone);
    long aim_y=n3ds_controls_invert_y(0)?-(long)cy:cy;
    gamepad.sticks[0].x=axis(sample->circle_x,DEFAULT_STICK_DEADZONE);
    gamepad.sticks[0].y=axis(sample->circle_y,DEFAULT_STICK_DEADZONE);
    cstick_default=deadzone==DEFAULT_STICK_DEADZONE?(point2d){.x=cx,.y=cy}:(point2d){
        .x=axis(sample->cstick_x,DEFAULT_STICK_DEADZONE),.y=axis(sample->cstick_y,DEFAULT_STICK_DEADZONE)
    };
    cstick_aim=(point2d){.x=cx,.y=clamp_stick(aim_y)};
    touch_look=(point2d){.x=clamp_stick(touch_dx),.y=clamp_stick(touch_dy)};
    combined_aim=(point2d){.x=clamp_stick(cx+touch_dx),.y=clamp_stick(aim_y+touch_dy)};
    n3ds_input_resolve_aim_sources(NATIVE_AIM_BOTH);
    if(gameplay_controls) gamepad.sticks[1]=combined_aim;
    else gamepad.sticks[1]=(point2d){.x=clamp_stick(cstick_default.x+touch_dx),.y=clamp_stick(cstick_default.y+touch_dy)};
}
boolean input_initialize(void)
{
    assert(!initialized);
    if (!n3ds_input_platform_initialize()) return FALSE;
    original_model=n3ds_input_platform_original_model();gameplay_controls=sampled_gameplay_controls=0;
    previous_buttons=blocked_buttons=0;
    n3ds_controls_initialize();
    n3ds_log(original_model?"CONTROLS: Original model layout loaded":"CONTROLS: New model layout loaded");
    memset(&gamepad,0,sizeof(gamepad)); memset(&neutral,0,sizeof(neutral));
    memset(&last_sample,0,sizeof(last_sample));
    cstick_default=cstick_aim=touch_look=combined_aim=(point2d){0};
    touch_motion=cstick_motion=0;
    initialized=active=TRUE; suppressed=FALSE;
    touch_was_down=touch_aiming=FALSE;touch_handler=NULL;
    n3ds_input_cancel_touch_actions();
    long long frequency=n3ds_engine_tick_frequency();
    tap_hold_limit=frequency/5;
    tap_gap_limit=frequency*7/20;
    return TRUE;
}
void input_dispose(void)
{
    if (initialized) n3ds_input_platform_dispose();
    initialized=active=suppressed=FALSE;
    gameplay_controls=0;
    touch_was_down=touch_aiming=FALSE;touch_handler=NULL;
    n3ds_input_cancel_touch_actions();
    memset(&gamepad,0,sizeof(gamepad)); memset(&last_sample,0,sizeof(last_sample));
}
void input_frame_begin(void)
{
    struct native_input_sample sample;
    assert(initialized);
    n3ds_input_platform_poll(&sample);
    n3ds_engine_input_sample(&sample);
}
/* The damaged-media path can end input outside a normal frame. Original Xbox
 * code only clears a flag used by its worker; native polling has no worker to
 * release, and ending a frame must remain idempotent. */
void input_frame_end(void) { assert(initialized); }
/* Original main samples at frame_begin, then update clears per-frame suppression.
 * Polling here too would double the held duration and lose first-press events. */
void input_update(void) { assert(initialized); suppressed=FALSE; }
void input_suppress(void) { suppressed=TRUE;touch_aiming=FALSE;n3ds_input_cancel_touch_actions(); }
void input_activate(void) { active=TRUE; }
void input_deactivate(void)
{
    active=FALSE;touch_aiming=FALSE;
    n3ds_input_cancel_touch_actions();
    sampled_gameplay_controls=gameplay_controls;
    previous_buttons=blocked_buttons=0;
    cstick_default=cstick_aim=touch_look=combined_aim=(point2d){0};
    touch_motion=cstick_motion=0;
    memset(&gamepad,0,sizeof(gamepad));
}
boolean input_has_gamepad(short index)
{
    assert(index>=0 && index<MAXIMUM_GAMEPADS);
    return initialized && index==0;
}
const struct gamepad_state *input_get_gamepad_state(short index)
{
    return input_has_gamepad(index) ? (suppressed || !active ? &neutral : &gamepad) : NULL;
}
/* Absent hardware is reported as absent, not as a successful emulated device. */
boolean input_key_is_down(short key) { assert(key>=0 && key<=_key_alt); return FALSE; }
boolean input_get_key(struct key_stroke *key) { assert(key); return FALSE; }
const struct mouse_state *input_get_mouse_state(void) { return NULL; }
boolean input_mouse_button_is_down(short button) { (void)button; return FALSE; }
void input_flush(void) { /* No keyboard event queue on this platform. */ }
void input_set_gamepad_rumbler_state(short index, word left, word right)
{
    assert(index>=0 && index<MAXIMUM_GAMEPADS);
    (void)left; (void)right; /* New 3DS has no rumble actuator. */
}
void input_vertical_blank_interrupt(void) { /* Input is polled on the game thread. */ }
void input_get_raw_data_string(char *buffer, short size)
{
    assert(buffer && size>0);
    snprintf(buffer,size,"3DS: Circle (%d,%d) C-stick (%d,%d)",
        last_sample.circle_x,last_sample.circle_y,last_sample.cstick_x,last_sample.cstick_y);
    buffer[size-1]=0;
}
