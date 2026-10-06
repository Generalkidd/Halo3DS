#ifndef HALO_N3DS_ENGINE_THREADS_H
#define HALO_N3DS_ENGINE_THREADS_H
/* Fixed-width ABI, with kernel/libctru structures confined to the platform. */
unsigned int n3ds_thread_create(unsigned int flags,unsigned int (*entry)(void *),void *argument);
int n3ds_thread_exited(unsigned int token);
int n3ds_thread_result(unsigned int token,unsigned int *result);
int n3ds_thread_close(unsigned int token);
unsigned int n3ds_mutex_create(void);
int n3ds_mutex_take(unsigned int token,unsigned int timeout_ms);
int n3ds_mutex_release(unsigned int token);
int n3ds_mutex_close(unsigned int token);
void n3ds_threads_collect(void);
unsigned int n3ds_thread_count(void);
unsigned int n3ds_mutex_count(void);
int halo_engine_thread_tests(void);
#endif
