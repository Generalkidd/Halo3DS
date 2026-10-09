/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_PC_DIAGNOSTIC_MAPS_H
#define HALO_N3DS_PC_DIAGNOSTIC_MAPS_H

/* Keep this order in sync with build.py's diagnostic PC multiplayer choices. */
static const char *const n3ds_pc_diagnostic_maps[] = {
    "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
    "damnation", "dangercanyon", "deathisland", "gephyrophobia",
    "hangemhigh", "icefields", "infinity", "longest", "prisoner",
    "putput", "ratrace", "sidewinder", "timberland", "wizard"
};

#if defined(HALO_N3DS_EXPERIMENTAL_PC_MAP_ID)
#if HALO_N3DS_EXPERIMENTAL_PC_MAP_ID < 0 || HALO_N3DS_EXPERIMENTAL_PC_MAP_ID >= 19
#error Invalid PC multiplayer diagnostic map ID
#endif
#define HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME n3ds_pc_diagnostic_maps[HALO_N3DS_EXPERIMENTAL_PC_MAP_ID]
#endif

#endif
