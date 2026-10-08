#ifndef HALO_N3DS_ENGINE_BOTS_H
#define HALO_N3DS_ENGINE_BOTS_H
#include "cseries/cseries.h"
#include "game/players.h"
void n3ds_bots_reset(void);
short n3ds_bots_requested_count(void);
short n3ds_bots_create(short count);
void n3ds_bots_fill_action(short queue_index, short identifier, struct player_action *action);
#endif
