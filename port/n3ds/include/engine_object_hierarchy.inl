/* Owner-thread scratch. A slot holds complete inputs and outputs from one
 * child multiplication before object-type matrix postprocessing. */
struct native_hierarchy_node
{
	long object_index, model_index;
	short node_index;
	boolean valid;
	real_orientation orientation;
	real_matrix4x3 local, parent, world;
};

_Static_assert(sizeof(real_orientation) == 32 && sizeof(real_matrix4x3) == 52,
	"Hierarchy comparisons cover only initialized float components");
_Static_assert(sizeof(struct native_hierarchy_node) == 200, "Bounded hierarchy slot size");

enum { NATIVE_HIERARCHY_ORIGINAL = 128, NATIVE_HIERARCHY_NEW = 512 };
static struct native_hierarchy_node *native_hierarchy;
static unsigned int native_hierarchy_capacity;
static boolean native_hierarchy_allocation_failed;

static void native_hierarchy_reset(void)
{
	(free)(native_hierarchy);
	native_hierarchy = NULL;
	native_hierarchy_capacity = 0;
	native_hierarchy_allocation_failed = FALSE;
}

static boolean native_hierarchy_begin(void)
{
	/* Reuse only under the game owner's scalar, nearest, DN/FZ mode. */
	if ((__builtin_arm_get_fpscr() & 0x03ffff00u) != 0x03000000u)
		return FALSE;
	if (!native_hierarchy && !native_hierarchy_allocation_failed)
	{
		extern int n3ds_input_platform_original_model(void);
		unsigned int capacity = n3ds_input_platform_original_model() ?
			NATIVE_HIERARCHY_ORIGINAL : NATIVE_HIERARCHY_NEW;
		native_hierarchy = (calloc)(capacity, sizeof(*native_hierarchy));
		if (!native_hierarchy)
			native_hierarchy_allocation_failed = TRUE;
		else
		{
			native_hierarchy_capacity = capacity;
			n3ds_algorithm_profile_max(N3DS_ALG_CPU_OBJECT_HIERARCHY_PEAK,
				capacity * sizeof(*native_hierarchy));
		}
	}
	return native_hierarchy != NULL;
}

static boolean native_hierarchy_child(long object_index, long model_index, short node_index,
	const real_orientation *orientation, const real_matrix4x3 *parent, real_matrix4x3 *result)
{
	if (parent == result)
		return FALSE;

	uint32_t key = (uint32_t)object_index * UINT32_C(0x9e3779b9) ^
		(uint32_t)node_index * UINT32_C(0x85ebca6b);
	struct native_hierarchy_node *slot = native_hierarchy +
		((key ^ (key >> 16)) & (native_hierarchy_capacity - 1));
	boolean same_node = slot->valid && slot->object_index == object_index &&
		slot->model_index == model_index && slot->node_index == node_index;
	boolean same_local = same_node && !memcmp(&slot->orientation, orientation, sizeof(*orientation));
	if (same_local)
	{
		n3ds_algorithm_profile_add(N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_HITS, 1);
#if HALO_N3DS_ALGO_LOCAL_HIERARCHY == N3DS_ALGORITHM_VERIFY
		real_matrix4x3 reference;
		matrix4x3_from_orientation(&reference, orientation);
		if (memcmp(&slot->local, &reference, sizeof(reference)))
		{
			vhalt("Cached object local hierarchy matrix differs");
		}
#endif
		if (!memcmp(&slot->parent, parent, sizeof(*parent)))
		{
#if HALO_N3DS_ALGO_LOCAL_HIERARCHY == N3DS_ALGORITHM_VERIFY
			matrix4x3_multiply(parent, &reference, &reference);
			if (memcmp(&slot->world, &reference, sizeof(reference)))
			{
				vhalt("Cached object world hierarchy matrix differs");
			}
#endif
			*result = slot->world;
			n3ds_algorithm_profile_add(N3DS_ALG_CPU_OBJECT_HIERARCHY_WORLD_HITS, 1);
			return TRUE;
		}
		*result = slot->local;
	}
	else
	{
		matrix4x3_from_orientation(result, orientation);
		n3ds_algorithm_profile_add(N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_BUILDS, 1);
		n3ds_algorithm_profile_add(N3DS_ALG_CPU_OBJECT_HIERARCHY_EVICTIONS, slot->valid && !same_node);
		slot->object_index = object_index;
		slot->model_index = model_index;
		slot->node_index = node_index;
		slot->orientation = *orientation;
		slot->local = *result;
		slot->valid = TRUE;
	}

	/* Keep the reference's in-place multiplication. Copy the exact parent
	 * consumed now, independently of its later in-place postprocessing. */
	matrix4x3_multiply(parent, result, result);
	slot->parent = *parent;
	slot->world = *result;
	n3ds_algorithm_profile_add(N3DS_ALG_CPU_OBJECT_HIERARCHY_WORLD_BUILDS, 1);
	return TRUE;
}
