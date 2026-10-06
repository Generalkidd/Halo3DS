#ifndef HALO_N3DS_ENGINE_DIAGNOSTICS_H
#define HALO_N3DS_ENGINE_DIAGNOSTICS_H
/* Fixed-width bridge; no newlib allocator structs cross the engine ABI. */
struct native_memory_snapshot {
    unsigned int heap_used, heap_free, linear_free, application_free;
    unsigned int application_size, application_used, application_query_ok, application_free_valid;
};
_Static_assert(sizeof(struct native_memory_snapshot)==32,"Memory snapshot ABI");
void n3ds_engine_memory_snapshot(struct native_memory_snapshot *result);
#endif
