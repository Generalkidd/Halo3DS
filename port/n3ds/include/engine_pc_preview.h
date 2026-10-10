#ifndef HALO_N3DS_ENGINE_PC_PREVIEW_H
#define HALO_N3DS_ENGINE_PC_PREVIEW_H

struct native_input_sample;
/* Owner-supplied PC maps are read locally. The compact preview geometry is
 * disposable and never included in a distributed launcher. */
void n3ds_pc_preview_input(const struct native_input_sample *sample);
void n3ds_pc_preview_draw(int map,int page,int field,int weapon_set,int vehicle_choice);
int n3ds_pc_preview_nav_enabled(void);
int n3ds_pc_preview_view_mode(void);
void n3ds_pc_preview_close(void);

#endif
