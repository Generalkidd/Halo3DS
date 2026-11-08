/* Cache the exact visible subset of a material's immutable triangle list.
 * Each eye owns its storage; a changed view never overwrites queued indices.
 * Missing metadata or allocation headroom falls back to the full material. */
unsigned int n3ds_bsp_visible_indices(const unsigned short *,unsigned int,unsigned short *);
static unsigned int bsp_filter_bytes,bsp_filter_builds,bsp_filter_hits,bsp_filter_saved,bsp_filter_frame;
static unsigned int bsp_visible_material_indices(const struct native_geometry *g,
    const struct native_bsp_material_visibility *visibility,const unsigned int *key,unsigned short *out)
{
    if(!visibility->runs) {
        unsigned int used=n3ds_bsp_visible_indices(g->index_source,g->index_count,out);
        if(out && g->vertex_remap)for(unsigned int i=0;i<used;++i) {
            unsigned int v=g->vertex_remap[out[i]];assert(v<g->vertex_count);out[i]=v;
        }
        return used;
    }
    /* Runs describe offsets, not vertex IDs. The resident full list retains
     * those offsets/order and is already in the compact vertex index space. */
    const unsigned short *source=g->indices;
    unsigned int offset=0,used=0,span=0;
    for(unsigned int i=0;i<visibility->run_count;++i) {
        const struct native_bsp_index_run *run=visibility->runs+i;
        unsigned int owner=run->cluster;
        assert(offset<=g->index_count && run->index_count<=g->index_count-offset);
        if(owner>=MAXIMUM_CLUSTERS_PER_STRUCTURE || (key[owner/32]&(1u<<(owner%32)))) {
            used+=run->index_count;
        } else {
            /* Adjacent visible ownership runs still form one source span.
             * Keep their exact triangle order with fewer small copies. */
            unsigned int retained=offset-span;
            if(out && retained) memcpy(out+used-retained,source+span,retained*sizeof(*out));
            span=offset+run->index_count;
        }
        offset+=run->index_count;
    }
    unsigned int retained=offset-span;
    if(out && retained) memcpy(out+used-retained,source+span,retained*sizeof(*out));
    assert(offset==g->index_count);
    return used;
}
static const unsigned short *bsp_visible_index_buffer(struct native_geometry *g,unsigned int *count,
    const struct native_bsp_material_visibility *visibility)
{
#ifdef HALO_N3DS_BSP_FILTER_OFF
 return g->indices;
#endif
 if(!visibility) return g->indices;
 unsigned int key[MAXIMUM_CLUSTERS_PER_STRUCTURE/32],hidden=0;
 for(unsigned int i=0;i<NUMBEROF(key);++i) {
  unsigned int owners=visibility->single_owner_clusters[i];
  key[i]=owners & render.visible_cluster_flags[i];
  hidden|=owners^key[i];
 }
 /* Shared/unassigned triangles are always included; only a missing unique
  * owner can remove an index. Unrelated visible clusters never dirty this key. */
 if(!hidden) return g->indices;
 unsigned int eye=n3ds_gpu_stereo_view();
 if(g->filter_ready[eye] && !memcmp(g->filter_key[eye],key,sizeof(key))){
  ++bsp_filter_hits;
 }else{
  if(g->filter_frame[eye]==bsp_filter_frame)return g->indices;
  unsigned int kept=bsp_visible_material_indices(g,visibility,key,NULL);
  /* A tiny reduction does not justify an extra resident index stream. */
  if(kept && kept*4>g->index_count*3)kept=g->index_count;
  if(kept && kept<g->index_count){
   if(!g->filtered[eye]){
    unsigned int bytes=g->index_count*2,budget=n3ds_input_platform_original_model()?192*1024:1024*1024;
    if(bytes<=budget && bsp_filter_bytes<=budget-bytes)
     g->filtered[eye]=n3ds_gpu_geometry_optional_allocate(bytes);
    if(g->filtered[eye])bsp_filter_bytes+=bytes;
   }
   if(g->filtered[eye]){
    assert(bsp_visible_material_indices(g,visibility,key,g->filtered[eye])==kept);
    n3ds_gpu_geometry_flush(g->filtered[eye],kept*2);
   }else kept=g->index_count; /* Cache low-memory fallback until visibility changes. */
  }
  g->filter_count[eye]=kept;memcpy(g->filter_key[eye],key,sizeof(key));g->filter_ready[eye]=1;++bsp_filter_builds;
 }
 g->filter_frame[eye]=bsp_filter_frame;
 *count=g->filter_count[eye];bsp_filter_saved+=(g->index_count-*count)/3;
 return *count<g->index_count?g->filtered[eye]:g->indices;
}
