#ifndef HALO_N3DS_ENGINE_SAVES_H
#define HALO_N3DS_ENGINE_SAVES_H
/* No SDK or native filesystem structures cross this boundary. */
struct native_save_info { char path[260]; unsigned short name[128]; unsigned int date[2],flags; };
unsigned int n3ds_save_create(const unsigned short *name,unsigned int disposition,unsigned int flags,char *path,unsigned int size);
unsigned int n3ds_save_delete(const unsigned short *name);
unsigned int n3ds_save_find_first(struct native_save_info *info);
int n3ds_save_find_next(unsigned int token,struct native_save_info *info);
int n3ds_save_find_close(unsigned int token);
unsigned int n3ds_save_find_count(void);
int n3ds_save_root_valid(const char *root);
int n3ds_disk_space(const char *path,unsigned long long *available,unsigned long long *total,unsigned long long *free_bytes);
int halo_engine_save_tests(void);
#endif
