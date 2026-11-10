/* New3DS integration additions by Kevin Boggs.
 * Reuse complete stationary, unanimated model poses. Physics, damage, script
 * functions, attachments and object clocks still update on every tick. */
struct native_stationary_pose {
    unsigned int generation;
    long object_index, definition_index;
    const void *nodes;
    real_point3d position;
    real_vector3d forward, up;
    real scale;
    int mirrored, valid;
};
static struct native_stationary_pose native_stationary_poses[128];
static unsigned int native_stationary_hits, native_stationary_misses;

/* Kevin Boggs: native_stationary_pose_reuse */
static boolean native_stationary_pose_reuse(long index, const struct object_datum *object,
    const struct object_definition *definition, const void *nodes)
{
    extern unsigned int n3ds_engine_cache_generation(void);
    struct native_stationary_pose *cache = native_stationary_poses + ((unsigned int)index & 127u);
    if (object->object.parent_object_index != NONE ||
        definition->object.animation_graph.index != NONE ||
        object->object.animation.interpolation_frame_count ||
        (object->object.type != _object_type_scenery &&
         !(TEST_FLAG(_object_mask_item, object->object.type) && TEST_FLAG(object->object.flags, _object_at_rest_bit))))
    { cache->valid = 0; return FALSE; }
    unsigned int generation = n3ds_engine_cache_generation();
    int mirrored = TEST_FLAG(object->object.flags, _object_mirrored_bit);
    if (cache->valid && cache->generation == generation && cache->object_index == index &&
        cache->definition_index == object->definition_index && cache->nodes == nodes &&
        cache->mirrored == mirrored && cache->scale == object->object.scale &&
        !memcmp(&cache->position, &object->object.position, sizeof(cache->position)) &&
        !memcmp(&cache->forward, &object->object.forward, sizeof(cache->forward)) &&
        !memcmp(&cache->up, &object->object.up, sizeof(cache->up))) {
        ++native_stationary_hits;
        return TRUE;
    }
    cache->valid = 1; cache->generation = generation; cache->object_index = index;
    cache->definition_index = object->definition_index; cache->nodes = nodes;
    cache->position = object->object.position; cache->forward = object->object.forward;
    cache->up = object->object.up; cache->scale = object->object.scale; cache->mirrored = mirrored;
    ++native_stationary_misses;
    return FALSE;
}

/* Kevin Boggs: n3ds_stationary_pose_clear */
void n3ds_stationary_pose_clear(void)
{
    memset(native_stationary_poses, 0, sizeof(native_stationary_poses));
    native_stationary_hits = native_stationary_misses = 0;
}

/* Kevin Boggs: n3ds_stationary_pose_report */
void n3ds_stationary_pose_report(void)
{
    extern void n3ds_log(const char *);
    char message[128];
    snprintf(message, sizeof(message), "STATIONARY POSE REUSE: hits=%u misses=%u", native_stationary_hits, native_stationary_misses);
    n3ds_log(message); native_stationary_hits = native_stationary_misses = 0;
}
