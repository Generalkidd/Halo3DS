/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_CAMPAIGN_OPTIONS_H
#define HALO_N3DS_ENGINE_CAMPAIGN_OPTIONS_H

#include "engine_input.h"

int n3ds_campaign_extras_enabled(void);
int n3ds_campaign_options_save_error(void);
void n3ds_campaign_options_filter(struct native_input_sample *sample);
int n3ds_ui_difficulty_menu_active(void);

#endif
