#include "cseries.h"
#include "engine_controls.h"
#include "engine_files.h"
#include "interface/ui_widget.h"

void n3ds_log(const char *message);
void n3ds_ui_controls_back(void);
static const struct native_control_settings defaults={100,100,0,0};
static struct native_control_settings settings={100,100,0,0},opened_settings;
static int initialized,modal,main_route,selected,save_error,repeat_ticks,swallow;
static unsigned int previous_keys;
static const char path[]="t:\\controller-settings.bin";
static const char settings_temporary[]="t:\\controller-settings.tmp";
static const char backup[]="t:\\controller-settings.bak";
struct settings_file {unsigned int magic,version;struct native_control_settings settings;unsigned int checksum;};

static int valid(const struct native_control_settings *s)
{return s->cstick>=25 && s->cstick<=300 && s->touch>=25 && s->touch<=300 && s->xbox_buttons<=1 && s->swap_shoulders<=1;}
static unsigned int checksum(const struct settings_file *f)
{
 const unsigned char *p=(const unsigned char *)f;unsigned int h=2166136261u;
 for(unsigned int i=0;i<sizeof(*f)-4;++i)h=(h^p[i])*16777619u;
 return h;
}
static int read_settings(const char *name,struct native_control_settings *s)
{
 struct settings_file f;unsigned int h=n3ds_file_open(name,1);if(!h)return 0;
 int ok=n3ds_file_size(h)==sizeof(f) && n3ds_file_read(h,&f,sizeof(f));
 ok=n3ds_file_close(h) && ok;
 if(!ok || f.magic!=0x3343544c || f.version!=1 || f.checksum!=checksum(&f) || !valid(&f.settings))return 0;
 *s=f.settings;return 1;
}
static int save_settings(void)
{
 struct settings_file f={0x3343544c,1,settings,0};f.checksum=checksum(&f);
 if(!n3ds_file_create(settings_temporary,0))return 0;
 unsigned int h=n3ds_file_open(settings_temporary,3);if(!h)return 0;
 int ok=n3ds_file_resize(h,0) && n3ds_file_write(h,&f,sizeof(f));ok=n3ds_file_close(h) && ok;
 struct native_control_settings verified;
 if(!ok || !read_settings(settings_temporary,&verified) || memcmp(&verified,&settings,sizeof(settings)))return 0;
 struct native_file_info info;
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
static unsigned int exchange(unsigned int held,unsigned int a,unsigned int b)
{unsigned int result=held&~(a|b);if(held&a)result|=b;if(held&b)result|=a;return result;}
unsigned int n3ds_controls_buttons(unsigned int held,int original,int gameplay)
{
 if(settings.xbox_buttons){held=exchange(held,NATIVE_KEY_A,NATIVE_KEY_B);held=exchange(held,NATIVE_KEY_X,NATIVE_KEY_Y);}
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
 if(selected<2){unsigned int *v=selected?&settings.touch:&settings.cstick;int value=(int)*v+delta*5;*v=value<25?25:value>300?300:value;}
 else if(selected==2)settings.xbox_buttons^=1;
 else if(!n3ds_input_platform_original_model())settings.swap_shoulders^=1;
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
 int count=n3ds_input_platform_original_model()?3:4;
 if(fresh&NATIVE_KEY_DUP)selected=(selected+count-1)%count;
 if(fresh&NATIVE_KEY_DDOWN)selected=(selected+1)%count;
 if(fresh&NATIVE_KEY_DLEFT)adjust(-1);
 if(fresh&NATIVE_KEY_DRIGHT)adjust(1);
 if(fresh&NATIVE_KEY_TOUCH){
  unsigned int x=s->touch_x,y=s->touch_y;
  if(y>=42 && y<170){int row=(y-42)/32;if(row<count){selected=row;if(x>=218)adjust(x<260?-1:1);}}
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
 settings.touch=125;settings.cstick=75;
 if(fabs(n3ds_controls_sensitivity(1)-1.25f)>.0001f || fabs(n3ds_controls_sensitivity(0)-.75f)>.0001f)ok=0;
 struct settings_file f={0x3343544c,1,settings,0};f.checksum=checksum(&f);f.settings.touch^=1;
 if(f.checksum==checksum(&f))ok=0;
 settings.touch=0;if(valid(&settings))ok=0;settings.touch=301;if(valid(&settings))ok=0;
 settings=saved;
 if(ok)n3ds_log("PASS: controller settings ranges, checksum, independent sensitivities, reversible Xbox face mapping, and New-only gameplay shoulder swap");
 return ok;
}

#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_controls_saved_matches(void){struct native_control_settings s;return read_settings(path,&s) && !memcmp(&s,&settings,sizeof(s));}
#endif
