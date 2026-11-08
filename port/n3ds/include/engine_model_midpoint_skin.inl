/* One synchronous CPU draw owns this scratch at a time. Material encoders
 * consume its contents into their existing GPU arenas before the next lookup.
 * Regular pose cache entries never allocate from or evict this buffer. */
static struct midpoint_skin_pose {
    struct native_render_vertex *vertices;
    real_matrix4x3 bones[64];
    real_vector2d uv_scale;
    unsigned long long revision,frame,allocation_failure_frame;
    unsigned int generation,owner,count,nodes,capacity;
    int valid,lighting,allocation_failed;
} midpoint_skin;

static void midpoint_skin_clear(void)
{
    free(midpoint_skin.vertices);
    memset(&midpoint_skin,0,sizeof(midpoint_skin));
}

static const struct native_render_vertex *skin_midpoint_stream(const struct decoded_stream *stream,
    struct model_skin_range *range,unsigned int owner)
{
    unsigned int count=stream->count,nodes=range->skin->node_matrix_count;
    unsigned int bytes=count*sizeof(*midpoint_skin.vertices),matrix_bytes=nodes*sizeof(*midpoint_skin.bones);
    /* Keep the same maximum size for an individual pose as the regular path. */
    if(bytes>POSE_BYTES-matrix_bytes)return NULL;
    unsigned int generation=n3ds_engine_cache_generation();
    unsigned long long frame=n3ds_gpu_model_frame_serial();
    n3ds_algorithm_profile_max(N3DS_ALG_CPU_SKIN_MIDPOINT_PEAK,midpoint_skin.capacity);
    if(midpoint_skin.valid && midpoint_skin.frame==frame && midpoint_skin.generation==generation &&
       midpoint_skin.revision==stream->revision && midpoint_skin.owner==owner &&
       midpoint_skin.count==count && midpoint_skin.nodes==nodes &&
       (!range->lighting || midpoint_skin.lighting) &&
       !memcmp(&midpoint_skin.uv_scale,range->scale,sizeof(*range->scale)) &&
       !memcmp(midpoint_skin.bones,range->skin->node_matrices,matrix_bytes)) {
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_MIDPOINT_REUSE,1);
        return midpoint_skin.vertices;
    }

    if(bytes>midpoint_skin.capacity) {
        if(midpoint_skin.allocation_failed && midpoint_skin.allocation_failure_frame==frame)return NULL;
        struct native_render_vertex *vertices=(realloc)(midpoint_skin.vertices,bytes);
        if(!vertices) {
            midpoint_skin.allocation_failed=1;
            midpoint_skin.allocation_failure_frame=frame;
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_MIDPOINT_PRESSURE,1);
            return NULL;
        }
        midpoint_skin.vertices=vertices;
        midpoint_skin.capacity=bytes;
        n3ds_algorithm_profile_max(N3DS_ALG_CPU_SKIN_MIDPOINT_PEAK,bytes);
    }
    midpoint_skin.valid=0;
    range->output=midpoint_skin.vertices;
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_VERTICES,count);
    if(!n3ds_parallel_range(skin_vertex_range,range,count,256,2))return NULL;
    memcpy(midpoint_skin.bones,range->skin->node_matrices,matrix_bytes);
    midpoint_skin.uv_scale=*range->scale;
    midpoint_skin.revision=stream->revision;
    midpoint_skin.frame=frame;
    midpoint_skin.generation=generation;
    midpoint_skin.owner=owner;
    midpoint_skin.count=count;
    midpoint_skin.nodes=nodes;
    midpoint_skin.lighting=range->lighting;
    midpoint_skin.valid=1;
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_MIDPOINT_BUILD,1);
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_COPY,matrix_bytes);
    if(range->normals)n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_NORMAL_REUSE,count);
    if(!range->lighting)n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_LIGHT_AVOIDED,count);
    return midpoint_skin.vertices;
}
