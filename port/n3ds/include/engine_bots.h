/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_BOTS_H
#define HALO_N3DS_ENGINE_BOTS_H
#include "cseries/cseries.h"
#include "game/players.h"
void n3ds_bots_reset(void);
void n3ds_bots_register_network(long index);
short n3ds_bots_requested_count(void);
void n3ds_bots_set_menu_count(short count);
void n3ds_bots_set_menu_difficulty(short difficulty);
short n3ds_bots_menu_difficulty(void);
short n3ds_bots_create(short count);
void n3ds_bots_fill_action(short queue_index, short identifier, struct player_action *action);
#endif
