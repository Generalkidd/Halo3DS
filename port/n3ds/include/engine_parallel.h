#ifndef N3DS_ENGINE_PARALLEL_H
#define N3DS_ENGINE_PARALLEL_H
/* Synchronous fork/join of disjoint outputs and immutable input. Called only
 * by the game thread; callbacks must not allocate, submit GPU work, mutate
 * engine state, perform I/O or recursively schedule another range. */
typedef int (*n3ds_range_function)(void *,unsigned int,unsigned int);
void n3ds_parallel_initialize(void);
void n3ds_parallel_shutdown(void);
int n3ds_parallel_range(n3ds_range_function function,void *context,
    unsigned int count,unsigned int minimum,unsigned int category);
int n3ds_parallel_tests(void);
#endif
