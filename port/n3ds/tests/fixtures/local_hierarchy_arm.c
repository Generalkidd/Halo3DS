/* Exercise actual VFP local/world math and the owner cache on synthetic nodes. */
#include "cseries.h"
#include "math/matrix_math.h"
#include "engine_algorithm_profile.h"
#include "engine_object_hierarchy.inl"

#undef memset
#undef memcmp

static union { uint64_t alignment; byte bytes[NATIVE_HIERARCHY_NEW * 200]; } arena;
static unsigned long long counters[N3DS_ALGORITHM_METRIC_COUNT];
static volatile int allocated;
static int allocate_ok = 1;

int n3ds_input_platform_original_model(void) { return 0; }
void *calloc(size_t count, size_t size)
{
	if (!allocate_ok) return NULL;
	if (allocated || count * size > sizeof(arena.bytes)) __builtin_trap();
	allocated = 1;
	memset(arena.bytes, 0, count * size);
	return arena.bytes;
}

void __wrap_free(void *pointer)
{
	if (pointer && (pointer != arena.bytes || !allocated)) __builtin_trap();
	allocated = 0;
}

void n3ds_algorithm_profile_add(enum n3ds_algorithm_metric metric, unsigned long long value)
{
	counters[metric] += value;
}

void n3ds_algorithm_profile_max(enum n3ds_algorithm_metric metric, unsigned long long value)
{
	if (value > counters[metric]) counters[metric] = value;
}

long csmemcmp(const void *left, const void *right, unsigned long size) { return memcmp(left, right, size); }
void display_assert(char *information, char *file, long line, boolean fatal) { __builtin_trap(); }
void system_exit(long code) { __builtin_trap(); }
void halt_and_catch_fire(void) { __builtin_trap(); }

#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)

static boolean exact_child(long object, long model, const real_orientation *orientation,
	const real_matrix4x3 *parent, real_matrix4x3 *actual)
{
	real_matrix4x3 expected;
	matrix4x3_from_orientation(&expected, orientation);
	matrix4x3_multiply(parent, &expected, &expected);
	return native_hierarchy_child(object, model, 1, orientation, parent, actual) &&
		!memcmp(&expected, actual, sizeof(expected));
}

int local_hierarchy_fixture(void)
{
	real_orientation orientation = {.rotation = {.v = {0.2f, -.75f, .3f}, .w = .8f},
		.translation = {0.f, -1.25f, 2.5f}, .scale = 1.25f};
	real_matrix4x3 parent = {.scale = 2.5f, .forward = {1.f, 0.f, 0.f}, .left = {0.f, 1.f, 0.f},
		.up = {0.f, 0.f, 1.f}, .position = {-.25f, 4.f, -7.f}};
	real_matrix4x3 out;

	__builtin_arm_set_fpscr(0);
	CHECK(!native_hierarchy_begin() && !allocated);
	__builtin_arm_set_fpscr(0x03000000u);
	CHECK(native_hierarchy_begin() && allocated);
	CHECK(exact_child(0x12340002, 5, &orientation, &parent, &out));
	/* Simulate output memory changed by postprocessing. */
	out.position.x = -15.f;
	CHECK(exact_child(0x12340002, 5, &orientation, &parent, &out));
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_HITS] == 1 &&
		counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_WORLD_HITS] == 1);

	parent.position.x = 512.f;
	CHECK(exact_child(0x12340002, 5, &orientation, &parent, &out));
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_HITS] == 2 &&
		counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_WORLD_HITS] == 1);
	orientation.translation.x = -0.f;
	CHECK(exact_child(0x12340002, 5, &orientation, &parent, &out));
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_BUILDS] == 2 &&
		counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_WORLD_BUILDS] == 3);

	CHECK(exact_child(0x12350002, 5, &orientation, &parent, &out));
	CHECK(exact_child(0x12350002, 7, &orientation, &parent, &out));
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_LOCAL_BUILDS] == 4);
	CHECK(!native_hierarchy_child(0x12350002, 7, 1, &orientation, &out, &out));
	for (unsigned int i = 0; i < NATIVE_HIERARCHY_NEW + 60; ++i)
		CHECK(exact_child(0x12350000 + i, 5, &orientation, &parent, &out));
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_EVICTIONS] > 0);
	CHECK(counters[N3DS_ALG_CPU_OBJECT_HIERARCHY_PEAK] == sizeof(arena.bytes));

	native_hierarchy_reset();
	CHECK(!allocated);
	allocate_ok = 0;
	CHECK(!native_hierarchy_begin() && !allocated);
	allocate_ok = 1;
	CHECK(!native_hierarchy_begin() && !allocated);
	native_hierarchy_reset();
	CHECK(native_hierarchy_begin() && allocated);
	CHECK(exact_child(0x12350002, 5, &orientation, &parent, &out));
	native_hierarchy_reset();
	CHECK(!allocated);
	return 0;
}
