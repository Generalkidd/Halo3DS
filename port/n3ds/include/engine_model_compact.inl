/* Exact compaction of immutable, already GPU-eligible opaque streams. Keep the
 * original palette/program and copy every native vertex attribute unchanged.
 * These owners never move or evict while GPU commands may reference them. */
enum { MODEL_COMPACT_SLOTS=24, MODEL_COMPACT_REJECTIONS=32 };
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
enum {
    MODEL_COMPACT_NONE=65535,
    MODEL_COMPACT_PROBE_LIMIT=32,
    MODEL_COMPACT_NO_ROOM=-1,
    MODEL_COMPACT_INVALID_INDEX=-2,
    MODEL_COMPACT_PROBE_EXHAUSTED=-3
};
_Static_assert(sizeof(struct model_vertex_compressed)==32,"Complete packed vertex tuple");
_Static_assert(sizeof(struct native_render_vertex)==32,"Complete rigid GPU vertex tuple");

static unsigned int model_compact_hash(const struct model_vertex_compressed *vertex)
{
    unsigned int hash=2166136261u;
    const byte *raw=(const void *)vertex;
    for(unsigned int offset=0;offset<sizeof(*vertex);offset+=sizeof(unsigned int)) {
        unsigned int word;
        memcpy(&word,raw+offset,sizeof(word));
        hash=(hash^word)*16777619u;
    }
    hash^=hash>>16;
    hash*=0x7feb352du;
    return hash^(hash>>15);
}

/* One table per draw context. Raw data keeps tangent, binormal and source
 * influence seams distinct even when the current program cannot see them.
 * The native tuple checks the exact palette-relative GPU input bytes. */
static int model_compact_remap_first_use(const struct model_vertex_compressed *raw,
    const byte *native,unsigned int count,unsigned int stride,
    const unsigned short *indices,unsigned int index_count,unsigned short *remap,
    unsigned short *table,unsigned int slots,unsigned short *first_use,unsigned int capacity,
    unsigned int *referenced)
{
    memset(table,255,slots*sizeof(*table));
    unsigned int used=0;
    *referenced=0;
    for(unsigned int i=0;i<index_count;++i) {
        unsigned int source=indices[i];
        if(source>=count)return MODEL_COMPACT_INVALID_INDEX;
        if(remap[source]!=MODEL_COMPACT_NONE)continue;
        ++*referenced;
        unsigned int slot=model_compact_hash(raw+source)&(slots-1);
        unsigned int probes=0;
        while(table[slot]!=MODEL_COMPACT_NONE) {
            unsigned int representative=first_use[table[slot]];
            if(!memcmp(raw+source,raw+representative,sizeof(*raw)) &&
               !memcmp(native+source*stride,native+representative*stride,stride))break;
            if(++probes==MODEL_COMPACT_PROBE_LIMIT)return MODEL_COMPACT_PROBE_EXHAUSTED;
            slot=(slot+1)&(slots-1);
        }
        if(table[slot]==MODEL_COMPACT_NONE) {
            if(used==capacity)return MODEL_COMPACT_NO_ROOM;
            first_use[used]=source;
            table[slot]=used++;
        }
        remap[source]=table[slot];
    }
    return used;
}

#if HALO_N3DS_ALGO_MODEL_COMPACT == N3DS_ALGORITHM_VERIFY
static int model_compact_verify(const struct model_vertex_compressed *raw,const byte *native,
    const byte *vertices,const unsigned short *indices,const unsigned short *out,
    const unsigned short *first_use,unsigned int index_count,unsigned int stride,unsigned int used)
{
    unsigned int next=0;
    for(unsigned int i=0;i<index_count;++i) {
        unsigned int source=indices[i],dest=out[i];
        if(dest>=used || dest>next ||
           memcmp(raw+source,raw+first_use[dest],sizeof(*raw)) ||
           memcmp(native+source*stride,vertices+dest*stride,stride))return 0;
        if(dest==next) {
            if(first_use[dest]!=source)return 0;
            ++next;
        }
    }
    return next==used;
}
#endif

static int model_compact_remap_tests(void)
{
    struct model_vertex_compressed raw[8]={0};
    struct native_skin_vertex native[8]={0};
    raw[2].binormal=1;
    raw[3].tangent=1;
    raw[4].position.x=-0.f;
    raw[5].node_weight=1;
    n3ds_skin_vertex_set_bones(native+6,0,3,0.f,0.f); /* Same raw hash, distinct GPU attribute. */
    const unsigned short indices[]={7,2,1,3,2,0,5,4,6,7,1,2};
    const unsigned short expected[]={0,1,0,2,1,0,3,4,5,0,0,1};
    const unsigned short representatives[]={7,2,3,5,4,6};
    unsigned short remap[8],table[16],first_use[8];
    unsigned int referenced;
    memset(remap,255,sizeof(remap));
    if(model_compact_remap_first_use(raw,(const void *)native,8,sizeof(*native),
        indices,NUMBEROF(indices),remap,table,16,first_use,8,&referenced)!=6 ||
        referenced!=8 || memcmp(first_use,representatives,sizeof(representatives)))return 0;
    for(unsigned int i=0;i<NUMBEROF(indices);++i)if(remap[indices[i]]!=expected[i])return 0;

    memset(remap,255,sizeof(remap));
    if(model_compact_remap_first_use(raw,(const void *)native,8,sizeof(*native),
        indices,NUMBEROF(indices),remap,table,16,first_use,5,&referenced)!=MODEL_COMPACT_NO_ROOM)return 0;
    const unsigned short invalid[]={7,2,8};
    memset(remap,255,sizeof(remap));
    if(model_compact_remap_first_use(raw,(const void *)native,8,sizeof(*native),
        invalid,NUMBEROF(invalid),remap,table,16,first_use,8,&referenced)!=MODEL_COMPACT_INVALID_INDEX)return 0;
    n3ds_log("PASS: model compaction first-use order, exact packed/GPU seams, capacity and index validation");
    return 1;
}
#endif

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
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
#include "engine_model_cooked.inl"
#endif
static void model_compact_release(void)
{
    if(model_compact_bytes) assert(!n3ds_gpu_frame_active() && n3ds_gpu_texture_barrier());
    for(unsigned int i=0;i<MODEL_COMPACT_SLOTS;++i)n3ds_gpu_geometry_free(model_compact_meshes[i].vertices);
    memset(model_compact_meshes,0,sizeof(model_compact_meshes));
    memset(model_compact_rejected,0,sizeof(model_compact_rejected));
    model_compact_bytes=model_compact_next=0;model_compact_failure_frame=0;model_compact_failed=0;
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
    model_cooked_release();
#endif
}
static const struct model_compact_mesh *model_compact_get(const void *source,const void *index_source,
    unsigned int triangles,short triangle_type,const struct decoded_stream *stream,
    const unsigned short *indices,unsigned int index_count)
{
    const unsigned int stride=stream->rigid_node>=0?sizeof(struct native_render_vertex):sizeof(struct native_skin_vertex);
    struct model_compact_key key={source,index_source,stream->count,triangles,index_count,stride,triangle_type};
    struct model_compact_mesh *slot=NULL;
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_REQUESTS,1);
    n3ds_algorithm_profile_max(N3DS_ALGORITHM_GPU_MODEL_COMPACT_RESIDENT_PEAK,model_compact_bytes);
    for(unsigned int i=0;i<MODEL_COMPACT_SLOTS;++i){
        struct model_compact_mesh *candidate=model_compact_meshes+i;
        if(candidate->vertices && model_compact_key_equal(&candidate->key,&key)){
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_HITS,1);
            return candidate;
        }
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
    unsigned int used=0,referenced=0,remap_bytes=stream->count*sizeof(unsigned short);
    const unsigned short *representatives=NULL;
    const struct model_compact_mesh *result=NULL;
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    unsigned short *table=NULL,*first_use=NULL;
#endif
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    const long long start=n3ds_engine_ticks();
#endif
    unsigned short *remap=(malloc)(remap_bytes);
    if(!remap)goto pressure;
    n3ds_algorithm_profile_max(N3DS_ALGORITHM_GPU_MODEL_COMPACT_SCRATCH_PEAK,remap_bytes);
    memset(remap,255,remap_bytes);
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
    if(stream->address==source) {
        const struct model_cooked_record *plan=model_cooked_find(source,index_source,stream->count,triangles,triangle_type,index_count);
        if(plan) {
            representatives=model_cooked_remap(plan,stream,stride,indices,remap,&referenced);
            if(representatives) {
                used=plan->used;
                n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COOKED_VALIDATED,1);
            } else {
                memset(remap,255,remap_bytes);
                n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COOKED_INVALID,1);
            }
        }
    }
#endif
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    if(!representatives && stream->address==source) {
        /* The original 4 KiB saving rule and resident budget bound the
         * number of unique tuples worth keeping and the table load. */
        unsigned int capacity=MIN(budget-model_compact_bytes-index_bytes,source_bytes-index_bytes-4096)/stride;
        if(!capacity)goto done;
        unsigned int slots=2;
        while(slots<capacity*2)slots*=2;
        unsigned int scratch_bytes=(slots+capacity)*sizeof(*table);
        table=(malloc)(scratch_bytes);
        if(table) {
            n3ds_algorithm_profile_max(N3DS_ALGORITHM_GPU_MODEL_COMPACT_SCRATCH_PEAK,remap_bytes+scratch_bytes);
            first_use=table+slots;
            int unique=model_compact_remap_first_use(source,(const void *)stream->local,stream->count,stride,
                indices,index_count,remap,table,slots,first_use,capacity,&referenced);
            if(unique==MODEL_COMPACT_NO_ROOM || unique==MODEL_COMPACT_INVALID_INDEX)goto done;
            if(unique>=0) {
                used=unique;
                representatives=first_use;
            }
            else {
                free(table);
                table=first_use=NULL;
                memset(remap,255,remap_bytes);
            }
        }
        if(!first_use)n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_FALLBACKS,1);
    }
#endif
    if(!representatives) {
        for(unsigned int i=0;i<index_count;++i){
            if(indices[i]>=stream->count)goto done;
            remap[indices[i]]=0;
        }
        for(unsigned int i=0;i<stream->count;++i)if(remap[i]!=65535)remap[i]=used++;
        referenced=used;
    }
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_REFERENCED,referenced);
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_DUPLICATES,referenced-used);
    unsigned int vertex_bytes=used*stride,index_offset=(vertex_bytes+3)&~3u;
    unsigned int bytes=index_offset+index_bytes;
    /* Pay for both compact streams and save at least one 4 KiB page against
     * the original vertex upload. Dense meshes retain the existing owner. */
    if(!used || bytes>budget || model_compact_bytes>budget-bytes ||
       bytes>source_bytes || source_bytes-bytes<4096)goto done;
    void *vertices=n3ds_gpu_geometry_optional_allocate(bytes);
    if(!vertices)goto pressure;
    unsigned short *out=(void *)((byte *)vertices+index_offset);
    if(representatives) {
        for(unsigned int i=0;i<used;++i)
            memcpy((byte *)vertices+i*stride,(const byte *)stream->local+representatives[i]*stride,stride);
    } else {
        for(unsigned int i=0;i<stream->count;++i)if(remap[i]!=65535)
            memcpy((byte *)vertices+remap[i]*stride,(const byte *)stream->local+i*stride,stride);
    }
    for(unsigned int i=0;i<index_count;++i)out[i]=remap[indices[i]];
#if HALO_N3DS_ALGO_MODEL_COMPACT == N3DS_ALGORITHM_VERIFY
    if(representatives && !model_compact_verify(source,(const void *)stream->local,vertices,indices,out,
        representatives,index_count,stride,used)) {
        n3ds_gpu_geometry_free(vertices);
        vhalt("Model compaction changed an indexed vertex tuple or first-use order");
        goto done;
    }
    if(representatives)n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_VERIFIED_CORNERS,index_count);
#endif
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    free(table);
    table=NULL;
#endif
    free(remap);
    remap=NULL;
    n3ds_gpu_geometry_flush(vertices,bytes);
    *slot=(struct model_compact_mesh){key,vertices,out};model_compact_bytes+=bytes;
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_BUILDS,1);
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_UPLOAD,bytes);
    n3ds_algorithm_profile_max(N3DS_ALGORITHM_GPU_MODEL_COMPACT_RESIDENT_PEAK,model_compact_bytes);
    memset(failed,0,sizeof(*failed));
    result=slot;
    goto done;
pressure:
    model_compact_failed=1;model_compact_failure_frame=frame;memset(failed,0,sizeof(*failed));
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_PRESSURE,1);
done:
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    free(table);
#endif
    free(remap);
    n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_MODEL_COMPACT_PREPARE,n3ds_engine_ticks()-start);
    return result;
}
