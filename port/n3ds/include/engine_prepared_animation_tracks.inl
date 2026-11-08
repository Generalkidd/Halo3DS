/* Included by model_animations.c after the reference track samplers. The
 * frontend owner is the only user of this cache. Workers consume poses. */

enum native_animation_track_kind
{
	NATIVE_ANIMATION_ROTATION,
	NATIVE_ANIMATION_TRANSLATION,
	NATIVE_ANIMATION_SCALE
};

enum
{
	NATIVE_ANIMATION_TRACKS_ORIGINAL = 256,
	NATIVE_ANIMATION_TRACKS_NEW = 1024
};

union native_animation_track_value
{
	real_quaternion rotation;
	real_point3d translation;
	real scale;
};

struct native_animation_track
{
	const struct animation *animation;
	const short *frames;
	const byte *keys, *default_key;
	short node, adjusted, count;
	short first_frame, end_frame, first_key, end_key;
	byte kind, decoded_mask;
	union native_animation_track_value decoded[2];
};

_Static_assert(sizeof(struct native_animation_track) == 64, "Bounded native animation track storage");

static struct native_animation_track *native_animation_tracks;
static unsigned int native_animation_track_capacity;
static unsigned int native_animation_track_generation;
static boolean native_animation_track_allocation_failed;

extern unsigned int n3ds_engine_tag_generation(void);

static void native_animation_track_cache_reset(void)
{
	(free)(native_animation_tracks);
	native_animation_tracks = NULL;
	native_animation_track_capacity = 0;
	native_animation_track_allocation_failed = FALSE;
}

static const byte *native_animation_track_span(
	const byte *data, unsigned int bytes, long offset,
	unsigned int first, unsigned int count, unsigned int stride)
{
	if (offset < 0 || (unsigned long)offset > bytes)
		return NULL;
	unsigned int available = (bytes - offset) / stride;
	if (first > available || count > available - first)
		return NULL;
	return data + offset + first * stride;
}

static unsigned int native_animation_track_stride(enum native_animation_track_kind kind)
{
	switch (kind)
	{
		case NATIVE_ANIMATION_ROTATION: return sizeof(struct compressed_quaternion_6byte);
		case NATIVE_ANIMATION_TRANSLATION: return sizeof(real_point3d);
		default: return sizeof(real);
	}
}

static void native_animation_track_read(enum native_animation_track_kind kind,
	const byte *source, union native_animation_track_value *value)
{
	switch (kind)
	{
		case NATIVE_ANIMATION_ROTATION:
			quaternion_decompress_6byte((const void *)source, &value->rotation);
			break;
		case NATIVE_ANIMATION_TRANSLATION:
			value->translation = animation_read_point(source);
			break;
		default:
			value->scale = animation_read_real(source);
			break;
	}
	NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_ENDPOINTS, 1);
}

static boolean native_animation_track_prepare(struct native_animation_track *track,
	const struct animation *animation, short adjusted, short node,
	enum native_animation_track_kind kind)
{
	const byte *data = animation->data.address;
	long data_bytes = animation->data.size;
	long offset = animation->compressed_data_offset;
	long node_offset, default_offset, frames_offset, keys_offset;
	unsigned int default_index = node;
	unsigned int stride = native_animation_track_stride(kind);

	if (!data || offset < 0 || data_bytes < offset ||
		data_bytes - offset < offsetof(struct compressed_animation_header, rotation_node_headers) ||
		!n3ds_animation_map_owned(animation, sizeof(*animation)) ||
		!n3ds_animation_map_owned(data, data_bytes))
		return FALSE;

	data += offset;
	unsigned int bytes = data_bytes - offset;
	if ((uintptr_t)data & (sizeof(long) - 1))
		return FALSE;
	const struct compressed_animation_header *header = (const void *)data;
	switch (kind)
	{
		case NATIVE_ANIMATION_ROTATION:
			node_offset = offsetof(struct compressed_animation_header, rotation_node_headers);
			default_offset = header->default_rotations_offset;
			frames_offset = header->rotation_keyframe_frame_indices_offset;
			keys_offset = header->rotation_keyframes_offset;
			break;
		case NATIVE_ANIMATION_TRANSLATION:
			node_offset = header->translation_node_headers_offset;
			default_offset = header->default_translations_offset;
			frames_offset = header->translation_keyframe_frame_indices_offset;
			keys_offset = header->translation_keyframes_offset;
			break;
		default:
			node_offset = header->scale_node_headers_offset;
			default_offset = header->default_scales_offset;
			frames_offset = header->scale_keyframe_frame_indices_offset;
			keys_offset = header->scale_keyframes_offset;
			default_index = adjusted;
			break;
	}

	track->default_key = native_animation_track_span(data, bytes, default_offset,
		default_index, 1, stride);
	if (!track->default_key || (kind == NATIVE_ANIMATION_ROTATION && ((uintptr_t)track->default_key & 1)))
		return FALSE;

	unsigned long node_header = 0;
	if (adjusted != NONE)
	{
		const byte *node_data = native_animation_track_span(data, bytes, node_offset,
			adjusted, 1, sizeof(node_header));
		if (!node_data || ((uintptr_t)node_data & (sizeof(node_header) - 1)))
			return FALSE;
		__builtin_memcpy(&node_header, node_data, sizeof(node_header));
	}

	unsigned int count = node_header & (FLAG(COMPRESSED_ANIMATION_NODE_HEADER_KEYFRAME_COUNT_BITS) - 1);
	unsigned int first = node_header >> COMPRESSED_ANIMATION_NODE_HEADER_KEYFRAME_COUNT_BITS;
	if (count)
	{
		const byte *frames = native_animation_track_span(data, bytes, frames_offset,
			first, count, sizeof(short));
		track->keys = native_animation_track_span(data, bytes, keys_offset, first, count, stride);
		if (first > INT16_MAX || !track->keys || !frames || ((uintptr_t)frames & 1) ||
			(kind == NATIVE_ANIMATION_ROTATION && ((uintptr_t)track->keys & 1)))
			return FALSE;

		track->frames = (const short *)frames;
		if (track->frames[0] < 0 || (count > 1 && !track->frames[0]) ||
			track->frames[count - 1] != animation->frame_count - 1)
			return FALSE;
		for (unsigned int i = 1; i < count; ++i)
			if (track->frames[i - 1] >= track->frames[i])
				return FALSE;
		NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_VALIDATED, count);
	}
	else
	{
		native_animation_track_read(kind, track->default_key, &track->decoded[0]);
		if (kind == NATIVE_ANIMATION_ROTATION)
			quaternion_normalize(&track->decoded[0].rotation);
	}

	track->animation = animation;
	track->kind = kind;
	track->adjusted = adjusted;
	track->node = node;
	track->count = count;
	return TRUE;
}

static struct native_animation_track *native_animation_track_slot(
	const struct animation *animation, short adjusted, short node,
	enum native_animation_track_kind kind)
{
	uint32_t key = ((uintptr_t)animation >> 2) ^ ((uint32_t)(node * 3 + kind) * UINT32_C(0x9e3779b9));
	key ^= (uint32_t)(adjusted + 1) * UINT32_C(0x85ebca6b);
	return native_animation_tracks + ((key ^ (key >> 16)) & (native_animation_track_capacity - 1));
}

static struct native_animation_track *native_animation_track_get(
	const struct animation *animation, short adjusted, short node,
	enum native_animation_track_kind kind)
{
	unsigned int generation = n3ds_engine_tag_generation();
	if (native_animation_track_generation != generation)
	{
		native_animation_track_cache_reset();
		native_animation_track_generation = generation;
	}
	if (native_animation_track_allocation_failed)
		return NULL;

	if (native_animation_tracks)
	{
		struct native_animation_track *track = native_animation_track_slot(animation, adjusted, node, kind);
		if (track->animation == animation && track->node == node && track->adjusted == adjusted && track->kind == kind)
		{
			NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_HITS, 1);
			return track;
		}
	}

	struct native_animation_track prepared = {0};
	if (!native_animation_track_prepare(&prepared, animation, adjusted, node, kind))
		return NULL;
	if (!native_animation_tracks)
	{
		unsigned int capacity = n3ds_input_platform_original_model() ?
			NATIVE_ANIMATION_TRACKS_ORIGINAL : NATIVE_ANIMATION_TRACKS_NEW;
		native_animation_tracks = (calloc)(capacity, sizeof(*native_animation_tracks));
		if (!native_animation_tracks)
		{
			native_animation_track_allocation_failed = TRUE;
			return NULL;
		}
		native_animation_track_capacity = capacity;
		n3ds_algorithm_profile_max(N3DS_ALG_CPU_ANIM_TRACK_PEAK,
			capacity * sizeof(*native_animation_tracks));
	}

	struct native_animation_track *track = native_animation_track_slot(animation, adjusted, node, kind);
	NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_EVICTIONS, track->animation != NULL);
	NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_BUILDS, 1);
	*track = prepared;
	return track;
}

static void native_animation_track_interval(struct native_animation_track *track, real frame)
{
	if (frame >= (real)track->first_frame && frame < (real)track->end_frame)
	{
		NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_INTERVAL_HITS, 1);
		return;
	}

	short previous_end_key = track->end_key;
	byte previous_decoded_mask = track->decoded_mask;
	short frame_index = (short)fast_ftol(floor(frame));
	short last = track->count - 1;
	if (frame_index < track->frames[0])
	{
		track->first_frame = 0;
		track->first_key = NONE;
		track->end_frame = track->frames[0];
		track->end_key = 0;
	}
	else if (frame_index == track->frames[last])
	{
		track->first_frame = track->frames[last];
		track->first_key = last;
		track->end_frame = track->first_frame + 1;
		track->end_key = NONE;
	}
	else
	{
		short key = animation_keyframe_search(track->frames, track->count, frame_index);
		track->first_frame = track->frames[key];
		track->first_key = key;
		track->end_frame = track->frames[key + 1];
		track->end_key = key + 1;
	}
	track->decoded_mask = 0;
	/* Forward playback commonly starts at the previous interval's end key.
	 * Reuse its raw decoded value; exact-frame normalization still happens
	 * only on the output copy in native_animation_track_sample. */
	if ((previous_decoded_mask & FLAG(1)) && track->first_key == previous_end_key)
	{
		track->decoded[0] = track->decoded[1];
		track->decoded_mask = FLAG(0);
	}
}

static const union native_animation_track_value *native_animation_track_endpoint(
	struct native_animation_track *track, unsigned int end)
{
	if (!(track->decoded_mask & FLAG(end)))
	{
		short key = end ? track->end_key : track->first_key;
		const byte *source = key == NONE ? track->default_key :
			track->keys + key * native_animation_track_stride(track->kind);
		native_animation_track_read(track->kind, source, &track->decoded[end]);
		track->decoded_mask |= FLAG(end);
	}
	return &track->decoded[end];
}

static boolean native_animation_track_sample(const struct animation *animation, real frame,
	short adjusted, short node, enum native_animation_track_kind kind,
	union native_animation_track_value *out)
{
	struct native_animation_track *track = NULL;
	if (frame >= 0.f && frame < (real)animation->frame_count &&
		node >= 0 && node < animation->node_count &&
		((adjusted >= 0 && adjusted < animation->node_count) ||
			(adjusted == NONE && kind != NATIVE_ANIMATION_SCALE)))
		track = native_animation_track_get(animation, adjusted, node, kind);
	if (!track)
	{
		NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_BYPASS, 1);
		return FALSE;
	}
	if (!track->count)
	{
		NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_CONSTANT, 1);
		*out = track->decoded[0];
		return TRUE;
	}

	native_animation_track_interval(track, frame);
	const union native_animation_track_value *first = native_animation_track_endpoint(track, 0);
	if (frame == (real)track->first_frame)
	{
		*out = *first;
		if (kind == NATIVE_ANIMATION_ROTATION)
			quaternion_normalize(&out->rotation);
		return TRUE;
	}

	const union native_animation_track_value *end = native_animation_track_endpoint(track, 1);
	real fraction = (frame - (real)track->first_frame) / (track->end_frame - track->first_frame);
	/* Endpoint rotations stay unnormalized through interpolation. Exact
	 * samples normalize the copied key separately above. */
	switch (kind)
	{
		case NATIVE_ANIMATION_ROTATION:
			quaternions_interpolate_and_normalize(&first->rotation, &end->rotation, fraction, &out->rotation);
			break;
		case NATIVE_ANIMATION_TRANSLATION:
			points_interpolate(&first->translation, &end->translation, fraction, &out->translation);
			break;
		default:
			scalars_interpolate(first->scale, end->scale, fraction, &out->scale);
			break;
	}
	return TRUE;
}

#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
static void native_animation_track_verify(const void *reference, const void *candidate, unsigned int size)
{
	if (memcmp(reference, candidate, size))
	{
		vhalt("Prepared compressed animation track differs from the reference sampler");
	}
}
#endif

static void animation_get_keyframe_rotation(const struct animation *animation,
	real frame, short adjusted, short node, real_quaternion *rotation)
{
	union native_animation_track_value value;
	boolean ready = native_animation_track_sample(animation, frame, adjusted, node, NATIVE_ANIMATION_ROTATION, &value);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
	animation_reference_keyframe_rotation(animation, frame, adjusted, node, rotation);
	if (ready) native_animation_track_verify(rotation, &value.rotation, sizeof(*rotation));
#else
	if (ready) *rotation = value.rotation;
	else animation_reference_keyframe_rotation(animation, frame, adjusted, node, rotation);
#endif
}

static void animation_get_keyframe_translation(const struct animation *animation,
	real frame, short adjusted, short node, real_point3d *translation)
{
	union native_animation_track_value value;
	boolean ready = native_animation_track_sample(animation, frame, adjusted, node, NATIVE_ANIMATION_TRANSLATION, &value);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
	animation_reference_keyframe_translation(animation, frame, adjusted, node, translation);
	if (ready) native_animation_track_verify(translation, &value.translation, sizeof(*translation));
#else
	if (ready) *translation = value.translation;
	else animation_reference_keyframe_translation(animation, frame, adjusted, node, translation);
#endif
}

static void animation_get_keyframe_scale(const struct animation *animation,
	real frame, short adjusted, short node, real *scale)
{
	union native_animation_track_value value;
	boolean ready = native_animation_track_sample(animation, frame, adjusted, node, NATIVE_ANIMATION_SCALE, &value);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
	animation_reference_keyframe_scale(animation, frame, adjusted, node, scale);
	if (ready) native_animation_track_verify(scale, &value.scale, sizeof(*scale));
#else
	if (ready) *scale = value.scale;
	else animation_reference_keyframe_scale(animation, frame, adjusted, node, scale);
#endif
}

static void native_animation_default_rotation(const struct animation *animation,
	const byte *data, short node, real_quaternion *rotation)
{
	union native_animation_track_value value;
	/* Keep the legacy default path when the frame-data helper uses a raw
	 * frame stride despite a compressed-only animation. */
	boolean ready = (uintptr_t)data == (uintptr_t)animation->data.address + animation->compressed_data_offset &&
		native_animation_track_sample(animation, 0.f, NONE, node, NATIVE_ANIMATION_ROTATION, &value);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_CANDIDATE
	if (ready)
	{
		*rotation = value.rotation;
		return;
	}
#endif
	const struct compressed_animation_header *header = (const void *)data;
	const struct compressed_quaternion_6byte *defaults = (const void *)(data + header->default_rotations_offset);
	quaternion_decompress_6byte(defaults + node, rotation);
	quaternion_normalize(rotation);
	NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_ENDPOINTS, 1);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
	if (ready) native_animation_track_verify(rotation, &value.rotation, sizeof(*rotation));
#endif
}

static void native_animation_default_translation(const struct animation *animation,
	const byte *data, short node, real_point3d *translation)
{
	union native_animation_track_value value;
	boolean ready = (uintptr_t)data == (uintptr_t)animation->data.address + animation->compressed_data_offset &&
		native_animation_track_sample(animation, 0.f, NONE, node, NATIVE_ANIMATION_TRANSLATION, &value);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_CANDIDATE
	if (ready)
	{
		*translation = value.translation;
		return;
	}
#endif
	const struct compressed_animation_header *header = (const void *)data;
	*translation = animation_read_point(data + header->default_translations_offset + node * sizeof(real_point3d));
	NATIVE_CPU_ADD(N3DS_ALG_CPU_ANIM_TRACK_ENDPOINTS, 1);
#if HALO_N3DS_ALGO_COMPRESSED_TRACKS == N3DS_ALGORITHM_VERIFY
	if (ready) native_animation_track_verify(translation, &value.translation, sizeof(*translation));
#endif
}
