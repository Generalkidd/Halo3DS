/* Include the production sampler to exercise its private reference and
 * candidate paths against the same immutable synthetic map stream. */
#include "models/model_animations.c"

#undef memcpy
#undef memset
#undef memcmp
#undef free

enum { STREAM_BYTES = 256 };
static struct animation animation, pressure[1100];
static union { uint32_t alignment; byte bytes[STREAM_BYTES]; } stream;
static union { uint64_t alignment; byte bytes[64 * 1024]; } cache;
static unsigned int generation = 1;
static int map_owned = 1, allocate_ok = 1;
static volatile int cache_live;

static int owns(const void *pointer, unsigned int bytes, const void *start, unsigned int length)
{
	uintptr_t p = (uintptr_t)pointer, base = (uintptr_t)start;
	return p >= base && p - base <= length && bytes <= length - (p - base);
}

int n3ds_animation_map_owned(const void *pointer, unsigned int bytes)
{
	return map_owned && (owns(pointer, bytes, &animation, sizeof(animation)) ||
		owns(pointer, bytes, &pressure, sizeof(pressure)) ||
		owns(pointer, bytes, &stream, sizeof(stream)));
}

unsigned int n3ds_engine_tag_generation(void) { return generation; }
int n3ds_input_platform_original_model(void) { return 0; }

void *calloc(size_t count, size_t size)
{
	if (!allocate_ok) return NULL;
	if (cache_live || count * size > sizeof(cache.bytes)) __builtin_trap();
	cache_live = 1;
	memset(cache.bytes, 0, count * size);
	return cache.bytes;
}

void __wrap_free(void *pointer)
{
	if (pointer && (pointer != cache.bytes || !cache_live)) __builtin_trap();
	cache_live = 0;
}

void *csmemset(void *dest, long value, unsigned long size) { return memset(dest, value, size); }
void *csmemcpy(void *dest, const void *src, unsigned long size) { return memcpy(dest, src, size); }
long csmemcmp(const void *left, const void *right, unsigned long size) { return memcmp(left, right, size); }

void display_assert(char *information, char *file, long line, boolean fatal) { __builtin_trap(); }
void system_exit(long code) { __builtin_trap(); }
void halt_and_catch_fire(void) { __builtin_trap(); }
void error(short priority, const char *format, ...) { __builtin_trap(); }

void *tag_data_get_pointer(const struct tag_data *data, long offset, long size)
{
	if (offset < 0 || size < 0 || offset > data->size || size > data->size - offset)
		__builtin_trap();
	return (byte *)data->address + offset;
}

static void set_frame_data(unsigned int skew, unsigned int count, short last_frame)
{
	native_animation_track_cache_reset();
	memset(&stream, 0, sizeof(stream));
	memset(&animation, 0, sizeof(animation));
	struct compressed_animation_header *header = (void *)stream.bytes;
	header->rotation_node_headers[0] = count;
	header->rotation_keyframe_frame_indices_offset = 64;
	header->rotation_keyframes_offset = 96;
	header->default_rotations_offset = 120;
	header->translation_node_headers_offset = 56;
	header->translation_keyframe_frame_indices_offset = 72;
	header->translation_keyframes_offset = 144 + skew;
	header->default_translations_offset = 128 + skew;
	header->scale_node_headers_offset = 60;
	header->scale_keyframe_frame_indices_offset = 80;
	header->scale_keyframes_offset = 200 + skew;
	header->default_scales_offset = 192 + skew;
	uint32_t node = count;
	memcpy(stream.bytes + 56, &node, sizeof(node));
	memcpy(stream.bytes + 60, &node, sizeof(node));

	short frames[3] = {2, 4, last_frame};
	if (count == 1) frames[0] = last_frame;
	memcpy(stream.bytes + 64, frames, sizeof(frames));
	memcpy(stream.bytes + 72, frames, sizeof(frames));
	memcpy(stream.bytes + 80, frames, sizeof(frames));
	real_quaternion defaults = {.v = {0.125f, 0.3f, -0.125f}, .w = .6f};
	real_quaternion rotations[] = {
		{.v = {.2f, .25f, .5f}, .w = .125f},
		{.v = {-.6f, .03f, .03f}, .w = .05f},
		{.v = {.4f, .125f, -.7f}, .w = .2f}
	};
	quaternion_compress_6byte(&defaults, (void *)(stream.bytes + 120));
	for (unsigned int i = 0; i < 3; ++i)
		quaternion_compress_6byte(rotations + i, (void *)(stream.bytes + 96 + 6 * i));

	real_point3d translation = {10.f, -0.f, 12.f};
	real_point3d translations[] = {{-2.f, 10.f, 0.f}, {0.2f, -.25f, 1.f}, {6.f, 11.f, -.5f}};
	real scale = -0.f, scales[] = {-.125f, 1.f, 2.25f};
	memcpy(stream.bytes + header->default_translations_offset, &translation, sizeof(translation));
	memcpy(stream.bytes + header->translation_keyframes_offset, translations, sizeof(translations));
	memcpy(stream.bytes + header->default_scales_offset, &scale, sizeof(scale));
	memcpy(stream.bytes + header->scale_keyframes_offset, scales, sizeof(scales));

	animation.type = _animation_overlay;
	animation.frame_count = last_frame + 1;
	animation.node_count = 1;
	animation.flags = FLAG(_animation_compressed_bit);
	animation.nodes_with_rotation_flags[0] = 1;
	animation.nodes_with_translation_flags[0] = 1;
	animation.nodes_with_scale_flags[0] = 1;
	animation.data.address = stream.bytes;
	animation.data.size = STREAM_BYTES;
}

static int compare_sample(real frame)
{
	real_quaternion ref_rotation, candidate_rotation;
	real_point3d ref_translation, candidate_translation;
	real ref_scale, candidate_scale;
	animation_reference_keyframe_rotation(&animation, frame, 0, 0, &ref_rotation);
	animation_reference_keyframe_translation(&animation, frame, 0, 0, &ref_translation);
	animation_reference_keyframe_scale(&animation, frame, 0, 0, &ref_scale);
	animation_get_keyframe_rotation(&animation, frame, 0, 0, &candidate_rotation);
	animation_get_keyframe_translation(&animation, frame, 0, 0, &candidate_translation);
	animation_get_keyframe_scale(&animation, frame, 0, 0, &candidate_scale);
	return !memcmp(&ref_rotation, &candidate_rotation, sizeof(ref_rotation)) &&
		!memcmp(&ref_translation, &candidate_translation, sizeof(ref_translation)) &&
		!memcmp(&ref_scale, &candidate_scale, sizeof(ref_scale));
}

#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)

int animation_tracks_fixture(void)
{
	static const real samples[] = {0.f, .25f, 1.75f, 2.f, 2.125f, 3.875f, 4.f, 4.125f,
		5.75f, 6.f, 6.75f, 6.25f, 5.f, 3.f, 1.f, 0.f, 6.75f, .5f};
	for (unsigned int skew = 0; skew < 4; ++skew)
	{
		set_frame_data(skew, 3, 6);
		for (unsigned int i = 0; i < NUMBEROF(samples); ++i)
			CHECK(compare_sample(samples[i]));

		/* Exercise the real overlay consumer and its historical integer
		 * compressed rotation sample alongside fractional other tracks. */
		real_orientation actual = {.rotation = {.w = 1.f}, .translation = {1.f, 2.f, 3.f}, .scale = 1.5f};
		real_orientation expected = actual;
		real_quaternion rotation;
		real_point3d translation;
		real scale;
		animation_reference_keyframe_rotation(&animation, 2.f, 0, 0, &rotation);
		animation_reference_keyframe_translation(&animation, 2.5f, 0, 0, &translation);
		animation_reference_keyframe_scale(&animation, 2.5f, 0, 0, &scale);
		quaternions_multiply(&rotation, &expected.rotation, &expected.rotation);
		expected.translation.x += translation.x;
		expected.translation.y += translation.y;
		expected.translation.z += translation.z;
		expected.scale *= scale;
		overlay_animation_apply_continuous(&animation, 2.5f, &actual);
		CHECK(!memcmp(&actual, &expected, sizeof(actual)));
	}

	set_frame_data(1, 0, 6);
	CHECK(compare_sample(0.f) && compare_sample(6.75f));
	CHECK(native_animation_tracks && cache_live);
	real_point3d before, after;
	animation_get_keyframe_translation(&animation, 0.f, 0, 0, &before);
	native_animation_default_translation(&animation, stream.bytes, 0, &after);
	CHECK(!memcmp(&before, &after, sizeof(after)));
	real_quaternion default_rotation, reference_rotation;
	animation_reference_keyframe_rotation(&animation, 0.f, 0, 0, &reference_rotation);
	native_animation_default_rotation(&animation, stream.bytes, 0, &default_rotation);
	CHECK(!memcmp(&default_rotation, &reference_rotation, sizeof(default_rotation)));
	struct {
		struct compressed_animation_header header;
		real_point3d value;
	} alternate = {.header = {.default_translations_offset = sizeof(struct compressed_animation_header)},
		.value = {21.f, 31.f, 41.f}};
	native_animation_default_translation(&animation, (const void *)&alternate, 0, &after);
	CHECK(!memcmp(&after, &alternate.value, sizeof(after)));
	/* A new generation can reuse the same source addresses. */
	++generation;
	real_point3d replacement = {19.f, -0.f, 12.f};
	const struct compressed_animation_header *header = (const void *)stream.bytes;
	memcpy(stream.bytes + header->default_translations_offset, &replacement, sizeof(replacement));
	animation_get_keyframe_translation(&animation, 0.f, 0, 0, &after);
	CHECK(memcmp(&before, &after, sizeof(before)) && !memcmp(&after, &replacement, sizeof(after)));

	set_frame_data(2, 1, 0);
	CHECK(compare_sample(0.f) && compare_sample(.5f));
	set_frame_data(0, 3, 6);
	for (unsigned int i = 0; i < NUMBEROF(pressure); ++i)
	{
		pressure[i] = animation;
		real_point3d out, ref;
		animation_reference_keyframe_translation(pressure + i, 2.5f, 0, 0, &ref);
		animation_get_keyframe_translation(pressure + i, 2.5f, 0, 0, &out);
		CHECK(!memcmp(&out, &ref, sizeof(out)));
	}
	CHECK(native_animation_track_capacity == NATIVE_ANIMATION_TRACKS_NEW);

	union native_animation_track_value value;
	set_frame_data(0, 3, 6);
	struct compressed_animation_header *bad = (void *)stream.bytes;
	bad->rotation_keyframes_offset = STREAM_BYTES;
	CHECK(!native_animation_track_sample(&animation, 1.f, 0, 0, NATIVE_ANIMATION_ROTATION, &value));
	set_frame_data(0, 3, 6);
	short unsorted[] = {3, 2, 6};
	memcpy(stream.bytes + 72, unsorted, sizeof(unsorted));
	CHECK(!native_animation_track_sample(&animation, 1.f, 0, 0, NATIVE_ANIMATION_TRANSLATION, &value));

	set_frame_data(0, 3, 6);
	map_owned = 0;
	CHECK(compare_sample(2.25f) && !cache_live);
	map_owned = 1;
	allocate_ok = 0;
	CHECK(compare_sample(2.25f) && !cache_live && native_animation_track_allocation_failed);
	allocate_ok = 1;
	native_animation_track_cache_reset();
	CHECK(compare_sample(2.25f) && cache_live);
	native_animation_track_cache_reset();
	CHECK(!cache_live);
	return 0;
}
