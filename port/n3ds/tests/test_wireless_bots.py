"""Host tests of the production wireless bot roster and lobby policy."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'source/networking/network_server_manager.c').read_text()
a=s.index('/* Kevin Boggs: n3ds_network_bot_options');b=s.index('\n#endif',a)
body="static void network_bots_prepare(struct network_game_server *server);\n"+s[a:b]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define NONE -1
#define TRUE 1
#define _network_game_server_state_pregame 0
struct network_player {unsigned short name[12];short primary_color_index,icon_index;char machine_index,controller_index,team_index,player_list_index;};
struct network_game {struct {char name[128];} map;short player_count;struct network_player players[16];};
struct network_game_server {int state,sent_start_game_message;struct {int active;} countdown_state;struct network_game game;};
static struct network_game_server server;
static int is_host=1,host_machine=0,difficulty_set=-1;
static int wireless_bot_count,wireless_bot_difficulty=1;
struct network_game_server *global_network_game_server_get(void){return is_host?&server:NULL;}
int n3ds_network_campaign_level(const char *m){return !strcmp(m,"campaign")?0:-1;}
int network_game_client_get_local_machine_index(void){return host_machine;}
int network_player_is_valid(struct network_player *p){return p->machine_index>=0 && p->machine_index<4 && p->controller_index>=0 && p->controller_index<4;}
void network_game_invalidate_player(struct network_player *p){memset(p,0,sizeof(*p));p->machine_index=p->controller_index=p->player_list_index=NONE;}
int network_game_add_player(struct network_game *g,struct network_player *p){
 if(g->player_count>=16)return 0;
 for(int i=0;i<16;++i)if(!network_player_is_valid(&g->players[i])){g->players[i]=*p;g->players[i].player_list_index=i;++g->player_count;return 1;}return 0;
}
int network_game_server_send_game_data_pregame(struct network_game_server *s){assert(s==&server);return 1;}
void network_game_server_pause_countdown(struct network_game_server *s,int pause){assert(s==&server && pause);s->countdown_state.active=0;}
void n3ds_bots_set_menu_difficulty(short n){difficulty_set=n;}
'''
tests=r'''
int main(void){
 for(int i=0;i<16;++i)network_game_invalidate_player(&server.game.players[i]);
 struct network_player human={0};human.machine_index=0;human.team_index=0;assert(network_game_add_player(&server.game,&human));
 human.machine_index=1;human.team_index=1;assert(network_game_add_player(&server.game,&human));
 struct network_player preserved[2];memcpy(preserved,server.game.players,sizeof(preserved));
 int n,d;assert(n3ds_network_bot_options(-1,-1,&n,&d) && n==3 && d==0);
 network_bots_prepare(&server);assert(server.game.player_count==5 && difficulty_set==0);
 assert(!memcmp(preserved,server.game.players,sizeof(preserved)));
 for(int i=2;i<5;++i){assert(server.game.players[i].machine_index==0);assert(server.game.players[i].controller_index==i-1);}
 network_bots_prepare(&server);assert(server.game.player_count==5); /* retry/rematch has no duplicates */
 is_host=0;assert(!n3ds_network_bot_options(1,1,&n,&d));is_host=1;
 server.countdown_state.active=1;assert(n3ds_network_bot_options(0,0,&n,&d));
 assert(n3ds_network_bot_options(0,1,&n,&d) && !server.countdown_state.active);
 assert(n3ds_network_bot_options(0,-1,&n,&d));
 server.sent_start_game_message=1;assert(!n3ds_network_bot_options(1,1,&n,&d));server.sent_start_game_message=0;
 server.state=1;assert(!n3ds_network_bot_options(1,1,&n,&d));server.state=0;
 strcpy(server.game.map.name,"campaign");assert(!n3ds_network_bot_options(1,1,&n,&d));
 network_bots_prepare(&server);assert(server.game.player_count==2); /* no extras in co-op */
 server.game.map.name[0]=0;assert(n3ds_network_bot_options(1,1,&n,&d) && n==0 && d==1);
 network_bots_prepare(&server);assert(server.game.player_count==2);
 puts("PASS: host-only bot choices, count/difficulty bounds, human preservation, duplicate-free rematch, co-op removal");
}
'''
cmd=sys.argv[1:];cmd=cmd[1:] if cmd and cmd[0]=='--' else cmd
with tempfile.TemporaryDirectory(prefix='halo-wireless-bots-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+body+tests)
 subprocess.run(cmd+[str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
