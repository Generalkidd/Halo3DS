/* New3DS integration additions by Kevin Boggs. */
#include "engine_campaign_options.h"
#include "cseries/cseries.h"
#include "main/main.h"
#include "game/game.h"
#include "interface/ui_widget.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
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

struct campaign_extras_state {unsigned int placed_mask;};
static struct campaign_extras_state *state;

/* Kevin Boggs: n3ds_campaign_extras_allocate_state - New3DS integration. */
void n3ds_campaign_extras_allocate_state(void)
{
    state=game_state_malloc("campaign extras",NULL,sizeof(*state));
    state->placed_mask=0;
}

/* Kevin Boggs: n3ds_campaign_extras_for_new_map - New3DS integration. */
void n3ds_campaign_extras_for_new_map(void)
{
    if(state)state->placed_mask=0;
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

/* Kevin Boggs: n3ds_campaign_extras_update - New3DS integration. */
void n3ds_campaign_extras_update(long player_unit)
{
    const char *full=main_get_map_name();
    const char *base=full?strrchr(full,'\\'):NULL;
    base=base?base+1:full;
    if(!base || !*base || !state || player_unit==NONE ||
       !game_in_progress() || main_menu_is_active() ||
       n3ds_main_is_solo_multiplayer())return;
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
