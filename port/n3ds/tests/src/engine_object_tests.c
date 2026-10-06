#include "cseries.h"
#include "real_math.h"
#include "matrix_math.h"
#include "memory_pool.h"
#include "game_state.h"
#include "data.h"
#include "object_lights.h"
#include "cluster_partitions.h"

void n3ds_log(const char *message);
extern struct data_array *light_data;
extern struct cluster_partition light_cluster_partition;

static int near_point(const real_point3d *a, const real_point3d *b)
{
    return fabsf(a->x-b->x) < 0.0001f && fabsf(a->y-b->y) < 0.0001f && fabsf(a->z-b->z) < 0.0001f;
}

int halo_engine_object_tests(void)
{
    real_matrix4x3 a = {0}, b = {0}, product, alias;
    real_point3d point = {{2.f, 3.f, 5.f}}, middle, expected, actual, restored;
    struct memory_pool *pool;
    byte *first = NULL, *second = NULL, *third = NULL, *old_second;
    long i, free_size;
#define CHECK(expr) do { if (!(expr)) { n3ds_log("OBJECT CORE FAIL: " #expr); return 1; } } while (0)
    /* Noncommuting rotations, translations and scales exercise the ARM path,
     * including the aliasing used by hierarchy/node transformation code. */
    a.scale = 2.f;
    a.forward.j = 1.f; a.left.i = -1.f; a.up.k = 1.f;
    a.position.x = 11.f; a.position.y = -7.f; a.position.z = 3.f;
    b.scale = 0.5f;
    b.forward.i = 1.f; b.left.k = 1.f; b.up.j = -1.f;
    b.position.x = -4.f; b.position.y = 2.f; b.position.z = 8.f;
    matrix4x3_transform_point(&b, &point, &middle);
    matrix4x3_transform_point(&a, &middle, &expected);
    matrix4x3_multiply(&a, &b, &product);
    matrix4x3_transform_point(&product, &point, &actual);
    CHECK(near_point(&expected, &actual) && product.scale == 1.f);
    matrix4x3_inverse_transform_point(&product, &actual, &restored);
    CHECK(near_point(&point, &restored));
    alias = a;
    matrix4x3_multiply(&alias, &b, &alias);
    matrix4x3_transform_point(&alias, &point, &actual);
    CHECK(near_point(&expected, &actual));
    alias = b;
    matrix4x3_multiply(&a, &alias, &alias);
    matrix4x3_transform_point(&alias, &point, &actual);
    CHECK(near_point(&expected, &actual));
    n3ds_log("PASS: original ARM object transforms, inverse and input aliasing");

    pool = game_state_memory_pool_new("object allocator verification", 4096);
    free_size = memory_pool_get_free_size(pool);
    CHECK(memory_pool_block_allocate(pool, (void **)&first, 128));
    CHECK(memory_pool_block_allocate(pool, (void **)&second, 256));
    CHECK(memory_pool_block_allocate(pool, (void **)&third, 128));
    for (i = 0; i < 256; ++i) second[i] = (byte)(i ^ 0xa5);
    memset(third, 0x5a, 128);
    old_second = second;
    memory_pool_block_free(pool, (void **)&first);
    memory_pool_compact(pool);
    CHECK(second != old_second);
    for (i = 0; i < 256; ++i) CHECK(second[i] == (byte)(i ^ 0xa5));
    for (i = 0; i < 128; ++i) CHECK(third[i] == 0x5a);
    CHECK(memory_pool_block_reallocate(pool, (void **)&second, 512));
    for (i = 0; i < 256; ++i) CHECK(second[i] == (byte)(i ^ 0xa5));
    CHECK(!memory_pool_block_allocate(pool, (void **)&first, 8192));
    for (i = 0; i < 128; ++i) CHECK(third[i] == 0x5a);
    memory_pool_block_free(pool, (void **)&second);
    memory_pool_block_free(pool, (void **)&third);
    CHECK(memory_pool_get_free_size(pool) == free_size && !pool->first_block && !pool->last_block);
    n3ds_log("PASS: original object allocator compacts/grows without losing references or data");

    lights_initialize();
    lights_initialize_for_new_map();
    CHECK(light_data && light_data->valid && light_data->actual_count == 0);
    CHECK(light_cluster_partition.data_reference_data->valid && light_cluster_partition.cluster_reference_data->valid);
    CHECK(!lights_enable(FALSE) && lights_enable(TRUE));
    lights_dispose_from_old_map();
    CHECK(!light_data->valid && !light_cluster_partition.data_reference_data->valid);
    lights_dispose();
    CHECK(!light_cluster_partition.cluster_first_data_references && !light_cluster_partition.data_reference_data && !light_cluster_partition.cluster_reference_data);
    n3ds_log("PASS: original object-light and cluster storage lifecycle");
    return 0;
#undef CHECK
}
