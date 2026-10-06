/* Built-in 3DS-family controller, exposed through Halo's original gamepad ABI.
 * There are no detachable controllers, Xbox memory units, keyboard, mouse, or
 * rumble motors. No Xbox service shims or input worker thread are necessary. */
#include "cseries.h"
#include "input/input.h"
#include "engine_input.h"
#include "engine_controls.h"

static struct gamepad_state gamepad, neutral;
static struct native_input_sample last_sample;
static boolean initialized, active, suppressed;
static boolean touch_was_down;
static boolean touch_aiming;
static int original_model,gameplay_controls;
void n3ds_log(const char *message);
void n3ds_input_gameplay_controls(int enabled) { gameplay_controls=!!enabled; }
static unsigned short touch_last_x,touch_last_y;
/* Small drags respond sooner; player_control separately lifts the stick-rate
 * ceiling for touch only, before zoom/stun scaling. */
enum { TOUCH_AIM_GAIN = 8192 };
static int touch_motion[2];
float n3ds_input_touch_look_scale(int component)
{
    if(component<0 || component>1 || !active || suppressed)return 1.f;
    return touch_aiming && touch_motion[component] ? 1.65f*n3ds_controls_sensitivity(1) :
        (component?gamepad.sticks[1].y:gamepad.sticks[1].x)?n3ds_controls_sensitivity(0):1.f;
}
static unsigned int (*touch_handler)(unsigned int,unsigned int);
void n3ds_input_touch_handler(unsigned int (*handler)(unsigned int,unsigned int))
{ if(handler!=touch_handler) touch_aiming=FALSE;touch_handler=handler; }
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
static short axis(short raw)
{
    /* Initial calibration policy, adjustable after hardware testing. Apply
     * Halo's dead zone once, after scaling to its signed 16-bit stick range. */
    long value=raw;
    if (value>156) value=156;
    if (value<-156) value=-156;
    value=value*(value<0 ? 32768 : 32767)/156;
    return fix_dead_zone((short)value,9000);
}
static short clamp_stick(long value)
{ return value>32767?32767:value<-32768?-32768:(short)value; }
static unsigned int button_mask(unsigned int held,int original,int gameplay)
{
    static const unsigned int keys[NUMBER_OF_GAMEPAD_BUTTONS]={
        NATIVE_KEY_A,NATIVE_KEY_B,NATIVE_KEY_X,NATIVE_KEY_Y,0,0,
        NATIVE_KEY_ZL,NATIVE_KEY_ZR,NATIVE_KEY_DUP,NATIVE_KEY_DDOWN,
        NATIVE_KEY_DLEFT,NATIVE_KEY_DRIGHT,NATIVE_KEY_START,NATIVE_KEY_SELECT,
        NATIVE_KEY_L,NATIVE_KEY_R
    };
    unsigned int pressed=0;
    for(unsigned int i=0;i<NUMBER_OF_GAMEPAD_BUTTONS;++i) {
        unsigned int key=keys[i];
        if(original && gameplay) switch(i) {
        case _gamepad_analog_button_left_trigger: key=NATIVE_KEY_R;break;
        case _gamepad_analog_button_right_trigger: key=NATIVE_KEY_L;break;
        case _gamepad_binary_button_left_thumb: key=NATIVE_KEY_DDOWN;break;
        case _gamepad_binary_button_right_thumb: key=NATIVE_KEY_DUP;break;
        /* Up/Down must not also move the player when crouching or zooming. */
        case _gamepad_binary_button_dpad_up:
        case _gamepad_binary_button_dpad_down: key=0;break;
        }
        if(held&key) pressed|=1u<<i;
    }
    return pressed;
}
int halo_engine_model_controls_tests(void)
{
    const unsigned int up=1u<<_gamepad_binary_button_dpad_up;
    const unsigned int down=1u<<_gamepad_binary_button_dpad_down;
    const unsigned int fire=1u<<_gamepad_analog_button_right_trigger;
    const unsigned int grenade=1u<<_gamepad_analog_button_left_trigger;
    const unsigned int crouch=1u<<_gamepad_binary_button_left_thumb;
    const unsigned int zoom=1u<<_gamepad_binary_button_right_thumb;
    if(button_mask(NATIVE_KEY_L,1,1)!=fire || button_mask(NATIVE_KEY_R,1,1)!=grenade ||
       button_mask(NATIVE_KEY_DUP,1,1)!=zoom || button_mask(NATIVE_KEY_DDOWN,1,1)!=crouch ||
       button_mask(NATIVE_KEY_DUP|NATIVE_KEY_DDOWN,1,0)!=(up|down) ||
       button_mask(NATIVE_KEY_L|NATIVE_KEY_R,1,0)!=(crouch|zoom) ||
       button_mask(NATIVE_KEY_ZL|NATIVE_KEY_ZR,1,1)) return 1;
    for(unsigned int i=0;i<21;++i) {
        unsigned int key=1u<<i;
        if(button_mask(key,0,0)!=button_mask(key,0,1) ||
           button_mask(key,0,0)!=button_mask(key,1,0)) return 1;
    }
    if(button_mask(NATIVE_KEY_ZR,0,1)!=fire || button_mask(NATIVE_KEY_ZL,0,1)!=grenade ||
       button_mask(NATIVE_KEY_L,0,1)!=crouch || button_mask(NATIVE_KEY_R,0,1)!=zoom) return 1;
    n3ds_log("PASS: original-model L fire/R grenade/Up zoom/Down crouch; no Up/Down movement; menu navigation and New-model mapping preserved");
    return 0;
}
void n3ds_engine_input_sample(const struct native_input_sample *sample)
{
    unsigned int i, pressed=0;
    assert(initialized && sample);
    last_sample=*sample;
    struct native_input_sample filtered=*sample;n3ds_controls_filter(&filtered);sample=&filtered;
    touch_motion[0]=touch_motion[1]=0;
    boolean touching=(sample->held&NATIVE_KEY_TOUCH)!=0;
    boolean touch_pressed=touching && !touch_was_down;
    touch_was_down=touching;
    if (!active) { touch_aiming=FALSE;memset(&gamepad,0,sizeof(gamepad)); return; }
    pressed=button_mask(n3ds_controls_buttons(sample->held,original_model,gameplay_controls),original_model,gameplay_controls);
    /* A press must start on a visible, enabled control. Holding or dragging
     * never repeats a toggle, including while input is deactivated. */
    long touch_dx=0,touch_dy=0;
    if(!touching || suppressed) touch_aiming=FALSE;
    if(touch_pressed && !suppressed && touch_handler && sample->touch_x<320 && sample->touch_y<240) {
        unsigned int action=touch_handler(sample->touch_x,sample->touch_y);
        if(action&NATIVE_TOUCH_GRENADE) pressed|=1u<<_gamepad_analog_button_black;
        if(action&NATIVE_TOUCH_FLASHLIGHT) pressed|=1u<<_gamepad_analog_button_white;
        touch_aiming=(action&NATIVE_TOUCH_AIM) && sample->touch_y<NATIVE_TOUCH_AIM_BOTTOM;
        touch_last_x=sample->touch_x;touch_last_y=sample->touch_y;
    } else if(touch_aiming) {
        /* Cancel until release on leaving the aim area or entering a menu.
         * Crossing the button row cannot trigger a button or resume aiming. */
        if(sample->touch_x>=320 || sample->touch_y>=NATIVE_TOUCH_AIM_BOTTOM ||
           !touch_handler || !(touch_handler(sample->touch_x,sample->touch_y)&NATIVE_TOUCH_AIM)) touch_aiming=FALSE;
        else {
            touch_dx=((long)sample->touch_x-touch_last_x)*TOUCH_AIM_GAIN;
            touch_dy=((long)touch_last_y-sample->touch_y)*TOUCH_AIM_GAIN;
            touch_last_x=sample->touch_x;touch_last_y=sample->touch_y;
        }
    }
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
    gamepad.sticks[0].x=axis(sample->circle_x); gamepad.sticks[0].y=axis(sample->circle_y);
    touch_motion[0]=touch_dx && !axis(sample->cstick_x);
    touch_motion[1]=touch_dy && !axis(sample->cstick_y);
    gamepad.sticks[1].x=clamp_stick((long)axis(sample->cstick_x)+touch_dx);
    gamepad.sticks[1].y=clamp_stick((long)axis(sample->cstick_y)+touch_dy);
}
boolean input_initialize(void)
{
    assert(!initialized);
    if (!n3ds_input_platform_initialize()) return FALSE;
    original_model=n3ds_input_platform_original_model();gameplay_controls=0;
    n3ds_controls_initialize();
    n3ds_log(original_model?"CONTROLS: original model; gameplay L=fire R=grenade Up=zoom Down=crouch":"CONTROLS: New model; existing ZR/ZL/L/R layout retained");
    memset(&gamepad,0,sizeof(gamepad)); memset(&neutral,0,sizeof(neutral));
    memset(&last_sample,0,sizeof(last_sample));
    initialized=active=TRUE; suppressed=FALSE;
    touch_was_down=touch_aiming=FALSE;touch_handler=NULL;
    return TRUE;
}
void input_dispose(void)
{
    if (initialized) n3ds_input_platform_dispose();
    initialized=active=suppressed=FALSE;
    gameplay_controls=0;
    touch_was_down=touch_aiming=FALSE;touch_handler=NULL;
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
void input_suppress(void) { suppressed=TRUE;touch_aiming=FALSE; }
void input_activate(void) { active=TRUE; }
void input_deactivate(void) { active=FALSE;touch_aiming=FALSE; memset(&gamepad,0,sizeof(gamepad)); }
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
