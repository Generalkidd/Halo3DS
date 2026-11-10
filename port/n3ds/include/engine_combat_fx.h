/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_COMBAT_FX_H
#define HALO_N3DS_ENGINE_COMBAT_FX_H
#include <string.h>
#include "engine_controls.h"

/* Kevin Boggs: n3ds_combat_fx_name - New3DS integration. */
static inline int n3ds_combat_fx_name(const char *name)
{
    return n3ds_controls_quality() && name && (strstr(name,"weapons\\") || strstr(name,"grenade") ||
        strstr(name,"explosion") || strstr(name,"impact") ||
        strstr(name,"shield") || strstr(name,"bullet") ||
        strstr(name,"tracer") || strstr(name,"muzzle") ||
        strstr(name,"blood") || strstr(name,"spark"));
}
#endif
