/* New3DS integration additions by Kevin Boggs. */
/* Repeat a skinned opaque mesh with a separate bone palette per instance.
 * This shares a draw, not a pose: aiming, hitboxes and animation are unchanged.
 * The PICA palette limit bounds each batch; exact material and light values
 * are required. Alpha/effect/decal/view-space draws remain ordered barriers. */
enum { CHARACTER_BATCH_MESHES=32, CHARACTER_BATCH_GROUPS=16, CHARACTER_BATCH_INSTANCES=4 };
static struct character_batch_mesh {
    const void *source,*key;
    struct native_skin_vertex *vertices;
    unsigned short *indices;
    unsigned int vertex_count,index_count,nodes,capacity;
} character_batch_meshes[CHARACTER_BATCH_MESHES];
static struct character_batch_group {
    struct character_batch_mesh *mesh;
    struct native_model_material material;
    struct native_model_lighting lighting;
    float rows[NATIVE_GPU_MODEL_BONES*12],uv[2];
    unsigned int count;
    int sides;
} character_batch_groups[CHARACTER_BATCH_GROUPS];
static unsigned int character_batch_bytes,character_batch_draws,character_batch_instances;

/* Kevin Boggs: character_batch_mesh_get */
static struct character_batch_mesh *character_batch_mesh_get(const void *source,const void *key,
    const struct native_skin_vertex *vertices,unsigned int count,const unsigned short *indices,
    unsigned int index_count,unsigned int nodes)
{
    struct character_batch_mesh *slot=NULL;
    for(unsigned int i=0;i<CHARACTER_BATCH_MESHES;++i) {
        struct character_batch_mesh *mesh=character_batch_meshes+i;
        if(mesh->source==source && mesh->key==key && mesh->nodes==nodes &&
            mesh->vertex_count==count && mesh->index_count==index_count)return mesh->vertices?mesh:NULL;
        if(!mesh->source && !slot)slot=mesh;
    }
    if(!slot || nodes<2 || nodes>NATIVE_GPU_MODEL_BONES/2 || !count || count>1024 ||
        !index_count || index_count>6144)return NULL;
    unsigned int capacity=MIN(CHARACTER_BATCH_INSTANCES,NATIVE_GPU_MODEL_BONES/nodes);
    unsigned int bytes=capacity*(count*sizeof(*vertices)+index_count*sizeof(*indices));
    extern int n3ds_input_platform_original_model(void);
    unsigned int budget=n3ds_input_platform_original_model()?64*1024:384*1024;
    if(bytes>budget || character_batch_bytes>budget-bytes)return NULL;
    *slot=(struct character_batch_mesh){.source=source,.key=key,.nodes=nodes,.capacity=capacity,
        .vertex_count=count,.index_count=index_count};
    slot->vertices=n3ds_gpu_geometry_optional_allocate(capacity*count*sizeof(*vertices));
    slot->indices=n3ds_gpu_geometry_optional_allocate(capacity*index_count*sizeof(*indices));
    if(!slot->vertices || !slot->indices) {
        n3ds_gpu_geometry_free(slot->vertices);n3ds_gpu_geometry_free(slot->indices);
        slot->vertices=NULL;slot->indices=NULL;return NULL;
    }
    for(unsigned int instance=0;instance<capacity;++instance) {
        for(unsigned int vertex=0;vertex<count;++vertex) {
            struct native_skin_vertex *out=slot->vertices+instance*count+vertex;
            *out=vertices[vertex];
            out->bones[0]+=instance*nodes*3;out->bones[1]+=instance*nodes*3;
        }
        for(unsigned int index=0;index<index_count;++index) {
            assert(indices[index]<count);
            slot->indices[instance*index_count+index]=indices[index]+instance*count;
        }
    }
    n3ds_gpu_geometry_flush(slot->vertices,capacity*count*sizeof(*vertices));
    n3ds_gpu_geometry_publish(slot->vertices,capacity*count*sizeof(*vertices));
    n3ds_gpu_geometry_flush(slot->indices,capacity*index_count*sizeof(*indices));
    character_batch_bytes+=bytes;return slot;
}

/* Kevin Boggs: character_batch_submit */
static void character_batch_submit(struct character_batch_group *group)
{
    if(!group->count)return;
    group->material.lighting=&group->lighting;
    assert(n3ds_gpu_model_skin_draw(group->mesh->vertices,group->mesh->indices,
        group->mesh->index_count*group->count,group->rows,group->mesh->nodes*group->count,
        group->uv,0,0,group->sides,&group->material));
    ++character_batch_draws;character_batch_instances+=group->count;group->count=0;
}

/* Kevin Boggs: character_batch_flush */
static void character_batch_flush(void)
{
#ifndef HALO_N3DS_CHARACTER_BATCH
    return;
#endif
    for(unsigned int i=0;i<CHARACTER_BATCH_GROUPS;++i)character_batch_submit(character_batch_groups+i);
}

/* Kevin Boggs: character_batch_release */
static void character_batch_release(void)
{
#ifndef HALO_N3DS_CHARACTER_BATCH
    return;
#endif
    if(character_batch_bytes)assert(n3ds_gpu_texture_barrier());
    for(unsigned int i=0;i<CHARACTER_BATCH_MESHES;++i) {
        n3ds_gpu_geometry_free(character_batch_meshes[i].vertices);
        n3ds_gpu_geometry_free(character_batch_meshes[i].indices);
    }
    memset(character_batch_meshes,0,sizeof(character_batch_meshes));
    memset(character_batch_groups,0,sizeof(character_batch_groups));character_batch_bytes=0;
}

/* Kevin Boggs: character_batch_add */
static int character_batch_add(const void *source,const void *key,const struct native_skin_vertex *vertices,
    unsigned int count,const unsigned short *indices,unsigned int index_count,unsigned int nodes,
    const float *rows,const float *uv,int sides,const struct native_model_material *material)
{
#ifndef HALO_N3DS_CHARACTER_BATCH
    return 0;
#endif
    if(!scenery_batch_allowed || !material->lighting || !material->base || !key)return 0;
    struct character_batch_mesh *mesh=character_batch_mesh_get(source,key,vertices,count,indices,index_count,nodes);
    if(!mesh)return 0;
    struct native_model_material compare=*material;compare.lighting=NULL;
    struct character_batch_group *empty=NULL;
    for(unsigned int i=0;i<CHARACTER_BATCH_GROUPS;++i) {
        struct character_batch_group *group=character_batch_groups+i;
        if(!group->count){if(!empty)empty=group;continue;}
        if(group->mesh!=mesh || group->sides!=sides || memcmp(group->uv,uv,sizeof(group->uv)) ||
            memcmp(&group->material,&compare,sizeof(compare)) ||
            memcmp(&group->lighting,material->lighting,sizeof(group->lighting)))continue;
        memcpy(group->rows+group->count*nodes*12,rows,nodes*12*sizeof(float));
        if(++group->count==mesh->capacity)character_batch_submit(group);
        return 1;
    }
    if(!empty){character_batch_flush();empty=character_batch_groups;}
    *empty=(struct character_batch_group){.mesh=mesh,.material=compare,.lighting=*material->lighting,.count=1,.sides=sides};
    memcpy(empty->uv,uv,sizeof(empty->uv));memcpy(empty->rows,rows,nodes*12*sizeof(float));return 1;
}

/* Kevin Boggs: n3ds_character_batch_report */
void n3ds_character_batch_report(void)
{
    char line[160];snprintf(line,sizeof(line),"RESULT: character batches instances=%u draws=%u saved=%u bytes=%u",
        character_batch_instances,character_batch_draws,character_batch_instances-character_batch_draws,character_batch_bytes);
    n3ds_log(line);
}
