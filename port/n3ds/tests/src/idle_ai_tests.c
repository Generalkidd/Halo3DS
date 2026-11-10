/* New3DS integration additions by Kevin Boggs. */
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
typedef int boolean;
typedef float real;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define REAL_MAX FLT_MAX
#define DATUM_INDEX_TO_ABSOLUTE_INDEX(x) ((x)&65535)
enum {_actor_mode_asleep=1,_actor_mode_alert=2,_actor_mode_combat=3};
enum {_actor_action_sleep=1,_actor_action_alert=2,_actor_action_guard=6,_actor_action_search=7};
struct vector {float i,j,k;};
struct actor_stimulus_data {unsigned int flags[8];};
/* Only engine fields read by this policy are needed by the host fixture. */
struct actor_datum {
 struct {int force_active,swarm,dormant,team_index,encounter_index;} meta;
 struct {int mode,been_in_combat,searching,flying,command_list_delay_timer,action,command_list_index,action_changed;} state;
 struct {int area_fighting_friends,visible_fighting_friends,known_enemies;} situation;
 struct {int playfighting,conversation_index;} external_orders;
 struct {int melee_attacker_attached,delayed_attached_projectile_index,vehicle_index,in_midair,burning_to_death;float recent_body_damage,recent_shield_damage;struct {struct vector velocity,body_position;} position;} input;
 struct {float analog_primary_trigger;struct vector throttle;} output;
 struct {int target_prop_index;} target;
 struct {int danger_type;} danger_zone;
 struct actor_stimulus_data stimuli;
};
struct player_datum {int unit_index;};
struct unit_datum {struct {struct vector position;int owner_team_index;} object;};
static struct player_datum players[2];
static struct unit_datum units[2];
static int tick,original,coop,connection,cinematic,player_count,hostile;
static struct encounter_datum {int force_active;} encounter;
/* Kevin Boggs: encounter_get */
struct encounter_datum *encounter_get(int index){(void)index;return &encounter;}
/* Kevin Boggs: n3ds_network_coop_active */
int n3ds_network_coop_active(void){return coop;}
/* Kevin Boggs: n3ds_input_platform_original_model */
int n3ds_input_platform_original_model(void){return original;}
/* Kevin Boggs: cinematic_in_progress */
boolean cinematic_in_progress(void){return cinematic;}
/* Kevin Boggs: game_connection */
int game_connection(void){return connection;}
/* Kevin Boggs: game_time_get */
int game_time_get(void){return tick;}
/* Kevin Boggs: local_player_get_next */
short local_player_get_next(short p){return p+1<player_count?p+1:NONE;}
/* Kevin Boggs: local_player_get_player_index */
int local_player_get_player_index(int p){return p;}
/* Kevin Boggs: player_get */
struct player_datum *player_get(int p){return &players[p];}
/* Kevin Boggs: unit_get */
struct unit_datum *unit_get(int p){return &units[p];}
/* Kevin Boggs: game_team_is_enemy */
int game_team_is_enemy(int a,int b){(void)a;(void)b;return hostile;}
/* Kevin Boggs: distance_squared3d */
float distance_squared3d(const struct vector *a,const struct vector *b){float x=a->i-b->i,y=a->j-b->j,z=a->k-b->k;return x*x+y*y+z*z;}
/* SCHEDULER_IMPLEMENTATION */
/* Kevin Boggs: main */
int main(void){
 struct actor_datum a={0};
 a.state.mode=_actor_mode_alert;a.state.action=_actor_action_guard;
 a.meta.encounter_index=NONE;
 a.state.command_list_index=a.target.target_prop_index=a.input.vehicle_index=NONE;
 a.external_orders.conversation_index=a.input.delayed_attached_projectile_index=NONE;
 player_count=1;hostile=1;players[0].unit_index=0;players[1].unit_index=1;
 units[0].object.position.i=100;
 for(original=0;original<=1;++original){
  for(int distance=20;distance<=100;distance+=5){
   units[0].object.position.i=(float)distance;int count=0;
   for(tick=0;tick<60;++tick)count+=native_decision_due(0,&a);
   int expected=distance<24?60:distance>=(original?65:80)?10:distance>=(original?40:50)?15:30;
   assert(count==expected);
  }
 }
 original=0;units[0].object.position.i=100;tick=2;
 assert(!native_decision_due(0,&a));
 a.state.mode=_actor_mode_asleep;a.state.action=_actor_action_sleep;
 assert(!native_decision_due(0,&a));
 a.state.mode=_actor_mode_alert;a.state.action=_actor_action_guard;
#define FULL(field,value) do {int old=a.field;a.field=value;assert(native_decision_due(0,&a));a.field=old;} while(0)
 FULL(meta.swarm,1);FULL(meta.dormant,1);FULL(state.command_list_index,0);FULL(state.action_changed,1);
 FULL(target.target_prop_index,0);FULL(situation.known_enemies,1);FULL(danger_zone.danger_type,1);
 FULL(input.vehicle_index,0);FULL(input.in_midair,1);FULL(input.burning_to_death,1);
 FULL(external_orders.conversation_index,0);FULL(stimuli.flags[3],1);
 a.input.recent_body_damage=.1f;assert(native_decision_due(0,&a));a.input.recent_body_damage=0;
 a.input.recent_shield_damage=.1f;assert(native_decision_due(0,&a));a.input.recent_shield_damage=0;
 coop=1;assert(native_decision_due(0,&a));coop=0;
 connection=1;assert(native_decision_due(0,&a));connection=0;
 cinematic=1;assert(native_decision_due(0,&a));cinematic=0;
 player_count=0;assert(native_decision_due(0,&a));player_count=1;
 players[0].unit_index=NONE;assert(native_decision_due(0,&a));players[0].unit_index=0;
 /* These keep the existing two-tick policy, never the new idle reduction. */
 FULL(meta.force_active,1);FULL(state.been_in_combat,1);FULL(state.searching,1);FULL(state.flying,1);
 FULL(state.mode,_actor_mode_combat);FULL(state.action,_actor_action_search);
 FULL(state.command_list_delay_timer,1);FULL(situation.area_fighting_friends,1);
 FULL(situation.visible_fighting_friends,1);FULL(external_orders.playfighting,1);
 FULL(input.melee_attacker_attached,1);FULL(input.delayed_attached_projectile_index,0);
 a.output.throttle.i=1;assert(native_decision_due(0,&a));a.output.throttle.i=0;
 a.input.position.velocity.k=.01f;assert(native_decision_due(0,&a));a.input.position.velocity.k=0;
 a.output.analog_primary_trigger=1;assert(native_decision_due(0,&a));a.output.analog_primary_trigger=0;
 hostile=0;assert(native_decision_due(0,&a));hostile=1;
 a.meta.encounter_index=0;encounter.force_active=1;assert(native_decision_due(0,&a));a.meta.encounter_index=NONE;
 player_count=2;units[1].object.position.i=10;assert(native_decision_due(0,&a));player_count=1;
 /* Phase staggering: one of six actors serviced per tick, no shared burst. */
 for(tick=0;tick<60;++tick){int count=0;for(int actor=0;actor<6;++actor)count+=native_decision_due(actor,&a);assert(count==1);}
 /* Alert after a deferred tick must be handled on the very next tick. */
 tick=1;assert(!native_decision_due(0,&a));tick=2;a.stimuli.flags[0]=1;assert(native_decision_due(0,&a));
 puts("PASS: idle AI cadence, model tiers, stagger, wakeup, scripts, movement, allies and network exclusions");
 return 0;
}
