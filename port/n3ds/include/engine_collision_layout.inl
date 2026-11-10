/* New3DS integration additions by Kevin Boggs. */
/* Compile the immutable collision-node layout once per map. Each query still
 * reads current region permutations and poses, in the original node order.
 * This removes skeleton nodes that can never contribute a collision and
 * reuses derived inverse transforms only when all source bytes still match. */
enum { COLLISION_LAYOUT_SLOTS=16, COLLISION_LAYOUT_NODES=64 };
static struct native_collision_layout {
    const struct collision_model *model;
    short count,nodes[COLLISION_LAYOUT_NODES];
} collision_layouts[COLLISION_LAYOUT_SLOTS];
static unsigned int collision_layout_next,collision_layout_queries,collision_layout_skipped;
static struct native_collision_transform {
    const real_matrix4x3 *source;
    real_matrix4x3 value,inverse;
} *collision_transforms;
static unsigned int collision_transform_capacity,collision_transform_hits,collision_transform_misses;
static int collision_transform_attempted;

/* Kevin Boggs: native_collision_layout_get */
static const struct native_collision_layout *native_collision_layout_get(const struct collision_model *model)
{
#ifndef HALO_N3DS_COLLISION_LAYOUT
    return NULL;
#endif
    if(model->nodes.count<0 || model->nodes.count>COLLISION_LAYOUT_NODES)return NULL;
    ++collision_layout_queries;
    for(unsigned int i=0;i<COLLISION_LAYOUT_SLOTS;++i)if(collision_layouts[i].model==model) {
        collision_layout_skipped+=model->nodes.count-collision_layouts[i].count;
        return collision_layouts+i;
    }
    struct native_collision_layout *layout=collision_layouts+(collision_layout_next++%COLLISION_LAYOUT_SLOTS);
    layout->model=model;layout->count=0;
    for(short i=0;i<model->nodes.count;++i) {
        const struct collision_node *node=TAG_BLOCK_GET_ELEMENT(&model->nodes,i,struct collision_node);
        if(node->region_index!=NONE && node->bsps.count>0)layout->nodes[layout->count++]=i;
    }
    collision_layout_skipped+=model->nodes.count-layout->count;return layout;
}

/* Kevin Boggs: native_collision_inverse */
static void native_collision_inverse(const real_matrix4x3 *source,real_matrix4x3 *out)
{
#ifdef HALO_N3DS_COLLISION_LAYOUT
    if(!collision_transform_attempted) {
        extern int n3ds_input_platform_original_model(void);
        collision_transform_attempted=1;
        collision_transform_capacity=n3ds_input_platform_original_model()?128:512;
        collision_transforms=(calloc)(collision_transform_capacity,sizeof(*collision_transforms));
    }
    if(collision_transforms) {
        unsigned int hash=((unsigned long)source>>2)*2654435761u;
        struct native_collision_transform *slot=collision_transforms+((hash>>16)&(collision_transform_capacity-1));
        if(slot->source==source && !memcmp(source,&slot->value,sizeof(*source))) {
            *out=slot->inverse;++collision_transform_hits;
#ifdef HALO_N3DS_COLLISION_LAYOUT_VERIFY
            real_matrix4x3 reference;matrix4x3_inverse(source,&reference);
            if(memcmp(out,&reference,sizeof(reference)))abort();
#endif
            return;
        }
        slot->source=source;slot->value=*source;matrix4x3_inverse(source,&slot->inverse);
        *out=slot->inverse;++collision_transform_misses;return;
    }
#endif
    matrix4x3_inverse(source,out);
}

/* Kevin Boggs: n3ds_collision_layout_clear */
void n3ds_collision_layout_clear(void)
{
    memset(collision_layouts,0,sizeof(collision_layouts));collision_layout_next=0;
    free(collision_transforms);collision_transforms=NULL;
    collision_transform_capacity=0;collision_transform_attempted=0;
}

/* Kevin Boggs: n3ds_collision_layout_report */
void n3ds_collision_layout_report(void)
{
    extern void n3ds_log(const char *);char line[192];
    snprintf(line,sizeof(line),"RESULT: collision layout queries=%u empty_nodes_skipped=%u inverse_hits=%u misses=%u",
        collision_layout_queries,collision_layout_skipped,collision_transform_hits,collision_transform_misses);
    n3ds_log(line);
}
