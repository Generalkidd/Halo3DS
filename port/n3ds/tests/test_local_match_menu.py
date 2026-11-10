"""Exercise the production local-match modal with host input/engine stubs."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "port/n3ds/src/engine_controls.c").read_text()
body = source[source.index("/* A preview owns"):source.index("int n3ds_controls_tests(void)")]
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_controls.h"
#define PIN(x,a,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
struct game_variant {
 unsigned short human_readable_game_description[12];
 long game_engine_index;
 struct {long weapon_set,vehicle_set,score_limit,teams;} universal_variant;
 unsigned char mode_rules[32];
};
static int wireless,menu=1,widgets=1,controls,pc,queue_ok=1,queues,bot_count,bot_difficulty=1;
static int pc_match_weapon_override=12;
static struct game_variant queued;
static char queued_map[96];
static int browser_selection=-1,preview_closes,preview_draws,preview_map,preview_pc,preview_ready;
void n3ds_gpu_local_match_gear(int map,int pc_map,int w,int v,int selected){
 ++preview_draws;preview_map=map;preview_pc=pc_map;(void)w;(void)v;(void)selected;
}
int n3ds_ui_map_browser_selection(void){return browser_selection;}
void n3ds_pc_preview_close(void){++preview_closes;}
void n3ds_pc_preview_input(const struct native_input_sample *sample){(void)sample;}
void n3ds_gpu_map_browser(int map,int pc_map,int ready){++preview_draws;preview_map=map;preview_pc=pc_map;preview_ready=ready;}

int n3ds_controls_active(void){return controls;}
int n3ds_pc_local_menu_active(void){return pc;}
int n3ds_wireless_active(void){return wireless;}
int main_menu_is_active(void){return menu;}
int ui_widgets_active(void){return widgets;}
short n3ds_bots_requested_count(void){return (short)bot_count;}
short n3ds_bots_menu_difficulty(void){return (short)bot_difficulty;}
void n3ds_bots_set_menu_count(short n){bot_count=n;}
void n3ds_bots_set_menu_difficulty(short n){bot_difficulty=n;}
void n3ds_log(const char *s){(void)s;}
int n3ds_main_queue_solo_multiplayer(const char *m,const struct game_variant *v,int controller){
 assert(controller==0);++queues;if(!queue_ok)return 0;strcpy(queued_map,m);queued=*v;return 1;
}
void n3ds_gpu_local_match_menu(const char *t,int b,int d,int w,int v,int s,int e){
 (void)t;(void)b;(void)d;(void)w;(void)v;(void)s;(void)e;
}
'''
tests = r'''
static void input(unsigned key,unsigned x,unsigned y){
 struct native_input_sample sample={0};sample.held=key;sample.touch_x=x;sample.touch_y=y;
 n3ds_local_match_menu_filter(&sample);assert(!sample.held);
}
static void tap(unsigned key){input(0,0,0);input(key,0,0);input(0,0,0);}
int main(void){
 struct game_variant variant={0};variant.game_engine_index=2;
 variant.universal_variant.score_limit=73;variant.universal_variant.teams=1;
 memset(variant.mode_rules,0x5a,sizeof(variant.mode_rules));
 const char *map="levels\\test\\bloodgulch\\bloodgulch";
 wireless=1;assert(!n3ds_local_match_menu_open(map,&variant,0));wireless=0;
 assert(!n3ds_local_match_menu_open(map,&variant,1));
 assert(!n3ds_local_match_menu_open(NULL,&variant,0));
 assert(!n3ds_local_match_menu_open(map,NULL,0));
#ifdef HALO_N3DS_PC_LOCAL_MENU
 pc=1;assert(!n3ds_local_match_menu_open(map,&variant,0));pc=0;
#endif
 assert(n3ds_local_match_menu_open(map,&variant,0));
 input(NATIVE_KEY_A,0,0);assert(!queues); /* opening press cannot start */
 tap(NATIVE_KEY_DRIGHT);tap(NATIVE_KEY_DRIGHT); /* two bots */
 tap(NATIVE_KEY_DDOWN);tap(NATIVE_KEY_DRIGHT); /* hard */
 input(NATIVE_KEY_TOUCH,289,115);input(0,0,0); /* weapons */
 input(NATIVE_KEY_TOUCH,289,143);input(0,0,0); /* vehicles */
 queue_ok=0;tap(NATIVE_KEY_A);assert(n3ds_local_match_menu_active());
 assert(!bot_count && bot_difficulty==1); /* failed queue commits nothing */
 queue_ok=1;tap(NATIVE_KEY_START);assert(!n3ds_local_match_menu_active());
 assert(bot_count==2 && bot_difficulty==2 && queues==2);
 assert(!strcmp(queued_map,map));
 variant.universal_variant.weapon_set=1;variant.universal_variant.vehicle_set=1;
 assert(!memcmp(&queued,&variant,sizeof(variant))); /* untouched preset fields */
#ifdef HALO_N3DS_PC_LOCAL_MENU
 assert(!pc_match_weapon_override);
#endif
 assert(n3ds_local_match_menu_open(map,&variant,0));tap(NATIVE_KEY_DRIGHT);tap(NATIVE_KEY_B);
 assert(!n3ds_local_match_menu_active() && bot_count==2 && queues==2);
 assert(n3ds_local_match_menu_open(map,&variant,0));input(0,0,0);wireless=1;input(NATIVE_KEY_A,0,0);
 assert(!n3ds_local_match_menu_active() && queues==2);
 wireless=0;
 /* Gear changes share the same state as the settings page; Done never starts. */
 assert(n3ds_local_match_menu_open(map,&variant,0));tap(NATIVE_KEY_X);
 assert(local_match_gear);n3ds_local_match_menu_draw();assert(preview_map==1 && !preview_pc);
 tap(NATIVE_KEY_DLEFT);tap(NATIVE_KEY_DLEFT);assert(local_match_weapons==10); /* Xbox wraps before PC extras */
 input(NATIVE_KEY_TOUCH,289,156); /* back to Normal */
 input(NATIVE_KEY_TOUCH,289,183);assert(local_match_row==2); /* held drag cannot change Vehicles */
 input(0,0,0);input(NATIVE_KEY_TOUCH,289,183);input(0,0,0);
 assert(local_match_row==3 && local_match_variant.universal_variant.vehicle_set==2);
 tap(NATIVE_KEY_A);assert(!local_match_gear && n3ds_local_match_menu_active() && queues==2);
 assert(local_match_weapons==0);tap(NATIVE_KEY_START);assert(queues==3 && queued.universal_variant.weapon_set==0);
 assert(queued.universal_variant.vehicle_set==2 && queued.universal_variant.score_limit==73);
 assert(n3ds_local_match_menu_open(map,&variant,0));input(0,0,0);input(NATIVE_KEY_TOUCH,40,180);input(0,0,0);
 assert(local_match_gear);tap(NATIVE_KEY_X);tap(NATIVE_KEY_B);assert(queues==3);
#ifdef HALO_N3DS_PC_LOCAL_MENU
 assert(n3ds_local_match_menu_open("pc\\levels\\test\\bloodgulch\\bloodgulch",&variant,0));tap(NATIVE_KEY_X);
 n3ds_local_match_menu_draw();assert(preview_map==1 && preview_pc);
 tap(NATIVE_KEY_DLEFT);tap(NATIVE_KEY_DLEFT);assert(local_match_weapons==12);
 tap(NATIVE_KEY_B);queue_ok=0;tap(NATIVE_KEY_A);assert(!pc_match_weapon_override);
 queue_ok=1;tap(NATIVE_KEY_A);assert(pc_match_weapon_override==2 && queued.universal_variant.weapon_set==0);
#endif
 assert(n3ds_local_match_menu_open(map,&variant,0));tap(NATIVE_KEY_X);widgets=0;input(0,0,0);
 assert(!n3ds_local_match_menu_active() && !local_match_gear);widgets=1;
 preview_closes=0;browser_selection=9;
 struct native_input_sample sample={0};sample.held=NATIVE_KEY_DRIGHT|NATIVE_KEY_TOUCH|NATIVE_KEY_R;
 n3ds_map_browser_filter(&sample);assert(sample.held==NATIVE_KEY_DRIGHT);
 n3ds_map_browser_draw();assert(preview_map==1 && !preview_pc && !preview_ready);
 for(int i=0;i<12;++i)n3ds_map_browser_filter(&sample);
 n3ds_map_browser_draw();assert(preview_ready);
 browser_selection=10;n3ds_map_browser_filter(&sample);assert(preview_closes==1);
 n3ds_map_browser_draw();assert(preview_map==18 && !preview_ready);
 for(int i=0;i<12;++i)n3ds_map_browser_filter(&sample);
 n3ds_map_browser_draw();controls=1;n3ds_map_browser_filter(&sample);assert(preview_closes==2);
 int drawn=preview_draws;n3ds_map_browser_draw();assert(preview_draws==drawn);controls=0;
 browser_selection=33;n3ds_map_browser_filter(&sample);n3ds_map_browser_draw();assert(preview_map==1 && preview_pc);
 browser_selection=13;n3ds_map_browser_filter(&sample);drawn=preview_draws;n3ds_map_browser_draw();assert(preview_draws==drawn);
 /* Postgame returns to the successful match, not cancelled settings. */
 assert(n3ds_local_match_menu_queue_reopen());
 menu=0;sample.held=0;n3ds_local_match_menu_filter(&sample);assert(!local_match_open);
 menu=1;sample.held=0;n3ds_local_match_menu_filter(&sample);assert(local_match_open);
#ifdef HALO_N3DS_PC_LOCAL_MENU
 assert(local_match_pc && local_match_weapons==12);
 assert(!strcmp(local_match_map,"pc\\levels\\test\\bloodgulch\\bloodgulch"));
#else
 assert(!local_match_pc && local_match_weapons==0);
#endif
 assert(local_match_variant.universal_variant.score_limit==73 && local_match_bots==2 && local_match_difficulty==2);
 tap(NATIVE_KEY_B);wireless=1;assert(!n3ds_local_match_menu_queue_reopen());wireless=0;
 puts("PASS: successful match snapshot survives gameplay and cancelled edits; wireless excluded");
 puts("PASS: browser map identities, debounce, gesture isolation and cleanup on leaving the browser");
 puts("PASS: local settings, preserved gametype, touch/keys, opening debounce, Back, failed launch, wireless exclusion");
 return 0;
}
'''
command=sys.argv[1:]
if command and command[0]=="--":command.pop(0)
if not command:command=["clang"]
with tempfile.TemporaryDirectory(prefix="halo-local-match-",ignore_cleanup_errors=True) as temp:
 path=Path(temp);c=path/"test.c";c.write_text(prefix+body+tests)
 for pc in (False,True):
  exe=path/("test.exe" if sys.platform=="win32" else "test")
  subprocess.run(command+["-std=c11","-I"+str(ROOT/"port/n3ds/include")]+(["-DHALO_N3DS_PC_LOCAL_MENU"] if pc else [])+[str(c),"-o",str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
