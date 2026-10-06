/*
DEAD_CAMERA.C

symbols in this file:
000740B0 0070:
	_player_has_allies (0000)
00074120 00b0:
	_player_get_next_player_with_a_unit (0000)
000741D0 0120:
	_dead_camera_new (0000)
000742F0 04e0:
	_dead_camera_update (0000)
00256AE8 000c:
	_dead_camera_constants (0000)
00256AF4 0024:
	??_C@_0CE@LCNMEPOD@c?3?2halo?2SOURCE?2camera?2dead_camer@ (0000)
*/

/* ---------- headers */

#include "dead_camera.h"
#include "observer.h"
#include "static_camera.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "objects/objects.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */


struct dead_camera_constants
{
	real dead_timer;
	real multiplayer_switch_timer;
	real singleplayer_switch_timer;
};

/* ---------- prototypes */

static boolean player_has_allies(
	long player_index);
static long player_get_next_player_with_a_unit(
	long player_index,
	long old_player_index,
	boolean match_team);

/* ---------- globals */

static struct dead_camera_constants const dead_camera_constants =
{
	3.f,
	15.f,
	3.f
};

/* ---------- public code */

void dead_camera_new(
	struct dead_camera *camera,
	short local_player_index,
	long unit_index)
{
	struct observer_result const *observer = observer_get_camera(local_player_index);
	real pitch;
	real switch_timer;

	match_assert("c:\\halo\\SOURCE\\camera\\dead_camera.c", 23, camera);
	camera->position = observer->position;
	camera->field_of_view = DEGREES_TO_RADIANS(70.f);
	camera->distance = real_local_random_range(2.f, 6.f);
	camera->facing.yaw = real_local_random_range(0.f, 2.f * _pi);
	pitch = real_local_random_range(0.47123894f, 1.0995574f);
	camera->facing.pitch = -pitch;
	camera->timer = dead_camera_constants.dead_timer;
	if (unit_index != NONE)
	{
		switch_timer = FLT_MAX;
	}
	else
	{
		switch_timer = game_engine_running()
			? dead_camera_constants.multiplayer_switch_timer
			: dead_camera_constants.singleplayer_switch_timer;
	}
	camera->switch_timer = switch_timer;
	camera->player_index = local_player_get_player_index(local_player_index);
	camera->unit_index = unit_index == NONE
		? player_get(camera->player_index)->dead_unit_index
		: unit_index;
	camera->current_player_index = camera->player_index;

	return;
}

void dead_camera_update(
	struct dead_camera *camera,
	struct camera_control const *controls,
	struct observer_command *result)
{
	struct object_datum *unit;

	unit = camera->unit_index == NONE
		? NULL
		: object_try_and_get(camera->unit_index);
	if (unit)
	{
		result->focus_position = unit->object.bounding_sphere_center;
	}
	else
	{
		result->focus_position = camera->position;
	}

	result->focus_distance = camera->distance;
	vector3d_from_euler_angles2d(&result->forward, &camera->facing);
	observer_up_from_forward(&result->forward, &result->up);
	result->field_of_view = camera->field_of_view;
	result->focus_offset = *global_zero_vector3d;
	result->focus_velocity = *global_zero_vector3d;
	result->flags = FLAG(0);
	result->timer = MAX(0.f, camera->timer);
	result->parameter_timers[_observer_command_parameter_focus_position] = 0.f;
	result->parameter_flags[_observer_command_parameter_focus_position] = 3;

	if (camera->timer == dead_camera_constants.dead_timer)
	{
		result->focus_distance = 0.5f;
		result->parameter_timers[_observer_command_parameter_focus_distance] = 0.f;
		result->parameter_flags[_observer_command_parameter_focus_distance] = 3;
	}

	camera->timer -= controls->seconds_elapsed;
	camera->switch_timer = MAX(
		0.f,
		camera->switch_timer - controls->seconds_elapsed);

	if (0.f == camera->switch_timer && !game_time_get_paused())
	{
		long next_player_index;
		long next_unit_index;
		boolean match_team;

		match_team = player_has_allies(camera->player_index);
		next_player_index = player_get_next_player_with_a_unit(
			camera->player_index,
			camera->current_player_index,
			match_team);
		camera->current_player_index = next_player_index;

		if (next_player_index != NONE)
		{
			next_unit_index = player_get(next_player_index)->unit_index;
		}
		else
		{
			next_unit_index = (long)result;
		}
		/* January preserves the command pointer as the fallback object index
		 * when no player is found. This is a bug; a corrected build should
		 * initialize next_unit_index to NONE instead. */

		if (next_unit_index != camera->unit_index && next_unit_index != NONE)
		{
			camera->timer = dead_camera_constants.dead_timer;
			camera->unit_index = next_unit_index;
		}

		camera->switch_timer = game_engine_running()
			? dead_camera_constants.multiplayer_switch_timer
			: dead_camera_constants.singleplayer_switch_timer;
	}

	match_vassert(
		"c:\\halo\\SOURCE\\camera\\dead_camera.c",
		158,
		!(result->flags & FLAG(0)) ||
		(valid_real_vector3d_axes2(&result->forward, &result->up) &&
			valid_real(result->focus_position.x) && result->focus_position.x>=-5000.f && result->focus_position.x<=5000.f &&
			valid_real(result->focus_position.y) && result->focus_position.y>=-5000.f && result->focus_position.y<=5000.f &&
			valid_real(result->focus_position.z) && result->focus_position.z>=-5000.f && result->focus_position.z<=5000.f &&
			valid_real(result->focus_offset.i) && result->focus_offset.i>=-5000.f && result->focus_offset.i<=5000.f &&
			valid_real(result->focus_offset.j) && result->focus_offset.j>=-5000.f && result->focus_offset.j<=5000.f &&
			valid_real(result->focus_offset.k) && result->focus_offset.k>=-5000.f && result->focus_offset.k<=5000.f &&
			valid_real_vector3d(&result->focus_velocity) &&
			valid_real(result->focus_distance) && result->focus_distance>=0.f && result->focus_distance<=5000.f &&
			valid_real(result->field_of_view) && result->field_of_view>=0.001f && result->field_of_view<=_pi / 2.f &&
			valid_real(result->timer) && result->timer>=0.f && result->timer<=3600.f),
		csprintf(
			temporary,
			"Invalid camera command.\nF: (%f, %f, %f) U: (%f, %f, %f)\nP: (%f, %f, %f) O: (%f, %f, %f)\nD: %f V: (%f, %f, %f), FOV: %f, T: %f, FL: %ld",
			result->forward.i,
			result->forward.j,
			result->forward.k,
			result->up.i,
			result->up.j,
			result->up.k,
			result->focus_position.x,
			result->focus_position.y,
			result->focus_position.z,
			result->focus_offset.i,
			result->focus_offset.j,
			result->focus_offset.k,
			result->focus_distance,
			result->focus_velocity.i,
			result->focus_velocity.j,
			result->focus_velocity.k,
			result->field_of_view,
			result->timer,
			result->flags));

	return;
}

/* ---------- private code */

static boolean player_has_allies(
	long player_index)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long team_index;
	boolean found;

	team_index = player_get(player_index)->team_index;
	found = FALSE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index != player_index && player->team_index == team_index)
		{
			found = TRUE;
			break;
		}
	}

	return found;
}

static long player_get_next_player_with_a_unit(
	long player_index,
	long old_player_index,
	boolean match_team)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long first_player_index;
	long next_player_index;
	long team_index;

	team_index = match_team ? player_get(player_index)->team_index : NONE;
	first_player_index = NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index != player_index &&
			player->unit_index != NONE &&
			(!match_team || player->team_index == team_index))
		{
			if (first_player_index == NONE)
			{
				first_player_index = iterator.datum_index;
			}
			else if ((iterator.datum_index & 0xFFFF) >
				(old_player_index & 0xFFFF))
			{
				first_player_index = iterator.datum_index;
				break;
			}
		}
	}

	next_player_index = first_player_index == NONE
		? old_player_index
		: first_player_index;
	return next_player_index;
}
