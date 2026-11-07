#ifndef N3DS_ENGINE_PARALLEL_H
#define N3DS_ENGINE_PARALLEL_H
#include "engine_algorithm_features.h"
/* Synchronous fork/join of disjoint outputs and immutable input. Called only
 * by the game thread; callbacks must not allocate, submit GPU work, mutate
 * engine state, perform I/O or recursively schedule another range. */
typedef int (*n3ds_range_function)(void *,unsigned int,unsigned int);
void n3ds_parallel_initialize(void);
void n3ds_parallel_shutdown(void);
int n3ds_parallel_range(n3ds_range_function function,void *context,
    unsigned int count,unsigned int minimum,unsigned int category);
int n3ds_parallel_tests(void);
struct model_vertex_compressed;
struct model_vertex_uncompressed;
struct n3ds_model_decode_range {
    const struct model_vertex_compressed *input;
    struct model_vertex_uncompressed *output;
};
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
/* A nonzero return is the owner's prefix length. The worker owns a copy
 * of the context. Referenced input/output storage must stay pinned until
 * end acknowledges completion, including when the owner discards a result. */
unsigned int n3ds_parallel_decode_begin(n3ds_range_function, const struct n3ds_model_decode_range *,
    unsigned int count, unsigned int minimum);
int n3ds_parallel_decode_end(int discard);
#endif
#endif
