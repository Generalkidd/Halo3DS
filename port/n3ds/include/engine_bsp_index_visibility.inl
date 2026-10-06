/* Cache the exact visible subset of a material's immutable triangle list.
 * Each eye owns its storage; a changed view never overwrites queued indices.
 * Missing metadata or allocation headroom falls back to the full material. */
unsigned int n3ds_bsp_visible_indices(const unsigned short *,unsigned int,unsigned short *);
void n3ds_bsp_visibility_key(unsigned int *);
static unsigned int bsp_filter_bytes,bsp_filter_builds,bsp_filter_hits,bsp_filter_saved,bsp_filter_frame;
static const unsigned short *bsp_visible_index_buffer(struct native_geometry *g,unsigned int *count)
{
#ifdef HALO_N3DS_BSP_FILTER_OFF
 return g->indices;
#endif
 unsigned int key[MAXIMUM_CLUSTERS_PER_STRUCTURE/32];n3ds_bsp_visibility_key(key);
 unsigned int eye=n3ds_gpu_stereo_view();
 if(g->filter_ready[eye] && !memcmp(g->filter_key[eye],key,sizeof(key))){
  ++bsp_filter_hits;
 }else{
  if(g->filter_frame[eye]==bsp_filter_frame)return g->indices;
  unsigned int kept=n3ds_bsp_visible_indices(g->index_source,g->index_count,NULL);
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
    assert(n3ds_bsp_visible_indices(g->index_source,g->index_count,g->filtered[eye])==kept);
    n3ds_gpu_geometry_flush(g->filtered[eye],kept*2);
   }else kept=g->index_count; /* Cache low-memory fallback until visibility changes. */
  }
  g->filter_count[eye]=kept;memcpy(g->filter_key[eye],key,sizeof(key));g->filter_ready[eye]=1;++bsp_filter_builds;
 }
 g->filter_frame[eye]=bsp_filter_frame;
 *count=g->filter_count[eye];bsp_filter_saved+=(g->index_count-*count)/3;
 return *count<g->index_count?g->filtered[eye]:g->indices;
}
