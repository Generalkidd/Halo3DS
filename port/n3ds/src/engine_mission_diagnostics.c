#include "cseries.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "physics/collisions.h"
void n3ds_log(const char *message);
void __real_free(void *pointer);
void __real_debug_free(void *pointer,const char *file,long line);
static int trace,logging;
void n3ds_mission_trace_free(int enabled) { trace=enabled; }
/* Opt-in integration fixture: an ordinary map-owned cyborg, with no actor,
 * player or scripted damage. Its physics, shields, damage and death remain
 * owned by the original game. Never used in the ordinary Wizard build. */
int n3ds_mission_target_step(long unit_index,long time)
{
    static long target=NONE;
    static real previous_body=-1,previous_shield=-1;
    static word previous_damage_flags;
    char message[320];
    if(time<30) return 0;
    if(time==30) {
        struct unit_datum *player=unit_get(unit_index);
        struct object_placement_data placement;
        struct collision_result floor,obstacle;
        real_vector3d direction,down={0,0,-3},sight;
        real_point3d start,eye,end;
        const unsigned long flags=FLAG(_collision_test_front_facing_surfaces_bit)|FLAG(_collision_test_back_facing_surfaces_bit)|FLAG(_collision_test_structure_bit);
        unit_get_aiming_vector(unit_index,&direction);
        direction.k=0;
        real length=sqrtf(direction.i*direction.i+direction.j*direction.j);
        if(!isfinite(length) || length<.1f) return 1;
        direction.i/=length;direction.j/=length;
        start=player->object.position;
        start.x+=direction.i*1.5f;start.y+=direction.j*1.5f;start.z+=1.f;
        if(!collision_test_vector(flags,&start,&down,unit_index,&floor) || floor.type!=_collision_result_structure || floor.plane.n.k<.5f) {
            n3ds_log("TARGET FAIL: no walkable structure floor in front of player");return 1;
        }
        unit_get_camera_position(unit_index,&eye);
        end=floor.point;end.z+=eye.z-player->object.position.z;
        sight.i=end.x-eye.x;sight.j=end.y-eye.y;sight.k=end.z-eye.z;
        if(collision_test_vector(flags,&eye,&sight,unit_index,&obstacle)) {
            n3ds_log("TARGET FAIL: structure blocks target's initial line of sight");return 1;
        }
        object_placement_data_new(&placement,player->definition_index,NONE);
        placement.position=floor.point;placement.position.z+=.01f;
        placement.forward.i=-direction.i;placement.forward.j=-direction.j;placement.forward.k=0;
        for(int i=0;i<NUMBER_OF_OBJECT_CHANGE_COLORS;++i) {
            placement.change_colors[i].red=1;placement.change_colors[i].green=.1f;placement.change_colors[i].blue=.1f;
        }
        target=object_new(&placement);
        if(target==NONE || !unit_try_and_get(target)) {n3ds_log("TARGET FAIL: original object_new failed");return 1;}
        snprintf(message,sizeof(message),"TARGET FIXTURE: stationary original cyborg; no AI or injected damage; time=%ld object=%08lx definition=%08lx player=%08lx position=%.6f,%.6f,%.6f",time,target,placement.definition_index,unit_index,placement.position.x,placement.position.y,placement.position.z);
        n3ds_log(message);
    }
    struct unit_datum *unit=unit_try_and_get(target);
    if(!unit) {n3ds_log("TARGET: original object removed");return 0;}
    if(time%30==0 || unit->object.body_vitality!=previous_body || unit->object.shield_vitality!=previous_shield || unit->object.damage_flags!=previous_damage_flags) {
        snprintf(message,sizeof(message),"TARGET STATE: time=%ld object=%08lx body=%.6f shield=%.6f max_body=%.6f max_shield=%.6f flags=%04x dead=%d position=%.6f,%.6f,%.6f",time,target,unit->object.body_vitality,unit->object.shield_vitality,unit->object.maximum_body_vitality,unit->object.maximum_shield_vitality,unit->object.damage_flags,!!TEST_FLAG(unit->object.damage_flags,_object_dead_bit),unit->object.position.x,unit->object.position.y,unit->object.position.z);
        n3ds_log(message);
    }
    previous_body=unit->object.body_vitality;previous_shield=unit->object.shield_vitality;previous_damage_flags=unit->object.damage_flags;
    return 0;
}
void n3ds_mission_combat_trace(long unit_index,long time)
{
    static long last_fired=NONE,last_projectiles=NONE;
    static int last_charging;
    struct unit_datum *unit=unit_get(unit_index);
    struct object_iterator iterator;
    long count=0,index=NONE;
    object_iterator_new(&iterator,_object_mask_projectile,0);
    while(object_iterator_next(&iterator)) ++count;
    if(unit->unit.current_weapon_index>=0 && unit->unit.current_weapon_index<MAXIMUM_WEAPONS_PER_UNIT)
        index=unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
    if(index!=NONE) {
        struct weapon_datum *weapon=weapon_get(index);
        int charging=weapon->weapon.triggers[0].state==3 && weapon->weapon.overcharged>0;
        /* A short charge can begin and end between the periodic samples.
         * Record its edges without altering input or weapon state. */
        if(time%30==0 || last_fired!=weapon->weapon.game_time_last_fired || last_projectiles!=count || charging!=last_charging) {
            char message[256];
            snprintf(message,sizeof(message),"COMBAT: time=%ld weapon=%08lx trigger=%.3f state=%d last_fired=%ld heat=%.6f age=%.6f charge=%.6f projectiles=%ld",time,index,weapon->weapon.primary_trigger,weapon->weapon.triggers[0].state,weapon->weapon.game_time_last_fired,weapon->weapon.heat,weapon->weapon.age,weapon->weapon.overcharged,count);
            n3ds_log(message);
        }
        last_fired=weapon->weapon.game_time_last_fired;
        last_charging=charging;
    }
    last_projectiles=count;
}
void __wrap_free(void *pointer)
{
    if(trace && !logging) {
        char message[128]; logging=1;
        snprintf(message,sizeof(message),"MISSION FREE: pointer=%p caller=%p",pointer,__builtin_return_address(0));
        n3ds_log(message); logging=0;
    }
    __real_free(pointer);
}
void __wrap_debug_free(void *pointer,const char *file,long line)
{
    if(trace && !logging) {
        char message[256]; logging=1;
        snprintf(message,sizeof(message),"MISSION DEBUG FREE: pointer=%p file=%.180s line=%ld",pointer,file?file:"(null)",line);
        n3ds_log(message); logging=0;
    }
    __real_debug_free(pointer,file,line);
}
