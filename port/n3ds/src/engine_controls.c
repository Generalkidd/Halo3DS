#include "cseries.h"
#include "engine_controls.h"
#include "engine_files.h"
#include "interface/ui_widget.h"
#ifdef HALO_N3DS_PC_LOCAL_MENU
#include "main/main.h"
#include "game/game_engine.h"
#include "engine_bots.h"
#include "pc_diagnostic_maps.h"
int n3ds_pc_tags_active(void);
#endif

void n3ds_log(const char *message);
void n3ds_ui_controls_back(void);
static const struct native_control_settings defaults={100,100,0,0,0,0,0};
static struct native_control_settings settings={100,100,0,0,0,0,0},opened_settings;
static int initialized,modal,main_route,selected,save_error,repeat_ticks,swallow;
static unsigned int previous_keys;
static const char path[]="t:\\controller-settings.bin";
static const char settings_temporary[]="t:\\controller-settings.tmp";
static const char backup[]="t:\\controller-settings.bak";
struct legacy_control_settings {unsigned int cstick,touch,xbox_buttons,swap_shoulders;};
struct legacy_settings_file {unsigned int magic,version;struct legacy_control_settings settings;unsigned int checksum;};
struct settings_file_v2 {unsigned int magic,version;unsigned int cstick,touch,xbox_buttons,swap_shoulders,high_textures;unsigned int checksum;};
struct settings_file {unsigned int magic,version;struct native_control_settings settings;unsigned int checksum;};

static int valid(const struct native_control_settings *s)
{return s->cstick>=25 && s->cstick<=300 && s->touch>=25 && s->touch<=300 && s->xbox_buttons<=1 && s->swap_shoulders<=1 && s->high_textures<=1 && s->swap_xy<=1 && s->show_fps<=1;}
static unsigned int checksum_bytes(const void *data,unsigned int size)
{
 const unsigned char *p=(const unsigned char *)data;unsigned int h=2166136261u;
 for(unsigned int i=0;i<size-4;++i)h=(h^p[i])*16777619u;
 return h;
}
static unsigned int checksum(const struct settings_file *f){return checksum_bytes(f,sizeof(*f));}
static int read_settings(const char *name,struct native_control_settings *s)
{
 struct settings_file f;struct settings_file_v2 prior;struct legacy_settings_file old;
 unsigned int h=n3ds_file_open(name,1);if(!h)return 0;
 unsigned int size=n3ds_file_size(h);
 int ok=size==sizeof(f)?n3ds_file_read(h,&f,sizeof(f)):
     size==sizeof(prior)?n3ds_file_read(h,&prior,sizeof(prior)):
     size==sizeof(old)?n3ds_file_read(h,&old,sizeof(old)):0;
 ok=n3ds_file_close(h) && ok;
 if(!ok)return 0;
 if(size==sizeof(old)){
  if(old.magic!=0x3343544c || old.version!=1 || old.checksum!=checksum_bytes(&old,sizeof(old)))return 0;
  *s=defaults;s->cstick=old.settings.cstick;s->touch=old.settings.touch;
  s->xbox_buttons=old.settings.xbox_buttons;s->swap_shoulders=old.settings.swap_shoulders;
 }else if(size==sizeof(prior)){
  if(prior.magic!=0x3343544c || prior.version!=2 || prior.checksum!=checksum_bytes(&prior,sizeof(prior)))return 0;
  *s=defaults;s->cstick=prior.cstick;s->touch=prior.touch;s->xbox_buttons=prior.xbox_buttons;
  s->swap_shoulders=prior.swap_shoulders;s->high_textures=prior.high_textures;
 }else{
  if(f.magic!=0x3343544c || f.version!=3 || f.checksum!=checksum(&f))return 0;
  *s=f.settings;
 }
 return valid(s);
}
static int save_settings(void)
{
 struct settings_file f={0x3343544c,3,settings,0};f.checksum=checksum(&f);
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
int n3ds_controls_high_textures(void){n3ds_controls_initialize();return settings.high_textures!=0;}
int n3ds_controls_show_fps(void){n3ds_controls_initialize();return settings.show_fps!=0;}
static unsigned int exchange(unsigned int held,unsigned int a,unsigned int b)
{unsigned int result=held&~(a|b);if(held&a)result|=b;if(held&b)result|=a;return result;}
unsigned int n3ds_controls_buttons(unsigned int held,int original,int gameplay)
{
 if(settings.xbox_buttons){held=exchange(held,NATIVE_KEY_A,NATIVE_KEY_B);held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);}
 if(settings.swap_xy)held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);
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
static void adjust(int delta)
{
 int field=selected>=3 && n3ds_input_platform_original_model()?selected+2:selected;
 if(field<2){unsigned int *v=field?&settings.touch:&settings.cstick;int value=(int)*v+delta*5;*v=value<25?25:value>300?300:value;}
 else if(field==2)settings.xbox_buttons^=1;
 else if(field==3)settings.swap_shoulders^=1;
 else if(field==4)settings.high_textures^=1;
 else if(field==5)settings.swap_xy^=1;
 else if(field==6)settings.show_fps^=1;
 save_error=0;
}
void n3ds_controls_filter(struct native_input_sample *s)
{
 unsigned int held=s->held,fresh=held&~previous_keys;previous_keys=held;
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
 int count=n3ds_input_platform_original_model()?5:7;
 if(fresh&NATIVE_KEY_DUP)selected=(selected+count-1)%count;
 if(fresh&NATIVE_KEY_DDOWN)selected=(selected+1)%count;
 if(fresh&NATIVE_KEY_DLEFT)adjust(-1);
 if(fresh&NATIVE_KEY_DRIGHT)adjust(1);
 if(fresh&NATIVE_KEY_TOUCH){
  unsigned int x=s->touch_x,y=s->touch_y;
  if(y>=24 && y<185){int row=(y-24)/23;if(row<count){selected=row;if(x>=242)adjust(x<273?-1:1);}}
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
}
#ifdef HALO_N3DS_PC_LOCAL_MENU
static int pc_menu_open,pc_menu_page,pc_menu_field,pc_menu_map,pc_menu_mode,pc_menu_bots,pc_menu_difficulty=1;
static int pc_menu_weapon_set,pc_menu_vehicle_choice,pc_menu_error,pc_menu_swallow;
static int pc_match_weapon_override;
int n3ds_pc_local_weapon_override(void){return pc_match_weapon_override;}
static unsigned int pc_menu_previous;
static const char *const pc_menu_modes[]={"slayer","ctf","oddball","king","race"};
static int pc_menu_available(void)
{
#ifdef HALO_N3DS_UNIFIED_UI
 return main_menu_is_active();
#else
 return main_menu_is_active() && n3ds_pc_tags_active();
#endif
}
static int pc_menu_field_count(void)
{return pc_menu_page?2:n3ds_input_platform_original_model()?4:5;}
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
int n3ds_pc_local_menu_active(void){return pc_menu_open;}
void n3ds_pc_local_menu_filter(struct native_input_sample *sample)
{
 unsigned int held=sample->held,fresh=held&~pc_menu_previous;
 pc_menu_previous=held;
 if(pc_menu_swallow){if(!held)pc_menu_swallow=0;memset(sample,0,sizeof(*sample));return;}
 if(!pc_menu_open){
  if((fresh&NATIVE_KEY_SELECT) && pc_menu_available() && !n3ds_controls_active()){
   pc_menu_open=1;pc_menu_page=0;pc_menu_field=0;pc_menu_error=0;
   pc_menu_bots=n3ds_bots_requested_count();
   pc_menu_swallow=1;n3ds_log("PC LOCAL MENU: opened");memset(sample,0,sizeof(*sample));
  }
  return;
 }
 if(!pc_menu_available()){
  pc_menu_open=0;pc_menu_swallow=1;memset(sample,0,sizeof(*sample));return;
 }
 if(fresh&NATIVE_KEY_B){
  if(pc_menu_page){pc_menu_page=0;pc_menu_field=0;}
  else {pc_menu_open=0;pc_menu_swallow=1;n3ds_log("PC LOCAL MENU: closed");}
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
  }else if(pc_menu_field==0)pc_menu_map=(pc_menu_map+19+delta)%19;
  else if(pc_menu_field==1)pc_menu_mode=(pc_menu_mode+5+delta)%5;
  else if(pc_menu_field==2)pc_menu_bots=(pc_menu_bots+16+delta)%16;
  else if(pc_menu_field==3)pc_menu_difficulty=(pc_menu_difficulty+3+delta)%3;
  else {pc_menu_toggle_textures();delta=0;}
  if(delta)pc_menu_error=0;
 }
 else if((fresh&NATIVE_KEY_TOUCH) && sample->touch_x>=12 && sample->touch_x<308 &&
         sample->touch_y>=(pc_menu_page?40:27) && sample->touch_y<(pc_menu_page?124:192)){
  int row=pc_menu_page?(sample->touch_y-40)/42:(sample->touch_y-27)/33;
  if(row<pc_menu_field_count()){
   pc_menu_field=row;
   if(pc_menu_page){
    if(row==0)pc_menu_weapon_set=(pc_menu_weapon_set+1)%13;
    else pc_menu_vehicle_choice=(pc_menu_vehicle_choice+1)%6;
   }else if(row==3)pc_menu_difficulty=(pc_menu_difficulty+1)%3;
   else if(row==4)pc_menu_toggle_textures();
   else pc_menu_error=0;
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
  int length=snprintf(map,sizeof(map),"levels\\test\\%s\\%s",name,name);
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
   snprintf(message,sizeof(message),"PC LOCAL MENU: queued %s mode=%s bots=%d difficulty=%d weapons=%d vehicles=%d",map,pc_menu_modes[pc_menu_mode],pc_menu_bots,pc_menu_difficulty,pc_menu_weapon_set,variant.universal_variant.vehicle_set);
   n3ds_log(message);pc_menu_open=0;pc_menu_swallow=1;
  }
 }
 memset(sample,0,sizeof(*sample));
}
void n3ds_pc_local_menu_draw(void)
{if(pc_menu_open)n3ds_gpu_pc_local_menu(pc_menu_map,pc_menu_mode,pc_menu_bots,pc_menu_difficulty,
 pc_menu_weapon_set,pc_menu_vehicle_choice,pc_menu_page,pc_menu_field,pc_menu_error,
 n3ds_controls_high_textures(),n3ds_input_platform_original_model());}
#endif
int n3ds_controls_tests(void)
{
 struct native_control_settings saved=settings;int ok=1;
 settings=defaults;settings.xbox_buttons=settings.swap_shoulders=1;
 for(unsigned int key=1;key<=NATIVE_KEY_TOUCH;key<<=1){
  unsigned int twice=n3ds_controls_buttons(n3ds_controls_buttons(key,0,1),0,1);if(twice!=key)ok=0;
 }
 if(n3ds_controls_buttons(NATIVE_KEY_A|NATIVE_KEY_X|NATIVE_KEY_R|NATIVE_KEY_ZL,0,1)!=(NATIVE_KEY_B|NATIVE_KEY_Y|NATIVE_KEY_ZR|NATIVE_KEY_L))ok=0;
 if(n3ds_controls_buttons(NATIVE_KEY_L|NATIVE_KEY_R,1,1)!=(NATIVE_KEY_L|NATIVE_KEY_R))ok=0;
 if(n3ds_controls_buttons(NATIVE_KEY_L|NATIVE_KEY_R,0,0)!=(NATIVE_KEY_L|NATIVE_KEY_R))ok=0;
 settings.xbox_buttons=0;settings.swap_shoulders=0;settings.swap_xy=1;
 if(n3ds_controls_buttons(NATIVE_KEY_X|NATIVE_KEY_A,0,1)!=(NATIVE_KEY_Y|NATIVE_KEY_A))ok=0;
 settings.swap_xy=0;
 settings.touch=125;settings.cstick=75;
 if(fabs(n3ds_controls_sensitivity(1)-1.25f)>.0001f || fabs(n3ds_controls_sensitivity(0)-.75f)>.0001f)ok=0;
 struct settings_file f={0x3343544c,3,settings,0};f.checksum=checksum(&f);f.settings.touch^=1;
 if(f.checksum==checksum(&f))ok=0;
 settings.touch=0;if(valid(&settings))ok=0;settings.touch=301;if(valid(&settings))ok=0;
 settings=saved;
 if(ok)n3ds_log("PASS: controller settings ranges, checksum, independent sensitivities, reversible Xbox face mapping, and New-only gameplay shoulder swap");
 return ok;
}

#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_controls_saved_matches(void){struct native_control_settings s;return read_settings(path,&s) && !memcmp(&s,&settings,sizeof(s));}
#endif
