#ifndef HALO_N3DS_ENGINE_TEXTURES_H
#define HALO_N3DS_ENGINE_TEXTURES_H
#include "engine_texture_descriptor.h"
int n3ds_gpu_texture_wrap(unsigned int stage,int clamp_u,int clamp_v);
/* Fixed-width scalar/pointer interface between the engine and libctru ABI. */
int n3ds_gpu_textures_initialize(void);
void n3ds_gpu_textures_dispose(void);
int n3ds_gpu_texture_bind(unsigned int stage, void *resource);
enum { NATIVE_SAMPLER_MAG_POINT=1, NATIVE_SAMPLER_MIN_POINT=2,
    NATIVE_SAMPLER_CLAMP_U=4, NATIVE_SAMPLER_CLAMP_V=8, NATIVE_SAMPLER_PROJECTIVE=16 };
/* Keep the real owner identity while overriding only this unit's sampler. */
int n3ds_gpu_texture_bind_sampler(unsigned int stage,void *resource,unsigned int flags);
int n3ds_gpu_texture_sampler_tests(void);
void *n3ds_gpu_texture_bound(unsigned int stage);
/* Queued map resource lookup without changing GPU sampler state. */
void *n3ds_engine_bitmap_resource(int group_index,int index);
int n3ds_engine_bitmap_resource_tests(void);
/* Authored dimensions, independent of decoded mip and hardware profile. */
int n3ds_engine_bitmap_dimensions(int group_index,int index,int size[2]);
struct bitmap_data;
/* Hints do no file or GPU work. Prewarm is for the loading screen only. */
int n3ds_engine_texture_prefetch(const struct bitmap_data *bitmap);
int n3ds_engine_texture_prewarm(const struct bitmap_data *bitmap);
/* Dispatch the next read-ahead job after frame submission, before pacing. */
void n3ds_engine_texture_prefetch_kick(int speculative);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
unsigned int n3ds_engine_texture_presentation_revision(void);
#endif
struct native_texture_load_profile {
    long long read_ticks, decode_ticks, tile_ticks, upload_ticks;
    unsigned int attempts, loads, failures, read_bytes, archive_hits;
};
struct native_texture_profile {
    struct native_texture_load_profile demand, queued, prefetch;
    long long maintenance_ticks, fence_ticks, worker_read_ticks, worker_prepare_ticks;
    unsigned int demand_hits, demand_evictions, maintenance_evictions;
    unsigned int slot_evictions, byte_evictions, headroom_evictions, allocation_failures;
    unsigned int prefetch_submitted, prefetch_late, prefetch_discarded, worker_read_bytes;
    unsigned int resident_bytes, resident_slots, budget_bytes, linear_free;
};
/* Snapshot the preceding frame after all draws and reset the counters. */
void n3ds_engine_texture_profile_read(struct native_texture_profile *result);
/* Map-owned metadata, valid only until its tag data unloads. No GPU handle is
 * retained: normal bitmap binding still resolves/loads the current resource. */
const struct bitmap_data *n3ds_engine_bitmap_metadata(int group_index,int index);
/* Bind the final sampler once, without installing a temporary default wrap. */
int n3ds_engine_bitmap_bind_sampler(unsigned int stage,const struct bitmap_data *bitmap,unsigned int flags);
int n3ds_gpu_texture_sampling_test(unsigned int stage, const unsigned int expected_corners[4]);
void *n3ds_gpu_texture_create(const unsigned int *pixels, unsigned int width, unsigned int height);
/* Main-thread ownership: reserve returns private RGBA8 tiled image pointers
 * (one image or six cube faces). A worker may fill them, but must finish before
 * finalize/abandon. Finalize flushes; only then may the owner publish/bind.
 * Abandon is only for an unpublished resource, never referenced by GPU work. */
void *n3ds_gpu_texture_reserve_descriptor(const struct native_texture_descriptor *,struct native_texture_span spans[6]);
int n3ds_gpu_texture_finalize_descriptor(void *resource,const struct native_texture_descriptor *);
/* Compatibility wrappers retain the same RGBA8 dimensions, cube limits and
 * sampler defaults. The descriptor path returns bounded byte spans, not an
 * assumed pixel type; only a validated RGBA8 owner may use unsigned pixels. */
void *n3ds_gpu_texture_reserve(unsigned int width,unsigned int height,int cube,unsigned int *pixels[6]);
int n3ds_gpu_texture_finalize(void *resource);
void n3ds_gpu_texture_abandon(void *resource);
void *n3ds_gpu_texture_create_rgba4(const void *pixels, unsigned int width, unsigned int height);
/* Six row-major RGBA byte faces in Xbox +X,-X,+Y,-Y,+Z,-Z order. */
void *n3ds_gpu_texture_create_cube(const unsigned char *rgba,unsigned int side);
int n3ds_gpu_cube_tests(void);
int n3ds_engine_cube_layout_tests(void);
void n3ds_gpu_texture_destroy(void *resource);
int n3ds_gpu_texture_barrier(void);
unsigned int n3ds_gpu_texture_memory_free(void);
unsigned int n3ds_gpu_texture_checksum(void *resource);
int n3ds_gpu_texture_validate_rgba8(const void *resource,const unsigned int *pixels,
    unsigned int width,unsigned int height);
/* Only map-cache/derivative owners publish immutable allocations: pixel bytes
 * stay fixed until destroy. Generated cache owners use revisions below. */
unsigned int n3ds_gpu_texture_publish_immutable(void *resource);
/* Main-thread generated-cache owners invalidate before every write or delete,
 * then publish after flushing a completed image. The owner still fences old
 * GPU uses and keeps published pixels fixed until the next begin. A repeated
 * publish without begin retains its ID; exhaustion safely returns zero. */
void n3ds_gpu_texture_revision_begin(void *resource);
unsigned int n3ds_gpu_texture_revision_publish(void *resource);
/* Unpublished resources return zero and require exact content comparison. */
unsigned int n3ds_gpu_texture_content_id(void *resource);
int n3ds_gpu_texture_identity_tests(void);
void texture_cache_flush(void);
void texture_cache_open(void);
void texture_cache_close(void);
unsigned int n3ds_engine_texture_count(void);
unsigned int n3ds_engine_texture_bytes(void);
/* Change quality only with an empty cache; dimensions stay power-of-two. */
int n3ds_engine_texture_resolution(unsigned int limit);
int halo_engine_texture_tests(void);
int n3ds_engine_sampled_texture_tests(void);
/* Immutable alpha-first copy for PICA plasma bump lookup; shares owner lifetime.
 * available is the map budget remaining after ordinary textures, including
 * all existing variants. Failure preserves the original material fallback. */
void *n3ds_gpu_texture_plasma(void *resource,unsigned int available);
unsigned int n3ds_gpu_texture_variant_bytes(void);
int n3ds_gpu_texture_plasma_tests(void);
int n3ds_engine_bitmap_bind_plasma(unsigned int stage,const struct bitmap_data *,unsigned int flags);
#endif
