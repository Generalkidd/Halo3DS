/*
FLYING_CAMERA.H

header included in hcex build.
*/

#ifndef __FLYING_CAMERA_H
#define __FLYING_CAMERA_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "camera_control.h"
#include "math/real_math.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct flying_camera
{
	real_point3d position;
	real_euler_angles2d facing;
	real roll;
	real field_of_view;
};

struct flying_camera_action
{
	short local_player_index;
	boolean active;
	byte pad3;
	real seconds_elapsed;
	real_euler_angles3d facing_delta;
	real_vector3d translation;
	real wheel_delta;
};


/* The director supplies this exact packet to flying/editor cameras. */
typedef char flying_camera_action_size_assert[
	sizeof(struct flying_camera_action) == sizeof(struct camera_control) ? 1 : -1];
typedef char flying_camera_action_local_player_index_offset_assert[
	offsetof(struct flying_camera_action, local_player_index) ==
		offsetof(struct camera_control, local_player_index) ? 1 : -1];
typedef char flying_camera_action_active_offset_assert[
	offsetof(struct flying_camera_action, active) ==
		offsetof(struct camera_control, active) ? 1 : -1];
typedef char flying_camera_action_seconds_elapsed_offset_assert[
	offsetof(struct flying_camera_action, seconds_elapsed) ==
		offsetof(struct camera_control, seconds_elapsed) ? 1 : -1];
typedef char flying_camera_action_facing_delta_offset_assert[
	offsetof(struct flying_camera_action, facing_delta) ==
		offsetof(struct camera_control, facing_delta) ? 1 : -1];
typedef char flying_camera_action_translation_offset_assert[
	offsetof(struct flying_camera_action, translation) ==
		offsetof(struct camera_control, position_delta) ? 1 : -1];
typedef char flying_camera_action_wheel_delta_offset_assert[
	offsetof(struct flying_camera_action, wheel_delta) ==
		offsetof(struct camera_control, wheel_delta) ? 1 : -1];

struct camera_command;

/* ---------- prototypes/FLYING_CAMERA.C */

void flying_camera_new(
	struct flying_camera *camera);
void flying_camera_new_from_point_and_vector(
	struct flying_camera *camera,
	real_point3d const *position,
	real_vector3d const *forward);
void flying_camera_update(
	struct flying_camera *camera,
	struct flying_camera_action const *controls,
	struct camera_command *result);

/* ---------- globals */

/* ---------- public code */

#endif // __FLYING_CAMERA_H
