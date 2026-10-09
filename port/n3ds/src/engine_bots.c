/* New3DS integration additions by Kevin Boggs. */
/* Local solo-match bots adapted from the Halo CE recomp combat bot logic.
 * A bot is a player in this process, never a network client. */
#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#define PATH_EXTERNAL_FLEE_ROUTINES
#include "ai/path.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_king.h"
#include "game/game_engine_race.h"
#include "game/players.h"
#include "game/player_queues_new.h"
#include "game/aim_assist.h"
#include "items/items.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "items/projectile_definitions.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "math/real_math.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
void n3ds_log(char const *message);
/* Kevin Boggs: platform_log - New3DS integration. */
static void platform_log(char const *format, ...)
{
#ifdef HALO_N3DS_BOT_TEST_COUNT
	char message[256];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	n3ds_log(message);
#else
	(void)format;
#endif
}
static boolean n3ds_bots_is_registered(short queue_index, short identifier);
#define MAXIMUM_NUMBER_OF_PLAYERS 16
static long n3ds_bot_players[MAXIMUM_NUMBER_OF_PLAYERS];
static long n3ds_star_bot_index = NONE;
static unsigned long n3ds_bot_match_serial;
static short n3ds_bot_difficulty=1;
static const char *const n3ds_bot_guest_names[]={
	"Sleepy","Dopey","Grumpy","Mopey","Hollywood","Crazy","Saucy","Wheezy",
	"Cupid","Killer","The Big L","Butcher","Darling","Disco","Donut","Hambone",
	"Dasher","Prancer","Noodle","Weasel","Snake","Shadow","Caboose","King",
	"Sneak","The Bear","Ghost","Penguin","Howard","Jack","Whisp","Whicker",
	"Wilshire","Pirate","Walla Walla","Tooth","Goat","Stumpy","Stompy","Winky"
};
/* Kevin Boggs: combat_bot_is_star - New3DS integration. */
static boolean combat_bot_is_star(short queue_index)
{
	return n3ds_star_bot_index != NONE &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(n3ds_star_bot_index) == queue_index;
}
wchar_t const *n3ds_bots_display_name(struct player_datum const *player)
{
	/* must keep */
	if (player && n3ds_star_bot_index != NONE &&
		player == player_try_and_get(n3ds_star_bot_index))
		return L"ChaoticSPARTAN1";
	return player ? player->name : L"";
}
struct scenario_netgame_flag
{
	real_point3d position;
	real facing;
	short type;
	short team_index;
	byte unused[0x80];
};
typedef char n3ds_bot_netgame_flag_size[sizeof(struct scenario_netgame_flag) == 0x94 ? 1 : -1];
boolean unit_get_seat_entrance_point(long unit_index, long target_unit_index,
	short seat_index, real_point3d *entry_position, real_point3d *exit_position,
	real_point3d *seat_transform);

struct combat_bot_state
{
	short identifier;
	long last_tick;
	long weapon_target;
	long equipment_target;
	long vehicle_target;
	boolean vehicle_target_gunner;
	long last_scan_tick;
	long last_action_tick;
	long last_grenade_tick;
	long last_melee_tick;
	unsigned long accuracy_attempts;
	unsigned long accuracy_hits;
	long melee_until_tick;
	long last_position_tick;
	long evade_until_tick;
	real_point3d last_position;
	long last_carried_weapon;
	long last_vehicle_parent;
	char last_grenade_count;
	short last_mode;
	long goal_object;
	long goal_progress_tick;
	real goal_distance2;
	long ignored_object;
	long ignore_until_tick;
	long route_goal_index;
	long route_last_tick;
	real_point3d route_destination;
	struct path_result route;
	real_point3d avoid_point;
	long avoid_until_tick;
};

static struct combat_bot_state combat_bot_states[MAXIMUM_NUMBER_OF_PLAYERS];

/* Kevin Boggs: combat_bot_distance_squared - New3DS integration. */
static real combat_bot_distance_squared(real_point3d const *a, real_point3d const *b)
{
	real dx = b->x - a->x;
	real dy = b->y - a->y;
	real dz = b->z - a->z;
	return dx * dx + dy * dy + dz * dz;
}

/* Stable for one 15-tick firing interval, independent of the game's RNG.
 * A missed shot changes the visible aim and cannot receive fallback damage. */
/* Kevin Boggs: combat_bot_accuracy_roll - New3DS integration. */
static unsigned long combat_bot_accuracy_roll(short queue_index, long tick,
	short shot_interval)
{
	unsigned long value = (unsigned long)(tick / shot_interval) ^
		((unsigned long)(queue_index + 1) * 0x9e3779b9UL);
	value ^= value >> 16;
	value *= 0x7feb352dUL;
	value ^= value >> 15;
	value *= 0x846ca68bUL;
	value ^= value >> 16;
	return value;
}

/* Kevin Boggs: combat_bot_flag_held_by_ally - New3DS integration. */
static boolean combat_bot_flag_held_by_ally(struct weapon_datum const *weapon,
	struct player_datum const *bot)
{
	struct unit_datum *carrier;
	struct player_datum *owner;
	if (!game_engine_has_teams() || weapon->object.parent_object_index == NONE)
		return FALSE;
	carrier = unit_try_and_get(weapon->object.parent_object_index);
	if (!carrier || carrier->unit.player_index == NONE)
		return FALSE;
	owner = player_try_and_get(carrier->unit.player_index);
	return owner && owner->team_index == bot->team_index;
}

/* Kevin Boggs: combat_bot_world_object_index - New3DS integration. */
static long combat_bot_world_object_index(long unit_index)
{
	struct object_datum *unit = object_try_and_get(unit_index);
	if (unit && unit->object.parent_object_index != NONE)
	{
		struct object_datum *parent = object_try_and_get(unit->object.parent_object_index);
		if (parent && parent->object.type == _object_type_vehicle)
			return unit->object.parent_object_index;
	}
	return unit_index;
}

/* Kevin Boggs: combat_bot_vehicle_is_turret - New3DS integration. */
static boolean combat_bot_vehicle_is_turret(long vehicle_index)
{
	struct unit_datum *vehicle = unit_try_and_get(vehicle_index);
	char const *name = vehicle ? tag_get_name(vehicle->definition_index) : NULL;
	return name && (strstr(name, "turret") || strstr(name, "shade"));
}

/* Kevin Boggs: combat_bot_can_see_object - New3DS integration. */
static boolean combat_bot_can_see_object(long unit_index, real_point3d const *from,
	long object_index, real_point3d const *to)
{
	real_point3d start = *from;
	real_point3d end = *to;
	start.z += 0.7f;
	end.z += 0.5f;
	return aim_assist_clear_line_of_sight(&start, &end, unit_index, object_index);
}

/* Kevin Boggs: combat_bot_ground_surface - New3DS integration. */
static long combat_bot_ground_surface(real_point3d const *position, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *position;
	real_vector3d down = { 0.f, 0.f, -6.f };
	start.z += 2.f;
	if (collision_test_vector(0x21, &start, &down, NONE, &collision) &&
		collision.surface_index != NONE)
	{
		*ground = collision.point;
		return collision.surface_index;
	}
	return NONE;
}

/* Kevin Boggs: combat_bot_local_detour - New3DS integration. */
static boolean combat_bot_local_detour(struct combat_bot_state *state, short queue_index,
	long tick, long unit_index, real_point3d const *position,
	real_point3d const *destination, real_point3d *next_point)
{
	static real const turns[6][2] = {
		{ 0.7071f, 0.7071f }, { 0.7071f, -0.7071f },
		{ 0.f, 1.f }, { 0.f, -1.f },
		{ -0.7071f, 0.7071f }, { -0.7071f, -0.7071f }
	};
	struct collision_result hit;
	real_point3d start = *position;
	real_vector3d vector;
	real length = (real)sqrt((destination->x - position->x) * (destination->x - position->x) +
		(destination->y - position->y) * (destination->y - position->y));
	real forward_x, forward_y;
	short side;
	if (length < 2.5f)
		return FALSE;
	if (tick < state->avoid_until_tick &&
		combat_bot_distance_squared(position, &state->avoid_point) > 0.8f * 0.8f)
	{
		*next_point = state->avoid_point;
		return TRUE;
	}
	forward_x = (destination->x - position->x) / length;
	forward_y = (destination->y - position->y) / length;
	start.z += 0.8f;
	vector.i = forward_x * 3.f;
	vector.j = forward_y * 3.f;
	vector.k = 0.f;
	if (!collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_structure_bit), &start, &vector, unit_index, &hit) || hit.t > 0.8f)
		return FALSE;
	for (side = 0; side < 6; side++)
	{
		short choice = queue_index & 1 ? 5 - side : side;
		real cosine = turns[choice][0];
		real sine = turns[choice][1];
		real_point3d ground;
		real_point3d candidate = *position;
		vector.i = (forward_x * cosine - forward_y * sine) * 3.f;
		vector.j = (forward_x * sine + forward_y * cosine) * 3.f;
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
			FLAG(_collision_test_structure_bit), &start, &vector, unit_index, &hit) &&
			hit.t < 0.9f)
			continue;
		candidate.x += vector.i;
		candidate.y += vector.j;
		if (combat_bot_ground_surface(&candidate, &ground) == NONE ||
			fabs(ground.z - position->z) > 1.4f)
			continue;
		candidate.z = ground.z;
		state->avoid_point = candidate;
		state->avoid_until_tick = tick + 45;
		*next_point = candidate;
		if (queue_index < 8)
			platform_log("combat bot detour: player %d chose side %d", queue_index, choice);
		return TRUE;
	}
	return FALSE;
}

/* Halo's walkable collision surfaces provide a short route. Refresh in
   batches so a 128-player lobby does not run 127 searches in one tick. */
/* Kevin Boggs: combat_bot_route_point - New3DS integration. */
static boolean combat_bot_route_point(struct combat_bot_state *state, short queue_index,
	long tick, long unit_index, long goal_index, real_point3d const *position,
	real_point3d const *destination, real_point3d *next_point)
{
	boolean changed = goal_index != state->route_goal_index ||
		combat_bot_distance_squared(destination, &state->route_destination) > 5.f * 5.f;
	if (changed)
	{
		state->route.valid = FALSE;
		state->route_goal_index = goal_index;
		state->route_destination = *destination;
		state->route_last_tick = -1000;
		state->avoid_until_tick = 0;
	}
	if (state->route.valid && state->route.step_count > 0 && !state->route.steps_finish_path &&
		state->route.step_index == state->route.step_count - 1 &&
		combat_bot_distance_squared(position,
			&state->route.steps[state->route.step_index].point) < 1.2f * 1.2f)
		state->route_last_tick = -1000;
	if ((changed || tick - state->route_last_tick >= 120 ||
		(state->route.valid && state->route.step_index >= state->route.step_count)) &&
		tick % 32 == queue_index % 32)
	{
		struct path_input input;
		struct path_state path_state;
		real_point3d start_point;
		real_point3d end_point;
		long start_surface = NONE;
		long end_surface = NONE;
		state->route_last_tick = tick;
		state->route.valid = FALSE;
		if (biped_try_and_get(unit_index))
			start_surface = biped_find_pathfinding_surface_index(unit_index, &start_point);
		if (start_surface == NONE)
			start_surface = combat_bot_ground_surface(position, &start_point);
		if (goal_index != NONE && biped_try_and_get(goal_index))
			end_surface = biped_find_pathfinding_surface_index(goal_index, &end_point);
		if (end_surface == NONE)
			end_surface = combat_bot_ground_surface(destination, &end_point);
		if (start_surface != NONE && end_surface != NONE)
		{
			path_input_new(&input, 0.35f, FALSE, unit_index);
			path_input_set_start(&input, &start_point, start_surface);
			path_input_set_target_object(&input, goal_index);
			path_state_new(&input, &path_state, NULL);
			path_state_destination(&path_state, &end_point, end_surface, 0.8f);
			if (path_state_find(&path_state))
				path_state_build_path(&path_state, &state->route);
			else if (path_state.node_count >= PATH_NODE_LIST_SIZE &&
				path_state.closest_node_index != NONE &&
				path_state.closest_distance + 2.f < distance3d(&start_point, &end_point))
			{
				/* A large multiplayer BSP can exhaust the original search's
				 * fixed node list. Use its closest explored node as a short
				 * waypoint, then search again after reaching that waypoint. */
				path_state.destination.target_radius = path_state.closest_distance + 0.01f;
				if (path_state_build_path(&path_state, &state->route))
				{
					state->route.steps_finish_path = FALSE;
					platform_log("combat bot route: player %d partial waypoint %.2f from goal",
						queue_index, path_state.closest_distance);
				}
			}
		}
		if (queue_index < 8 && tick - state->route_last_tick == 0)
			platform_log("combat bot route: player %d %s steps %d start %ld end %ld",
				queue_index, state->route.valid ? "found" : "failed",
				state->route.valid ? state->route.step_count : 0,
				start_surface, end_surface);
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MAP_ID
		if (queue_index < 8 && start_surface != NONE && end_surface != NONE)
		{
			struct structure_bsp const *bsp = global_structure_bsp_get();
			byte const *flags = bsp && bsp->pathfinding_surfaces.address ?
				(byte const *)bsp->pathfinding_surfaces.address : NULL;
			platform_log("combat bot path data: surfaces %ld start_flag %u end_flag %u nodes %d closest %.3f",
				bsp ? bsp->pathfinding_surfaces.count : 0,
				flags && start_surface < bsp->pathfinding_surfaces.count ? flags[start_surface] : 0,
				flags && end_surface < bsp->pathfinding_surfaces.count ? flags[end_surface] : 0,
				path_state.node_count, path_state.closest_distance);
		}
#endif
	}
	if (!state->route.valid || state->route.step_count <= 0)
		return FALSE;
	while (state->route.step_index < state->route.step_count - 1 &&
		combat_bot_distance_squared(position,
			&state->route.steps[state->route.step_index].point) < 1.2f * 1.2f)
		state->route.step_index++;
	*next_point = state->route.steps[state->route.step_index].point;
	return TRUE;
}

/* Kevin Boggs: combat_bot_weapon_value - New3DS integration. */
static real combat_bot_weapon_value(long weapon_index, real enemy_distance2)
{
	struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
	char const *name;
	if (!weapon)
		return 0.f;
	name = tag_get_name(weapon->definition_index);
	if (!name)
		return 1.f;
	if (strstr(name, "rocket launcher") || strstr(name, "rocket_launcher") || strstr(name, "plasma_cannon"))
		return 5.f;
	if (strstr(name, "sniper"))
		return enemy_distance2 > 8.f * 8.f ? 4.5f : 1.5f;
	if (strstr(name, "shotgun") || strstr(name, "flamethrower"))
		return enemy_distance2 < 12.f * 12.f ? 4.5f : 2.f;
	if (strstr(name, "pistol"))
		return 3.f;
	if (strstr(name, "plasma rifle") || strstr(name, "plasma_rifle"))
		return 2.5f;
	return 2.f;
}

/* Kevin Boggs: combat_bot_fire_range - New3DS integration. */
static real combat_bot_fire_range(struct unit_datum const *unit)
{
	struct weapon_datum *weapon;
	char const *name;
	if (!unit || unit->unit.current_weapon_index == NONE)
		return 35.f;
	weapon = weapon_try_and_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index]);
	name = weapon ? tag_get_name(weapon->definition_index) : NULL;
	if (!name)
		return 35.f;
	if (strstr(name,"sniper"))
		return 512.f;
	if (strstr(name,"rocket launcher") || strstr(name,"rocket_launcher"))
		return 100.f;
	if (strstr(name,"pistol"))
		return 60.f;
	return 35.f;
}

/* Kevin Boggs: combat_bot_scan_objects - New3DS integration. */
static void combat_bot_scan_objects(struct combat_bot_state *state, struct unit_datum *unit,
	long unit_index, real enemy_distance2, real_point3d const *enemy_position,
	long enemy_index, boolean race_mode)
{
	struct object_iterator objects;
	long weapon_target = NONE;
	long equipment_target = NONE;
	long vehicle_target = NONE;
	boolean vehicle_target_gunner = FALSE;
	real weapon_score = -1.0e30f;
	real equipment_score = -1.0e30f;
	real vehicle_score = -1.0e30f;
	short slot;

	object_iterator_new(&objects, _object_mask_weapon, 0);
	while (object_iterator_next(&objects))
	{
		struct weapon_datum *weapon = weapon_try_and_get(objects.index);
		real distance2;
		real value;
		boolean carried = FALSE;
		if (!weapon || weapon->object.parent_object_index != NONE ||
			weapon_is_flag(objects.index) ||
			!TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(weapon->item.flags, _item_attached_to_unit_bit))
			continue;
		if (objects.index == state->ignored_object && state->last_tick < state->ignore_until_tick)
			continue;
		distance2 = combat_bot_distance_squared(&unit->object.position, &weapon->object.position);
		if (distance2 > 25.f * 25.f ||
			fabs(weapon->object.position.z - unit->object.position.z) > 1.3f ||
			(distance2 > 8.f * 8.f &&
				!combat_bot_can_see_object(unit_index, &unit->object.position,
					objects.index, &weapon->object.position)))
			continue;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long carried_index = unit->unit.weapon_object_indices[slot];
			struct weapon_datum *carried_weapon = weapon_try_and_get(carried_index);
			if (carried_weapon && carried_weapon->definition_index == weapon->definition_index)
				carried = TRUE;
		}
		if (carried)
			continue;
		value = combat_bot_weapon_value(objects.index, enemy_distance2);
		if (value * 80.f - distance2 > weapon_score)
		{
			weapon_score = value * 80.f - distance2;
			weapon_target = objects.index;
		}
	}
	object_iterator_new(&objects, _object_mask_equipment, 0);
	while (object_iterator_next(&objects))
	{
		struct item_datum *equipment = equipment_try_and_get(objects.index);
		struct equipment_definition *definition;
		real distance2;
		short grenades = 0;
		if (!equipment || equipment->object.parent_object_index != NONE ||
			!TEST_FLAG(equipment->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(equipment->item.flags, _item_attached_to_unit_bit))
			continue;
		if (objects.index == state->ignored_object && state->last_tick < state->ignore_until_tick)
			continue;
		definition = equipment_definition_get(equipment->definition_index);
		if (definition->equipment.powerup_type != _equipment_powerup_grenade)
			continue;
		for (slot = 0; slot < NUMBER_OF_UNIT_GRENADE_TYPES; slot++)
			grenades += unit->unit.grenade_counts[slot];
		if (grenades >= 2)
			continue;
		distance2 = combat_bot_distance_squared(&unit->object.position, &equipment->object.position);
		if (distance2 > 20.f * 20.f ||
			fabs(equipment->object.position.z - unit->object.position.z) > 1.3f ||
			(distance2 > 8.f * 8.f &&
				!combat_bot_can_see_object(unit_index, &unit->object.position,
					objects.index, &equipment->object.position)))
			continue;
		if (150.f - distance2 > equipment_score)
		{
			equipment_score = 150.f - distance2;
			equipment_target = objects.index;
		}
	}
	object_iterator_new(&objects, _object_mask_vehicle, 0);
	while (object_iterator_next(&objects))
	{
		struct unit_datum *vehicle = unit_try_and_get(objects.index);
		real distance2;
		char const *name;
		real value = 2.f;
		boolean turret;
		boolean gunner_target;
		boolean has_gunner_seat = FALSE;
		struct unit_definition *definition;
		short seat_index;
		if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
			vehicle->object.up.k < 0.5f)
			continue;
		name = tag_get_name(vehicle->definition_index);
		turret = name && (strstr(name, "turret") || strstr(name, "shade"));
		definition = unit_definition_get(vehicle->definition_index);
		for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats,
				seat_index, struct unit_seat);
			if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
			{
				has_gunner_seat = TRUE;
				break;
			}
		}
		/* Some bots take a mounted gun even before a driver arrives. */
		gunner_target = has_gunner_seat && (turret ||
			vehicle->unit.driver_object_index != NONE ||
			(state - combat_bot_states) % 4 == 1);
		if (turret && state == &combat_bot_states[0] && state->last_tick < 240)
			platform_log("combat bot turret available: %s at %.1f %.1f gunner %ld",
				name, vehicle->object.position.x, vehicle->object.position.y,
				vehicle->unit.gunner_object_index);
		if (gunner_target)
		{
			if (race_mode || !enemy_position || vehicle->unit.gunner_object_index != NONE ||
				combat_bot_distance_squared(&vehicle->object.position, enemy_position) > 45.f * 45.f ||
				!combat_bot_can_see_object(objects.index, &vehicle->object.position,
					enemy_index, enemy_position))
				continue;
			value = 5.5f;
		}
		else if (vehicle->unit.driver_object_index != NONE)
			continue;
		if (objects.index == state->ignored_object && state->last_tick < state->ignore_until_tick)
			continue;
		distance2 = combat_bot_distance_squared(&unit->object.position, &vehicle->object.position);
		if (distance2 > 30.f * 30.f ||
			!combat_bot_can_see_object(unit_index, &unit->object.position,
				objects.index, &vehicle->object.position))
			continue;
		if (!turret && name && (strstr(name, "scorpion") || strstr(name, "rwarthog") || strstr(name, "banshee")))
			value = 4.f;
		if (value * 100.f - distance2 > vehicle_score)
		{
			vehicle_score = value * 100.f - distance2;
			vehicle_target = objects.index;
			vehicle_target_gunner = gunner_target;
		}
	}
	state->weapon_target = weapon_target;
	state->equipment_target = equipment_target;
	state->vehicle_target = vehicle_target;
	state->vehicle_target_gunner = vehicle_target_gunner;
}

/* Multiplayer objectives outrank ordinary loot when there is no immediate
   close fight. Keep the game's own pickup/scoring rules in charge. */
/* Kevin Boggs: combat_bot_objective - New3DS integration. */
static short combat_bot_objective(short queue_index, long bot_index,
	long vehicle_parent, long vehicle_target, struct unit_datum *unit, struct player_datum *bot,
	real_point3d const *bot_position, real_point3d const *enemy_position,
	real enemy_distance2, real_point3d *objective, long *objective_index)
{
	struct game_variant *variant = game_engine_get_variant();
	struct object_iterator objects;
	real closest_flag_distance2 = 1.0e30f;
	long closest_flag_index = NONE;
	short slot;
	*objective_index = NONE;
	if (!variant)
		return 0;
	if (variant->game_engine_index == game_engine_race)
	{
		if (vehicle_parent == NONE && vehicle_target != NONE)
			return 0;
		return game_engine_race_next_goal(bot_index, bot_position, objective) ? 12 : 0;
	}
	if (variant->game_engine_index == game_engine_king)
	{
		if (game_engine_king_active_hill_center(objective) &&
			combat_bot_distance_squared(bot_position, objective) > 5.f * 5.f &&
			enemy_distance2 > 3.f * 3.f)
			return 8;
		return 0;
	}
	if (variant->game_engine_index != game_engine_ctf &&
		variant->game_engine_index != game_engine_oddball)
		return 0;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];
		struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
		if (!weapon || !weapon_is_flag(weapon_index))
			continue;
		if (variant->game_engine_index == game_engine_ctf)
		{
			long flag_index = find_netgame_flag(NULL, 0.f, 0.f,
				_netgame_flag_ctf_flag, bot->team_index);
			if (flag_index != NONE)
			{
				struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
					&global_scenario_get()->netgame_flags, flag_index, struct scenario_netgame_flag);
				*objective = flag->position;
				return 6;
			}
		}
		else if (enemy_position && enemy_distance2 < 20.f * 20.f)
		{
			objective->x = bot_position->x + (bot_position->x - enemy_position->x) * 0.7f;
			objective->y = bot_position->y + (bot_position->y - enemy_position->y) * 0.7f;
			objective->z = bot_position->z;
			return 9;
		}
		return 0;
	}
	/* One CTF attacker in each group keeps running the flag through a fight;
	   the others can break off to defend or duel. */
	if (enemy_distance2 < 8.f * 8.f &&
		!(variant->game_engine_index == game_engine_ctf && queue_index % 3 == 1))
		return 0;
	if (variant->game_engine_index == game_engine_ctf && queue_index % 4 == 0)
	{
		long flag_index = find_netgame_flag(NULL, 0.f, 0.f,
			_netgame_flag_ctf_flag, bot->team_index);
		if (flag_index != NONE)
		{
			struct scenario_netgame_flag *base = TAG_BLOCK_GET_ELEMENT(
				&global_scenario_get()->netgame_flags, flag_index, struct scenario_netgame_flag);
			object_iterator_new(&objects, _object_mask_weapon, 0);
			while (object_iterator_next(&objects))
			{
				struct weapon_datum *weapon = weapon_try_and_get(objects.index);
				if (weapon && weapon_is_flag(objects.index) &&
					weapon->object.owner_team_index == bot->team_index)
				{
					item_get_position_even_if_in_inventory(objects.index, objective);
					if (combat_bot_distance_squared(objective, &base->position) > 4.f * 4.f)
					{
						*objective_index = objects.index;
						return 11; /* return our displaced flag */
					}
					break;
				}
			}
			if (enemy_distance2 > 15.f * 15.f)
			{
				*objective = base->position;
				return 10; /* one defender in each group of four */
			}
		}
	}
	object_iterator_new(&objects, _object_mask_weapon, 0);
	while (object_iterator_next(&objects))
	{
		struct weapon_datum *weapon = weapon_try_and_get(objects.index);
		real_point3d flag_position;
		real distance2;
		if (!weapon || !weapon_is_flag(objects.index) ||
			(variant->game_engine_index == game_engine_ctf &&
			 weapon->object.owner_team_index == bot->team_index) ||
			combat_bot_flag_held_by_ally(weapon, bot))
			continue;
		item_get_position_even_if_in_inventory(objects.index, &flag_position);
		distance2 = combat_bot_distance_squared(bot_position, &flag_position);
		if (distance2 < closest_flag_distance2)
		{
			closest_flag_distance2 = distance2;
			closest_flag_index = objects.index;
			*objective = flag_position;
		}
	}
	if (closest_flag_index != NONE)
	{
		*objective_index = closest_flag_index;
		return variant->game_engine_index == game_engine_ctf ? 5 : 7;
	}
	return 0;
}

/* A small, host-driven opponent for explicitly named test clients.  The
   ordinary network input path is unchanged for human players. */
/* Kevin Boggs: n3ds_bots_fill_action - New3DS integration. */
void n3ds_bots_fill_action(short queue_index, short identifier,
	struct player_action *action)
{
	long bot_index = ((long)(unsigned short)identifier << 16) | (unsigned short)queue_index;
	struct player_datum *bot = player_try_and_get(bot_index);
	struct object_datum *bot_unit;
	struct unit_datum *unit;
	struct combat_bot_state *state = &combat_bot_states[queue_index];
	struct player_datum *target = NULL;
	real_point3d bot_position;
	struct data_iterator iterator;
	struct player_datum *candidate;
	real best_distance2 = 1.0e30f;
	real_vector3d direction;
	real_point3d objective;
	real_point3d steering_point;
	real_point3d enemy_position;
	real objective_distance2 = 1.0e30f;
	long vehicle_parent = NONE;
	long goal_index = NONE;
	long objective_index = NONE;
	long route_object_index = NONE;
	boolean target_visible = FALSE;
	boolean accurate_shot = TRUE;
	short mode = 0; /* fight, weapon, vehicle, driving, equipment, flag, capture, ball, hill, evade */
	short slot;
	short desired_seat_index = NONE;
	boolean desired_turret = FALSE;
	short shot_interval;
	long tick;

	if (!n3ds_bots_is_registered(queue_index, identifier) || !bot || bot->unit_index == NONE)
		return;
	bot_unit = object_try_and_get(bot->unit_index);
	if (!bot_unit || TEST_FLAG(bot_unit->object.damage_flags, _object_dead_bit))
		return;
	unit = unit_try_and_get(bot->unit_index);
	if (!unit)
		return;
	tick = game_time_initialized() ? game_time_get() : 0;
	shot_interval = combat_bot_is_star(queue_index) ? 12 :
		(n3ds_bot_difficulty==0?20:n3ds_bot_difficulty==2?11:15);
	if (state->identifier != identifier || tick < state->last_tick)
	{
		csmemset(state, 0, sizeof(*state));
		state->identifier = identifier;
		state->weapon_target = NONE;
		state->equipment_target = NONE;
		state->vehicle_target = NONE;
		state->last_scan_tick = -1000;
		state->last_action_tick = -1000;
		state->last_grenade_tick = -1000;
		state->last_melee_tick = -1000;
		state->melee_until_tick = -1000;
		state->last_position_tick = tick;
		state->last_position = bot_unit->object.position;
		state->last_vehicle_parent = NONE;
		state->last_grenade_count = -1;
		state->last_mode = -1;
		state->goal_object = NONE;
		state->ignored_object = NONE;
		state->route_goal_index = NONE;
		state->route_last_tick = -1000;
	}
	state->last_tick = tick;
	vehicle_parent = bot_unit->object.parent_object_index;
	if (vehicle_parent != NONE && (!object_try_and_get(vehicle_parent) ||
		object_get(vehicle_parent)->object.type != _object_type_vehicle))
		vehicle_parent = NONE;
	bot_position = vehicle_parent != NONE ? object_get(vehicle_parent)->object.position :
		bot_unit->object.position;
	if (vehicle_parent != state->last_vehicle_parent)
	{
		if (vehicle_parent != NONE && queue_index < 20)
			platform_log("combat bot: player %d entered vehicle %ld", queue_index,
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_parent));
		state->last_vehicle_parent = vehicle_parent;
	}
	if (state->weapon_target != NONE)
	{
		struct weapon_datum *target_weapon = weapon_try_and_get(state->weapon_target);
		if (!target_weapon)
		{
			state->weapon_target = NONE;
			state->last_scan_tick = -1000;
		}
		else if (bot->action_object_index == state->weapon_target &&
			(bot->action_result == _player_action_result_add_weapon_to_inventory ||
			 bot->action_result == _player_action_result_swap_for_weapon) &&
			combat_bot_distance_squared(&bot_unit->object.position,
				&target_weapon->object.position) < 1.2f * 1.2f)
		{
			long weapon_index = state->weapon_target;
			const char *weapon_name = tag_get_name(object_get(weapon_index)->definition_index);
			if (bot->action_result == _player_action_result_swap_for_weapon)
				unit_drop_current_weapon(bot->unit_index, TRUE);
			if (unit_add_weapon_to_inventory(bot->unit_index, weapon_index, TRUE))
			{
				if (queue_index < 20)
					platform_log("combat bot: player %d picked up %s", queue_index, weapon_name);
				state->weapon_target = NONE;
				state->last_scan_tick = tick;
			}
		}
		for (slot = 0; state->weapon_target != NONE && slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			if (unit->unit.weapon_object_indices[slot] == state->weapon_target)
			{
				if (queue_index < 20)
					platform_log("combat bot: player %d picked up %s", queue_index,
						tag_get_name(object_get(state->weapon_target)->definition_index));
				state->weapon_target = NONE;
				state->last_scan_tick = tick;
				break;
			}
	}
	{
		short grenades = 0;
		for (slot = 0; slot < NUMBER_OF_UNIT_GRENADE_TYPES; slot++)
			grenades += unit->unit.grenade_counts[slot];
		if (state->last_grenade_count >= 0 && queue_index < 20)
		{
			if (grenades < state->last_grenade_count)
				platform_log("combat bot: player %d threw a grenade (%d left)", queue_index, grenades);
			else if (grenades > state->last_grenade_count)
				platform_log("combat bot: player %d picked up grenades (%d now)", queue_index, grenades);
		}
		state->last_grenade_count = (char)grenades;
	}
	data_iterator_new(&iterator, player_data);
	while ((candidate = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *candidate_unit;
		real dx, dy, dz, distance2;
		if (candidate == bot || candidate->unit_index == NONE ||
			(game_engine_has_teams() && candidate->team_index == bot->team_index))
			continue;
		candidate_unit = object_try_and_get(candidate->unit_index);
		if (!candidate_unit || TEST_FLAG(candidate_unit->object.damage_flags, _object_dead_bit))
			continue;
		candidate_unit = object_get(combat_bot_world_object_index(candidate->unit_index));
		dx = candidate_unit->object.position.x - bot_position.x;
		dy = candidate_unit->object.position.y - bot_position.y;
		dz = candidate_unit->object.position.z - bot_position.z;
		distance2 = dx * dx + dy * dy + dz * dz;
		if (distance2 < best_distance2)
		{
			best_distance2 = distance2;
			target = candidate;
			enemy_position = candidate_unit->object.position;
		}
	}
	if (tick - state->last_scan_tick >= 30 && vehicle_parent == NONE)
	{
		combat_bot_scan_objects(state, unit, bot->unit_index, best_distance2,
			target ? &enemy_position : NULL,
			target ? combat_bot_world_object_index(target->unit_index) : NONE,
			game_engine_get_variant()->game_engine_index == game_engine_race);
		state->last_scan_tick = tick;
	}
	objective = bot_position;
	if (vehicle_parent != NONE)
		mode = 3;
	else if (state->vehicle_target != NONE &&
		(game_engine_get_variant()->game_engine_index == game_engine_race ||
		 (best_distance2 > 6.f * 6.f &&
		 (state->vehicle_target_gunner || queue_index % 3 == 0 ||
		  (state->weapon_target == NONE && state->equipment_target == NONE)))))
	{
		struct unit_datum *vehicle = unit_try_and_get(state->vehicle_target);
		boolean turret = state->vehicle_target_gunner;
		if (vehicle && vehicle->object.type == _object_type_vehicle &&
			(turret ? vehicle->unit.gunner_object_index == NONE :
			 vehicle->unit.driver_object_index == NONE))
		{
			struct unit_definition *definition = unit_definition_get(vehicle->definition_index);
			for (slot = 0; slot < definition->unit.seats.count; slot++)
			{
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, slot, struct unit_seat);
				if (TEST_FLAG(seat->flags, turret ? _unit_seat_gunner_bit : _unit_seat_driver_bit) &&
					unit_get_seat_entrance_point(bot->unit_index, state->vehicle_target,
						slot, &objective, NULL, NULL))
				{
					mode = 2;
					desired_seat_index = slot;
					desired_turret = turret;
					break;
				}
			}
		}
		if (mode != 2)
			state->vehicle_target = NONE;
	}
	if (mode == 0 && state->equipment_target != NONE && best_distance2 > 3.f * 3.f)
	{
		struct item_datum *equipment = equipment_try_and_get(state->equipment_target);
		short grenades = 0;
		for (slot = 0; slot < NUMBER_OF_UNIT_GRENADE_TYPES; slot++)
			grenades += unit->unit.grenade_counts[slot];
		if (equipment && equipment->object.parent_object_index == NONE && grenades < 2)
		{
			objective = equipment->object.position;
			mode = 4;
		}
	}
	if (mode == 0 && state->weapon_target != NONE && best_distance2 > 3.f * 3.f)
	{
		struct weapon_datum *weapon = weapon_try_and_get(state->weapon_target);
		real current_value = 0.f;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			current_value = MAX(current_value,
				combat_bot_weapon_value(unit->unit.weapon_object_indices[slot], best_distance2));
		if (weapon && weapon->object.parent_object_index == NONE &&
			combat_bot_weapon_value(state->weapon_target, best_distance2) > current_value + 0.5f)
		{
			objective = weapon->object.position;
			mode = 1;
		}
	}
	if (vehicle_parent == NONE || game_engine_get_variant()->game_engine_index == game_engine_race)
	{
		real_point3d strategic_point;
		short strategic_mode = combat_bot_objective(queue_index, bot_index,
			vehicle_parent, state->vehicle_target, unit, bot, &bot_position,
			target ? &enemy_position : NULL, best_distance2,
			&strategic_point, &objective_index);
		if (strategic_mode)
		{
			mode = strategic_mode;
			objective = strategic_point;
		}
	}
	if (mode != state->last_mode)
	{
		if (queue_index < 20 && mode != 0)
			platform_log("combat bot: player %d seeking mode %d", queue_index, mode);
		state->last_mode = mode;
	}
	if (target)
		target_visible = combat_bot_can_see_object(bot->unit_index,
			&bot_position, combat_bot_world_object_index(target->unit_index), &enemy_position);
	if (mode == 0 && target)
	{
		objective = enemy_position;
		route_object_index = target->unit_index;
	}
	else if (mode == 1 || mode == 2 || mode == 4)
		route_object_index = mode == 1 ? state->weapon_target :
			mode == 2 ? state->vehicle_target : state->equipment_target;
	else if (mode == 5 || mode == 7 || mode == 11)
		route_object_index = objective_index;
	steering_point = objective;
	if (vehicle_parent == NONE && (mode != 0 || !target_visible) &&
		combat_bot_distance_squared(&bot_position, &objective) > 3.f * 3.f)
	{
		if (!combat_bot_route_point(state, queue_index, tick, bot->unit_index,
			route_object_index, &bot_position, &objective, &steering_point))
			combat_bot_local_detour(state, queue_index, tick, bot->unit_index,
				&bot_position, &objective, &steering_point);
	}
	if (target && (mode == 0 || mode == 3))
	{
		struct object_datum *target_unit = object_get(combat_bot_world_object_index(target->unit_index));
		struct object_datum *from = vehicle_parent != NONE ? object_get(vehicle_parent) : bot_unit;
		if (mode == 0 && !target_visible)
		{
			direction.i = steering_point.x - from->object.position.x;
			direction.j = steering_point.y - from->object.position.y;
			direction.k = steering_point.z - from->object.position.z;
		}
		else
		{
			direction.i = target_unit->object.position.x - from->object.position.x;
			direction.j = target_unit->object.position.y - from->object.position.y;
			direction.k = target_unit->object.position.z - from->object.position.z + 0.6f;
		}
	}
	else if (mode != 0)
	{
		direction.i = steering_point.x - bot_position.x;
		direction.j = steering_point.y - bot_position.y;
		direction.k = steering_point.z - bot_position.z + 0.1f;
	}
	else
	{
		direction.i = 1.f;
		direction.j = ((tick / 180 + queue_index) & 1) ? 0.5f : -0.5f;
		direction.k = 0.f;
	}
	euler_angles2d_from_vector3d(&action->desired_facing, &direction);
	objective_distance2 = (objective.x - bot_position.x) *
		(objective.x - bot_position.x) +
		(objective.y - bot_position.y) *
		(objective.y - bot_position.y);
	if ((mode == 5 || mode == 7) && queue_index < 8 && tick % 300 == 0)
		platform_log("combat bot objective range: player %d mode %d distance %.2f pos %.2f %.2f goal %.2f %.2f action %d",
			queue_index, mode, (real)sqrt(objective_distance2),
			bot_position.x, bot_position.y, objective.x, objective.y,
			(int)bot->action_result);
	if (mode == 2 && desired_seat_index != NONE &&
		objective_distance2 < 1.2f * 1.2f &&
		unit_enter_seat(bot->unit_index, state->vehicle_target, desired_seat_index))
	{
		if (queue_index < 20)
			platform_log("combat bot: player %d took %s seat %d", queue_index,
				desired_turret ? "turret" : "driver", desired_seat_index);
		state->vehicle_target = NONE;
		state->route.valid = FALSE;
	}
	goal_index = mode == 1 ? state->weapon_target :
		mode == 2 ? state->vehicle_target :
		mode == 4 ? state->equipment_target : NONE;
	if (goal_index != state->goal_object)
	{
		state->goal_object = goal_index;
		state->goal_distance2 = objective_distance2;
		state->goal_progress_tick = tick;
	}
	else if (goal_index != NONE)
	{
		if (objective_distance2 < state->goal_distance2 - MAX(1.f, state->goal_distance2 * 0.1f))
		{
			state->goal_distance2 = objective_distance2;
			state->goal_progress_tick = tick;
		}
		else if (tick - state->goal_progress_tick > 180)
		{
			state->ignored_object = goal_index;
			state->ignore_until_tick = tick + 300;
			state->last_scan_tick = -1000;
			state->goal_object = NONE;
			if (queue_index < 20)
				platform_log("combat bot: player %d abandoned blocked item %ld", queue_index,
					(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(goal_index));
		}
	}
	action->control_flags = 0;
	action->primary_trigger = 0.f;
	action->throttle.i = 0.f;
	action->throttle.j = 0.f;
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	if (mode == 1 || mode == 2 || mode == 4 || mode == 5 ||
		mode == 6 || mode == 7 || mode == 8 || mode == 9 || mode == 10 || mode == 11 || mode == 12)
	{
		action->throttle.i = objective_distance2 > (mode == 8 ? 4.f * 4.f :
			mode == 10 ? 8.f * 8.f : 0.5f * 0.5f) ? 1.f : 0.f;
		if (mode == 12 && vehicle_parent != NONE)
		{
			struct unit_datum *vehicle = unit_try_and_get(vehicle_parent);
			action->throttle.i = objective_distance2 > 8.f * 8.f ? 1.f :
			objective_distance2 > 2.f * 2.f ? 0.35f : -0.25f;
			if (vehicle && queue_index < 8 && tick % 300 == 0)
				platform_log("combat bot race: player %d driver %ld self %ld vehicle %s",
					queue_index, vehicle->unit.driver_object_index, bot->unit_index,
					tag_get_name(vehicle->definition_index));
		}
		if ((mode == 5 || mode == 7) && objective_index != NONE &&
			objective_distance2 < 1.2f * 1.2f &&
			bot->action_object_index == objective_index &&
			(bot->action_result == _player_action_result_add_weapon_to_inventory ||
			 bot->action_result == _player_action_result_swap_for_weapon))
		{
			if (bot->action_result == _player_action_result_swap_for_weapon)
				unit_drop_current_weapon(bot->unit_index, TRUE);
			if (unit_add_weapon_to_inventory(bot->unit_index, objective_index, TRUE) &&
				queue_index < 20)
				platform_log("combat bot objective: player %d picked up %s", queue_index,
					mode == 5 ? "enemy flag" : "oddball");
		}
		if (mode == 1 && objective_distance2 < 1.2f * 1.2f)
		{
			/* A ground weapon is accepted by the swap input, including when
			   there is a free inventory slot.  The action input only reloads. */
			if (bot->action_object_index == state->weapon_target &&
				(bot->action_result == _player_action_result_add_weapon_to_inventory ||
				 bot->action_result == _player_action_result_swap_for_weapon))
				action->control_flags |= FLAG(_unit_control_swap_weapons_bit);
		}
		if (mode != 2 && objective_distance2 < 1.2f * 1.2f &&
			tick - state->last_action_tick > 20)
		{
			action->control_flags |= FLAG(_unit_control_action_bit);
			state->last_action_tick = tick;
			if (queue_index < 20 && mode == 1)
				platform_log("combat bot: player %d near weapon %s action_result %d inventory %d",
					queue_index, tag_get_name(object_get(state->weapon_target)->definition_index),
					(int)bot->action_result, (int)unit_get_weapon_count(bot->unit_index));
			else if (queue_index < 20 && tick % 90 == queue_index % 90)
				platform_log("combat bot: player %d action near %s", queue_index,
					mode == 1 ? "weapon" : mode == 2 ? "vehicle" : "grenades");
		}
	}
	else if (mode == 3)
	{
		struct unit_datum *vehicle = unit_try_and_get(vehicle_parent);
		if (vehicle && vehicle->unit.driver_object_index == bot->unit_index)
			action->throttle.i = best_distance2 > 6.f * 6.f ? 1.f : 0.3f;
		else
		{
			{
				real fire_range=combat_bot_fire_range(unit);
				action->primary_trigger = best_distance2 < fire_range * fire_range ? 1.f : 0.f;
			}
			if (vehicle && vehicle->unit.gunner_object_index == bot->unit_index &&
				queue_index < 8 && tick % 300 == 0)
			{
				long gun_index = vehicle->unit.current_weapon_index == NONE ? NONE :
					vehicle->unit.weapon_object_indices[vehicle->unit.current_weapon_index];
				struct weapon_datum *gun = weapon_try_and_get(gun_index);
				platform_log("combat bot turret: player %d firing %d target_distance %.2f vehicle %s gun %s last_fired %ld",
					queue_index, action->primary_trigger > 0.f, (real)sqrt(best_distance2),
					tag_get_name(vehicle->definition_index),
					gun ? tag_get_name(gun->definition_index) : "none",
					gun ? gun->weapon.game_time_last_fired : -1L);
			}
		}
	}
	else if (target)
	{
		real desired_range = queue_index % 3 == 1 ? 1.4f : 7.f;
		action->throttle.i = best_distance2 > desired_range * desired_range ? 1.f :
			best_distance2 < 2.f * 2.f ? -0.3f : 0.f;
		action->throttle.j = ((tick / 90 + queue_index) & 1) ? 0.55f : -0.55f;
		{
			real fire_range=combat_bot_fire_range(unit);
			action->primary_trigger = best_distance2 < fire_range * fire_range ? 1.f : 0.f;
		}
	}
	else
		action->throttle.i = 0.5f;
	if (action->primary_trigger > 0.f)
	{
		if (target)
		{
			unsigned long roll = combat_bot_accuracy_roll(queue_index, tick, shot_interval);
			real fraction = (real)((roll >> 16) & 255) / 255.f;
			real side = (roll & 0x100UL) ? 1.f : -1.f;
			accurate_shot = roll % 100 < (combat_bot_is_star(queue_index) ? 93 :
				(n3ds_bot_difficulty==0?70:n3ds_bot_difficulty==2?93:85));
			action->desired_facing.yaw += side *
				(accurate_shot ? 0.002f + 0.007f * fraction : 0.045f + 0.025f * fraction);
			action->desired_facing.pitch += ((roll & 0x200UL) ? 1.f : -1.f) *
				(accurate_shot ? 0.002f + 0.004f * fraction : 0.012f + 0.010f * fraction);
		}
		action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
	}
	if (mode == 0 && target && tick < state->melee_until_tick && best_distance2 > 0.7f * 0.7f)
	{
		action->throttle.i = 0.75f;
		action->throttle.j = 0.f;
	}
	if (tick - state->last_position_tick >= 90)
	{
		if (action->throttle.i > 0.5f &&
			combat_bot_distance_squared(&bot_position, &state->last_position) < 0.8f * 0.8f)
			state->evade_until_tick = tick + 45;
		state->last_position = bot_position;
		state->last_position_tick = tick;
	}
	if (tick < state->evade_until_tick)
	{
		action->throttle.i = vehicle_parent != NONE ? -1.f : -0.4f;
		action->throttle.j = (queue_index & 1) ? 1.f : -1.f;
		if (vehicle_parent == NONE && tick % 30 == queue_index % 30)
			action->control_flags |= FLAG(_unit_control_jump_bit);
	}
	else if (vehicle_parent == NONE && best_distance2 > 3.f * 3.f && tick % 120 == queue_index % 120)
		action->control_flags |= FLAG(_unit_control_jump_bit);
	if (vehicle_parent == NONE && target && mode == 0)
	{
		short best_slot = NONE;
		real best_value = -1.f;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long weapon_index = unit->unit.weapon_object_indices[slot];
			real value = combat_bot_weapon_value(weapon_index, best_distance2);
			if (weapon_index != NONE && value > best_value)
			{
				best_value = value;
				best_slot = slot;
			}
		}
		if (best_slot != NONE)
			action->desired_weapon_index = best_slot;
		if (best_distance2 < 2.f * 2.f && tick - state->last_melee_tick > 45)
		{
			action->control_flags |= FLAG(_unit_control_use_equipment_bit);
			action->control_flags &= ~FLAG(_unit_control_weapon_primary_trigger_bit);
			action->primary_trigger = 0.f;
			state->last_melee_tick = tick;
			state->melee_until_tick = tick + 15;
			if (best_distance2 < 1.6f * 1.6f && unit->unit.current_weapon_index != NONE)
			{
				long weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
				struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
				if (weapon)
				{
					long target_object_index = combat_bot_world_object_index(target->unit_index);
					struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
					long damage_index = definition->weapon.melee_attack_damage.index;
					real_point3d start = bot_position;
					real_point3d end = object_get(target_object_index)->object.position;
					start.z += 0.8f;
					end.z += 0.8f;
					if (damage_index != NONE &&
						aim_assist_clear_line_of_sight(&start, &end, bot->unit_index, target_object_index))
					{
						struct damage_data damage;
						damage_data_new(&damage, damage_index);
						damage.owner_player_index = bot_index;
						damage.owner_object_index = bot->unit_index;
						damage.owner_team_index = bot_unit->object.owner_team_index;
						damage.origin = start;
						damage.epicenter = end;
						damage.direction = direction;
						normalize3d(&damage.direction);
						damage.scale = 0.5f;
						scenario_location_from_point(&damage.location, &damage.epicenter);
						object_cause_damage(&damage, target_object_index, NONE, NONE, NONE, NULL);
						if (queue_index < 20)
							platform_log("combat bot: player %d landed melee", queue_index);
					}
				}
			}
			if (queue_index < 20)
				platform_log("combat bot: player %d melee attack", queue_index);
		}
		else if (best_distance2 > 5.f * 5.f && best_distance2 < 20.f * 20.f &&
			tick - state->last_grenade_tick > 120)
		{
			short grenade = best_distance2 > 12.f * 12.f ? 1 : 0;
			if (unit->unit.grenade_counts[grenade] <= 0)
				grenade = (short)(1 - grenade);
			if (unit->unit.grenade_counts[grenade] > 0)
			{
				real_point3d start = bot_unit->object.position;
				real_point3d end = object_get(combat_bot_world_object_index(target->unit_index))->object.position;
				start.z += 0.8f;
				end.z += 0.8f;
				if (aim_assist_clear_line_of_sight(&start, &end, bot->unit_index,
					combat_bot_world_object_index(target->unit_index)))
				{
					action->control_flags |= FLAG(_unit_control_throw_grenade_bit) |
						FLAG(_unit_control_weapon_secondary_trigger_bit);
					action->desired_grenade_index = grenade;
					state->last_grenade_tick = tick;
					if (queue_index < 20)
						platform_log("combat bot: player %d grenade request type %d", queue_index, grenade);
				}
			}
		}
		/* The fake client cannot report a projectile hit. The fallback applies
		   damage only to a visible target when this shot's aim roll hit. */
		if (action->primary_trigger > 0.f && tick % shot_interval == queue_index % shot_interval &&
			unit->unit.current_weapon_index != NONE)
		{
			long weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
			struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
			if (weapon)
			{
				real_point3d start = bot_unit->object.position;
				long target_object_index = combat_bot_world_object_index(target->unit_index);
				real_point3d end = object_get(target_object_index)->object.position;
				long damage_index = NONE;
				struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
				start.z += 0.8f;
				end.z += 0.8f;
				if (definition->weapon.triggers.count > 0)
				{
					struct weapon_trigger_definition *trigger = TAG_BLOCK_GET_ELEMENT(
						&definition->weapon.triggers, 0, struct weapon_trigger_definition);
					if (trigger->projectile.index != NONE)
						damage_index = projectile_definition_get(trigger->projectile.index)->projectile.impact_damage.index;
				}
				if (damage_index != NONE &&
					aim_assist_clear_line_of_sight(&start, &end, bot->unit_index, target_object_index))
				{
				++state->accuracy_attempts;
				if (accurate_shot)
				{
					struct damage_data damage;
					damage_data_new(&damage, damage_index);
					damage.owner_player_index = bot_index;
					damage.owner_object_index = bot->unit_index;
					damage.owner_team_index = bot_unit->object.owner_team_index;
					damage.origin = start;
					damage.epicenter = end;
					damage.direction = direction;
					normalize3d(&damage.direction);
					damage.scale = 0.25f;
					scenario_location_from_point(&damage.location, &damage.epicenter);
					object_cause_damage(&damage, target_object_index, NONE, NONE, NONE, NULL);
					++state->accuracy_hits;
					if (queue_index < 5)
						platform_log("combat bot: tick %ld player %d hit player %d", tick,
							queue_index, (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(target - (struct player_datum *)player_data->data));
				}
				if (queue_index < 5 && state->accuracy_attempts % 20 == 0)
					platform_log("combat bot accuracy: player %d shots %lu hits %lu misses %lu",
						queue_index, state->accuracy_attempts, state->accuracy_hits,
						state->accuracy_attempts - state->accuracy_hits);
				}
			}
		}
	}
	if (queue_index < 5 && tick % 300 == 0)
		platform_log("combat bot: tick %ld player %d mode %d pos %.2f %.2f %.2f target %.2f %.2f %.2f distance %.2f move %.2f fire %.2f",
			tick, queue_index, mode, bot_position.x, bot_position.y,
			bot_position.z, direction.i, direction.j, direction.k,
			best_distance2, action->throttle.i, action->primary_trigger);
}

/* Kevin Boggs: n3ds_bots_reset - New3DS integration. */
void n3ds_bots_reset(void)
{
	short i;
	for (i = 0; i < MAXIMUM_NUMBER_OF_PLAYERS; ++i)
		n3ds_bot_players[i] = NONE;
	n3ds_star_bot_index = NONE;
	csmemset(combat_bot_states, 0, sizeof(combat_bot_states));
}

/* Kevin Boggs: n3ds_bots_is_registered - New3DS integration. */
static boolean n3ds_bots_is_registered(short queue_index, short identifier)
{
	long player_index;
	if (queue_index < 0 || queue_index >= MAXIMUM_NUMBER_OF_PLAYERS)
		return FALSE;
	player_index = n3ds_bot_players[queue_index];
	return player_index != NONE &&
		(short)((unsigned long)player_index >> 16) == identifier;
}

#ifdef HALO_N3DS_PC_LOCAL_MENU
static short n3ds_menu_bot_count=-1;
/* Kevin Boggs: n3ds_bots_set_menu_count - New3DS integration. */
void n3ds_bots_set_menu_count(short count)
{ n3ds_menu_bot_count=(count>=0 && count<=15)?count:-1; }
/* Kevin Boggs: n3ds_bots_set_menu_difficulty - New3DS integration. */
void n3ds_bots_set_menu_difficulty(short difficulty)
{ if(difficulty>=0 && difficulty<=2)n3ds_bot_difficulty=difficulty; }
#endif
/* Kevin Boggs: n3ds_bots_requested_count - New3DS integration. */
short n3ds_bots_requested_count(void)
{
#ifdef HALO_N3DS_PC_LOCAL_MENU
	if(n3ds_menu_bot_count>=0) return n3ds_menu_bot_count;
#endif
#ifdef HALO_N3DS_BOT_TEST_COUNT
	return HALO_N3DS_BOT_TEST_COUNT;
#else
	char buffer[32];
	char *end;
	long count;
	FILE *file = fopen("sdmc:/halo-source/bots.txt", "rb");
	if (!file)
		return 0;
	if (!fgets(buffer, sizeof(buffer), file) || fgetc(file) != EOF)
	{
		fclose(file);
		n3ds_log("BOTS: invalid bots.txt; expected one number from 0 to 15");
		return 0;
	}
	fclose(file);
	count = strtol(buffer, &end, 10);
	while (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t') ++end;
	if (end == buffer || *end || count < 0 || count > 15)
	{
		n3ds_log("BOTS: invalid bots.txt; expected one number from 0 to 15");
		return 0;
	}
	return (short)count;
#endif
}

/* Kevin Boggs: n3ds_bots_create - New3DS integration. */
short n3ds_bots_create(short count)
{
	short i, created = 0;
	short named_slot = NONE;
	unsigned long seed;
	unsigned char guest_order[NUMBEROF(n3ds_bot_guest_names)];
	unsigned char color_order[16];
	short guest_count=0;
	n3ds_bots_reset();
	if (count < 0 || count > 15)
		return 0;
	seed = (unsigned long)system_seconds() ^
		((unsigned long)system_milliseconds() * 0x85ebca6bUL) ^
		(++n3ds_bot_match_serial * 0x9e3779b9UL);
	if (count && seed_random_range(&seed, 0,
		(n3ds_bot_difficulty==0?12:n3ds_bot_difficulty==2?3:4)) == 0)
		named_slot = seed_random_range(&seed, 0, count);
	for (i=0;i<NUMBEROF(guest_order);++i) guest_order[i]=(unsigned char)i;
	for (i=NUMBEROF(guest_order)-1;i>0;--i) {
		short chosen=seed_random_range(&seed,0,i+1);
		unsigned char prior=guest_order[i];
		guest_order[i]=guest_order[chosen];
		guest_order[chosen]=prior;
	}
	for (i=0;i<NUMBEROF(color_order);++i)
		color_order[i]=(unsigned char)(i<6?i:i+1);
	for (i=NUMBEROF(color_order)-1;i>0;--i) {
		short chosen=seed_random_range(&seed,0,i+1);
		unsigned char prior=color_order[i];
		color_order[i]=color_order[chosen];
		color_order[chosen]=prior;
	}
	for (i = 0; i < count; ++i)
	{
		struct network_player profile = {0};
		long player_index;
		short slot = (short)(i + 1);
		short machine = (short)(slot / 4);
		char name[12];
		short n;
		if (i == named_slot)
			snprintf(name, sizeof(name), "ChaoticSP1");
		else
			snprintf(name, sizeof(name), "%s", n3ds_bot_guest_names[guest_order[guest_count++]]);
		for (n = 0; name[n] && n < 11; ++n)
			profile.name[n] = (wchar_t)(unsigned char)name[n];
		profile.primary_color_index = i == named_slot ? 6 :
			color_order[i < named_slot || named_slot == NONE ? i : i-1];
		profile.machine_index = (char)machine;
		profile.controller_index = (char)(slot % 4);
		profile.player_list_index = (char)slot;
		player_index = player_new(machine, NONE, NONE, &profile);
		if (player_index == NONE)
		{
			n3ds_log("BOTS: player pool full before requested count was reached");
			break;
		}
		n3ds_bot_players[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)] = player_index;
		if (i == named_slot)
			n3ds_star_bot_index = player_index;
		platform_log("BOTS: player %d profile %s color %d", slot, name,
			profile.primary_color_index);
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
		{
			char message[64];
			snprintf(message, sizeof(message), "BOTS: player %d profile %s color %d", slot,
			name, profile.primary_color_index);
			n3ds_log(message);
		}
#endif
		++created;
	}
	{
		char message[96];
		snprintf(message, sizeof(message), "BOTS: registered %d of %d requested local AI players", created, count);
		n3ds_log(message);
	}
	return created;
}
