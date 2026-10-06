#ifndef HALO_N3DS_ENGINE_SETTINGS_H
#define HALO_N3DS_ENGINE_SETTINGS_H
unsigned int n3ds_engine_language_from_cfg(int language);
/* Game ABI uses 16-bit wchar_t; these declarations stay on the Clang side. */
int n3ds_engine_machine_name_set(const wchar_t *name);
void n3ds_engine_machine_name_get(wchar_t *name);
#endif
