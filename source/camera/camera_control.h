/* Shared director input packet; original Xbox layout. */
#ifndef __CAMERA_CONTROL_H
#define __CAMERA_CONTROL_H
#pragma once

#include "cseries/cseries.h"
#include "math/real_math.h"

struct camera_control
{
	short local_player_index;
	boolean active;
	byte pad3;
	real seconds_elapsed;
	real_euler_angles3d facing_delta;
	real_vector3d position_delta;
	real wheel_delta;
};

typedef char camera_control_size_assert[
	sizeof(struct camera_control) == 0x24 ? 1 : -1];
typedef char camera_control_elapsed_offset_assert[
	offsetof(struct camera_control, seconds_elapsed) == 0x04 ? 1 : -1];
typedef char camera_control_active_offset_assert[
	offsetof(struct camera_control, active) == 0x02 ? 1 : -1];

#endif
