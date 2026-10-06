/*
CAMERA_SCRIPTING.C

symbols in this file:
00073650 0020:
	_scripted_camera_enable (0000)
00073670 00d0:
	_scripted_camera_set_animation (0000)
00073740 0040:
	_scripted_camera_set_first_person (0000)
00073780 0040:
	_scripted_camera_set_dead (0000)
000737C0 0030:
	_scripted_camera_object_is_first_person_camera (0000)
000737F0 00e0:
	_scripted_camera_set (0000)
000738D0 0020:
	_scripted_camera_set_absolute (0000)
000738F0 00d0:
	_scripted_camera_set_camera_point_relative (0000)
000739C0 0030:
	_scripted_camera_set_camera_point_absolute (0000)
000739F0 0010:
	_scripted_camera_next_camera_point (0000)
00073A00 0010:
	_scripted_camera_object_relative_to (0000)
00073A10 0020:
	_scripted_camera_time (0000)
00073A30 0680:
	_scripted_camera_update (0000)
00256A7C 003d:
	??_C@_0DN@JNLEKBID@cannot?5set?5first?5person?5camera?5o@ (0000)
00256ABC 0029:
	??_C@_0CJ@GLCFCHKA@c?3?2halo?2SOURCE?2camera?2camera_scr@ (0000)
002DCB60 0040:
	_data_002dcb60 (0000)
*/

/* ---------- headers */

#include "camera_scripting.h"
#include "director.h"
#include "dead_camera.h"
#include "first_person_camera.h"
#include "observer.h"

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

/* ---------- constants */

enum
{
	_camera_script_mode_point = 0,
	_camera_script_mode_animation,
	_camera_script_mode_first_person,
	_camera_script_mode_dead,
	NUMBER_OF_CAMERA_SCRIPT_MODES,
};

/* ---------- macros */

/* ---------- structures */

struct scripted_camera_globals
{
	boolean enabled;
	boolean first_update;
	short mode;
	short camera_point_index;
	byte pad06[2];
	real timer;
	real_point3d point;
	real_vector3d forward;
	real_vector3d up;
	real field_of_view;
	long relative_object_index;
	long animation_graph_index;
	short animation_index;
};

typedef char scripted_camera_globals_size_assert[
	sizeof(struct scripted_camera_globals) == 0x40 ? 1 : -1];

/* ---------- prototypes */

void scripted_camera_set(
	short camera_point_index,
	word transition_time,
	long relative_object_index);
/* ---------- globals */

struct scripted_camera_globals data_002dcb60 =
{
	FALSE,
	FALSE,
	NONE,
	NONE,
	{ 0, 0 },
	0.f,
	{ { 0.f, 0.f, 0.f } },
	{ { 0.f, 0.f, 1.f } },
	{ { 0.f, 1.f, 0.f } },
	1.22173047f,
	NONE,
	NONE,
	0
};

/* ---------- public code */
#ifdef HALO_N3DS
static int native_menu_camera_setup;
void n3ds_scripted_camera_menu_begin(void) {native_menu_camera_setup=1;}
#endif

void scripted_camera_enable(
	boolean enabled)
{
	data_002dcb60.enabled = enabled;
	data_002dcb60.first_update = TRUE;
	return;
}

void scripted_camera_set_animation(
	long animation_graph_index,
	char const *animation_name)
{
	struct animation_graph *animation_graph;
	struct animation *animation;
	short animation_index;

	if (animation_graph_index != NONE)
	{
		animation_graph = animation_graph_definition_get(animation_graph_index);
		if (animation_graph->nodes.count == 1)
		{
			animation_index = 0;
			while (animation_index < animation_graph->animations.count)
			{
				animation = TAG_BLOCK_GET_ELEMENT(
					&animation_graph->animations,
					animation_index,
					struct animation);
				if (!_stricmp(animation_name, animation->name))
				{
					data_002dcb60.camera_point_index = NONE;
					data_002dcb60.relative_object_index = NONE;
					data_002dcb60.mode = _camera_script_mode_animation;
					data_002dcb60.first_update = TRUE;
					data_002dcb60.animation_index = animation_index;
					data_002dcb60.animation_graph_index = animation_graph_index;
					data_002dcb60.field_of_view = 1.22173047f;
					data_002dcb60.timer =
						(real)(animation->frame_count / TICKS_PER_SECOND);
					break;
				}

				animation_index++;
			}
		}
	}

	return;
}

void scripted_camera_set_first_person(
	long unit_index)
{
	if (unit_index != NONE)
	{
		data_002dcb60.mode = _camera_script_mode_first_person;
		data_002dcb60.first_update = TRUE;
		data_002dcb60.relative_object_index = unit_index;
	}
	else
	{
		error(
			_error_silent,
			"cannot set first person camera on a unit that doesn't exist.");
	}

	return;
}

void scripted_camera_set_dead(
	long unit_index)
{
	if (unit_index != NONE)
	{
		data_002dcb60.mode = _camera_script_mode_dead;
		data_002dcb60.first_update = TRUE;
		data_002dcb60.relative_object_index = unit_index;
	}
	else
	{
		error(
			_error_silent,
			"cannot set first person camera on a unit that doesn't exist.");
	}

	return;
}

boolean scripted_camera_object_is_first_person_camera(
	long object_index)
{
	return data_002dcb60.enabled &&
		data_002dcb60.mode == _camera_script_mode_first_person &&
		data_002dcb60.relative_object_index == object_index;
}

void scripted_camera_set(
	short camera_point_index,
	word transition_time,
	long relative_object_index)
{
	struct scenario_cutscene_camera_point *camera_point;
	long camera_time;

	camera_point = TAG_BLOCK_GET_ELEMENT(
		&global_scenario_get()->cutscene_camera_points,
		camera_point_index,
		struct scenario_cutscene_camera_point);
	camera_time = (short)transition_time / TICKS_PER_SECOND;
#if defined(HALO_N3DS) && !defined(HALO_N3DS_MENU_CAMERA_BASELINE)
	/* Establish the initial anchor before skipping its stationary hold. Keep
	 * every moving leg's authored duration: shortening a leg also changes the
	 * interpolation velocity carried into the following camera transition. */
	if(global_scenario_get()->type==_scenario_type_main_menu && native_menu_camera_setup>0) {
		camera_time=0;
		--native_menu_camera_setup;
	}
#endif
	data_002dcb60.mode = _camera_script_mode_point;
	data_002dcb60.first_update = TRUE;
	data_002dcb60.camera_point_index = camera_point_index;
	data_002dcb60.point = camera_point->position;
	vectors3d_from_euler_angles3d(
		&data_002dcb60.forward,
		&data_002dcb60.up,
		&camera_point->orientation);
	if (camera_point->field_of_view != 0.f)
		data_002dcb60.field_of_view = camera_point->field_of_view;
	else
		data_002dcb60.field_of_view = 1.22173047f;
	data_002dcb60.relative_object_index = relative_object_index;
	data_002dcb60.timer = (real)camera_time;

	director_update(0.f);
	observer_update(0.0001f);
	return;
}

void scripted_camera_set_absolute(
	short camera_point_index,
	word transition_time)
{
	scripted_camera_set(camera_point_index, transition_time, NONE);
	return;
}

void scripted_camera_set_camera_point_relative(
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	real field_of_view,
	word transition_time,
	long relative_object_index)
{
	data_002dcb60.mode = _camera_script_mode_point;
	data_002dcb60.camera_point_index = NONE;
	data_002dcb60.point = *position;
	data_002dcb60.forward = *forward;
	data_002dcb60.up = *up;
	if (field_of_view != 0.f)
		data_002dcb60.field_of_view = field_of_view;
	else
		data_002dcb60.field_of_view = 1.22173047f;
	data_002dcb60.timer =
		(real)((short)transition_time / TICKS_PER_SECOND);
	data_002dcb60.relative_object_index = relative_object_index;

	director_update(0.f);
	observer_update(0.0001f);
	return;
}

void scripted_camera_set_camera_point_absolute(
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	real field_of_view,
	word transition_time)
{
	scripted_camera_set_camera_point_relative(
		position,
		forward,
		up,
		field_of_view,
		transition_time,
		NONE);
	return;
}

short scripted_camera_next_camera_point(
	void)
{
	return data_002dcb60.camera_point_index;
}

long scripted_camera_object_relative_to(
	void)
{
	return data_002dcb60.relative_object_index;
}

short scripted_camera_time(
	void)
{
	return (short)(data_002dcb60.timer * 30.f);
}

void scripted_camera_update(
	struct dead_camera *camera,
	struct camera_control const *controls,
	struct observer_command *result)
{
	real_point3d focus_position;
	real speed;

	focus_position = *global_origin3d;
	speed = game_time_get_speed();
	result->flags = FLAG(3);
	if (game_time_get_paused())
	{
		result->flags |= FLAG(5);
	}
	else
	{
		result->flags &= ~FLAG(5);
	}

	switch (data_002dcb60.mode)
	{
	case _camera_script_mode_point:
		if (data_002dcb60.relative_object_index != NONE)
		{
			struct object_datum *object;

			object = object_try_and_get_and_verify_type(
				data_002dcb60.relative_object_index,
				_object_mask_all);
			if (!object)
			{
				break;
			}
			focus_position = object->object.bounding_sphere_center;
		}

		result->timer = speed != 0.f
			? data_002dcb60.timer / speed
			: 0.f;
		result->field_of_view = data_002dcb60.field_of_view;
		result->forward = data_002dcb60.forward;
		result->up = data_002dcb60.up;
		if (data_002dcb60.relative_object_index != NONE)
		{
			real angle;
			real cosine_value;
			real dot;
			real rotated_offset_i;
			real sine_value;
			real_vector3d offset;

			angle = arctangent(result->forward.j, result->forward.i);
			dot = dot_product3d(
				(real_vector3d const *)&data_002dcb60.point,
				&result->forward);
			if (dot > 0.f)
			{
				dot = 0.f;
			}

			result->focus_distance = -dot;
			result->focus_position = focus_position;
			offset.i = data_002dcb60.point.x - dot * result->forward.i;
			offset.j = data_002dcb60.point.y - dot * result->forward.j;
			offset.k = data_002dcb60.point.z - dot * result->forward.k;
			result->parameter_timers[_observer_command_parameter_focus_position] = 0.f;
			result->parameter_flags[_observer_command_parameter_focus_position] = 1;
			sine_value = sine(angle);
			cosine_value = cosine(angle);
			rotated_offset_i = offset.i * cosine_value;
			rotated_offset_i += sine_value * offset.j;
			result->focus_offset.i = rotated_offset_i;
			result->focus_offset.j = sine_value * offset.i - cosine_value * offset.j;
			result->focus_offset.k = offset.k;
			result->flags |= FLAG(0);
		}
		else
		{
			result->focus_position = data_002dcb60.point;
			result->flags |= FLAG(0);
		}
		break;

	case _camera_script_mode_animation:
		{
			struct animation_graph *animation_graph;
			struct animation *animation;
			short frame;
			long frame_count;
			real_matrix4x3 root_matrix;

			animation_graph = animation_graph_definition_get(
				data_002dcb60.animation_graph_index);
			animation = TAG_BLOCK_GET_ELEMENT(
				&animation_graph->animations,
				data_002dcb60.animation_index,
				struct animation);
			frame_count = animation->frame_count;
			frame = (short)(frame_count - data_002dcb60.timer * 30.f);
			animation_get_root_matrix(
				NULL,
				animation,
				frame < 0 ? 0 : MIN(frame, frame_count - 1),
				&root_matrix);
			result->forward = root_matrix.forward;
			result->up = root_matrix.up;
			result->field_of_view = DEGREES_TO_RADIANS(70.f);
			result->focus_position = root_matrix.position;
			result->focus_distance = 0.f;
			result->timer = 0.f;
			result->flags |= FLAG(0);
		}
		break;

	case _camera_script_mode_first_person:
		if (object_try_and_get_and_verify_type(
			data_002dcb60.relative_object_index,
			_object_mask_unit))
		{
			struct camera_command prefix;

			/* The first-person helper updates only the 0x4C-byte prefix. */
			csmemcpy(&prefix, result, sizeof(prefix));
			first_person_camera_fake(
				data_002dcb60.relative_object_index,
				&prefix);
			csmemcpy(result, &prefix, sizeof(prefix));
		}
		break;

	case _camera_script_mode_dead:
		if (object_try_and_get_and_verify_type(
			data_002dcb60.relative_object_index,
			_object_mask_unit))
		{
			if (data_002dcb60.first_update)
			{
				dead_camera_new(
					camera,
					controls->local_player_index,
					data_002dcb60.relative_object_index);
			}
			dead_camera_update(camera, controls, result);
		}
		break;
	}

	data_002dcb60.timer = MAX(
		0.f,
		data_002dcb60.timer - speed * controls->seconds_elapsed);
	data_002dcb60.first_update = FALSE;

	match_vassert(
		"c:\\halo\\SOURCE\\camera\\camera_scripting.c",
		0x172,
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
