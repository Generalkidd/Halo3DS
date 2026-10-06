/* Associate immutable material batches with the original portal clusters.
 * A batch spanning several clusters is retained if ANY owner is visible.
 * Missing/incomplete metadata disables the optimization for that batch. */
struct native_visibility_cluster {
    byte prefix[52];struct tag_block subclusters;byte lens[4];
    struct tag_block surface_indices;byte tail[24];
};
struct native_visibility_subcluster {real_rectangle3d bounds;struct tag_block surfaces;};
_Static_assert(sizeof(struct native_visibility_cluster)==104,"Cluster visibility ABI");
static unsigned short *surface_cluster_owners;
static unsigned int surface_cluster_count;
static void surface_clusters_release(void){free(surface_cluster_owners);surface_cluster_owners=NULL;surface_cluster_count=0;}
/* Single-cluster triangles can be culled exactly. Shared or unassigned
 * triangles remain visible rather than guessing which portal owns them. */
unsigned int n3ds_bsp_visible_indices(const unsigned short *indices,unsigned int count,unsigned short *out)
{
 if(!surface_cluster_owners || !loaded_bsp)return count;
 uintptr_t base=(uintptr_t)loaded_bsp->surfaces.address,at=(uintptr_t)indices;
 if(at<base || (at-base)%6 || (at-base)/6>surface_cluster_count || count/3>surface_cluster_count-(at-base)/6)return count;
 unsigned int first=(at-base)/6,used=0;
 for(unsigned int i=0;i<count/3;++i){unsigned int cluster=surface_cluster_owners[first+i];
  if(cluster>=MAXIMUM_CLUSTERS_PER_STRUCTURE || (render.visible_cluster_flags[cluster/32]&(1u<<(cluster%32)))){
   if(out)memcpy(out+used,indices+i*3,6);used+=3;
  }
 }
 return used;
}
void n3ds_bsp_visibility_key(unsigned int key[MAXIMUM_CLUSTERS_PER_STRUCTURE/32])
{memcpy(key,render.visible_cluster_flags,MAXIMUM_CLUSTERS_PER_STRUCTURE/8);}
static int batch_cluster_visible(const struct mission_batch *b)
{
    if(!b->clusters_valid) return 1;
    for(unsigned int i=0;i<NUMBEROF(b->clusters);++i)
        if(b->clusters[i]&render.visible_cluster_flags[i]) return 1;
    return 0;
}
static int batch_add_surface(long surface,long cluster,const unsigned short *owners,unsigned short *seen,long surfaces)
{
    if(surface<0 || surface>=surfaces) return 0;
    if(seen[surface]==65535)seen[surface]=(unsigned short)cluster;
    else if(seen[surface]!=cluster)seen[surface]=65534;
    unsigned int owner=owners[surface];
    if(owner<(unsigned int)count) batches[owner].clusters[cluster/32]|=1u<<(cluster%32);
    return 1;
}
static void batch_clusters_prepare(struct structure_bsp *bsp)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(1);
    surface_clusters_release();
    for(long i=0;i<count;++i) {memset(batches[i].clusters,0,sizeof(batches[i].clusters));batches[i].clusters_valid=0;}
    if(!count || bsp->surfaces.count<=0 || bsp->surfaces.count>131072 || bsp->clusters.count<=0 ||
       bsp->clusters.count>MAXIMUM_CLUSTERS_PER_STRUCTURE) return;
    unsigned short *owners=(malloc)(bsp->surfaces.count*sizeof(*owners));
    unsigned short *seen=(malloc)(bsp->surfaces.count*sizeof(*seen));
    if(!owners || !seen) goto done;
    memset(seen,0xff,bsp->surfaces.count*sizeof(*seen));
    memset(owners,0xff,bsp->surfaces.count*sizeof(*owners));
    for(long i=0;i<count;++i) {
        struct structure_material *m=batches[i].material;
        for(long s=m->first_surface_index;s<m->first_surface_index+m->surface_count;++s) {
            if(owners[s]!=65535) goto done;
            owners[s]=(unsigned short)i;
        }
    }
    const struct native_visibility_cluster *clusters=n3ds_cache_resolve(view,bsp->clusters.address,bsp->clusters.count*sizeof(*clusters));
    if(!clusters) goto done;
    int subclusters=clusters[0].subclusters.count!=0;
    for(long c=0;c<bsp->clusters.count;++c) {
        const struct tag_block *block=subclusters?&clusters[c].subclusters:&clusters[c].surface_indices;
        unsigned int stride=subclusters?sizeof(struct native_visibility_subcluster):sizeof(long);
        if(block->count<0 || block->count>131072) goto done;
        const void *data=block->count?n3ds_cache_resolve(view,block->address,block->count*stride):NULL;
        if(block->count && !data) goto done;
        if(subclusters) {
            const struct native_visibility_subcluster *sub=data;
            for(long i=0;i<block->count;++i) {
                const struct tag_block *surfaces=&sub[i].surfaces;
                if(surfaces->count<0 || surfaces->count>131072) goto done;
                const long *list=surfaces->count?n3ds_cache_resolve(view,surfaces->address,surfaces->count*sizeof(long)):NULL;
                if(surfaces->count && !list) goto done;
                for(long j=0;j<surfaces->count;++j) if(!batch_add_surface(list[j],c,owners,seen,bsp->surfaces.count)) goto done;
            }
        } else {
            const long *list=data;
            for(long i=0;i<block->count;) {
                if(block->count-i<3 || list[i+2]<0 || list[i+2]>block->count-i-3) goto done;
                long end=i+3+list[i+2];i+=3;
                while(i<end) if(!batch_add_surface(list[i++],c,owners,seen,bsp->surfaces.count)) goto done;
            }
        }
    }
    unsigned int indexed=0;
    for(long i=0;i<count;++i) {
        const struct structure_material *m=batches[i].material;int complete=1;
        for(long s=m->first_surface_index;s<m->first_surface_index+m->surface_count;++s) if(seen[s]==65535) {complete=0;break;}
        batches[i].clusters_valid=complete;indexed+=complete;
    }
    {char message[128];snprintf(message,sizeof(message),"BSP PORTAL BATCHES: indexed=%u conservative=%ld",indexed,count-indexed);n3ds_log(message);}
    surface_cluster_owners=seen;surface_cluster_count=bsp->surfaces.count;seen=NULL;
done:
    free(owners);free(seen);
}
#ifdef HALO_N3DS_ARCHIVE_VERIFY
static int batch_visibility_test(void)
{
    structure_visibility_compute();
    for(long i=0;i<count;++i) if(!batch_cluster_visible(batches+i)) {
        const struct structure_material *m=batches[i].material;
        for(long s=m->first_surface_index;s<m->first_surface_index+m->surface_count;++s)
            if(BIT_VECTOR_TEST_FLAG(render.environment_surface_flags,s)) return 0;
    }
    if(surface_cluster_owners)for(unsigned int s=0;s<surface_cluster_count;++s){
        unsigned int c=surface_cluster_owners[s];
        if(c<MAXIMUM_CLUSTERS_PER_STRUCTURE && !(render.visible_cluster_flags[c/32]&(1u<<(c%32))) && BIT_VECTOR_TEST_FLAG(render.environment_surface_flags,s))return 0;
    }
    n3ds_structure_visibility_compute();return 1;
}
#endif
