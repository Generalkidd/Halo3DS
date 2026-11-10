/* New3DS integration additions by Kevin Boggs.
 * Cache a pruned BSP traversal, not collision outcomes. Within the enclosing
 * sphere, omitted branches cannot be reached. All remaining plane, surface,
 * breakable and moving-object tests retain the original order and precision.
 * Inspired by Halo64's spatial reuse; this implementation retains exact queries.
 * Collision queries run on the simulation thread. No heap allocation per query. */
#define QUERY_CACHE_SLOTS 16
#define QUERY_CACHE_NODES 96
struct native_query_node { long original, children[2]; };
struct native_query_tree {
    const struct collision_bsp *bsp;
    real_point3d center;
    real radius;
    unsigned int stamp;
    long root, count, skipped;
    struct native_query_node nodes[QUERY_CACHE_NODES];
};
static struct native_query_tree native_query_trees[QUERY_CACHE_SLOTS];
static unsigned int native_query_clock, native_query_hits, native_query_misses;
static unsigned int native_query_miss_run, native_query_cooldown, native_query_bypassed;
static unsigned int native_feature_hash[1024], native_feature_stamp;

/* Kevin Boggs: n3ds_collision_query_clear */
void n3ds_collision_query_clear(void)
{
    memset(native_query_trees, 0, sizeof(native_query_trees));
    native_query_clock = native_query_hits = native_query_misses = 0;
    native_query_miss_run = native_query_cooldown = native_query_bypassed = 0;
}

/* Kevin Boggs: native_feature_begin */
static void native_feature_begin(void)
{
    native_feature_stamp += 0x10000u;
    if (!native_feature_stamp) {
        memset(native_feature_hash, 0, sizeof(native_feature_hash));
        native_feature_stamp = 0x10000u;
    }
}

/* Kevin Boggs: native_feature_insert */
static void native_feature_insert(long *count, long *indices, long index)
{
    unsigned int slot = (((unsigned int)index ^ ((unsigned int)indices >> 4)) * 2654435761u) >> 22;
    for (unsigned int probe = 0; probe < 1024; ++probe) {
        unsigned int entry = native_feature_hash[slot];
        unsigned int position = (entry & 0xffffu) - 1u;
        if ((entry & 0xffff0000u) != native_feature_stamp) {
            if (*count < 256) {
                indices[*count] = index;
                native_feature_hash[slot] = native_feature_stamp | (unsigned int)++*count;
            }
            return;
        }
        if (position < (unsigned int)*count && indices[position] == index) return;
        slot = (slot + 1) & 1023;
    }
    /* Defensive fallback; the three lists together contain at most 768 items. */
    for (long i = 0; i < *count; ++i) if (indices[i] == index) return;
    if (*count < 256) indices[(*count)++] = index;
}

/* Kevin Boggs: native_query_build */
static long native_query_build(struct native_query_tree *tree, long index, unsigned int depth)
{
    if (depth >= MAXIMUM_BSP3D_DEPTH) { tree->count = QUERY_CACHE_NODES + 1; return NONE; }
    while (!(index & LONG_MIN)) {
        const struct bsp3d_node *node = TAG_BLOCK_GET_ELEMENT(&tree->bsp->bsp3d.nodes, index, struct bsp3d_node);
        const real_plane3d *plane = TAG_BLOCK_GET_ELEMENT(&tree->bsp->bsp3d.planes, node->plane_designator, real_plane3d);
        real distance = plane3d_distance_to_point(plane, &tree->center);
        /* A conservative margin also covers float rounding at the boundary. */
        real extent = tree->radius * MAX(1.f, fabs(plane->n.i) + fabs(plane->n.j) + fabs(plane->n.k)) + 0.001f;
        if (distance > extent) { index = node->children[1]; ++tree->skipped; continue; }
        if (distance < -extent) { index = node->children[0]; ++tree->skipped; continue; }
        if (tree->count >= QUERY_CACHE_NODES) { tree->count = QUERY_CACHE_NODES + 1; return NONE; }
        long result = tree->count++;
        tree->nodes[result].original = index;
        tree->nodes[result].children[0] = native_query_build(tree, node->children[0], depth + 1);
        if (tree->count > QUERY_CACHE_NODES) return NONE;
        tree->nodes[result].children[1] = native_query_build(tree, node->children[1], depth + 1);
        return result;
    }
    return index;
}

/* Kevin Boggs: native_query_get */
static const struct native_query_tree *native_query_get(const struct collision_bsp *bsp,
    const real_point3d *center, real radius)
{
    extern int n3ds_input_platform_original_model(void);
    if (bsp != global_collision_bsp || !(radius > 0.01f && radius < 4.f)) return NULL;
    /* Crowded scenes can cycle through more actors than the cache holds.
     * Back off after sustained misses; periodically retry as the scene changes.
     * This controls derived-data work only, never actual collision queries. */
    if(native_query_cooldown) {--native_query_cooldown;++native_query_bypassed;return NULL;}
    unsigned int slots = n3ds_input_platform_original_model() ? 8 : QUERY_CACHE_SLOTS;
    struct native_query_tree *oldest = native_query_trees;
    for (unsigned int i = 0; i < slots; ++i) {
        struct native_query_tree *tree = native_query_trees + i;
        real room = tree->radius - radius - 0.001f;
        if (tree->bsp == bsp && room > 0.f && distance_squared3d(&tree->center, center) < room * room) {
            tree->stamp = ++native_query_clock; ++native_query_hits;
            native_query_miss_run=0;
            return tree->count<=QUERY_CACHE_NODES ? tree : NULL;
        }
        if (tree->stamp < oldest->stamp) oldest = tree;
    }
    ++native_query_misses;
    if(++native_query_miss_run>=32) {
        native_query_cooldown=1024;native_query_miss_run=0;return NULL;
    }
    oldest->bsp = bsp; oldest->center = *center; oldest->radius = radius + 0.25f;
    oldest->stamp = ++native_query_clock; oldest->count = oldest->skipped = 0;
    oldest->root = native_query_build(oldest, 0, 0);
    /* Remember overflow too: a crowded region must not pay for a failed
     * construction again every tick before taking the ordinary traversal. */
    if (oldest->count > QUERY_CACHE_NODES) return NULL;
    return oldest;
}

/* Kevin Boggs: n3ds_collision_query_report */
void n3ds_collision_query_report(void)
{
    extern void n3ds_log(const char *);
    char message[120];
    snprintf(message, sizeof(message), "COLLISION TRAVERSAL REUSE: hits=%u misses=%u bypassed=%u", native_query_hits, native_query_misses, native_query_bypassed);
    n3ds_log(message); native_query_hits = native_query_misses = native_query_bypassed = 0;
}


/* A surface can be reached through several BSP leaves in one sphere query.
 * Cache completed visits, including misses, only for that query. Projection
 * axis/sign and exact projected-center bits are part of the key. A hash
 * collision evicts an entry; it can never discard a collision feature.
 * The simulation thread owns this 5 KiB table, like native_feature_hash.
 * Per-query generations also prevent reuse after breakage, movement or a map
 * change. Generation rollover explicitly empties the table. */
static unsigned int native_surface_generation;
static struct {
    unsigned int generation;
    long surface;
    short axis;
    boolean sign;
    unsigned int center_bits[2];
} native_surface_visits[256];

/* Kevin Boggs: native_surface_begin */
static unsigned int native_surface_begin(void)
{
    if (!++native_surface_generation) {
        memset(native_surface_visits, 0, sizeof(native_surface_visits));
        native_surface_generation = 1;
    }
    return native_surface_generation;
}

/* Kevin Boggs: native_surface_repeated */
static boolean native_surface_repeated(unsigned int generation, long surface,
    short axis, boolean sign, const real_point2d *center)
{
    unsigned int slot = ((unsigned int)surface * 2654435761u) >> 24;
    unsigned int bits[2];
    memcpy(bits, center, sizeof(bits));
    if (native_surface_visits[slot].generation == generation &&
        native_surface_visits[slot].surface == surface &&
        native_surface_visits[slot].axis == axis && native_surface_visits[slot].sign == sign &&
        native_surface_visits[slot].center_bits[0] == bits[0] &&
        native_surface_visits[slot].center_bits[1] == bits[1]) return TRUE;
    native_surface_visits[slot].generation = generation;
    native_surface_visits[slot].surface = surface;
    native_surface_visits[slot].axis = axis;
    native_surface_visits[slot].sign = sign;
    memcpy(native_surface_visits[slot].center_bits, bits, sizeof(bits));
    return FALSE;
}
