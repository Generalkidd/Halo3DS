/* Original HUD owners; screen postprocessing remains an explicit capability
 * boundary. The game's HUD, inventory and motion tracking stay authoritative. */
#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "rasterizer/rasterizer_cinematics.h"
#include "bitmaps/bitmap_group.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "interface/hud_draw.h"
#include "game/players.h"
#include "game/player_control.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "cutscene/cinematics.h"
#include "units/units.h"
#include "engine_input.h"
#include "render/render.h"
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_hud.h"
#include "engine_cache.h"
#include "cache_reader.h"
#define real_rgb_color_to_pixel32 n3ds_header_hud_rgb_to_pixel32
#include "bitmaps/bitmaps_inlines.h"
#undef real_rgb_color_to_pixel32
pixel32 real_rgb_color_to_pixel32(const real_rgb_color *color) { return n3ds_header_hud_rgb_to_pixel32(color); }
extern struct rasterizer_window_begin_parameters global_window_parameters;
void n3ds_log(const char *);
boolean rasterizer_set_texture_bitmap_data(short,const struct bitmap_data *);
struct bitmap_data *bitmap_group_try_and_get_bitmap(long,short);
static int hud_active,sensor_active,split_hud,hud_visible;
static long control_unit=NONE;
static unsigned int control_generation;
static unsigned int panel_pixels[512*64],panel_revision;
static int panel_state=-1;
static long touch_unit(void)
{
    if(!game_in_progress() || main_menu_is_active() || game_time_get_paused() || ui_widgets_active_for_local_player(0) ||
       control_generation!=n3ds_engine_cache_generation()) return NONE;
    long player=local_player_get_player_index(0);
    if(player==NONE) return NONE;
    long unit=player_get(player)->unit_index;
    return unit!=NONE && unit==control_unit && !TEST_FLAG(unit_get(unit)->object.damage_flags,_object_dead_bit)?unit:NONE;
}
static unsigned int touch_action(unsigned int x,unsigned int y)
{
    long unit=touch_unit();
    if(unit==NONE || x>=320) return 0;
    if(y<NATIVE_TOUCH_AIM_BOTTOM) {
        if(!player_control_camera_control_is_active()) return 0;
        /* A10's calibration/tutorial can hide every HUD element while the
         * player must look around. Full unit control can remain disabled.
         * Feed the original look path, including its tutorial action tests;
         * director inhibition is applied later by the original engine. */
        static unsigned int reported_generation;
        if(!hud_visible && reported_generation!=control_generation) {
            n3ds_log("TOUCH AIM: available during gameplay with HUD hidden");
            reported_generation=control_generation;
        }
        return NATIVE_TOUCH_AIM;
    }
    /* Invisible buttons remain inactive and cannot steal a drag gesture. */
    if(!hud_visible || cinematic_in_progress() || !player_input_enabled()) return 0;
    if(y<184 || y>=236) return 0;
    int grenade=x>=8 && x<104?0:x>=112 && x<208?1:-1;
    if(grenade>=0) {
        struct player_control *control=player_control_get(0);
        if(control->unit_index!=unit || !unit_get_grenade_count(unit,grenade)) return 0;
        int desired=control->desired_grenade_index;
        if(desired<0 || desired>=NUMBER_OF_UNIT_GRENADE_TYPES) desired=unit_get(unit)->unit.desired_grenade_index;
        if(desired<0 || desired>=NUMBER_OF_UNIT_GRENADE_TYPES) desired=unit_get_current_grenade_type(unit);
        if(desired==grenade) return 0;
        extern void n3ds_log(const char *);char message[96];
        snprintf(message,sizeof(message),"TOUCH CONTROL: grenade request=%d prior=%d through original input",grenade,desired);n3ds_log(message);
        return 1;
    }
    if(x>=216 && x<312 && game_engine_allow_integrated_lights(unit)) {
        extern void n3ds_log(const char *);
        n3ds_log("TOUCH CONTROL: flashlight press through original input");return 2;
    }
    return 0;
}
static void panel_rect(int x,int y,int w,int h,unsigned int color)
{
    for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i) panel_pixels[j*512+i]=color;
}
static void panel_text(int x,int y,const char *text,unsigned int color)
{
    /* Small 5x7 face at native LCD resolution; each glyph has seven row masks. */
    static const char letters[]="FLIGHTON-RAPSM0123456789";
    static const unsigned char glyphs[][7]={
        {31,16,16,30,16,16,16},{16,16,16,16,16,16,31},{31,4,4,4,4,4,31},
        {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,4},
        {14,17,17,17,17,17,14},{17,25,25,21,19,19,17},{0,0,0,31,0,0,0},
        {30,17,17,30,20,18,17},{14,17,17,31,17,17,17},{30,17,17,30,16,16,16},
        {15,16,16,14,1,1,30},{17,27,21,21,17,17,17},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    for(;*text;++text,x+=12) {
        const char *g=strchr(letters,*text);if(!g) continue;
        for(int j=0;j<7;++j) for(int i=0;i<5;++i) if(glyphs[g-letters][j]&(16>>i)) panel_rect(x+i*2,y+j*2,2,2,color);
    }
}
static void draw_touch_panel(void)
{
    if(!hud_visible || cinematic_in_progress() || !player_input_enabled()) return;
    long unit=touch_unit();if(unit==NONE) return;
    int light_on=unit_solo_player_integrated_night_vision_is_active()?
        TEST_FLAG(unit_get(unit)->unit.flags,_unit_integrated_night_vision_on_bit):unit_get_current_flashlight_state(unit);
    int state=game_engine_allow_integrated_lights(unit)?(light_on?2:1):0;
    int counts[2]={unit_get_grenade_count(unit,0),unit_get_grenade_count(unit,1)};
    int selected=unit_get_current_grenade_type(unit);
    int key=state|(counts[0]<<4)|(counts[1]<<12)|((selected+1)<<20);
    if(key!=panel_state) {
        memset(panel_pixels,0,sizeof(panel_pixels));
        unsigned int color=state?0x71b9ffff:0x516172ff;
        panel_rect(216,0,96,52,color);panel_rect(218,2,92,48,0x12283eff);
        panel_text(228,6,"LIGHT",color);panel_text(228,31,state==2?"ON":state==1?"OFF":"-",color);
        /* A simple lamp and beam remain distinguishable beside the label. */
        panel_rect(279,32,12,8,color);panel_rect(291,30,3,12,color);
        if(state==2) {panel_rect(298,27,6,2,color);panel_rect(298,35,6,2,color);panel_rect(298,43,6,2,color);}
        for(int g=0;g<2;++g) {
            int x=8+104*g;unsigned int ink=counts[g]?0x71b9ffff:0x516172ff;
            panel_rect(x,0,96,52,selected==g?0xd7ec7eff:ink);panel_rect(x+2,2,92,48,0x12283eff);
            panel_text(x+8,6,g?"PLASMA":"FRAG",ink);
            char number[8];snprintf(number,sizeof(number),"%d",counts[g]);panel_text(x+60,29,number,ink);
        }
        panel_state=key;++panel_revision;
        extern void n3ds_log(const char *);char message[128];
        snprintf(message,sizeof(message),"TOUCH CONTROL: flashlight state=%d unit=%08lx grenades=%d,%d selected=%d",state,unit,counts[0],counts[1],selected);n3ds_log(message);
    }
    n3ds_hud_screen(1);assert(n3ds_gpu_touch_panel(panel_pixels,panel_revision));
    n3ds_gpu_hud_layout(0);
    /* Draw the same atlas sprites selected by each grenade's original HUD tag.
     * Keep their aspect ratio, counts and full-size touch areas. */
    for(int g=0;g<2;++g) {
        extern long n3ds_hud_grenade_icon_bitmap(int,short *);
        short sequence;long tag=n3ds_hud_grenade_icon_bitmap(g,&sequence);
        const struct bitmap_data *bitmap=NULL;const real_rectangle2d *clip=NULL;
        if(tag==NONE || sequence<0) continue;
        hud_retrieve_bitmap_and_bounding_rect(tag,sequence,0,&bitmap,&clip);
        if(!bitmap || !rasterizer_set_texture_bitmap_data(0,bitmap)) continue;
        /* Interface sequences can use a whole bitmap instead of an atlas sprite. */
        real_rectangle2d full_clip;
        full_clip.x0=full_clip.y0=0;full_clip.x1=full_clip.y1=1;
        if(!clip) clip=&full_clip;
        float bounds[4]={clip->x0,clip->y0,clip->x1,clip->y1};
        if(!n3ds_gpu_hud_icon_bounds(n3ds_gpu_texture_bound(0),bounds)) continue;
        full_clip.x0=bounds[0];full_clip.y0=bounds[1];full_clip.x1=bounds[2];full_clip.y1=bounds[3];clip=&full_clip;
        float w=bitmap->width*(clip->x1-clip->x0),h=bitmap->height*(clip->y1-clip->y0);
        if(w<=0 || h<=0) continue;
        float scale=fminf(34.f/w,28.f/h);w*=scale;h*=scale;
        float x=8+104*g+27-w*.5f,y=184+35-h*.5f;
        struct native_text_vertex v[4]={0};
        static const float corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
        for(int i=0;i<4;++i) {
            float u=corners[i][0],t=corners[i][1];
            v[i].position[0]=x+w*u;v[i].position[1]=y+h*t;
            v[i].uv[0]=clip->x0+(clip->x1-clip->x0)*u;
            v[i].uv[1]=clip->y0+(clip->y1-clip->y0)*t;
            for(int c=0;c<4;++c) v[i].color[c]=1;
        }
        assert(n3ds_gpu_text_begin(320,240,counts[g]?0xffffb971:0xff726151,0,0));
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
        assert(n3ds_gpu_text_draw(v));
        static unsigned int logged_generation[2];
        if(logged_generation[g]!=control_generation) {
            extern void n3ds_log(const char *);char message[128];
            snprintf(message,sizeof(message),"HUD GRENADE ICON: type=%d bitmap=%08lx sequence=%d size=%.1fx%.1f",g,tag,sequence,w,h);
            n3ds_log(message);logged_generation[g]=control_generation;
        }
    }
    n3ds_gpu_world_state_restore();n3ds_hud_screen(0);
}
void n3ds_hud_screen(int bottom)
{
    if(!split_hud) return;
    if(bottom) hud_visible=1;
    if(!bottom) n3ds_gpu_hud_layout(0);
    render.camera.viewport_bounds.x1=render.camera.window_bounds.x1=
        global_window_parameters.camera.viewport_bounds.x1=global_window_parameters.camera.window_bounds.x1=bottom?640:800;
    n3ds_gpu_hud_screen(bottom);
}
static void *blips[2];
wchar_t *n3ds_hud_compat_text(long tag_index,short index)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    struct cache_file_tag_instance *tag;
    static wchar_t strings[27][128];static int loaded;
    _Static_assert(sizeof(wchar_t)==2,"Extracted Xbox UTF16 format");
    if(index<74 || index>100 || !view || !view->tags || strcmp(view->header.build,"01.10.12.2276")) return NULL;
    tag=n3ds_cache_tag(view,tag_index,'ustr');
    if(!tag || strcmp(tag->name,"ui\\multiplayer_game_text")) return NULL;
    if(!loaded) {
        extern void n3ds_log(const char *);
        unsigned int header[4],hash=2166136261u;
        FILE *f=fopen("sdmc:/halo-source/ntsc2276-hud-strings.bin","rb");
        assert(f);
        assert(fread(header,sizeof(header),1,f)==1 && header[0]==0x5453484e && header[1]==2276 && header[2]==74 && header[3]==27);
        assert(fread(strings,sizeof(strings),1,f)==1 && fgetc(f)==EOF);
        assert(!fclose(f));
        for(unsigned int i=0;i<sizeof(strings);++i) hash=(hash^((const byte *)strings)[i])*16777619u;
        assert(hash==2004606140u);
        for(int i=0;i<27;++i) assert(strings[i][0] && !strings[i][127]);
        loaded=1;n3ds_log("NATIVE HUD TEXT: imported 27 original NTSC2276 executable formats for PAL HUD indices 74..100");
    }
    return strings[index-74];
}
void n3ds_hud_draw_screen(void)
{
    extern void interface_draw_screen(void);
    rectangle2d *rects[4]={&render.camera.viewport_bounds,&render.camera.window_bounds,
        &global_window_parameters.camera.viewport_bounds,&global_window_parameters.camera.window_bounds};
    rectangle2d saved[4];
    /* Original HUD offsets/glyphs target a 480-line display. A virtual 800x480
     * surface preserves their shape and anchors on the wider 400x240 top LCD. */
    for(int i=0;i<4;++i) { saved[i]=*rects[i]; for(int c=0;c<4;++c) rects[i]->v[c]*=2; }
    split_hud=1;hud_visible=0;
    interface_draw_screen();
    long player=local_player_get_player_index(0);
    control_unit=player==NONE?NONE:player_get(player)->unit_index;
    control_generation=n3ds_engine_cache_generation();
    n3ds_input_touch_handler(touch_action);draw_touch_panel();
    n3ds_hud_screen(0);split_hud=0;
    for(int i=0;i<4;++i) *rects[i]=saved[i];
}
void n3ds_menu_draw_screen(void)
{
    extern void render_ui_widgets(short,const rectangle2d *);
    rectangle2d *rects[4]={&render.camera.viewport_bounds,&render.camera.window_bounds,
        &global_window_parameters.camera.viewport_bounds,&global_window_parameters.camera.window_bounds};
    rectangle2d saved[4];
    for(int i=0;i<4;++i) {
        saved[i]=*rects[i];rects[i]->x0=rects[i]->y0=0;rects[i]->x1=640;rects[i]->y1=480;
    }
    extern int n3ds_controls_active(void);extern void n3ds_controls_draw(void);
    if(!n3ds_controls_active()) {
        render_ui_widgets(NONE,&render.camera.window_bounds);
        if(main_menu_is_active()) {extern void n3ds_gpu_campaign_help(void);n3ds_gpu_campaign_help();}
    }
    n3ds_controls_draw();
    for(int i=0;i<4;++i) *rects[i]=saved[i];
}
void n3ds_cinematic_draw_screen(void)
{
    extern void player_effect_get_screen_flash(short,struct render_screen_flash *);
    rectangle2d *rects[4]={&render.camera.viewport_bounds,&render.camera.window_bounds,
        &global_window_parameters.camera.viewport_bounds,&global_window_parameters.camera.window_bounds};
    rectangle2d saved[4];
    for(int i=0;i<4;++i) {saved[i]=*rects[i];rects[i]->x0=rects[i]->y0=0;rects[i]->x1=640;rects[i]->y1=480;}
    /* NONE requests only the original script fade, without consuming a
     * player's damage-flash clock. The fade's color always has alpha one. */
    struct render_screen_flash fade={0};
    player_effect_get_screen_flash(NONE,&fade);
    if(fade.type && rasterizer_debug_options.screen_flashes) {
        assert(fade.type==1 && fade.color.alpha==1.f);
        fade.color.alpha=PIN(fade.intensity,0.f,1.f);
        if(fade.color.alpha>0) draw_quad(&render.camera.viewport_bounds,real_argb_color_to_pixel32(&fade.color));
    }
    cinematic_render();
    /* The native frame loop replaces interface_draw_fullscreen_overlays.
     * Its scripted mission timer is separate from the player's HUD, so draw
     * it here too for vehicles/hidden unit HUDs, but never over a cinematic. */
    if(!main_menu_is_active() && !cinematic_in_progress()) {
        extern void hud_render_timer(void);
        rasterizer_hud_begin();
        hud_render_timer();
        rasterizer_hud_end();
    }
    for(int i=0;i<4;++i) *rects[i]=saved[i];
}
void _rasterizer_hud_begin(void) { assert(!hud_active && n3ds_engine_window_active()); hud_active=1; }
void _rasterizer_hud_end(void) { assert(hud_active && !sensor_active); hud_active=0; n3ds_gpu_world_state_restore(); }
/* The original x87 helper corrects rint's residue toward zero. Unlike fast_ftol
 * this has C cast semantics (see upstream hud_draw helper reconstruction). */
long fast_ftol_C(real value) { assert(isfinite(value) && value>=-2147483648.f && value<2147483648.f); return (long)value; }
void _rasterizer_screen_effect(struct rasterizer_cinematic_screen_effect_parameters *parameters)
{
    parameters=rasterizer_screen_effect_get_cinematic_parameters(parameters);
    struct native_screen_effect effect={0};
    if(rasterizer_debug_options.screen_effects && parameters) {
        effect.enhancement=parameters->filter_light_enhancement_intensity;
        effect.desaturation=parameters->filter_desaturation_intensity;
        memcpy(effect.tint,parameters->filter_desaturation_tint.n,sizeof(effect.tint));
        effect.additive=parameters->filter_desaturation_is_additive;
        effect.video=parameters->video_on;
        effect.video_overbright=parameters->video_overbright_mode;
        effect.noise=parameters->video_noise_intensity;
    }
    n3ds_gpu_screen_effect(&effect);
#ifdef HALO_N3DS_RENDERER_TESTS
    static int previous=-1;int key=(effect.enhancement>0)|((effect.desaturation>0)<<1)|(!!effect.video<<2);
    if(key!=previous){char line[160];snprintf(line,sizeof(line),"SCREEN EFFECT OUTPUT: enabled=%d parameters=%d light=%.3f desat=%.3f video=%d",rasterizer_debug_options.screen_effects,!!parameters,effect.enhancement,effect.desaturation,effect.video);n3ds_log(line);previous=key;}
#endif
}

static void *bitmap(short kind)
{
    struct bitmap_data *b=bitmap_group_try_and_get_bitmap(interface_get_tag_index(kind),0);
    assert(b && rasterizer_set_texture_bitmap_data(0,b));
    return n3ds_gpu_texture_bound(0);
}
void _rasterizer_hud_motion_sensor_blip_begin(void)
{
    assert(hud_active && !sensor_active);
    if(!rasterizer_debug_options.hud_motion_sensor) return;
    blips[0]=bitmap(_interface_bitmap_motion_blip); blips[1]=bitmap(_interface_bitmap_iface_map1);
    n3ds_gpu_sensor_begin();sensor_active=1;
}
void _rasterizer_hud_motion_sensor_blip_draw(const real_point2d *p,real intensity,real size,const real_rgb_color *color,boolean large)
{
    if(!sensor_active) return;
    float rgb[3]={color->red*intensity,color->green*intensity,color->blue*intensity};
    assert(n3ds_gpu_sensor_blip(blips[!!large],p->x,p->y,size,rgb));
}
void _rasterizer_hud_motion_sensor_blip_end(const real_point2d *center,real scale)
{
    if(!sensor_active) return;
    void *sweep=bitmap(_interface_bitmap_motion_sweep),*mask=bitmap(_interface_bitmap_motion_sweep_mask);
    const rectangle2d *r=&global_window_parameters.camera.viewport_bounds;
    n3ds_gpu_hud_sensor_origin(center->x,center->y,local_player_count()>1?32.f:42.f);
    assert(n3ds_gpu_sensor_end(sweep,mask,scale,center->x,center->y,local_player_count()>1?32.f:42.f,r->x1-r->x0,r->y1-r->y0));
    sensor_active=0;n3ds_gpu_world_state_restore();
}
