#ifndef HALO_N3DS_ENGINE_PC_PREVIEW_H
#define HALO_N3DS_ENGINE_PC_PREVIEW_H

struct native_input_sample;
/* Owner-supplied PC maps are read locally. The compact preview geometry is
 * disposable and never included in a distributed launcher. */
void n3ds_pc_preview_input(const struct native_input_sample *sample);
void n3ds_pc_preview_draw(int map,int page,int field,int weapon_set,int vehicle_choice);
int n3ds_pc_preview_nav_enabled(void);
int n3ds_pc_preview_view_mode(void);
int n3ds_pc_preview_backfaces_enabled(void);
void n3ds_pc_preview_close(void);
void n3ds_xbox_preview_draw(int map);
void n3ds_gear_preview_draw(int map,int pc,int field,int weapons,int vehicles);
int n3ds_preview_available(void);
void *n3ds_pc_preview_thumbnail(int map);
int n3ds_preview_loading(void);
/* Main thread: update after GPU queue completion; dispose at map teardown. */
void n3ds_preview_update(void);
void n3ds_preview_dispose(void);

#endif
