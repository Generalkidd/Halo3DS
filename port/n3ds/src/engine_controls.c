/* New3DS integration additions by Kevin Boggs. */
#include "cseries.h"
#include "engine_controls.h"
#include "engine_map_format.h"
#include "engine_wireless.h"
#include "engine_network.h"
#include "engine_files.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "game/game_engine.h"
#include "saved games/playlist_profile.h"
#include "engine_bots.h"
#include "engine_pc_preview.h"
#ifdef HALO_N3DS_PC_LOCAL_MENU
#include "pc_diagnostic_maps.h"
int n3ds_pc_tags_active(void);
int n3ds_pc_map_available(const char *);
#endif

void n3ds_log(const char *message);
void n3ds_ui_controls_back(void);
static void local_match_remember(const char *,const struct game_variant *,int,int,int);
static const struct native_control_settings defaults={100,100,0,0,0,0,0,1,0,0,NATIVE_QUALITY_BALANCED};
static struct native_control_settings settings={100,100,0,0,0,0,0,1,0,0,NATIVE_QUALITY_BALANCED},opened_settings;
static int initialized,modal,main_route,selected,save_error,repeat_ticks,swallow;
static unsigned int previous_keys;
static const char path[]="t:\\controller-settings.bin";
static const char settings_temporary[]="t:\\controller-settings.tmp";
static const char backup[]="t:\\controller-settings.bak";
struct settings_file {unsigned int magic,version;struct native_control_settings settings;unsigned int checksum;};

static int valid(const struct native_control_settings *s)
{return s->cstick>=25 && s->cstick<=300 && s->touch>=25 && s->touch<=300 && s->xbox_buttons<=1 && s->swap_shoulders<=1 && s->high_textures<=1 && s->swap_xy<=1 && s->show_fps<=1 && s->decals<=1 && s->swap_ab<=1 && s->circle_pad_pro<=1 && s->quality<=NATIVE_QUALITY_PERFORMANCE;}
/* Kevin Boggs: checksum_bytes - New3DS integration. */
static unsigned int checksum_bytes(const void *data,unsigned int size)
{
 const unsigned char *p=(const unsigned char *)data;unsigned int h=2166136261u;
 for(unsigned int i=0;i<size-4;++i)h=(h^p[i])*16777619u;
 return h;
}
static unsigned int checksum(const struct settings_file *f){return checksum_bytes(f,sizeof(*f));}
/* Kevin Boggs: decode_settings - shared versioned settings migration. */
static int decode_settings(const unsigned int *f,unsigned int size,struct native_control_settings *s)
{
 if(size<28 || size>sizeof(struct settings_file) || size%4 || f[0]!=0x3343544c ||
    f[size/4-1]!=checksum_bytes(f,size))return 0;
 unsigned int version=f[1];
 if(!((version==1 && size==28) || (version==2 && size==32) ||
      (version==3 && size==40) || (version==4 && size==44) ||
      (version==5 && size==48) || (version==6 && size==sizeof(struct settings_file))))return 0;
 struct native_control_settings value=defaults;
 value.cstick=f[2];value.touch=f[3];value.xbox_buttons=f[4];value.swap_shoulders=f[5];
 /* Public dev v2 owns this slot. Experimental v2 was byte-for-byte ambiguous;
  * prefer preserving the shipped CPP choice rather than guessing from a bit. */
 if(version==2)value.circle_pad_pro=f[6];
 if(version>=3){value.high_textures=f[6];value.swap_xy=f[7];value.show_fps=f[8];}
 if(version>=4)value.decals=f[9];
 if(version>=5)value.swap_ab=f[10];
 if(version==6){value.circle_pad_pro=f[11];value.quality=f[12];}
 /* New independent swaps remain neutral for older profiles. For v3-v5 their
  * explicit values are retained, preserving the effective physical mapping. */
 if(!valid(&value))return 0;
 *s=value;return 1;
}
static int read_settings(const char *name,struct native_control_settings *s)
{
 unsigned int f[sizeof(struct settings_file)/4]={0};
 unsigned int h=n3ds_file_open(name,1);if(!h)return 0;
 unsigned int size=n3ds_file_size(h);
 int ok=size>=28 && size<=sizeof(f) && n3ds_file_read(h,f,size);
 ok=n3ds_file_close(h) && ok;
 return ok && decode_settings(f,size,s);
}
static int save_settings(void)
{
 struct settings_file f={0x3343544c,6,settings,0};f.checksum=checksum(&f);
 struct native_file_info info;
 if(n3ds_file_stat(settings_temporary,&info) && !n3ds_file_delete(settings_temporary,0))return 0;
 if(!n3ds_file_create(settings_temporary,0))return 0;
 unsigned int h=n3ds_file_open(settings_temporary,3);if(!h)return 0;
 int ok=n3ds_file_resize(h,0) && n3ds_file_write(h,&f,sizeof(f));ok=n3ds_file_close(h) && ok;
 struct native_control_settings verified;
 if(!ok || !read_settings(settings_temporary,&verified) || memcmp(&verified,&settings,sizeof(settings)))return 0;
 if(n3ds_file_stat(path,&info)) {
  if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0))return 0;
  if(!n3ds_file_rename(path,backup))return 0;
 }
 if(!n3ds_file_rename(settings_temporary,path))return 0;
 return 1;
}
void n3ds_controls_initialize(void)
{
 if(initialized)return;initialized=1;
 if(!read_settings(path,&settings) && !read_settings(backup,&settings))settings=defaults;
}
void n3ds_controls_get(struct native_control_settings *s){*s=settings;}
void n3ds_controls_set(const struct native_control_settings *s){if(valid(s))settings=*s;}
float n3ds_controls_sensitivity(int touch){return (touch?settings.touch:settings.cstick)*.01f;}
/* Kevin Boggs: n3ds_controls_high_textures - New3DS integration. */
int n3ds_controls_high_textures(void){n3ds_controls_initialize();return settings.high_textures!=0;}
/* Kevin Boggs: n3ds_controls_show_fps - New3DS integration. */
int n3ds_controls_show_fps(void){n3ds_controls_initialize();return settings.show_fps!=0;}
/* Kevin Boggs: expose the persistent pre match decal preference to simulation and rendering. */
/* Kevin Boggs: n3ds_controls_decals - New3DS integration. */
int n3ds_controls_decals(void){n3ds_controls_initialize();return settings.decals!=0 && settings.quality==NATIVE_QUALITY_QUALITY;}
int n3ds_controls_circle_pad_pro(void){return settings.circle_pad_pro!=0;}
/* Kevin Boggs: n3ds_controls_quality - cosmetic policy, never simulation authority. */
int n3ds_controls_quality(void){return settings.quality==NATIVE_QUALITY_QUALITY;}
/* Kevin Boggs: n3ds_controls_performance - explicit cosmetic reductions on either model. */
int n3ds_controls_performance(void){return settings.quality==NATIVE_QUALITY_PERFORMANCE;}
static unsigned int exchange(unsigned int held,unsigned int a,unsigned int b)
{unsigned int result=held&~(a|b);if(held&a)result|=b;if(held&b)result|=a;return result;}
unsigned int n3ds_controls_buttons(unsigned int held,int original,int gameplay)
{
 if(settings.xbox_buttons){held=exchange(held,NATIVE_KEY_A,NATIVE_KEY_B);held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);}
 if(settings.swap_xy)held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);
 if(settings.swap_ab)held=exchange(held,NATIVE_KEY_A,NATIVE_KEY_B);
 if(settings.swap_shoulders && !original && gameplay){held=exchange(held,NATIVE_KEY_L,NATIVE_KEY_ZL);held=exchange(held,NATIVE_KEY_R,NATIVE_KEY_ZR);}
 return held;
}
int n3ds_controls_active(void){return modal;}
void n3ds_controls_open(int from_main)
{
 if(modal)return;
 n3ds_controls_initialize();modal=1;main_route=from_main;selected=0;save_error=0;
 opened_settings=settings;swallow=1;repeat_ticks=0;
 n3ds_log(from_main?"CONTROLLER SETTINGS: main menu opened":"CONTROLLER SETTINGS: pause menu opened");
}
static void close_settings(void)
{
 if(memcmp(&settings,&opened_settings,sizeof(settings)) && !save_settings()){
  save_error=1;n3ds_log("CONTROLLER SETTINGS: could not save preferences; retry or cancel");return;
 }
 modal=0;swallow=1;if(main_route)n3ds_ui_controls_back();
 n3ds_log("CONTROLLER SETTINGS: saved and returned");
}
/* Kevin Boggs: keep the reduced original model menu mapped to the same setting fields. */
/* Kevin Boggs: setting_field - New3DS integration. */
static int setting_field(int row){return n3ds_controls_row_field(row);}
/* Kevin Boggs: n3ds_controls_row_field - use the same visible rows for input and drawing. */
int n3ds_controls_row_field(int row)
{
 int visible=0;
 extern int n3ds_pc_maps_present(void);
 for(int field=0;field<11;++field){
  if(field==3 && n3ds_input_platform_original_model())continue;
  if(field==4 && (n3ds_input_platform_original_model() || !n3ds_pc_maps_present()))continue;
  if(visible++==row)return field;
 }
 return -1;
}
/* Kevin Boggs: n3ds_controls_row_count - count available settings, including CPP on all models. */
int n3ds_controls_row_count(void)
{int count=0;while(n3ds_controls_row_field(count)>=0)++count;return count;}
static void adjust(int delta)
{
 int field=setting_field(selected);
 if(field<2){unsigned int *v=field?&settings.touch:&settings.cstick;int value=(int)*v+delta*5;*v=value<25?25:value>300?300:value;}
 else if(field==2)settings.xbox_buttons^=1;
 else if(field==3)settings.swap_shoulders^=1;
 else if(field==4)settings.high_textures^=1;
 else if(field==5)settings.swap_xy^=1;
 else if(field==6)settings.swap_ab^=1;
 else if(field==7)settings.show_fps^=1;
 else if(field==8)settings.decals^=1;
 else if(field==9)settings.circle_pad_pro^=1;
 else if(field==10)settings.quality=(settings.quality+3+delta)%3;
 save_error=0;
}
void n3ds_controls_filter(struct native_input_sample *s)
{
 unsigned int held=s->held,fresh=held&~previous_keys;previous_keys=held;
 if(!modal && !swallow && n3ds_ui_wireless_lobby_active() &&
    n3ds_network_bot_options(0,0,NULL,NULL) && (fresh&NATIVE_KEY_TOUCH) &&
    s->touch_x>=16 && s->touch_x<304 && s->touch_y>=80 && s->touch_y<152){
  int direction=s->touch_x<160?-1:1;
  n3ds_network_bot_options(s->touch_y<116?direction:0,s->touch_y>=116?direction:0,NULL,NULL);
  swallow=1;memset(s,0,sizeof(*s));return;
 }
 int pause=n3ds_ui_pause_menu_active();
 if(!modal && !swallow && pause && ((fresh&NATIVE_KEY_SELECT) ||
    ((fresh&NATIVE_KEY_TOUCH) && s->touch_x>=24 && s->touch_x<296 && s->touch_y>=202 && s->touch_y<232)))n3ds_controls_open(0);
 if(swallow){if(!held)swallow=0;memset(s,0,sizeof(*s));return;}
 if(!modal)return;
 /* Do not retain a modal across a mission transition or a destroyed menu. */
 if(!ui_widgets_active()){modal=0;swallow=1;memset(s,0,sizeof(*s));return;}
 unsigned int nav=held&(NATIVE_KEY_DUP|NATIVE_KEY_DDOWN|NATIVE_KEY_DLEFT|NATIVE_KEY_DRIGHT);
 if(nav && !(fresh&nav)){if(++repeat_ticks>=18 && (repeat_ticks-18)%5==0)fresh|=nav;}
 else repeat_ticks=0;
 int count=n3ds_controls_row_count();
 if(fresh&NATIVE_KEY_DUP)selected=(selected+count-1)%count;
 if(fresh&NATIVE_KEY_DDOWN)selected=(selected+1)%count;
 if(fresh&NATIVE_KEY_DLEFT)adjust(-1);
 if(fresh&NATIVE_KEY_DRIGHT)adjust(1);
 if(fresh&NATIVE_KEY_TOUCH){
  unsigned int x=s->touch_x,y=s->touch_y;
  if(y>=24 && y<178){int row=(y-24)/14;if(row<count){selected=row;if(x>=242)adjust(x<273?-1:1);}}
  if(y>=202 && y<234){if(x>=16 && x<150){settings=defaults;save_error=0;}else if(x>=170 && x<304)close_settings();}
 }
 if(fresh&NATIVE_KEY_START)close_settings();
 /* B cancels an unsuccessful disk save, so an SD error cannot trap the user. */
 if(save_error && (fresh&NATIVE_KEY_B)){settings=opened_settings;modal=0;swallow=1;if(main_route)n3ds_ui_controls_back();}
 memset(s,0,sizeof(*s));
}
void n3ds_controls_draw(void)
{
 if(modal)n3ds_gpu_controls_draw(&settings,selected,n3ds_input_platform_original_model(),save_error);
 else if(n3ds_ui_pause_menu_active())n3ds_gpu_controls_entry();
 else if(n3ds_ui_wireless_lobby_active()){
  int bots,difficulty;
  if(n3ds_network_bot_options(0,0,&bots,&difficulty))n3ds_gpu_wireless_bots(bots,difficulty);
 }
}
#ifdef HALO_N3DS_PC_LOCAL_MENU
static int pc_menu_open,pc_menu_page,pc_menu_field,pc_menu_map,pc_menu_mode,pc_menu_bots,pc_menu_difficulty=1;
static int pc_menu_weapon_set,pc_menu_vehicle_choice,pc_menu_error,pc_menu_swallow,pc_menu_reopen_pending;
static int pc_match_weapon_override;
/* Kevin Boggs: n3ds_pc_local_weapon_override - New3DS integration. */
int n3ds_pc_local_weapon_override(void){return pc_match_weapon_override;}
static unsigned int pc_menu_previous;
static const char *const pc_menu_modes[]={"slayer","ctf","oddball","king","race"};
/* Kevin Boggs: pc_menu_available - New3DS integration. */
static int pc_menu_available(void)
{
 if(!n3ds_pc_maps_present() || n3ds_wireless_active() || n3ds_local_match_menu_active())return 0;
#ifdef HALO_N3DS_UNIFIED_UI
 return main_menu_is_active();
#else
 return main_menu_is_active() && n3ds_pc_tags_active();
#endif
}
/* Kevin Boggs: pc_menu_next_map - only offer installed PC maps. */
static int pc_menu_next_map(int from,int delta)
{
 if(!delta){if(n3ds_pc_map_available(n3ds_pc_diagnostic_maps[from]))return from;delta=1;}
 for(int n=0;n<19;++n){from=(from+19+delta)%19;if(n3ds_pc_map_available(n3ds_pc_diagnostic_maps[from]))return from;}
 return 0;
}
/* Kevin Boggs: pc_menu_field_count - New3DS integration. */
static int pc_menu_field_count(void)
{return pc_menu_page?2:n3ds_input_platform_original_model()?4:5;}
/* Kevin Boggs: pc_menu_toggle_textures - New3DS integration. */
static void pc_menu_toggle_textures(void)
{
 n3ds_controls_initialize();
 settings.high_textures^=1;
 if(!save_settings()){
  settings.high_textures^=1;pc_menu_error=1;
  n3ds_log("PC LOCAL MENU: could not save texture preference");
 }else{
  pc_menu_error=0;
  n3ds_log(settings.high_textures?
   "PC LOCAL MENU: high textures saved ON for next map load":
   "PC LOCAL MENU: high textures saved OFF for next map load");
 }
}
/* Kevin Boggs: n3ds_pc_local_menu_active - New3DS integration. */
int n3ds_pc_local_menu_active(void){return pc_menu_open;}
/* Kevin Boggs: n3ds_pc_local_menu_queue_reopen - New3DS integration. */
void n3ds_pc_local_menu_queue_reopen(void){pc_menu_reopen_pending=1;}
/* Kevin Boggs: n3ds_pc_local_menu_filter - New3DS integration. */
void n3ds_pc_local_menu_filter(struct native_input_sample *sample)
{
 unsigned int held=n3ds_controls_buttons(sample->held,0,0),fresh=held&~pc_menu_previous;
 pc_menu_previous=held;
 if(pc_menu_reopen_pending && pc_menu_available()){
  pc_menu_reopen_pending=0;pc_menu_open=1;pc_menu_page=0;pc_menu_field=0;pc_menu_error=0;
  pc_menu_bots=n3ds_bots_requested_count();pc_menu_difficulty=n3ds_bots_menu_difficulty();pc_menu_map=pc_menu_next_map(pc_menu_map,0);pc_menu_swallow=1;
  n3ds_log("PC LOCAL MENU: reopened after postgame");memset(sample,0,sizeof(*sample));return;
 }
 if(pc_menu_swallow){if(!held)pc_menu_swallow=0;memset(sample,0,sizeof(*sample));return;}
 if(!pc_menu_open){
  if((fresh&NATIVE_KEY_SELECT) && pc_menu_available() && !n3ds_controls_active()){
   pc_menu_open=1;pc_menu_page=0;pc_menu_field=0;pc_menu_error=0;
   pc_menu_bots=n3ds_bots_requested_count();pc_menu_difficulty=n3ds_bots_menu_difficulty();pc_menu_map=pc_menu_next_map(pc_menu_map,0);
   pc_menu_swallow=1;n3ds_log("PC LOCAL MENU: opened");memset(sample,0,sizeof(*sample));
  }
  return;
 }
 if(!pc_menu_available()){
  pc_menu_open=0;n3ds_pc_preview_close();pc_menu_swallow=1;memset(sample,0,sizeof(*sample));return;
 }
 n3ds_pc_preview_input(sample);
 if(fresh&NATIVE_KEY_B){
  if(pc_menu_page){pc_menu_page=0;pc_menu_field=0;}
  else {pc_menu_open=0;n3ds_pc_preview_close();pc_menu_swallow=1;n3ds_log("PC LOCAL MENU: closed");}
 }
 else if((fresh&NATIVE_KEY_X) || ((fresh&NATIVE_KEY_TOUCH) &&
         sample->touch_y>=202 && sample->touch_y<234 && sample->touch_x>=168 && sample->touch_x<304)){
  pc_menu_page^=1;pc_menu_field=0;pc_menu_error=0;
 }
 else if(fresh&NATIVE_KEY_DUP)pc_menu_field=(pc_menu_field+pc_menu_field_count()-1)%pc_menu_field_count();
 else if(fresh&NATIVE_KEY_DDOWN)pc_menu_field=(pc_menu_field+1)%pc_menu_field_count();
 else if(fresh&(NATIVE_KEY_DLEFT|NATIVE_KEY_DRIGHT)){
  int delta=(fresh&NATIVE_KEY_DRIGHT)?1:-1;
  if(pc_menu_page){
   if(pc_menu_field==0)pc_menu_weapon_set=(pc_menu_weapon_set+13+delta)%13;
   else pc_menu_vehicle_choice=(pc_menu_vehicle_choice+6+delta)%6;
  }else if(pc_menu_field==0)pc_menu_map=pc_menu_next_map(pc_menu_map,delta);
  else if(pc_menu_field==1)pc_menu_mode=(pc_menu_mode+5+delta)%5;
  else if(pc_menu_field==2)pc_menu_bots=(pc_menu_bots+16+delta)%16;
  else if(pc_menu_field==3)pc_menu_difficulty=(pc_menu_difficulty+3+delta)%3;
  else {pc_menu_toggle_textures();delta=0;}
  if(delta)pc_menu_error=0;
 }
 else if((fresh&NATIVE_KEY_TOUCH) && sample->touch_x>=12 && sample->touch_x<308 &&
         ((!pc_menu_page && sample->touch_y>=182 && sample->touch_y<204) ||
          (pc_menu_page && sample->touch_y>=148 && sample->touch_y<196))){
  if(!pc_menu_page){pc_menu_field=0;pc_menu_map=pc_menu_next_map(pc_menu_map,1);pc_menu_error=0;}
  else {int row=(sample->touch_y-148)/24;
   pc_menu_field=row;
   if(row==0)pc_menu_weapon_set=(pc_menu_weapon_set+1)%13;
   else pc_menu_vehicle_choice=(pc_menu_vehicle_choice+1)%6;
  }
 }
 else if(pc_menu_page && ((fresh&NATIVE_KEY_A) || ((fresh&NATIVE_KEY_TOUCH) &&
         sample->touch_y>=202 && sample->touch_y<234 && sample->touch_x>=16 && sample->touch_x<152))){
  pc_menu_page=0;pc_menu_field=0;
 }
 else if((fresh&NATIVE_KEY_A) || ((fresh&NATIVE_KEY_TOUCH) &&
         sample->touch_y>=202 && sample->touch_y<234 && sample->touch_x>=16 && sample->touch_x<152)){
  char map[96],message[192];struct game_variant variant;
  const char *name=n3ds_pc_diagnostic_maps[pc_menu_map];
  int length=snprintf(map,sizeof(map),"pc\\levels\\test\\%s\\%s",name,name);
  struct game_variant *chosen=game_engine_get_variant_by_name(&variant,pc_menu_modes[pc_menu_mode]);
  if(chosen){
   variant.universal_variant.weapon_set=pc_menu_weapon_set<11?pc_menu_weapon_set:0;
   if(pc_menu_vehicle_choice)variant.universal_variant.vehicle_set=pc_menu_vehicle_choice-1;
  }
  if(length<0 || length>=(int)sizeof(map) || !chosen || !chosen->game_engine_index ||
     !n3ds_main_queue_solo_multiplayer(map,&variant,0)){
   pc_menu_error=1;n3ds_log("PC LOCAL MENU: selected map is unavailable or cannot be queued");
  }else{
   n3ds_bots_set_menu_count((short)pc_menu_bots);
   n3ds_bots_set_menu_difficulty((short)pc_menu_difficulty);
   pc_match_weapon_override=pc_menu_weapon_set==11?1:pc_menu_weapon_set==12?2:0;
   local_match_remember(map,&variant,pc_menu_bots,pc_menu_difficulty,pc_menu_weapon_set);
   snprintf(message,sizeof(message),"PC LOCAL MENU: queued %s mode=%s bots=%d difficulty=%d weapons=%d vehicles=%d",map,pc_menu_modes[pc_menu_mode],pc_menu_bots,pc_menu_difficulty,pc_menu_weapon_set,variant.universal_variant.vehicle_set);
   n3ds_log(message);pc_menu_open=0;n3ds_pc_preview_close();pc_menu_swallow=1;
  }
 }
 memset(sample,0,sizeof(*sample));
}
/* Kevin Boggs: n3ds_pc_local_menu_draw - New3DS integration. */
void n3ds_pc_local_menu_draw(void)
{if(pc_menu_open)n3ds_gpu_pc_local_menu(pc_menu_map,pc_menu_mode,pc_menu_bots,pc_menu_difficulty,
 pc_menu_weapon_set,pc_menu_vehicle_choice,pc_menu_page,pc_menu_field,pc_menu_error,
 n3ds_controls_high_textures(),n3ds_input_platform_original_model());}
#endif

/* A preview owns no live engine tags and is released on leaving the browser. */
static int browser_map=-1,browser_pc,browser_settle,browser_owned;
/* Kevin Boggs: n3ds_map_browser_filter - preview gestures never reach the game widgets. */
void n3ds_map_browser_filter(struct native_input_sample *sample)
{
    extern int n3ds_ui_map_browser_selection(void);
    static const unsigned char xbox_order[]={0,16,5,15,13,9,4,3,2,1,18,14,12};
    int selection=n3ds_controls_active() || n3ds_local_match_menu_active()?-1:n3ds_ui_map_browser_selection();
#ifdef HALO_N3DS_PC_LOCAL_MENU
    if(n3ds_pc_local_menu_active())selection=-1;
#endif
    int pc=selection>=32 && selection<51;
    int map=pc?selection-32:selection>=0 && selection<13?xbox_order[selection]:-1;
    if(map!=browser_map || pc!=browser_pc){
        if(browser_owned)n3ds_pc_preview_close();
        browser_owned=0;browser_map=map;browser_pc=pc;browser_settle=0;
    }
    if(map<0)return;
    if(browser_settle<12)++browser_settle;
    if(browser_owned)n3ds_pc_preview_input(sample);
    sample->held&=~(NATIVE_KEY_TOUCH|NATIVE_KEY_L|NATIVE_KEY_R);
}
/* Kevin Boggs: n3ds_map_browser_draw - debounce map changes before reading preview data. */
void n3ds_map_browser_draw(void)
{
    if(browser_map<0 || n3ds_local_match_menu_active() || n3ds_controls_active())return;
#ifdef HALO_N3DS_PC_LOCAL_MENU
    if(n3ds_pc_local_menu_active())return;
#endif
    int ready=browser_settle>=12;
    if(ready)browser_owned=1;
    n3ds_gpu_map_browser(browser_map,browser_pc,ready);
}

/* Local match choices are copied from the chosen playlist, so changing bots or
 * equipment never replaces custom scoring, teams or mode-specific rules. */
static struct game_variant local_match_variant;
static char local_match_map[96],local_match_title[13];
static int local_match_open,local_match_swallow,local_match_row,local_match_error;
static int local_match_bots,local_match_difficulty;
static int local_match_gear,local_match_preview_map,local_match_pc,local_match_weapons;
static unsigned int local_match_previous;
static int local_match_inline;
static long local_match_profile=NONE;
static struct {
 struct game_variant variant;
 char map[96];
 int valid,pending,bots,difficulty,weapons;
} last_local_match;
/* Kevin Boggs: local_match_remember - commit the successful launch, not cancelled edits. */
static void local_match_remember(const char *map,const struct game_variant *variant,int bots,int difficulty,int weapons)
{
 last_local_match.variant=*variant;
 snprintf(last_local_match.map,sizeof(last_local_match.map),"%s",map);
 last_local_match.bots=bots;last_local_match.difficulty=difficulty;
 last_local_match.weapons=weapons;last_local_match.valid=1;last_local_match.pending=0;
}
/* Kevin Boggs: n3ds_local_match_menu_queue_reopen - restore only a successfully launched match. */
int n3ds_local_match_menu_queue_reopen(void)
{
 if(!last_local_match.valid || n3ds_wireless_active())return 0;
 last_local_match.pending=1;
 return 1;
}
/* Kevin Boggs: n3ds_local_match_menu_active - native match options modal. */
int n3ds_local_match_menu_active(void) { return local_match_open; }
/* Kevin Boggs: n3ds_local_match_menu_open - keep the selected Xbox or PC map and playlist. */
int n3ds_local_match_menu_open(const char *map,const struct game_variant *variant,int controller)
{
 if(local_match_open || n3ds_controls_active() || n3ds_wireless_active() ||
    !main_menu_is_active() || !map || !*map || strlen(map)>=sizeof(local_match_map) ||
    !variant || variant->game_engine_index<1 || variant->game_engine_index>5 || controller!=0)return 0;
#ifdef HALO_N3DS_PC_LOCAL_MENU
 if(n3ds_pc_local_menu_active())return 0;
#endif
 strcpy(local_match_map,map);local_match_variant=*variant;
 for(int i=0;i<12;++i){unsigned int c=variant->human_readable_game_description[i];
  local_match_title[i]=c>=32 && c<=126?(char)c:c?'?':0;}
 local_match_title[12]=0;
 local_match_variant.universal_variant.weapon_set=PIN(variant->universal_variant.weapon_set,0,10);
 local_match_variant.universal_variant.vehicle_set=PIN(variant->universal_variant.vehicle_set,0,4);
 local_match_weapons=local_match_variant.universal_variant.weapon_set;
 local_match_bots=n3ds_bots_requested_count();local_match_difficulty=n3ds_bots_menu_difficulty();
 local_match_open=local_match_swallow=1;local_match_row=local_match_error=local_match_gear=0;
 local_match_inline=0;local_match_profile=NONE;
 static const char *const maps[]={"beavercreek","bloodgulch","boardingaction","carousel","chillout",
  "damnation","dangercanyon","deathisland","gephyrophobia","hangemhigh","icefields","infinity",
  "longest","prisoner","putput","ratrace","sidewinder","timberland","wizard"};
 const char *name=strrchr(map,'\\');name=name?name+1:map;
 local_match_pc=!strncmp(map,"pc\\",3);local_match_preview_map=-1;
 for(int i=0;i<19;++i)if(!strcmp(name,maps[i])){local_match_preview_map=i;break;}
 n3ds_log("LOCAL MATCH: bottom-screen options opened");return 1;
}
/* Kevin Boggs: local_match_adjust - change only the requested match option. */
static void local_match_adjust(int delta)
{
 if(local_match_row==0)local_match_bots=(local_match_bots+16+delta)%16;
 else if(local_match_row==1)local_match_difficulty=(local_match_difficulty+3+delta)%3;
 else if(local_match_row==2){int count=local_match_pc?13:11;local_match_weapons=(local_match_weapons+count+delta)%count;}
 else local_match_variant.universal_variant.vehicle_set=(local_match_variant.universal_variant.vehicle_set+5+delta)%5;
 local_match_error=0;
}
/* Kevin Boggs: n3ds_local_match_menu_filter - consume touch and keys until Start or Back. */
void n3ds_local_match_menu_filter(struct native_input_sample *sample)
{
 /* Match the original menu's logical A/B/X layout. Only interpret a mapped
  * copy here: input that passes through is remapped by engine_input later. */
 unsigned int held=n3ds_controls_buttons(sample->held,0,0),fresh=held&~local_match_previous;local_match_previous=held;
 /* Show touch settings beside the original carousel. Navigation stays with
  * the top screen; confirmation starts the selected preset immediately. */
 long profile=n3ds_ui_local_match_profile();
 if(local_match_inline && profile==NONE){
  n3ds_pc_preview_close();local_match_open=local_match_inline=local_match_gear=0;
 }
 if(profile!=NONE && !local_match_swallow && !n3ds_wireless_active() && !n3ds_controls_active() &&
    !last_local_match.pending && (!local_match_open || (local_match_inline && profile!=local_match_profile))){
  struct game_variant selected;
  if(playlist_profile_get(profile,&selected)){
   if(!local_match_open){
    if(n3ds_local_match_menu_open(main_get_multiplayer_map_name(),&selected,0)){
     local_match_inline=1;local_match_swallow=0;local_match_profile=profile;
    }
   }else{
    local_match_variant=selected;local_match_profile=profile;local_match_error=0;
    local_match_weapons=PIN(selected.universal_variant.weapon_set,0,10);
    local_match_variant.universal_variant.vehicle_set=PIN(selected.universal_variant.vehicle_set,0,4);
    for(int i=0;i<12;++i){unsigned int c=selected.human_readable_game_description[i];
     local_match_title[i]=c>=32 && c<=126?(char)c:c?'?':0;}
    local_match_title[12]=0;
   }
  }
 }
 if(last_local_match.pending && n3ds_wireless_active())last_local_match.pending=0;
 if(last_local_match.pending && main_menu_is_active() && ui_widgets_active() &&
    n3ds_local_match_menu_open(last_local_match.map,&last_local_match.variant,0)){
  last_local_match.pending=0;
  local_match_bots=last_local_match.bots;local_match_difficulty=last_local_match.difficulty;
  local_match_weapons=last_local_match.weapons;
  memset(sample,0,sizeof(*sample));return;
 }
 if(local_match_swallow){if(!held)local_match_swallow=0;memset(sample,0,sizeof(*sample));return;}
 if(!local_match_open)return;
 if(!main_menu_is_active() || !ui_widgets_active() || n3ds_wireless_active()){
  n3ds_pc_preview_close();local_match_gear=0;
  local_match_open=0;local_match_swallow=1;memset(sample,0,sizeof(*sample));return;
 }
 if(local_match_gear){
  if(local_match_preview_map>=0)n3ds_pc_preview_input(sample);
  if(fresh&(NATIVE_KEY_DUP|NATIVE_KEY_DDOWN))local_match_row=local_match_row==2?3:2;
  if(fresh&NATIVE_KEY_DLEFT)local_match_adjust(-1);
  if(fresh&NATIVE_KEY_DRIGHT)local_match_adjust(1);
  int done=!!(fresh&(NATIVE_KEY_A|NATIVE_KEY_B|NATIVE_KEY_X|NATIVE_KEY_START));
  if(fresh&NATIVE_KEY_TOUCH){unsigned x=sample->touch_x,y=sample->touch_y;
   if(x>=12 && x<308 && y>=148 && y<196){
    local_match_row=2+(y-148)/24;
    if(x>=240)local_match_adjust(x<272?-1:1);
   }
   if(x>=16 && x<304 && y>=202 && y<234)done=1;
  }
  if(done){n3ds_pc_preview_close();local_match_gear=0;local_match_swallow=1;}
  memset(sample,0,sizeof(*sample));return;
 }
 int gear=!!(fresh&NATIVE_KEY_X);
 int start=!!(fresh&(NATIVE_KEY_A|NATIVE_KEY_START)),back=!!(fresh&NATIVE_KEY_B);
 if(!local_match_inline){
  if(fresh&NATIVE_KEY_DUP)local_match_row=(local_match_row+3)%4;
  if(fresh&NATIVE_KEY_DDOWN)local_match_row=(local_match_row+1)%4;
  if(fresh&NATIVE_KEY_DLEFT)local_match_adjust(-1);
  if(fresh&NATIVE_KEY_DRIGHT)local_match_adjust(1);
 }
 if(fresh&NATIVE_KEY_TOUCH){
  unsigned x=sample->touch_x,y=sample->touch_y;
  if(x>=12 && x<308 && y>=48 && y<160){
   local_match_row=(y-48)/28;
   if(x>=240)local_match_adjust(x<272?-1:1);
  }
  if(x>=16 && x<304 && y>=166 && y<196)gear=1;
  if(y>=202 && y<234){if(x>=16 && x<152)back=1;else if(x>=168 && x<304)start=1;}
 }
 if(gear && !back && !start){local_match_gear=1;local_match_row=local_match_row==3?3:2;local_match_swallow=1;}
 if(local_match_inline && back){
  n3ds_pc_preview_close();local_match_open=local_match_inline=local_match_gear=local_match_swallow=0;
  memset(sample,0,sizeof(*sample));sample->held=n3ds_controls_buttons(NATIVE_KEY_B,0,0);return;
 }
 if(back){n3ds_pc_preview_close();local_match_open=0;local_match_swallow=1;n3ds_log("LOCAL MATCH: returned to gametype selection");}
 else if(start){
  local_match_swallow=1;
  struct game_variant launch=local_match_variant;
  launch.universal_variant.weapon_set=local_match_weapons<11?local_match_weapons:0;
  if(!n3ds_main_queue_solo_multiplayer(local_match_map,&launch,0)){
   local_match_error=1;n3ds_log("LOCAL MATCH: map unavailable or match could not be queued");
  }else{
   local_match_remember(local_match_map,&local_match_variant,local_match_bots,local_match_difficulty,local_match_weapons);
   n3ds_bots_set_menu_count((short)local_match_bots);
   n3ds_bots_set_menu_difficulty((short)local_match_difficulty);
#ifdef HALO_N3DS_PC_LOCAL_MENU
   pc_match_weapon_override=local_match_pc && local_match_weapons>=11?local_match_weapons-10:0;
#endif
   char message[192];snprintf(message,sizeof(message),"LOCAL MATCH: queued %s mode=%ld bots=%d difficulty=%d weapons=%ld vehicles=%ld",
    local_match_map,local_match_variant.game_engine_index,local_match_bots,local_match_difficulty,
    (long)local_match_weapons,local_match_variant.universal_variant.vehicle_set);n3ds_log(message);
   n3ds_pc_preview_close();local_match_open=0;local_match_swallow=1;
  }
 }
 if(!local_match_inline || start || back || gear || (held&NATIVE_KEY_TOUCH))memset(sample,0,sizeof(*sample));
}
/* Kevin Boggs: n3ds_local_match_menu_draw - present shared local bot/equipment settings. */
void n3ds_local_match_menu_draw(void)
{
 if(!local_match_open)return;
 if(local_match_gear){n3ds_gpu_local_match_gear(local_match_preview_map,local_match_pc,
  local_match_weapons,local_match_variant.universal_variant.vehicle_set,local_match_row-2);return;}
 n3ds_gpu_local_match_menu(local_match_title,local_match_bots,local_match_difficulty,
  local_match_weapons,local_match_variant.universal_variant.vehicle_set,local_match_row,local_match_error);
}

int n3ds_controls_tests(void)
{
 n3ds_controls_initialize();
 struct native_control_settings saved=settings;int ok=1;
 settings=defaults;settings.xbox_buttons=settings.swap_shoulders=1;settings.swap_xy=0;settings.swap_ab=0;
 for(unsigned int key=1;key<=NATIVE_KEY_TOUCH;key<<=1){
  unsigned int twice=n3ds_controls_buttons(n3ds_controls_buttons(key,0,1),0,1);if(twice!=key)ok=0;
 }
 if(n3ds_controls_buttons(NATIVE_KEY_A|NATIVE_KEY_X|NATIVE_KEY_R|NATIVE_KEY_ZL,0,1)!=(NATIVE_KEY_B|NATIVE_KEY_Y|NATIVE_KEY_ZR|NATIVE_KEY_L))ok=0;
 if(n3ds_controls_buttons(NATIVE_KEY_L|NATIVE_KEY_R,1,1)!=(NATIVE_KEY_L|NATIVE_KEY_R))ok=0;
 if(n3ds_controls_buttons(NATIVE_KEY_L|NATIVE_KEY_R,0,0)!=(NATIVE_KEY_L|NATIVE_KEY_R))ok=0;
 settings.xbox_buttons=0;settings.swap_shoulders=0;settings.swap_xy=1;settings.swap_ab=1;
 if(n3ds_controls_buttons(NATIVE_KEY_X|NATIVE_KEY_A,0,1)!=(NATIVE_KEY_Y|NATIVE_KEY_B))ok=0;
 settings.swap_ab=0;
 if(n3ds_controls_buttons(NATIVE_KEY_A,0,1)!=NATIVE_KEY_A)ok=0;
 settings.swap_xy=0;
 settings.touch=125;settings.cstick=75;
 if(fabs(n3ds_controls_sensitivity(1)-1.25f)>.0001f || fabs(n3ds_controls_sensitivity(0)-.75f)>.0001f)ok=0;
 struct settings_file f={0x3343544c,6,settings,0};f.checksum=checksum(&f);f.settings.touch^=1;
 if(f.checksum==checksum(&f))ok=0;
 settings.touch=0;if(valid(&settings))ok=0;settings.touch=301;if(valid(&settings))ok=0;
 settings=defaults;if(!settings.decals || n3ds_controls_decals() || settings.swap_xy || settings.swap_ab ||
                      n3ds_controls_buttons(NATIVE_KEY_X|NATIVE_KEY_A,0,1)!=(NATIVE_KEY_X|NATIVE_KEY_A))ok=0;
 settings.quality=NATIVE_QUALITY_QUALITY;if(!n3ds_controls_decals())ok=0;
 settings.quality=NATIVE_QUALITY_PERFORMANCE;if(!valid(&settings) || !n3ds_controls_performance() || n3ds_controls_quality() || n3ds_controls_decals())ok=0;
 settings.quality=3;if(valid(&settings))ok=0;
 settings.quality=NATIVE_QUALITY_BALANCED;
 settings.decals=0;if(n3ds_controls_decals() || !valid(&settings))ok=0;
 settings.decals=2;if(valid(&settings))ok=0;
 settings=saved;
 if(ok)n3ds_log("PASS: controller settings ranges, checksum, decals default/on/off, separate X/Y and A/B defaults, independent sensitivities, reversible Xbox face mapping, and New-only gameplay shoulder swap");
 return ok;
}

#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_controls_saved_matches(void){struct native_control_settings s;return read_settings(path,&s) && !memcmp(&s,&settings,sizeof(s));}
#endif
