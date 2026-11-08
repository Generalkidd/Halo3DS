/* Included by render_objects.c. History and draw scratch stay on the render
 * owner; gameplay and marker consumers always see the authoritative nodes. */
#include "engine_cache.h"
#include "engine_algorithm_profile.h"
#include "engine_input.h"
#include "engine_presentation.h"
#include "game/game.h"
#include "models/model_definitions.h"

enum
{
	NATIVE_POSE_OBJECTS = 128,
	NATIVE_POSE_NODES = 512,
	NATIVE_POSE_TYPES = _object_mask_unit | _object_mask_item |
		_object_mask_scenery | _object_mask_device,
};

/* A movement of more than half a world unit in one tick is a cut for this
 * history. Use root space for the same limit on animated nodes. */
#define NATIVE_POSE_MAX_STEP_SQUARED 0.25f

struct native_object_pose
{
	long object_index, definition_index, model_index, animation_graph_index;
	long leaf_index;
	short cluster_index, animation_index, shader_permutation;
	unsigned short node_offset, node_count;
	byte regions[MAXIMUM_REGIONS_PER_OBJECT];
	real_point3d center;
	real radius;
};

struct native_pose_snapshot
{
	unsigned int tick, generation;
	unsigned short object_slots[MAXIMUM_OBJECTS_PER_MAP];
	struct native_object_pose objects[NATIVE_POSE_OBJECTS];
	real_matrix4x3 nodes[NATIVE_POSE_NODES];
};

static struct native_pose_history
{
	struct native_pose_snapshot snapshots[2];
	real_matrix4x3 draw_nodes[MAXIMUM_NODES_PER_MODEL];
} *native_pose_history;
#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS != N3DS_ALGORITHM_REFERENCE
static struct native_pose_rotations
{
	real_quaternion values[NATIVE_POSE_NODES];
	unsigned int valid[NATIVE_POSE_NODES / 32];
} *native_pose_rotations;
#endif
static boolean native_pose_allocation_failed;
static unsigned int native_pose_latest, native_pose_samples;

#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
static struct native_pose_draw_snapshots
{
	unsigned int tick, generation;
	unsigned int valid[NATIVE_POSE_OBJECTS / 32];
	real_matrix4x3 nodes[NATIVE_POSE_NODES];
} *native_pose_draw_snapshots;
static boolean native_pose_draw_allocation_failed;

static void native_pose_draw_snapshots_reset(void)
{
	if (native_pose_draw_snapshots)
		memset(native_pose_draw_snapshots->valid, 0, sizeof(native_pose_draw_snapshots->valid));
}
#endif

void n3ds_presentation_poses_reset(void)
{
	native_pose_samples = 0;
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
	native_pose_draw_snapshots_reset();
#endif
}

static boolean native_pose_eligible(
	struct _object_datum const *object,
	struct object_definition const *definition)
{
	return (object->flags & (FLAG(_object_invisible_bit) | FLAG(_object_mirrored_bit) |
		FLAG(_object_outside_of_map_bit) | FLAG(_object_has_attached_lights_bit) |
		FLAG(_object_has_attached_shader_bit))) == 0 &&
		TEST_FLAG(object->flags, _object_connected_to_map_bit) &&
		object->location.leaf_index != NONE && object->location.cluster_index != NONE &&
		object->parent_object_index == NONE && object->first_child_object_index == NONE &&
		object->first_widget_index == NONE && definition->object.attachments.count == 0 &&
		definition->object.model.index != NONE && definition->object.modifier_shader.index == NONE &&
		!TEST_FLAG(definition->object.flags, _object_transparency_self_occludes_bit);
}

void n3ds_presentation_poses_capture(void)
{
	if (n3ds_input_platform_original_model() || !game_in_progress())
	{
		n3ds_presentation_poses_reset();
		return;
	}

	if (!native_pose_history)
	{
		if (native_pose_allocation_failed)
			return;
		native_pose_history = (malloc)(sizeof(*native_pose_history));
		if (!native_pose_history)
		{
			native_pose_allocation_failed = TRUE;
			return;
		}
#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS != N3DS_ALGORITHM_REFERENCE
		/* Optional storage cannot prevent the base pose history from working. */
		native_pose_rotations = (malloc)(2 * sizeof(*native_pose_rotations));
		if (!native_pose_rotations)
			n3ds_algorithm_profile_add(N3DS_ALG_CPU_MIDPOINT_QUAT_PRESSURE,1);
#endif
	}

	unsigned int generation = n3ds_engine_cache_generation();
	unsigned int tick = (unsigned int)game_time_get();
	struct native_pose_snapshot const *latest = &native_pose_history->snapshots[native_pose_latest];
	if (native_pose_samples && latest->generation == generation && latest->tick == tick)
		return;

	native_pose_samples = native_pose_samples && latest->generation == generation &&
		tick - latest->tick == 1u ? 2u : 1u;
	native_pose_latest ^= 1u;
	struct native_pose_snapshot *snapshot = &native_pose_history->snapshots[native_pose_latest];
	snapshot->generation = generation;
	snapshot->tick = tick;
	memset(snapshot->object_slots, 0, sizeof(snapshot->object_slots));
#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS != N3DS_ALGORITHM_REFERENCE
	if (native_pose_rotations)
		memset(native_pose_rotations[native_pose_latest].valid, 0, sizeof(native_pose_rotations[0].valid));
#endif

	unsigned int objects = 0, nodes = 0;
	struct object_iterator iterator;
	struct object_datum *datum;
	object_iterator_new(&iterator, NATIVE_POSE_TYPES, FLAG(_object_header_active_bit));
	while (objects < NATIVE_POSE_OBJECTS && nodes < NATIVE_POSE_NODES &&
		(datum = object_iterator_next(&iterator)))
	{
		struct _object_datum *object = &datum->object;
		if (object->animation.animation_graph_index == NONE &&
			(object->type == _object_type_scenery || TEST_FLAG(object->flags, _object_at_rest_bit)))
			continue;

		struct object_definition const *definition = object_definition_get(datum->definition_index);
		if (!native_pose_eligible(object, definition) ||
			(TEST_FLAG(_object_mask_unit, object->type) && unit_get(iterator.index)->unit.active_camouflage > 0.f))
			continue;

		long count = model_definition_get(definition->object.model.index)->nodes.count;
		struct object_header_datum const *header = object_header_get(iterator.index);
		struct object_header_block_reference const *block = &object->node_matrices;
		if (count <= 0 || count > MAXIMUM_NODES_PER_MODEL || (unsigned int)count > NATIVE_POSE_NODES - nodes ||
			(header->flags & (FLAG(_object_header_being_created_bit) | FLAG(_object_header_being_deleted_bit))) ||
			block->size != count * sizeof(real_matrix4x3) || block->offset <= 0 ||
			block->offset + block->size > header->data_size)
			continue;

		struct native_object_pose *pose = &snapshot->objects[objects];
		*pose = (struct native_object_pose){
			.object_index = iterator.index,
			.definition_index = datum->definition_index,
			.model_index = definition->object.model.index,
			.animation_graph_index = object->animation.animation_graph_index,
			.leaf_index = object->location.leaf_index,
			.cluster_index = object->location.cluster_index,
			.animation_index = object->animation.state.index,
			.shader_permutation = object->forced_shader_permutation_index,
			.node_offset = nodes,
			.node_count = count,
			.center = object->bounding_sphere_center,
			.radius = object->bounding_sphere_radius,
		};
		memcpy(pose->regions, object->region_permutations, sizeof(pose->regions));
		memcpy(&snapshot->nodes[nodes], (char const *)datum + block->offset, block->size);
		snapshot->object_slots[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index)] = ++objects;
		nodes += count;
	}
}

static struct native_object_pose const *native_pose_find(
	struct native_pose_snapshot const *snapshot, long object_index)
{
	unsigned int absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP || !snapshot->object_slots[absolute_index])
		return NULL;
	struct native_object_pose const *pose = &snapshot->objects[snapshot->object_slots[absolute_index] - 1];
	return pose->object_index == object_index ? pose : NULL;
}

#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
static void native_pose_draw_snapshot(long object_index, real_matrix4x3 const *nodes)
{
	struct native_pose_snapshot const *latest = &native_pose_history->snapshots[native_pose_latest];
	struct native_object_pose const *pose = native_pose_find(latest, object_index);
	if (!pose || native_pose_draw_allocation_failed)
		return;

	if (!native_pose_draw_snapshots)
	{
		native_pose_draw_snapshots = (calloc)(1, sizeof(*native_pose_draw_snapshots));
		if (!native_pose_draw_snapshots)
		{
			native_pose_draw_allocation_failed = TRUE;
			n3ds_algorithm_profile_add(N3DS_ALG_CPU_PARTICLE_PRESSURE, 1);
			return;
		}
	}

	/* The latest endpoint bank owns these disjoint offsets. It cannot rotate
	 * between accepted world draws and their synchronous particle pass. */
	native_pose_draw_snapshots->tick = latest->tick;
	native_pose_draw_snapshots->generation = latest->generation;
	unsigned int slot = pose - latest->objects;
	memcpy(&native_pose_draw_snapshots->nodes[pose->node_offset], nodes, pose->node_count * sizeof(*nodes));
	native_pose_draw_snapshots->valid[slot / 32] |= 1u << (slot % 32);
}

real_matrix4x3 const *n3ds_presentation_particle_parent_node(long object_index, short node_index)
{
	if (!native_pose_draw_snapshots || native_pose_samples != 2 ||
		!n3ds_presentation_midpoint_active() || n3ds_presentation_midpoint_rejected())
		return NULL;

	struct native_pose_snapshot const *latest = &native_pose_history->snapshots[native_pose_latest];
	if (native_pose_draw_snapshots->tick != latest->tick ||
		native_pose_draw_snapshots->generation != latest->generation ||
		latest->generation != n3ds_engine_cache_generation())
		return NULL;

	struct native_object_pose const *pose = native_pose_find(latest, object_index);
	if (!pose || (unsigned short)node_index >= pose->node_count)
		return NULL;

	unsigned int slot = pose - latest->objects;
	if (!(native_pose_draw_snapshots->valid[slot / 32] & (1u << (slot % 32))))
		return NULL;
	return &native_pose_draw_snapshots->nodes[pose->node_offset + node_index];
}
#endif

static struct native_object_pose const *native_pose_pair(
	long object_index, struct native_object_pose const **previous)
{
	if (!n3ds_presentation_midpoint_active() || native_pose_samples != 2)
		return NULL;

	struct native_pose_snapshot const *latest = &native_pose_history->snapshots[native_pose_latest];
	if (latest->generation != n3ds_engine_cache_generation() ||
		latest->tick != (unsigned int)game_time_get())
		return NULL;

	struct native_object_pose const *a = native_pose_find(
		&native_pose_history->snapshots[native_pose_latest ^ 1u], object_index);
	struct native_object_pose const *b = native_pose_find(latest, object_index);
	/* Keep the canonical cluster registration. A different BSP leaf, changed
	 * bounds, or large motion needs the authoritative draw for this interval. */
	if (!a || !b || a->definition_index != b->definition_index || a->model_index != b->model_index ||
		a->node_count != b->node_count || a->leaf_index != b->leaf_index || a->cluster_index != b->cluster_index ||
		a->animation_graph_index != b->animation_graph_index || a->animation_index != b->animation_index ||
		a->shader_permutation != b->shader_permutation || memcmp(a->regions, b->regions, sizeof(b->regions)) ||
		a->radius != b->radius ||
		!(distance_squared3d(&a->center, &b->center) <= NATIVE_POSE_MAX_STEP_SQUARED))
		return NULL;

	*previous = a;
	return b;
}

static void native_pose_render_bounding_sphere(long object_index, real_point3d *center, real *radius)
{
	object_get_render_bounding_sphere(object_index, center, radius);
	struct native_object_pose const *previous;
	struct native_object_pose const *current = native_pose_pair(object_index, &previous);
	if (current && distance_squared3d(center, &current->center) == 0.f)
		*radius += 0.5f * distance3d(center, &previous->center);
}

static boolean native_pose_valid_rotation(real_matrix4x3 const *matrix)
{
	real_vector3d up;
	cross_product3d(&matrix->forward, &matrix->left, &up);
	return matrix->scale > 0.0001f && valid_real_matrix4x3(matrix) && dot_product3d(&up, &matrix->up) > 0.f;
}

#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS != N3DS_ALGORITHM_REFERENCE
static void native_pose_rotation(unsigned int bank, unsigned int node, real_quaternion *out)
{
	struct native_pose_snapshot *snapshot=&native_pose_history->snapshots[bank];
	if (!native_pose_rotations)
	{
		matrix4x3_rotation_to_quaternion(&snapshot->nodes[node],out);
		return;
	}
	struct native_pose_rotations *rotations=&native_pose_rotations[bank];
	unsigned int *valid=&rotations->valid[node/32];
	unsigned int bit=1u<<(node%32);
	if (*valid & bit)
	{
		n3ds_algorithm_profile_add(N3DS_ALG_CPU_MIDPOINT_QUAT_REUSE,1);
#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS == N3DS_ALGORITHM_VERIFY
		real_quaternion expected;
		matrix4x3_rotation_to_quaternion(&snapshot->nodes[node],&expected);
		assert(!memcmp(&expected,&rotations->values[node],sizeof(expected)));
#endif
	}
	else
	{
		matrix4x3_rotation_to_quaternion(&snapshot->nodes[node],&rotations->values[node]);
		*valid|=bit;
		n3ds_algorithm_profile_add(N3DS_ALG_CPU_MIDPOINT_QUAT_BUILD,1);
	}
	*out=rotations->values[node];
}
#endif

static real_matrix4x3 const *native_pose_for_draw(
	long object_index, struct object_datum const *datum, struct object_definition const *definition,
	real_matrix4x3 const *live, real_point3d *center)
{
	struct native_object_pose const *previous;
	struct native_object_pose const *current = native_pose_pair(object_index, &previous);
	struct _object_datum const *object = &datum->object;
	if (!current || !live || !native_pose_eligible(object, definition) ||
		datum->definition_index != current->definition_index || definition->object.model.index != current->model_index ||
		object->node_matrices.size != current->node_count * sizeof(*live) ||
		object->location.leaf_index != current->leaf_index || object->location.cluster_index != current->cluster_index ||
		object->animation.animation_graph_index != current->animation_graph_index ||
		object->animation.state.index != current->animation_index ||
		object->forced_shader_permutation_index != current->shader_permutation ||
		memcmp(object->region_permutations, current->regions, sizeof(current->regions)) ||
		object->bounding_sphere_radius != current->radius || distance_squared3d(center, &current->center) != 0.f)
		return live;

	real_matrix4x3 const *a = &native_pose_history->snapshots[native_pose_latest ^ 1u].nodes[previous->node_offset];
	real_matrix4x3 const *b = &native_pose_history->snapshots[native_pose_latest].nodes[current->node_offset];
	unsigned int bytes = current->node_count * sizeof(*live);
	if (memcmp(b, live, bytes) || !memcmp(a, b, bytes) ||
		!native_pose_valid_rotation(a) || !native_pose_valid_rotation(b) ||
		!(distance_squared3d(&a->position, &b->position) <= NATIVE_POSE_MAX_STEP_SQUARED) ||
		dot_product3d(&a->forward, &b->forward) < 0.7f || dot_product3d(&a->up, &b->up) < 0.7f)
		return live;

	for (unsigned int i = 0; i < current->node_count; ++i)
	{
		real_matrix4x3 *node = &native_pose_history->draw_nodes[i];
		if (!memcmp(&a[i], &b[i], sizeof(*node)))
		{
			*node = b[i];
			continue;
		}

		if (i)
		{
			real_point3d local_a, local_b;
			if (!native_pose_valid_rotation(&a[i]) || !native_pose_valid_rotation(&b[i]))
				return live;
			matrix4x3_inverse_transform_point(a, &a[i].position, &local_a);
			matrix4x3_inverse_transform_point(b, &b[i].position, &local_b);
			if (!(distance_squared3d(&local_a, &local_b) <= NATIVE_POSE_MAX_STEP_SQUARED))
				return live;
		}

		real_quaternion rotation_a, rotation_b, midpoint;
#if HALO_N3DS_ALGO_MIDPOINT_QUATERNIONS != N3DS_ALGORITHM_REFERENCE
		native_pose_rotation(native_pose_latest^1u,previous->node_offset+i,&rotation_a);
		native_pose_rotation(native_pose_latest,current->node_offset+i,&rotation_b);
#else
		matrix4x3_rotation_to_quaternion(&a[i], &rotation_a);
		matrix4x3_rotation_to_quaternion(&b[i], &rotation_b);
#endif
		/* Matrix conversion normalizes the interpolated rotation. */
		quaternions_interpolate(&rotation_a, &rotation_b, 0.5f, &midpoint);
		matrix4x3_rotation_from_quaternion(node, &midpoint);
		node->scale = 0.5f * a[i].scale + 0.5f * b[i].scale;
		points_interpolate(&a[i].position, &b[i].position, 0.5f, &node->position);
	}

	points_interpolate(&previous->center, &current->center, 0.5f, center);
	/* render_model consumes the scratch synchronously; native deferred packets
	 * copy their own node matrices before the next object can reuse it. */
	return native_pose_history->draw_nodes;
}
