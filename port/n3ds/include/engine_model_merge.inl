/* Adjacent opaque parts only. Cache storage lives until the map/stream cache
 * is released: no stale pointer reuse in either decoded or GPU index caches. */
#define MERGED_MODELS 24
static struct merged_model {
    const struct triangle_buffer *source_tri[16];
    const struct vertex_buffer *source_vert[16];
    unsigned int parts,bytes;
    struct triangle_buffer triangles;
    struct vertex_buffer vertices;
} merged_models[MERGED_MODELS];
static unsigned int merged_bytes,merged_saved_draws;
static const struct merged_model *active_merged;
void n3ds_model_merge_report(void)
{char text[128];snprintf(text,sizeof(text),"MODEL MERGE: avoided_draws=%u storage=%u",merged_saved_draws,merged_bytes);n3ds_log(text);}
static void merged_models_release(void)
{
    if(merged_saved_draws) {char text[160];snprintf(text,sizeof(text),"MODEL MERGE: avoided_draws=%u storage=%u",merged_saved_draws,merged_bytes);n3ds_log(text);}
    for(unsigned int i=0;i<MERGED_MODELS;++i) {free(merged_models[i].vertices.base_address);free(merged_models[i].triangles.base_address);}
    memset(merged_models,0,sizeof(merged_models));merged_bytes=merged_saved_draws=0;active_merged=NULL;
}
static struct merged_model *merged_model_get(const struct triangle_buffer *const *tri,const struct vertex_buffer *const *vert,unsigned int parts,int map_owned)
{
    struct merged_model *slot=NULL;
    for(unsigned int k=0;k<MERGED_MODELS;++k) {
        struct merged_model *m=merged_models+k;
        if(m->parts==parts && !memcmp(m->source_tri,tri,parts*sizeof(*tri)) && !memcmp(m->source_vert,vert,parts*sizeof(*vert)))return m->bytes?m:NULL;
        if(!m->parts && !slot)slot=m;
    }
    if(!slot)return NULL;
    memcpy(slot->source_tri,tri,parts*sizeof(*tri));memcpy(slot->source_vert,vert,parts*sizeof(*vert));slot->parts=parts;
    const struct n3ds_cache_view *tags=n3ds_engine_cache_view(0);
    unsigned int vertices=0,indices=0,bones=0;unsigned long long bone_mask=0;
    for(unsigned int p=0;p<parts;++p) {
        if(map_owned && (n3ds_cache_resolve(tags,vert[p],sizeof(*vert[p]))!=vert[p] || n3ds_cache_resolve(tags,tri[p],sizeof(*tri[p]))!=tri[p]))return NULL;
        if(vert[p]->type!=5 || vert[p]->count<=0 || tri[p]->count<=0 || tri[p]->count>16384 || (tri[p]->type!=0 && tri[p]->type!=1))return NULL;
        vertices+=vert[p]->count;indices+=tri[p]->count*3;
        if(vertices>4096 || indices>49152)return NULL;
        const struct model_vertex_compressed *v=vert[p]->base_address;
        unsigned int bytes=(tri[p]->type?tri[p]->count+2:tri[p]->count*3)*2;
        if(map_owned && (n3ds_cache_resolve(tags,v,vert[p]->count*sizeof(*v))!=v || n3ds_cache_resolve(tags,tri[p]->base_address,bytes)!=tri[p]->base_address))return NULL;
        for(unsigned int i=0;i<(unsigned int)vert[p]->count;++i) {
            struct model_vertex_uncompressed decoded;if(!n3ds_model_decode_vertex(v+i,&decoded))return NULL;
            for(int k=0;k<2;++k) if(decoded.node_weights[k]) {
                if(decoded.nodes[k]<0 || decoded.nodes[k]>=64)return NULL;
                unsigned long long bit=1ULL<<decoded.nodes[k];if(!(bone_mask&bit)){bone_mask|=bit;if(++bones>NATIVE_GPU_MODEL_BONES)return NULL;}
            }
        }
    }
    extern int n3ds_input_platform_original_model(void);
    unsigned int bytes=vertices*sizeof(struct model_vertex_compressed)+indices*2;
    unsigned int budget=n3ds_input_platform_original_model()?128*1024:384*1024;
    if(bytes>budget || merged_bytes>budget-bytes)return NULL;
    struct model_vertex_compressed *data=(malloc)(vertices*sizeof(*data));unsigned short *out=(malloc)(indices*2);
    if(!data || !out){free(data);free(out);return NULL;}
    unsigned int base=0,used=0;
    for(unsigned int p=0;p<parts;++p) {
        long n=n3ds_model_triangle_list(tri[p]->type,tri[p]->base_address,tri[p]->count,vert[p]->count,out+used,indices-used);
        if(n<0){free(data);free(out);return NULL;}
        for(long i=0;i<n;++i)out[used+i]+=base;
        memcpy(data+base,vert[p]->base_address,vert[p]->count*sizeof(*data));base+=vert[p]->count;used+=n;
    }
    if(!used){free(data);free(out);return NULL;}
#ifdef HALO_N3DS_CROWD_VERIFY
    /* Verify the actual tagged meshes, including strip winding/degenerates:
     * every triangle addresses exactly the same compressed vertex bytes. */
    unsigned int checked=0;
    for(unsigned int p=0;p<parts;++p) {
        long n=n3ds_model_triangle_list(tri[p]->type,tri[p]->base_address,tri[p]->count,vert[p]->count,index_scratch,65535*3);
        assert(n>=0 && checked+n<=used);
        for(long i=0;i<n;++i)assert(!memcmp(data+out[checked+i],(const struct model_vertex_compressed *)vert[p]->base_address+index_scratch[i],sizeof(*data)));
        checked+=n;
    }
    assert(checked==used);n3ds_log("PASS: merged model preserves ordered triangle vertices and strip winding");
#endif
    memcpy(slot->source_tri,tri,parts*sizeof(*tri));memcpy(slot->source_vert,vert,parts*sizeof(*vert));
    slot->parts=parts;slot->bytes=bytes;merged_bytes+=bytes;
    slot->vertices=(struct vertex_buffer){.type=5,.count=vertices,.base_address=data};
    slot->triangles=(struct triangle_buffer){.type=0,.count=used/3,.base_address=out};
    return slot;
}
int n3ds_model_draw_merged(struct shader *shader,short permutation,const struct triangle_buffer *const *tri,const struct vertex_buffer *const *vert,unsigned int parts)
{
    if(parts<2 || parts>16 || depth_only || parameters.effect.type || (shader->base.type!=3 && shader->base.type!=4))return 0;
    struct merged_model *m=merged_model_get(tri,vert,parts,1);if(!m)return 0;
    active_merged=m;
    _rasterizer_model_draw(shader,permutation,&m->triangles,NONE,m->triangles.count,&m->vertices,NONE);
    active_merged=NULL;merged_saved_draws+=parts-1;return 1;
}
int n3ds_model_merge_tests(void)
{
    n3ds_model_decode_cache_flush();
    /* Pure mesh preparation test; the playable entry always validates map
     * ownership. No synthetic buffers are passed into the live draw path. */
    struct model_vertex_compressed raw[40]={0};
    for(unsigned int i=0;i<40;++i){raw[i].position.x=i;raw[i].nodes[0]=0;raw[i].nodes[1]=253;raw[i].node_weight=32767;raw[i].normal=0x3ff;}
    unsigned short strip[]={0,1,2,3},list[]={0,2,1};
    struct vertex_buffer v[2]={{.type=5,.count=4,.base_address=raw},{.type=5,.count=3,.base_address=raw+4}};
    struct triangle_buffer t[2]={{.type=1,.count=2,.base_address=strip},{.type=0,.count=1,.base_address=list}};
    const struct vertex_buffer *vp[]={v,v+1};const struct triangle_buffer *tp[]={t,t+1};
    struct merged_model *m=merged_model_get(tp,vp,2,0);
    const unsigned short expected[]={0,1,2,2,1,3,4,6,5};
    if(!m || m->vertices.count!=7 || m->triangles.count!=3 || memcmp(expected,m->triangles.base_address,sizeof(expected)) || memcmp(raw,m->vertices.base_address,7*sizeof(*raw)))return 0;
    if(merged_model_get(tp,vp,2,0)!=m)return 0;
    merged_models_release();
    /* A union exceeding PICA's bone uniform limit must keep separate draws. */
    for(unsigned int i=0;i<40;++i)raw[i].nodes[0]=i*3;
    v[0].count=20;v[1].count=20;v[1].base_address=raw+20;
    if(merged_model_get(tp,vp,2,0))return 0;
    merged_models_release();v[0].count=65536;
    if(merged_model_get(tp,vp,2,0))return 0;
    merged_models_release();n3ds_log("PASS: model merge strip winding, vertex rebasing, cache reuse, bone and size limits");return 1;
}
