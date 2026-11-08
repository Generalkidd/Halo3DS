#ifndef HALO_N3DS_ENGINE_SOUNDS_H
#define HALO_N3DS_ENGINE_SOUNDS_H
unsigned int n3ds_engine_sound_count(void);
unsigned int n3ds_engine_sound_bytes(void);
void n3ds_engine_sounds_unload(void);
int halo_engine_sound_tests(void);

/* Game-thread API for one private encoded SD read. No tag or cache pointer
 * crosses into the worker. */
#define N3DS_SOUND_READ_LIMIT (128u*1024)
struct n3ds_sound_read_key {
    unsigned int map_generation,handle,offset,bytes;
};
int n3ds_sound_reader_supported(void);
void n3ds_sound_reader_offer(const char *path,const struct n3ds_sound_read_key *key);
int n3ds_sound_reader_copy(const struct n3ds_sound_read_key *key,void *destination);
void n3ds_sound_reader_discard(unsigned int map_generation,unsigned int handle);
void n3ds_sound_reader_cancel(void);
void n3ds_sound_reader_shutdown(void);
#endif
