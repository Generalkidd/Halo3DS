#ifndef HALO_N3DS_ENGINE_TEXTURE_PREFETCH_H
#define HALO_N3DS_ENGINE_TEXTURE_PREFETCH_H

/* One render-thread owner. A job's buffers and prepare context remain owned
 * by the worker until poll completes or shutdown joins it. */
typedef int (*n3ds_texture_prepare_fn)(void *context);
struct native_texture_read_result {
    unsigned int bytes;
    long long read_ticks, prepare_ticks;
};
enum { NATIVE_TEXTURE_READ_LIMIT = 512 * 1024 };
enum { NATIVE_TEXTURE_READ_RANGES = 6 };
struct native_texture_read_range {
    unsigned int offset,bytes;
    void *destination;
};
/* Ranges are copied at submission; their destinations remain worker-owned. */
int n3ds_texture_reader_submit_ranges(const char *path,const struct native_texture_read_range *ranges,
    unsigned int count,n3ds_texture_prepare_fn prepare,void *context);
int n3ds_texture_reader_submit(const char *path, unsigned int offset,
    unsigned int bytes, void *destination, n3ds_texture_prepare_fn prepare, void *context);
int n3ds_texture_reader_poll(struct native_texture_read_result *result);
void n3ds_texture_reader_cancel(void);
int n3ds_texture_reader_cancelled(void);
void n3ds_texture_reader_shutdown(void);

#endif
