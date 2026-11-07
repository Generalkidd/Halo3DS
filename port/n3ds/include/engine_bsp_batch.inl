/* Adjacent opaque terrain only. Visibility is resolved before admission and
 * each full list is concatenated in its original order. No material sorting,
 * vertex welding, texture retention across frames, or mutable index streams. */
enum { BSP_BATCH_PACKETS=4, BSP_BATCH_SLOTS=16, BSP_BATCH_ENTRY_BYTES=128*1024 };
struct bsp_draw_packet {
    const struct native_geometry *owner;
    const struct shader *shader;
    int permutation,lightmap_group,lightmap_index;
    void *textures[3];
    int lightmap,detail,alpha;
    float scale[2];
    unsigned int material;
};
struct bsp_merged_geometry {
    const struct native_geometry *owners[BSP_BATCH_PACKETS];
    unsigned int packets,count;
    void *allocation;
    struct native_render_vertex *vertices;
    float *light_uv;
    unsigned short *indices;
};
static struct bsp_draw_packet bsp_packets[BSP_BATCH_PACKETS];
static struct bsp_merged_geometry bsp_merged[BSP_BATCH_SLOTS];
static unsigned int bsp_packet_count,bsp_merged_count,bsp_merged_bytes,bsp_merge_attempt_frame;
static unsigned int bsp_merged_groups,bsp_merged_packets_saved,bsp_merged_draws_saved;

static int bsp_packet_key_equal(const struct bsp_draw_packet *a,const struct bsp_draw_packet *b)
{
    return a->shader==b->shader && a->permutation==b->permutation &&
        a->lightmap_group==b->lightmap_group && a->lightmap_index==b->lightmap_index &&
        a->lightmap==b->lightmap && a->detail==b->detail && a->alpha==b->alpha &&
        a->material==b->material && !memcmp(a->scale,b->scale,sizeof(a->scale)) &&
        !memcmp(a->textures,b->textures,sizeof(a->textures));
}
static void bsp_packet_bind(const struct bsp_draw_packet *packet)
{
    assert(n3ds_gpu_texture_bind(0,packet->textures[0]));
    assert(n3ds_gpu_texture_bind_sampler(1,packet->textures[1],packet->lightmap?
        NATIVE_SAMPLER_CLAMP_U|NATIVE_SAMPLER_CLAMP_V:0));
    assert(n3ds_gpu_texture_bind(2,packet->textures[2]));
}
static void bsp_packet_draw(const struct bsp_draw_packet *packet,
    const struct native_render_vertex *vertices,const float *light_uv,
    const unsigned short *indices,unsigned int count)
{
    assert(n3ds_gpu_environment_draw(vertices,light_uv,indices,count,packet->lightmap,packet->detail,
        packet->scale[0],packet->scale[1],packet->material,packet->alpha));
}
static struct bsp_merged_geometry *bsp_batch_geometry(void)
{
    if(bsp_packet_count<2) return NULL;
    for(unsigned int i=0;i<bsp_merged_count;++i) {
        struct bsp_merged_geometry *entry=bsp_merged+i;
        if(entry->packets!=bsp_packet_count) continue;
        unsigned int p=0;
        while(p<bsp_packet_count && entry->owners[p]==bsp_packets[p].owner) ++p;
        if(p==bsp_packet_count) return entry;
    }
    /* Copies are independent of camera/material state and remain immutable
     * through both eyes and optional views. Never build during a midpoint. */
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active()) return NULL;
#endif
    if(bsp_merged_count==BSP_BATCH_SLOTS || bsp_merge_attempt_frame==bsp_filter_frame) return NULL;
    unsigned int vertices=0,indices=0;
    for(unsigned int p=0;p<bsp_packet_count;++p) {
        const struct native_geometry *owner=bsp_packets[p].owner;
        if(owner->vertex_count>65535-vertices) return NULL;
        vertices+=owner->vertex_count;indices+=owner->index_count;
    }
    unsigned int vertex_bytes=vertices*sizeof(struct native_render_vertex),uv_bytes=vertices*2*sizeof(float);
    unsigned int bytes=vertex_bytes+uv_bytes+indices*sizeof(unsigned short);
    unsigned int budget=n3ds_input_platform_original_model()?128*1024:1024*1024;
    if(bytes>BSP_BATCH_ENTRY_BYTES || bytes>budget || bsp_merged_bytes>budget-bytes) return NULL;
    /* Bound first-use copy work and repeated allocation attempts per frame;
     * optional allocation keeps the renderer's existing 2 MiB reserve. */
    bsp_merge_attempt_frame=bsp_filter_frame;
    byte *allocation=n3ds_gpu_geometry_optional_allocate(bytes);
    if(!allocation) return NULL;
    struct bsp_merged_geometry *entry=bsp_merged+bsp_merged_count;
    entry->allocation=allocation;entry->vertices=(void *)allocation;
    entry->light_uv=(void *)(allocation+vertex_bytes);
    entry->indices=(void *)(allocation+vertex_bytes+uv_bytes);
    unsigned int v=0,n=0;
    for(unsigned int p=0;p<bsp_packet_count;++p) {
        const struct native_geometry *owner=bsp_packets[p].owner;
        entry->owners[p]=owner;
        memcpy(entry->vertices+v,owner->vertices,owner->vertex_count*sizeof(*owner->vertices));
        memcpy(entry->light_uv+v*2,owner->light_uv,owner->vertex_count*2*sizeof(float));
        for(unsigned int i=0;i<owner->index_count;++i) {
            assert(owner->indices[i]<owner->vertex_count);
            entry->indices[n+i]=v+owner->indices[i];
        }
        v+=owner->vertex_count;n+=owner->index_count;
    }
    entry->packets=bsp_packet_count;entry->count=indices;
    n3ds_gpu_geometry_flush(allocation,bytes);
    ++bsp_merged_count;bsp_merged_bytes+=bytes;
    return entry;
}
static void bsp_batch_flush(void)
{
    if(!bsp_packet_count) return;
    if(!n3ds_presentation_check_midpoint()) {
        struct bsp_merged_geometry *merged=bsp_batch_geometry();
        bsp_packet_bind(bsp_packets);
        if(merged) {
            unsigned int original_draws=0;
            for(unsigned int p=0;p<bsp_packet_count;++p) original_draws+=(bsp_packets[p].owner->index_count+29999)/30000;
            ++bsp_merged_groups;bsp_merged_packets_saved+=bsp_packet_count-1;
            bsp_merged_draws_saved+=original_draws-(merged->count+29999)/30000;
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_BSP_MERGED_GROUPS,1);
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_BSP_PACKETS_SAVED,bsp_packet_count-1);
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_BSP_DRAWS_SAVED,
                original_draws-(merged->count+29999)/30000);
            bsp_packet_draw(bsp_packets,merged->vertices,merged->light_uv,merged->indices,merged->count);
        }
        else for(unsigned int p=0;p<bsp_packet_count;++p) {
            const struct native_geometry *owner=bsp_packets[p].owner;
            bsp_packet_draw(bsp_packets+p,owner->vertices,owner->light_uv,owner->indices,owner->index_count);
        }
    }
    bsp_packet_count=0;
}
static void bsp_batch_submit(const struct bsp_draw_packet *packet)
{
    /* The caller captures all current bindings before this flush can replace
     * them with the preceding packet's handles. Cache use stamps keep these
     * resources resident until the regular frame has finished. */
    if(bsp_packet_count && !bsp_packet_key_equal(bsp_packets,packet)) bsp_batch_flush();
    if(n3ds_presentation_midpoint_rejected()) return;
    bsp_packets[bsp_packet_count++]=*packet;
    if(bsp_packet_count==BSP_BATCH_PACKETS) bsp_batch_flush();
}
static void bsp_batch_release(void)
{
    /* Called only behind the existing BSP unload fence. No live draw ever
     * replaces an allocation that may still be referenced by the GPU. */
    assert(!bsp_packet_count);
    if(bsp_merged_count) {
        extern void n3ds_log(const char *);
        char text[192];snprintf(text,sizeof(text),"BSP ADJACENCY CACHE: entries=%u bytes=%u merged_groups=%u packet_calls_saved=%u draw_calls_saved=%u",
            bsp_merged_count,bsp_merged_bytes,bsp_merged_groups,bsp_merged_packets_saved,bsp_merged_draws_saved);n3ds_log(text);
    }
    for(unsigned int i=0;i<bsp_merged_count;++i) n3ds_gpu_geometry_free(bsp_merged[i].allocation);
    memset(bsp_merged,0,sizeof(bsp_merged));memset(bsp_packets,0,sizeof(bsp_packets));
    bsp_merged_count=bsp_merged_bytes=bsp_merge_attempt_frame=0;
    bsp_merged_groups=bsp_merged_packets_saved=bsp_merged_draws_saved=0;
}
