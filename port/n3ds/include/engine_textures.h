#ifndef HALO_N3DS_ENGINE_TEXTURES_H
#define HALO_N3DS_ENGINE_TEXTURES_H
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
/* Map-owned metadata, valid only until its tag data unloads. No GPU handle is
 * retained: normal bitmap binding still resolves/loads the current resource. */
const struct bitmap_data *n3ds_engine_bitmap_metadata(int group_index,int index);
/* Bind the final sampler once, without installing a temporary default wrap. */
int n3ds_engine_bitmap_bind_sampler(unsigned int stage,const struct bitmap_data *bitmap,unsigned int flags);
int n3ds_gpu_texture_sampling_test(unsigned int stage, const unsigned int expected_corners[4]);
void *n3ds_gpu_texture_create(const unsigned int *pixels, unsigned int width, unsigned int height);
void *n3ds_gpu_texture_create_rgba4(const void *pixels, unsigned int width, unsigned int height);
/* Six row-major RGBA byte faces in Xbox +X,-X,+Y,-Y,+Z,-Z order. */
void *n3ds_gpu_texture_create_cube(const unsigned char *rgba,unsigned int side);
int n3ds_gpu_cube_tests(void);
int n3ds_engine_cube_layout_tests(void);
void n3ds_gpu_texture_destroy(void *resource);
int n3ds_gpu_texture_barrier(void);
unsigned int n3ds_gpu_texture_memory_free(void);
unsigned int n3ds_gpu_texture_checksum(void *resource);
/* Only map-cache owners may publish: pixel bytes stay fixed until destroy.
 * Unpublished/dynamic resources return zero and require content comparison. */
unsigned int n3ds_gpu_texture_publish_immutable(void *resource);
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
