/* New3DS integration additions by Kevin Boggs. */
#include "engine_campaign_options.h"
#include "cseries/cseries.h"
#include "main/main.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players_runtime.h"
#include "interface/ui_widget.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "saved games/game_state.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include <stdio.h>
#include <string.h>

void n3ds_log(const char *message);

struct campaign_weapon_spawn {
    const char *map, *tag;
    float x,y,z;
};

/* Each coordinate sits beside an original pickup in the same retail scenario.
 * The original placement is left untouched. */
static const struct campaign_weapon_spawn weapon_spawns[]={
    {"c20","weapons\\flamethrower\\flamethrower",22.54f,-38.67f,-4.45f},
    {"c40","weapons\\fuel rod gun\\fuel rod",333.85f,-656.33f,80.45f},
    {"d20","weapons\\flamethrower\\flamethrower",54.32f,-34.90f,-25.83f},
    {"d20","weapons\\fuel rod gun\\fuel rod",31.14f,-49.65f,-22.86f}
};
static const struct campaign_weapon_spawn rocket_warthog_spawn={
    "b30","vehicles\\rwarthog\\rwarthog",-16.11f,42.75f,3.04f
};
/* The retail PC caches carry Banshee assets on these open maps but do not
 * place them. All PC vehicles adds two at clear sites on opposing sides;
 * the other match vehicle choices retain the authored placements. */
static const struct campaign_weapon_spawn multiplayer_banshee_spawns[]={
    {"dangercanyon","vehicles\\banshee\\banshee_mp",-16.3f,-11.4f,-4.0f},
    {"dangercanyon","vehicles\\banshee\\banshee_mp", 16.4f,-11.4f,-4.0f},
    {"icefields","vehicles\\banshee\\banshee_mp",-86.0f,84.6f,0.8f},
    {"icefields","vehicles\\banshee\\banshee_mp", 16.8f,-12.0f,0.8f},
    {"timberland","vehicles\\banshee\\banshee_mp",-20.0f,23.0f,-20.8f},
    {"timberland","vehicles\\banshee\\banshee_mp", 12.0f,-23.0f,-20.24f}
};

struct campaign_extras_state {unsigned int placed_mask,failed_reported_mask;};
static struct campaign_extras_state *state;

/* Kevin Boggs: n3ds_campaign_extras_allocate_state - New3DS integration. */
void n3ds_campaign_extras_allocate_state(void)
{
    state=game_state_malloc("campaign extras",NULL,sizeof(*state));
    state->placed_mask=state->failed_reported_mask=0;
}

/* Kevin Boggs: n3ds_campaign_extras_for_new_map - New3DS integration. */
void n3ds_campaign_extras_for_new_map(void)
{
    if(state)state->placed_mask=state->failed_reported_mask=0;
}

/* Kevin Boggs: object_at_spawn - New3DS integration. */
static long object_at_spawn(long definition,unsigned long type_mask,
    const struct campaign_weapon_spawn *spawn)
{
    struct object_iterator iterator;
    struct object_datum *object;
    object_iterator_new(&iterator,type_mask,0);
    while((object=object_iterator_next(&iterator))!=NULL) {
        if(object->definition_index!=definition || object->object.parent_object_index!=NONE)continue;
        const real_point3d *point=&object->object.position;
        float dx=point->x-spawn->x,dy=point->y-spawn->y,dz=point->z-spawn->z;
        if(dx*dx+dy*dy+dz*dz<0.25f)return iterator.index;
    }
    return NONE;
}
/* Each seed is beside an authored vehicle. Search nearby valid ground so the
 * extra flyer does not overlap that vehicle or a team spawn. */
static const signed char banshee_offsets[][2]={
    {0,0},{0,4},{0,-4},{4,0},{-4,0},
    {4,4},{4,-4},{-4,4},{-4,-4},
    {0,8},{0,-8},{8,0},{-8,0},
    {8,4},{8,-4},{-8,4},{-8,-4},
    {4,8},{4,-8},{-4,8},{-4,-8},
    {0,12},{0,-12},{12,0},{-12,0},
    {12,8},{12,-8},{-12,8},{-12,-8},
    {8,12},{8,-12},{-8,12},{-8,-12},
    {0,16},{0,-16},{16,0},{-16,0},
    {12,12},{12,-12},{-12,12},{-12,-12},
    {0,20},{0,-20},{20,0},{-20,0},
    {20,4},{20,-4},{-20,4},{-20,-4},
    {4,20},{4,-20},{-4,20},{-4,-20},
    {0,24},{0,-24},{24,0},{-24,0},
    {24,4},{24,-4},{-24,4},{-24,-4},
    {4,24},{4,-24},{-4,24},{-4,-24}
};
/* Kevin Boggs: banshee_ground_point - New3DS integration. */
static int banshee_ground_point(const struct campaign_weapon_spawn *spawn,
    unsigned int candidate,real_point3d *point,unsigned int progress[6],float *best_normal)
{
    if(candidate>=NUMBEROF(banshee_offsets))return 0;
    real_point3d start={spawn->x+banshee_offsets[candidate][0],
        spawn->y+banshee_offsets[candidate][1],spawn->z+2.5f};
    real_vector3d down={0.f,0.f,-8.f};
    struct collision_result hit;
    unsigned long flags=FLAG(_collision_test_front_facing_surfaces_bit)|
        FLAG(_collision_test_back_facing_surfaces_bit)|FLAG(_collision_test_structure_bit);
    if(!collision_test_vector(flags,&start,&down,NONE,&hit) ||
       hit.type!=_collision_result_structure)return 0;
    ++progress[0];
    if(hit.plane.n.k>*best_normal)*best_normal=hit.plane.n.k;
    if(hit.plane.n.k<.97f)return 0;
    ++progress[1];
    static const signed char footprint[][2]={{2,0},{-2,0},{0,2},{0,-2}};
    for(unsigned int i=0;i<NUMBEROF(footprint);++i) {
        real_point3d edge={hit.point.x+footprint[i][0],
            hit.point.y+footprint[i][1],hit.point.z+2.f};
        real_vector3d edge_down={0.f,0.f,-4.f};
        struct collision_result edge_hit;
        if(!collision_test_vector(flags,&edge,&edge_down,NONE,&edge_hit) ||
           edge_hit.type!=_collision_result_structure ||
           edge_hit.plane.n.k<.97f)return 0;
        float dz=edge_hit.point.z-hit.point.z;
        if(dz*dz>.25f)return 0;
    }
    ++progress[2];
    *point=hit.point;
    point->z+=.25f;
    struct location location;
    scenario_location_from_point(&location,point);
    if(location.cluster_index==NONE)return 0;
    ++progress[3];
    short starts=player_get_starting_location_count();
    for(short i=0;i<starts;++i) {
        const struct player_starting_location *start_location=player_get_starting_location(i);
        if(!start_location)continue;
        float dx=start_location->position.x-point->x;
        float dy=start_location->position.y-point->y;
        float dz=start_location->position.z-point->z;
        if(dx*dx+dy*dy<9.f && dz*dz<9.f)return 0;
    }
    ++progress[4];
    struct object_iterator iterator;
    object_iterator_new(&iterator,_object_mask_vehicle,0);
    struct object_datum *other;
    while((other=object_iterator_next(&iterator))!=NULL) {
        float dx=other->object.position.x-point->x;
        float dy=other->object.position.y-point->y;
        float dz=other->object.position.z-point->z;
        if(dx*dx+dy*dy<16.f && dz*dz<9.f)return 0;
    }
    ++progress[5];
    return 1;
}
/* Kevin Boggs: n3ds_multiplayer_banshees_update - New3DS integration. */
static void n3ds_multiplayer_banshees_update(const char *base)
{
    const struct game_variant *variant=game_engine_get_variant();
    if(!variant || variant->universal_variant.vehicle_set!=0)return;
    long definition=tag_loaded('vehi',"vehicles\\banshee\\banshee_mp");
    if(definition==NONE || game_engine_remap_vehicle(definition)==NONE)return;
    for(unsigned int i=0;i<sizeof(multiplayer_banshee_spawns)/sizeof(multiplayer_banshee_spawns[0]);++i) {
        const struct campaign_weapon_spawn *spawn=multiplayer_banshee_spawns+i;
        unsigned int bit=1u<<(sizeof(weapon_spawns)/sizeof(weapon_spawns[0])+1+i);
        if(strcmp(base,spawn->map) || (state->placed_mask&bit) ||
           (state->failed_reported_mask&bit))continue;
        if(object_at_spawn(definition,_object_mask_vehicle,spawn)!=NONE) {
            state->placed_mask|=bit;continue;
        }
        int placed=0;
        unsigned int progress[6]={0};
        float best_normal=-2.f;
        for(unsigned int candidate=0;candidate<NUMBEROF(banshee_offsets) && !placed;++candidate) {
            real_point3d point;
            if(!banshee_ground_point(spawn,candidate,&point,progress,&best_normal))continue;
            struct object_placement_data placement;
            object_placement_data_new(&placement,definition,NONE);
            placement.position=point;
            placement.flags|=FLAG(_new_object_never_automatically_delete_bit);
            long object=object_new(&placement);
            struct object_datum *actual=object_try_and_get(object);
            if(!actual)continue;
            if(!TEST_FLAG(actual->object.flags,_object_connected_to_map_bit) ||
               TEST_FLAG(actual->object.flags,_object_outside_of_map_bit) ||
               actual->object.location.cluster_index==NONE) {
                object_delete(object);continue;
            }
            SET_FLAG(actual->object.flags,_object_deleted_when_deactivated_bit,FALSE);
            state->placed_mask|=bit;
            char detail[192];
            snprintf(detail,sizeof(detail),
                "PC VEHICLE EXTRA: map=%s Banshee point=%u candidate=%u object=%08lx cluster=%d at=%.2f,%.2f,%.2f",
                base,i,candidate,(unsigned long)object,actual->object.location.cluster_index,
                point.x,point.y,point.z);
            n3ds_log(detail);placed=1;
        }
        if(!placed && !(state->failed_reported_mask&bit)) {
            char detail[192];snprintf(detail,sizeof(detail),
                "PC VEHICLE EXTRA: map=%s point=%u no clear ground stages=%u,%u,%u,%u,%u,%u maxnormal=%.3f",
                base,i,progress[0],progress[1],progress[2],progress[3],progress[4],progress[5],best_normal);
            n3ds_log(detail);
            state->failed_reported_mask|=bit;
        }
    }
}

/* Campaign areas stream in as the player moves. Wait until a player is close
 * and the point belongs to the active area, then add one optional pickup.
 * The saved placed mask prevents duplicates after a checkpoint restore. */
/* Kevin Boggs: n3ds_campaign_extras_update - New3DS integration. */
void n3ds_campaign_extras_update(long player_unit)
{
    const char *full=main_get_map_name();
    const char *base=full?strrchr(full,'\\'):NULL;
    base=base?base+1:full;
    if(!base || !*base || !state || player_unit==NONE ||
       !game_in_progress() || main_menu_is_active())return;
    if(n3ds_main_is_solo_multiplayer()) {
        n3ds_multiplayer_banshees_update(base);
        return;
    }
    struct object_datum *player=object_try_and_get(player_unit);
    if(!player)return;
    int enabled=n3ds_campaign_extras_enabled();
    for(unsigned int i=0;i<sizeof(weapon_spawns)/sizeof(weapon_spawns[0]);++i) {
        const struct campaign_weapon_spawn *spawn=&weapon_spawns[i];
        if(strcmp(base,spawn->map))continue;
        unsigned int bit=1u<<i;
        if((!enabled && !(state->placed_mask&bit)) ||
           (enabled && (state->placed_mask&bit)))continue;
        if(!enabled) {
            if(state->placed_mask&bit) {
                long definition=tag_loaded(WEAPON_DEFINITION_TAG,spawn->tag);
                if(definition==NONE)continue;
                long existing=object_at_spawn(definition,_object_mask_weapon,spawn);
                if(existing!=NONE) {
                    object_delete(existing);
                    n3ds_log("CAMPAIGN EXTRA: removed uncollected pickup after option disabled");
                }
                state->placed_mask&=~bit;
            }
            continue;
        }
        float dx=player->object.position.x-spawn->x;
        float dy=player->object.position.y-spawn->y;
        float dz=player->object.position.z-spawn->z;
        if(dx*dx+dy*dy+dz*dz>50.f*50.f)continue;
        real_point3d point={spawn->x,spawn->y,spawn->z};
        struct location location;
        scenario_location_from_point(&location,&point);
        if(location.cluster_index==NONE)continue;
        long definition=tag_loaded(WEAPON_DEFINITION_TAG,spawn->tag);
        if(definition==NONE)continue;
        if(object_at_spawn(definition,_object_mask_weapon,spawn)!=NONE) {
            state->placed_mask|=bit;
            continue;
        }
        struct object_placement_data placement;
        object_placement_data_new(&placement,definition,NONE);
        placement.position=point;
        placement.flags|=FLAG(_new_object_never_automatically_delete_bit);
        long object=object_new(&placement);
        if(object!=NONE) {
            struct object_datum *actual=object_try_and_get(object);
            if(!actual || !TEST_FLAG(actual->object.flags,_object_connected_to_map_bit) ||
               TEST_FLAG(actual->object.flags,_object_outside_of_map_bit) ||
               actual->object.location.cluster_index==NONE) {
                if(actual)object_delete(object);
                continue;
            }
            SET_FLAG(actual->object.flags,_object_deleted_when_deactivated_bit,FALSE);
            state->placed_mask|=bit;
            char detail[160];
            snprintf(detail,sizeof(detail),"CAMPAIGN EXTRA: map=%s tag=%s cluster=%d",
                base,spawn->tag,actual->object.location.cluster_index);
            n3ds_log(detail);
        }
    }
    if(strcmp(base,rocket_warthog_spawn.map))return;
    unsigned int bit=1u<<(sizeof(weapon_spawns)/sizeof(weapon_spawns[0]));
    if((!enabled && !(state->placed_mask&bit)) ||
       (enabled && (state->placed_mask&bit)))return;
    if(!enabled) {
        if(state->placed_mask&bit) {
            long definition=tag_loaded('vehi',rocket_warthog_spawn.tag);
            if(definition==NONE)return;
            long existing=object_at_spawn(definition,_object_mask_vehicle,&rocket_warthog_spawn);
            struct object_datum *vehicle=object_try_and_get(existing);
            struct unit_datum *unit=unit_try_and_get(existing);
            if(vehicle && vehicle->object.first_child_object_index==NONE &&
               (!unit || (unit->unit.driver_object_index==NONE &&
                          unit->unit.gunner_object_index==NONE))) {
                object_delete(existing);
                n3ds_log("CAMPAIGN EXTRA: removed unoccupied Rocket Warthog after option disabled");
            }
            state->placed_mask&=~bit;
        }
        return;
    }
    const struct campaign_weapon_spawn *spawn=&rocket_warthog_spawn;
    float dx=player->object.position.x-spawn->x;
    float dy=player->object.position.y-spawn->y;
    float dz=player->object.position.z-spawn->z;
    if(dx*dx+dy*dy+dz*dz>50.f*50.f)return;
    real_point3d point={spawn->x,spawn->y,spawn->z};
    struct location location;
    scenario_location_from_point(&location,&point);
    if(location.cluster_index==NONE)return;
    long definition=tag_loaded('vehi',rocket_warthog_spawn.tag);
    if(definition==NONE)return;
    long existing=object_at_spawn(definition,_object_mask_vehicle,spawn);
    if(existing!=NONE) {state->placed_mask|=bit;return;}
    struct object_placement_data placement;
    object_placement_data_new(&placement,definition,NONE);
    placement.position=point;
    placement.flags|=FLAG(_new_object_never_automatically_delete_bit);
    long object=object_new(&placement);
    struct object_datum *actual=object_try_and_get(object);
    if(!actual)return;
    if(!TEST_FLAG(actual->object.flags,_object_connected_to_map_bit) ||
       TEST_FLAG(actual->object.flags,_object_outside_of_map_bit) ||
       actual->object.location.cluster_index==NONE) {
        object_delete(object);
        return;
    }
    SET_FLAG(actual->object.flags,_object_deleted_when_deactivated_bit,FALSE);
    state->placed_mask|=bit;
    char detail[160];
    snprintf(detail,sizeof(detail),"CAMPAIGN EXTRA: map=b30 Rocket Warthog cluster=%d",
        actual->object.location.cluster_index);
    n3ds_log(detail);
}
