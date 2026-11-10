/* New3DS integration additions by Kevin Boggs. */
#include "cseries.h"
#include "input/input.h"
#include "engine_input.h"
#include "engine_controls.h"
#include "interface/event_manager.h"
void n3ds_log(const char *message);
static unsigned int test_touch(unsigned int x,unsigned int y)
{ return y>=176 && y<232 && x>=236 && x<312?2:0; }
/* Kevin Boggs: test_score_touch - New3DS integration. */
static unsigned int test_score_touch(unsigned int x,unsigned int y)
{ return x>=240 && x<312 && y>=116 && y<168?NATIVE_TOUCH_SCOREBOARD:0; }
/* Kevin Boggs: test_stub_touch - New3DS integration. */
static unsigned int test_stub_touch(unsigned int x,unsigned int y)
{
    if(x>=8 && x<80 && y>=36 && y<94)return NATIVE_TOUCH_STUB_1;
    if(x>=240 && x<312 && y>=48 && y<100)return NATIVE_TOUCH_STUB_2;
    if(x>=8 && x<72 && y>=116 && y<168)return NATIVE_TOUCH_STUB_3;
    if(x>=240 && x<312 && y>=116 && y<168)return NATIVE_TOUCH_STUB_4;
    return 0;
}

static int aim_available,aim_hit_tests;
/* Kevin Boggs: test_aim_enabled - Live camera gate independent of touch location. */
static int test_aim_enabled(void) { return aim_available; }
static unsigned int test_aim_touch(unsigned int x,unsigned int y)
{
    ++aim_hit_tests;
    if(!aim_available || x>=320) return 0;
    unsigned int panel=test_stub_touch(x,y);
    if(panel)return panel==NATIVE_TOUCH_STUB_4?NATIVE_TOUCH_SCOREBOARD:panel;
    if(y>=184 && y<236 && x>=8 && x<208)return NATIVE_TOUCH_GRENADE;
    if(y<NATIVE_TOUCH_AIM_BOTTOM) return NATIVE_TOUCH_AIM;
    if(y>=184 && y<236 && x>=216 && x<312) return NATIVE_TOUCH_FLASHLIGHT;
    return 0;
}
int halo_engine_touch_aim_tests(void)
{
    n3ds_controls_initialize();struct native_control_settings saved,defaults={100,100,0,0,0,0,0,1,0};n3ds_controls_get(&saved);n3ds_controls_set(&defaults);
    struct native_input_sample s={0};const struct gamepad_state *pad;
#define AIM_CHECK(e) do {if(!(e)){n3ds_log("TOUCH AIM FAIL: " #e);input_dispose();n3ds_controls_set(&saved);return 1;}}while(0)
#define SAMPLE() n3ds_engine_input_sample(&s)
#define RELEASE() do {s.held=0;SAMPLE();s.held=NATIVE_KEY_TOUCH;}while(0)
    AIM_CHECK(input_initialize());pad=input_get_gamepad_state(0);
    aim_available=1;n3ds_input_touch_handler(test_aim_touch,test_aim_enabled);
    s.held=NATIVE_KEY_TOUCH;s.touch_x=160;s.touch_y=100;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x && !pad->sticks[1].y);
    aim_hit_tests=0;
    s.touch_x=162;s.touch_y=99;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==16384 && pad->sticks[1].y==8192);
    AIM_CHECK(n3ds_input_touch_look_scale(0)==1.65f && n3ds_input_touch_look_scale(1)==1.65f);
    SAMPLE();AIM_CHECK(!pad->sticks[1].x && !pad->sticks[1].y);
    AIM_CHECK(n3ds_input_touch_look_scale(0)==1.f && n3ds_input_touch_look_scale(1)==1.f);
    s.touch_x=160;s.touch_y=101;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==-16384 && pad->sticks[1].y==-16384);
    s.touch_x=319;s.touch_y=0;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==32767 && pad->sticks[1].y==32767);
    s.touch_x=0;s.touch_y=175;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==-32768 && pad->sticks[1].y==-32768);
    s.touch_x=270;s.touch_y=200;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==32767 && pad->sticks[1].y==-32768 && !pad->buttons[5]);
    /* Cross every new panel, both grenades, and all four LCD corners. */
    static const unsigned short drag[][2]={{40,65},{270,74},{40,140},{270,140},
        {40,200},{150,200},{319,239},{0,239},{0,0},{319,0},{160,100}};
    for(unsigned int point=0;point<sizeof(drag)/sizeof(drag[0]);++point) {
        s.touch_x=drag[point][0];s.touch_y=drag[point][1];SAMPLE();
        AIM_CHECK(pad->sticks[1].x || pad->sticks[1].y);
        for(int button=0;button<NUMBER_OF_GAMEPAD_BUTTONS;++button) AIM_CHECK(!pad->buttons[button]);
        AIM_CHECK(!n3ds_input_touch_panel_pressed());
    }
    AIM_CHECK(!aim_hit_tests);
    s.touch_x=100;s.touch_y=100;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==-32768 && !pad->sticks[1].y && !pad->buttons[5]);
    RELEASE();s.touch_x=270;s.touch_y=200;SAMPLE();AIM_CHECK(pad->buttons[5]==1);
    SAMPLE();AIM_CHECK(!pad->buttons[5]);
    s.touch_x=100;s.touch_y=100;SAMPLE();AIM_CHECK(!pad->sticks[1].x && !pad->buttons[5]);
    RELEASE();SAMPLE();s.touch_x=105;SAMPLE();AIM_CHECK(pad->sticks[1].x==32767);
    aim_available=0;s.touch_x=110;SAMPLE();AIM_CHECK(!pad->sticks[1].x);
    aim_available=1;s.touch_x=115;SAMPLE();AIM_CHECK(!pad->sticks[1].x);
    RELEASE();aim_available=0;SAMPLE();aim_available=1;s.touch_x=120;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x);
    RELEASE();SAMPLE();input_suppress();input_update();s.touch_x=125;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x);
    RELEASE();SAMPLE();input_deactivate();input_activate();s.touch_x=130;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x);
    RELEASE();SAMPLE();s.touch_y=176;SAMPLE();AIM_CHECK(pad->sticks[1].y==-32768);
    s.touch_y=239;SAMPLE();AIM_CHECK(pad->sticks[1].y==-32768);
    s.touch_y=240;SAMPLE();s.touch_y=100;SAMPLE();AIM_CHECK(!pad->sticks[1].y);
    RELEASE();SAMPLE();n3ds_input_touch_handler(NULL,NULL);s.touch_x=131;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x);n3ds_input_touch_handler(test_aim_touch,test_aim_enabled);
    s.touch_x=130;SAMPLE();AIM_CHECK(!pad->sticks[1].x);
    RELEASE();SAMPLE();s.touch_x=320;SAMPLE();s.touch_x=130;SAMPLE();
    AIM_CHECK(!pad->sticks[1].x);
    RELEASE();SAMPLE();s.cstick_x=156;s.touch_x=140;SAMPLE();
    AIM_CHECK(pad->sticks[1].x==32767);
    AIM_CHECK(n3ds_input_touch_look_scale(0)==1.f);
    s.held=0;SAMPLE();AIM_CHECK(pad->sticks[1].x==32767);
    s.cstick_x=0;SAMPLE();AIM_CHECK(!pad->sticks[1].x && !pad->sticks[1].y);
    n3ds_input_touch_handler(test_score_touch,NULL);
    s.held=0;SAMPLE();s.held=NATIVE_KEY_TOUCH;s.touch_x=260;s.touch_y=150;SAMPLE();
    AIM_CHECK(pad->buttons[_gamepad_binary_button_back]==1);
    SAMPLE();AIM_CHECK(pad->buttons[_gamepad_binary_button_back]==2);
    s.touch_x=100;SAMPLE();AIM_CHECK(!pad->buttons[_gamepad_binary_button_back]);
    s.held=0;SAMPLE();s.held=NATIVE_KEY_TOUCH;s.touch_x=260;SAMPLE();
    AIM_CHECK(pad->buttons[_gamepad_binary_button_back]==1);
    AIM_CHECK(n3ds_input_touch_panel_pressed()==4);
    s.held=0;SAMPLE();AIM_CHECK(!pad->buttons[_gamepad_binary_button_back]);
    AIM_CHECK(!n3ds_input_touch_panel_pressed());
    n3ds_input_touch_handler(test_stub_touch,NULL);
    static const unsigned int px[]={40,270,40,270},py[]={65,74,140,140};
    for(int panel=1;panel<=4;++panel){
        s.held=0;SAMPLE();s.held=NATIVE_KEY_TOUCH;s.touch_x=px[panel-1];s.touch_y=py[panel-1];SAMPLE();
        AIM_CHECK(n3ds_input_touch_panel_pressed()==panel);
        AIM_CHECK(!pad->buttons[_gamepad_binary_button_back] && !pad->buttons[_gamepad_analog_button_a]);
        SAMPLE();AIM_CHECK(n3ds_input_touch_panel_pressed()==panel);
        s.touch_x=160;s.touch_y=100;SAMPLE();AIM_CHECK(!n3ds_input_touch_panel_pressed());
        s.touch_x=px[panel-1];s.touch_y=py[panel-1];SAMPLE();AIM_CHECK(!n3ds_input_touch_panel_pressed());
    }
    s.held=0;SAMPLE();AIM_CHECK(!n3ds_input_touch_panel_pressed());
    input_dispose();
    n3ds_log("PASS: full-screen captured touch aiming, no button hit tests during drag, direction, scoreboard and stub feedback hold/release, C-stick coexistence, button isolation and gameplay cancellation");
    n3ds_controls_set(&saved);return 0;
#undef RELEASE
#undef SAMPLE
#undef AIM_CHECK
}

int halo_engine_input_tests(void)
{
    n3ds_controls_initialize();struct native_control_settings saved,defaults={100,100,0,0,0,0,0,1,0};n3ds_controls_get(&saved);n3ds_controls_set(&defaults);
    static const struct { unsigned int key; short button; } cases[]={
        {NATIVE_KEY_A,_gamepad_analog_button_a},{NATIVE_KEY_B,_gamepad_analog_button_b},
        {NATIVE_KEY_X,_gamepad_analog_button_x},{NATIVE_KEY_Y,_gamepad_analog_button_y},
        {NATIVE_KEY_ZL,_gamepad_analog_button_left_trigger},{NATIVE_KEY_ZR,_gamepad_analog_button_right_trigger},
        {NATIVE_KEY_DUP,_gamepad_binary_button_dpad_up},{NATIVE_KEY_DDOWN,_gamepad_binary_button_dpad_down},
        {NATIVE_KEY_DLEFT,_gamepad_binary_button_dpad_left},{NATIVE_KEY_DRIGHT,_gamepad_binary_button_dpad_right},
        {NATIVE_KEY_START,_gamepad_binary_button_start},{NATIVE_KEY_SELECT,_gamepad_binary_button_back},
        {NATIVE_KEY_L,_gamepad_binary_button_left_thumb},{NATIVE_KEY_R,_gamepad_binary_button_right_thumb}
    };
    struct native_input_sample sample={0};
    const struct gamepad_state *pad;
    struct key_stroke key={0};
    struct gamepad_state empty={0};
    long i,j,raw,previous=-32768;
    char tiny[2]={'x','y'};
#define CHECK(expr) do { if (!(expr)) { n3ds_log("INPUT FAIL: " #expr); input_dispose(); n3ds_controls_set(&saved); return 1; } } while (0)
    CHECK(!input_has_gamepad(0) && input_get_gamepad_state(0)==NULL);
    CHECK(input_initialize());
    CHECK(input_has_gamepad(0));
    for (i=1;i<MAXIMUM_GAMEPADS;++i) CHECK(!input_has_gamepad(i) && !input_get_gamepad_state(i));
    for (i=0;i<NUMBEROF(cases);++i) {
        sample.held=cases[i].key;
        n3ds_engine_input_sample(&sample); pad=input_get_gamepad_state(0);
        for (j=0;j<NUMBER_OF_GAMEPAD_BUTTONS;++j) CHECK(pad->buttons[j]==(j==cases[i].button));
        if (cases[i].button<8) CHECK(pad->analog_buttons[cases[i].button]==255);
        for (j=0;j<300;++j) n3ds_engine_input_sample(&sample);
        CHECK(pad->buttons[cases[i].button]==255);
        sample.held=0; n3ds_engine_input_sample(&sample);
        CHECK(!pad->buttons[cases[i].button]);
        if (cases[i].button<8) CHECK(!pad->analog_buttons[cases[i].button]);
        sample.held=cases[i].key; n3ds_engine_input_sample(&sample);
        CHECK(pad->buttons[cases[i].button]==1);
        sample.held=0; n3ds_engine_input_sample(&sample);
    }
    sample.held=NATIVE_KEY_A|NATIVE_KEY_ZL|NATIVE_KEY_ZR|NATIVE_KEY_L;
    n3ds_engine_input_sample(&sample);
    CHECK(pad->buttons[0]==1 && pad->buttons[6]==1 && pad->buttons[7]==1 && pad->buttons[14]==1);
    /* A held resume modifier must not discard a new confirmation event. */
    {
        struct event_record event;
        event_manager_initialize();
        sample.held=0;n3ds_engine_input_sample(&sample);
        sample.held=NATIVE_KEY_R;n3ds_engine_input_sample(&sample);
        event_manager_update();
        CHECK(get_next_event(&event,0) && event.type==3 &&
            event.data.button.index==_gamepad_binary_button_right_thumb && event.data.button.value==1);
        CHECK(!get_next_event(&event,0));
        sample.held|=NATIVE_KEY_A;n3ds_engine_input_sample(&sample);
        event_manager_update();
        CHECK(get_next_event(&event,0) && event.type==3 &&
            event.data.button.index==_gamepad_analog_button_a && event.data.button.value==1);
        CHECK(get_next_event(&event,0) && event.type==3 &&
            event.data.button.index==_gamepad_binary_button_right_thumb && event.data.button.value==2);
        CHECK(!get_next_event(&event,0) && !get_next_event(&event,1));
        event_manager_suppress(TRUE);event_manager_update();
        CHECK(!get_next_event(&event,0));
        event_manager_suppress(FALSE);event_manager_update();event_manager_flush();
        CHECK(!get_next_event(&event,0));
        event_manager_dispose();
        sample.held=0;n3ds_engine_input_sample(&sample);
        sample.held=NATIVE_KEY_A|NATIVE_KEY_ZL|NATIVE_KEY_ZR|NATIVE_KEY_L;
        n3ds_engine_input_sample(&sample);
        n3ds_log("PASS: menu events preserve R + confirm order, held values, controller isolation, suppression and flush");
    }
    input_suppress(); CHECK(!memcmp(input_get_gamepad_state(0),&empty,sizeof(empty)));
    input_update(); input_update(); CHECK(input_get_gamepad_state(0)->buttons[0]==1);
    input_deactivate(); n3ds_engine_input_sample(&sample);
    CHECK(!memcmp(input_get_gamepad_state(0),&empty,sizeof(empty)));
    input_activate(); n3ds_engine_input_sample(&sample); CHECK(pad->buttons[0]==1);
    sample.held=NATIVE_KEY_TOUCH; sample.touch_x=159; sample.touch_y=239;
    n3ds_engine_input_sample(&sample); CHECK(!pad->buttons[4] && !pad->buttons[5]);
    n3ds_input_touch_handler(test_touch,NULL);
    sample.touch_x=270;sample.touch_y=200;n3ds_engine_input_sample(&sample);CHECK(!pad->buttons[5]);
    sample.held=0;n3ds_engine_input_sample(&sample);
    sample.held=NATIVE_KEY_TOUCH;n3ds_engine_input_sample(&sample);CHECK(pad->buttons[5]==1);
    for(j=0;j<10;++j) {n3ds_engine_input_sample(&sample);CHECK(!pad->buttons[5]);}
    sample.held=0;n3ds_engine_input_sample(&sample);
    input_deactivate();sample.held=NATIVE_KEY_TOUCH;n3ds_engine_input_sample(&sample);
    input_activate();n3ds_engine_input_sample(&sample);CHECK(!pad->buttons[5]);
    sample.held=0;n3ds_engine_input_sample(&sample);
    sample.held=NATIVE_KEY_TOUCH;n3ds_engine_input_sample(&sample);CHECK(pad->buttons[5]==1);
    n3ds_input_touch_handler(NULL,NULL);
    sample.touch_x=320; n3ds_engine_input_sample(&sample); CHECK(!pad->buttons[4] && !pad->buttons[5]);
    sample.touch_x=0; sample.touch_y=240; n3ds_engine_input_sample(&sample); CHECK(!pad->buttons[4]);
    sample.held=0; sample.touch_y=0; n3ds_engine_input_sample(&sample); CHECK(!pad->buttons[4]);
    /* Derived virtual circle/C-stick direction bits must not become D-pad taps. */
    sample.held=0xff000000u; n3ds_engine_input_sample(&sample);
    for (j=0;j<NUMBER_OF_GAMEPAD_BUTTONS;++j) CHECK(!pad->buttons[j]);
    sample.held=0;
    for (raw=-32768;raw<=32767;++raw) {
        sample.circle_x=sample.circle_y=sample.cstick_x=sample.cstick_y=(short)raw;
        n3ds_engine_input_sample(&sample);
        CHECK(pad->sticks[0].x>=previous);
        CHECK(pad->sticks[0].y==pad->sticks[0].x && pad->sticks[1].x==pad->sticks[0].x && pad->sticks[1].y==pad->sticks[0].x);
        if (raw<=-156) CHECK(pad->sticks[0].x==-32768);
        if (raw>=156) CHECK(pad->sticks[0].x==32767);
        if (raw>=-42 && raw<=42) CHECK(pad->sticks[0].x==0);
        if (raw==-43) CHECK(pad->sticks[0].x<0);
        if (raw==43) CHECK(pad->sticks[0].x>0);
        previous=pad->sticks[0].x;
    }
    n3ds_log("PASS: native 3DS input mapping, hold saturation, release, suppression and all 65536 axis values");
    CHECK(!input_get_mouse_state() && !input_mouse_button_is_down(0) && !input_get_key(&key) && !input_key_is_down(_key_a));
    input_flush(); input_set_gamepad_rumbler_state(0,65535,65535);
    input_get_raw_data_string(tiny,1); CHECK(tiny[0]==0 && tiny[1]=='y');
    input_dispose(); CHECK(!input_has_gamepad(0));
    CHECK(input_initialize()); CHECK(!memcmp(input_get_gamepad_state(0),&empty,sizeof(empty)));
    input_frame_end(); input_frame_end(); /* Original damaged-media transition. */
    /* Real HID/IRRST poll: no synthetic injection or user interaction. */
    input_frame_begin(); input_update(); input_frame_end();
    CHECK(input_get_gamepad_state(0)!=NULL);
    input_dispose();
    n3ds_log("PASS: native HID poll and balanced input initialization/disposal");
    n3ds_controls_set(&saved);return 0;
#undef CHECK
}
