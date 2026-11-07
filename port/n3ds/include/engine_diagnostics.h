#ifndef HALO_N3DS_ENGINE_DIAGNOSTICS_H
#define HALO_N3DS_ENGINE_DIAGNOSTICS_H

#if defined(HALO_N3DS_PERFORMANCE_PROFILE) || defined(HALO_N3DS_RENDERER_TESTS) || HALO_N3DS_MISSION_TICKS || \
    defined(HALO_N3DS_ARCHIVE_VERIFY) || defined(HALO_N3DS_HUD_PROFILE) || defined(HALO_N3DS_MODEL_STAGE_PROFILE)
#define N3DS_MISSION_PROFILE 1
#else
#define N3DS_MISSION_PROFILE 0
#endif

/* Fixed-width bridge; no newlib allocator structs cross the engine ABI. */
struct native_memory_snapshot {
    unsigned int heap_used, heap_free, linear_free, application_free;
    unsigned int application_size, application_used, application_query_ok, application_free_valid;
};
_Static_assert(sizeof(struct native_memory_snapshot)==32,"Memory snapshot ABI");
void n3ds_engine_memory_snapshot(struct native_memory_snapshot *result);
#endif
