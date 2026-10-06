#ifndef HALO_N3DS_ENGINE_FILES_H
#define HALO_N3DS_ENGINE_FILES_H
/* Fixed-width ABI; all FILE, DIR, stat and off_t types stay in the GCC side. */
struct native_file_info { unsigned int size, date[2]; int directory, readonly; };
_Static_assert(sizeof(struct native_file_info)==20,"Native file metadata ABI");
void n3ds_files_lock(void);
void n3ds_files_unlock(void);
void n3ds_files_thread_exit(void);
int n3ds_files_path_in_use(const char *path);
int n3ds_file_resolve(const char *xbox_path, char *native_path, unsigned int capacity, int write);
unsigned int n3ds_file_open(const char *path, unsigned int permissions);
int n3ds_file_valid(unsigned int handle);
int n3ds_file_close(unsigned int handle);
int n3ds_file_read(unsigned int handle, void *buffer, unsigned int size);
int n3ds_file_write(unsigned int handle, const void *buffer, unsigned int size);
/* Dedicated sequential writer: no intervening reads; close/seek commits it. */
int n3ds_file_write_buffered(unsigned int handle,const void *buffer,unsigned int size);
int n3ds_file_read_at(unsigned int handle, unsigned int position, void *buffer, unsigned int size);
int n3ds_file_write_at(unsigned int handle, unsigned int position, const void *buffer, unsigned int size);
unsigned int n3ds_file_position(unsigned int handle);
unsigned int n3ds_file_size(unsigned int handle);
int n3ds_file_seek(unsigned int handle, unsigned int position);
int n3ds_file_resize(unsigned int handle, unsigned int size);
int n3ds_file_stat(const char *path, struct native_file_info *info);
int n3ds_file_create(const char *path, int directory);
int n3ds_file_delete(const char *path, int directory);
int n3ds_file_rename(const char *old_path, const char *new_path);
int n3ds_file_copy(const char *old_path,const char *new_path,int fail_if_exists);
int n3ds_files_find_start(const char *path, unsigned int flags);
int n3ds_files_find_next(char *relative, unsigned int capacity, struct native_file_info *info);
void n3ds_files_find_end(void);
unsigned int n3ds_files_open_count(void);
unsigned int n3ds_files_directory_count(void);
int halo_engine_file_tests(void);
int halo_engine_file_thread_tests(void);
#endif
