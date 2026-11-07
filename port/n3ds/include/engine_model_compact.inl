/* Exact compaction of immutable, already GPU-eligible opaque streams. Keep the
 * original palette/program and copy every native vertex attribute unchanged.
 * These owners never move or evict while GPU commands may reference them. */
enum { MODEL_COMPACT_SLOTS=24, MODEL_COMPACT_REJECTIONS=32 };
struct model_compact_key {
    const void *source,*indices;
    unsigned int vertices,triangles,index_count,stride;
    short type;
};
static struct model_compact_mesh {
    struct model_compact_key key;
    void *vertices;
    unsigned short *indices;
} model_compact_meshes[MODEL_COMPACT_SLOTS];
static struct model_compact_key model_compact_rejected[MODEL_COMPACT_REJECTIONS];
static unsigned int model_compact_bytes,model_compact_next;
static unsigned long long model_compact_failure_frame;
static int model_compact_failed;
static int model_compact_key_equal(const struct model_compact_key *a,const struct model_compact_key *b)
{
    return a->source==b->source && a->indices==b->indices && a->vertices==b->vertices &&
        a->triangles==b->triangles && a->index_count==b->index_count && a->stride==b->stride && a->type==b->type;
}
static void model_compact_release(void)
{
    if(model_compact_bytes) assert(!n3ds_gpu_frame_active() && n3ds_gpu_texture_barrier());
    for(unsigned int i=0;i<MODEL_COMPACT_SLOTS;++i)n3ds_gpu_geometry_free(model_compact_meshes[i].vertices);
    memset(model_compact_meshes,0,sizeof(model_compact_meshes));
    memset(model_compact_rejected,0,sizeof(model_compact_rejected));
    model_compact_bytes=model_compact_next=0;model_compact_failure_frame=0;model_compact_failed=0;
}
static const struct model_compact_mesh *model_compact_get(const void *source,const void *index_source,
    unsigned int triangles,short type,const struct decoded_stream *stream,
    const unsigned short *indices,unsigned int index_count)
{
    const unsigned int stride=stream->rigid_node>=0?sizeof(struct native_render_vertex):sizeof(struct native_skin_vertex);
    struct model_compact_key key={source,index_source,stream->count,triangles,index_count,stride,type};
    struct model_compact_mesh *slot=NULL;
    for(unsigned int i=0;i<MODEL_COMPACT_SLOTS;++i){
        struct model_compact_mesh *candidate=model_compact_meshes+i;
        if(candidate->vertices && model_compact_key_equal(&candidate->key,&key))return candidate;
        if(!candidate->vertices && !slot)slot=candidate;
    }
    if(!slot || !stream->local || !index_count)return NULL;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active())return NULL;
#endif
    unsigned long long frame=n3ds_gpu_model_frame_serial();
    if(model_compact_failed && model_compact_failure_frame==frame)return NULL;
    for(unsigned int i=0;i<MODEL_COMPACT_REJECTIONS;++i)
        if(model_compact_key_equal(model_compact_rejected+i,&key))return NULL;
    struct model_compact_key *failed=model_compact_rejected+(model_compact_next++%MODEL_COMPACT_REJECTIONS);
    *failed=key;
    extern int n3ds_input_platform_original_model(void);
    unsigned int budget=n3ds_input_platform_original_model()?64*1024:256*1024;
    unsigned int index_bytes=index_count*sizeof(*indices),source_bytes=stream->count*stride;
    /* Reject impossible savings/admission before allocating or scanning a
     * source. The final used-vertex count still decides viable candidates. */
    if(model_compact_bytes>budget || index_bytes>budget-model_compact_bytes ||
       index_bytes>source_bytes || source_bytes-index_bytes<4096)return NULL;
    unsigned short *remap=(malloc)(stream->count*sizeof(*remap));
    if(!remap)goto pressure;
    memset(remap,255,stream->count*sizeof(*remap));
    for(unsigned int i=0;i<index_count;++i){
        if(indices[i]>=stream->count){free(remap);return NULL;}
        remap[indices[i]]=0;
    }
    unsigned int used=0;
    for(unsigned int i=0;i<stream->count;++i)if(remap[i]!=65535)remap[i]=used++;
    unsigned int vertex_bytes=used*stride,index_offset=(vertex_bytes+3)&~3u;
    unsigned int bytes=index_offset+index_bytes;
    /* Pay for both compact streams and save at least one 4 KiB page against
     * the original vertex upload. Dense meshes retain the existing owner. */
    if(!used || bytes>budget || model_compact_bytes>budget-bytes ||
       bytes>source_bytes || source_bytes-bytes<4096){free(remap);return NULL;}
    void *vertices=n3ds_gpu_geometry_optional_allocate(bytes);
    if(!vertices){free(remap);goto pressure;}
    unsigned short *out=(void *)((byte *)vertices+index_offset);
    for(unsigned int i=0;i<stream->count;++i)if(remap[i]!=65535)
        memcpy((byte *)vertices+remap[i]*stride,(const byte *)stream->local+i*stride,stride);
    for(unsigned int i=0;i<index_count;++i)out[i]=remap[indices[i]];
    free(remap);n3ds_gpu_geometry_flush(vertices,bytes);
    *slot=(struct model_compact_mesh){key,vertices,out};model_compact_bytes+=bytes;
    memset(failed,0,sizeof(*failed));return slot;
pressure:
    model_compact_failed=1;model_compact_failure_frame=frame;memset(failed,0,sizeof(*failed));return NULL;
}
