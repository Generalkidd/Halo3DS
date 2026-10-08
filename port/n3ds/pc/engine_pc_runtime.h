#ifndef HALO_N3DS_ENGINE_PC_RUNTIME_H
#define HALO_N3DS_ENGINE_PC_RUNTIME_H

struct n3ds_cache_view;
struct scenario_structure_bsp_reference;

/* Experimental PC retail handoff and resource-file reads. */
int n3ds_pc_map_available(const char *name);
int n3ds_pc_tags_open(const char *name, struct n3ds_cache_view *view);
void n3ds_pc_tags_close(void);
int n3ds_pc_tags_active(void);
int n3ds_pc_cache_read(unsigned long offset, unsigned long bytes, void *destination);
int n3ds_pc_cache_range_valid(unsigned long offset, unsigned long bytes);
int n3ds_pc_bsp_load(struct n3ds_cache_view *view,
    const struct scenario_structure_bsp_reference *reference);

#endif
