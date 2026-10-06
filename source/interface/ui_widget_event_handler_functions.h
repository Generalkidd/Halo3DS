/*
UI_WIDGET_EVENT_HANDLER_FUNCTIONS.H

header included in hcex build.
*/

#ifndef __UI_WIDGET_EVENT_HANDLER_FUNCTIONS_H
#define __UI_WIDGET_EVENT_HANDLER_FUNCTIONS_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct event_record;
struct widget_instance;

/* ---------- prototypes/UI_WIDGET_EVENT_HANDLER_FUNCTIONS.C */

boolean ui_widget_event_handler_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	word function_index,
	boolean *widget_deleted);

void reset_last_player1_profile_index(
	void);

#ifdef HALO_N3DS
short n3ds_ui_multiplayer_map_original_index(short index);
#endif

/* ---------- globals */

/* ---------- public code */

#endif // __UI_WIDGET_EVENT_HANDLER_FUNCTIONS_H
