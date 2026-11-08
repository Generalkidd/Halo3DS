/* Associate immutable material batches with the original portal clusters.
 * A batch spanning several clusters is retained if ANY owner is visible.
 * Missing/incomplete metadata disables the optimization for that batch. */
#include "engine_algorithm_profile.h"
struct native_visibility_cluster {
    byte prefix[52];struct tag_block subclusters;byte lens[4];
    struct tag_block surface_indices;byte tail[24];
};
struct native_visibility_subcluster {real_rectangle3d bounds;struct tag_block surfaces;};
_Static_assert(sizeof(struct native_visibility_cluster)==104,"Cluster visibility ABI");
static unsigned short *surface_cluster_owners;
static unsigned int surface_cluster_count;
enum { BATCH_CANDIDATE_WORDS=32, BATCH_INDEX_RUN_BYTES=128*1024 };
static unsigned int *batch_cluster_candidates;
static unsigned int batch_candidate_words,batch_candidate_clusters;
static struct batch_candidate_view {
    unsigned int visible[MAXIMUM_CLUSTERS_PER_STRUCTURE/32];
    unsigned int materials[BATCH_CANDIDATE_WORDS];
    int valid;
} batch_candidate_views[2];
static unsigned int batch_candidate_next;
static struct native_bsp_index_run *batch_index_runs;
static void surface_clusters_release(void)
{
    free(surface_cluster_owners);surface_cluster_owners=NULL;surface_cluster_count=0;
    free(batch_cluster_candidates);batch_cluster_candidates=NULL;batch_candidate_words=batch_candidate_clusters=0;
    memset(batch_candidate_views,0,sizeof(batch_candidate_views));batch_candidate_next=0;
    free(batch_index_runs);batch_index_runs=NULL;
}
/* Single-cluster triangles can be culled exactly. Shared or unassigned
 * triangles remain visible rather than guessing which portal owns them. */
unsigned int n3ds_bsp_visible_indices(const unsigned short *indices,unsigned int count,unsigned short *out)
{
 if(!surface_cluster_owners || !loaded_bsp)return count;
 uintptr_t base=(uintptr_t)loaded_bsp->surfaces.address,at=(uintptr_t)indices;
 if(at<base || (at-base)%6 || (at-base)/6>surface_cluster_count || count/3>surface_cluster_count-(at-base)/6)return count;
 unsigned int first=(at-base)/6,used=0,span=0;
 n3ds_algorithm_profile_add(N3DS_ALG_CPU_INDEX_SCAN,count/3);
 for(unsigned int i=0;i<count/3;++i){unsigned int cluster=surface_cluster_owners[first+i];
  if(cluster>=MAXIMUM_CLUSTERS_PER_STRUCTURE || (render.visible_cluster_flags[cluster/32]&(1u<<(cluster%32)))){
   used+=3;
  }else{
   /* Consecutive retained triangles are already contiguous in the immutable
    * source. Copy the whole span instead of issuing one six-byte copy each. */
   unsigned int retained=i-span;
   if(out && retained)memcpy(out+used-retained*3,indices+span*3,retained*6);
   span=i+1;
  }
 }
 unsigned int retained=count/3-span;
 if(out && retained)memcpy(out+used-retained*3,indices+span*3,retained*6);
 if(out)n3ds_algorithm_profile_add(N3DS_ALG_CPU_INDEX_COPY,used*sizeof(*out));
 return used;
}
static int batch_cluster_visible(const struct mission_batch *b)
{
    if(!b->clusters_valid) return 1;
    for(unsigned int i=0;i<NUMBEROF(b->clusters);++i)
        if(b->clusters[i]&render.visible_cluster_flags[i]) return 1;
    return 0;
}
static void batch_candidates_prepare(unsigned int clusters)
{
    unsigned int words=((unsigned int)count+31)/32;
    if(words>BATCH_CANDIDATE_WORDS) return;
    unsigned int *rows=(calloc)(clusters+1,words*sizeof(*rows));
    if(!rows) return;
    unsigned int *conservative=rows+clusters*words;
    for(unsigned int batch=0;batch<(unsigned int)count;++batch) {
        unsigned int bit=1u<<(batch%32);
        if(!batches[batch].clusters_valid) {
            conservative[batch/32]|=bit;
            continue;
        }
        for(unsigned int word=0;word<(clusters+31)/32;++word) {
            unsigned int owners=batches[batch].clusters[word];
            while(owners) {
                unsigned int cluster=word*32+__builtin_ctz(owners);
                if(cluster<clusters) rows[cluster*words+batch/32]|=bit;
                owners&=owners-1;
            }
        }
    }
    batch_cluster_candidates=rows;batch_candidate_words=words;batch_candidate_clusters=clusters;
}
/* OR the exact coarse ownership rows and keep unknown material batches. Use
 * the original visibility mask, including the camera-outside-BSP fallback. */
static const unsigned int *batch_view_candidates(void)
{
    /* Dense or unrestricted views can cost more to expand than to scan. */
    if(!batch_cluster_candidates || render.cluster_index==NONE || render.rendered_cluster_count>32) {
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_BATCH_BYPASS,1);return NULL;
    }
    const unsigned int words=batch_candidate_words;
    const unsigned int visible_words=(batch_candidate_clusters+31)/32;
    const unsigned int visible_bytes=visible_words*sizeof(batch_candidate_views[0].visible[0]);
    /* Camera motion can retain the same portal set. Cache both eye sets so
     * stereo and optional views reuse the exact expansion across frames. */
    for(unsigned int i=0;i<NUMBEROF(batch_candidate_views);++i) {
        const struct batch_candidate_view *view=batch_candidate_views+i;
        if(view->valid && !memcmp(view->visible,render.visible_cluster_flags,visible_bytes)) {
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_BATCH_HIT,1);
            return view->materials;
        }
    }
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_BATCH_MISS,1);
    struct batch_candidate_view *view=batch_candidate_views+batch_candidate_next;
    batch_candidate_next=(batch_candidate_next+1)%NUMBEROF(batch_candidate_views);
    unsigned int *candidates=view->materials;
    memcpy(candidates,batch_cluster_candidates+batch_candidate_clusters*words,words*sizeof(*candidates));
    for(unsigned int word=0;word<visible_words;++word) {
        unsigned int visible=render.visible_cluster_flags[word];
        while(visible) {
            unsigned int cluster=word*32+__builtin_ctz(visible);
            if(cluster<batch_candidate_clusters) {
                const unsigned int *row=batch_cluster_candidates+cluster*words;
                for(unsigned int i=0;i<words;++i) candidates[i]|=row[i];
            }
            visible&=visible-1;
        }
    }
    memcpy(view->visible,render.visible_cluster_flags,visible_bytes);view->valid=1;
    return candidates;
}
/* Ascending set-bit order preserves the authored material submission order. */
static long batch_next_candidate(const unsigned int *candidates,long first)
{
    if(!candidates || first>=count) return first;
    unsigned int word=(unsigned int)first/32;
    unsigned int remaining=candidates[word]&(~0u<<(first%32));
    while(!remaining) {
        if(++word>=batch_candidate_words) return count;
        remaining=candidates[word];
    }
    return word*32+__builtin_ctz(remaining);
}
static unsigned short batch_run_owner(unsigned short owner)
{
    return owner<MAXIMUM_CLUSTERS_PER_STRUCTURE?owner:MAXIMUM_CLUSTERS_PER_STRUCTURE;
}
static void batch_index_runs_prepare(const unsigned short *owners,unsigned int total)
{
    if(!total) return;
    batch_index_runs=(malloc)(total*sizeof(*batch_index_runs));
    if(!batch_index_runs) return;
    unsigned int used=0;
    for(long i=0;i<count;++i) {
        struct mission_batch *batch=batches+i;
        if(!batch->index_run_count) continue;
        const struct structure_material *m=batch->material;
        const unsigned short *surface=owners+m->first_surface_index;
        batch->index_runs=batch_index_runs+used;
        for(long s=0;s<m->surface_count;) {
            unsigned short owner=batch_run_owner(surface[s]);
            long end=s+1;
            while(end<m->surface_count && batch_run_owner(surface[end])==owner) ++end;
            assert(used<total);
            batch_index_runs[used++]=(struct native_bsp_index_run){(unsigned int)(end-s)*3,owner};
            s=end;
        }
    }
    assert(used==total);
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
    for(long i=0;i<count;++i) {
        memset(batches[i].clusters,0,sizeof(batches[i].clusters));batches[i].clusters_valid=0;
        memset(batches[i].single_owner_clusters,0,sizeof(batches[i].single_owner_clusters));
        batches[i].index_runs=NULL;batches[i].index_run_count=0;
    }
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
    unsigned int indexed=0,total_runs=0;
    int new_model=!n3ds_input_platform_original_model();
    for(long i=0;i<count;++i) {
        struct mission_batch *batch=batches+i;
        const struct structure_material *m=batch->material;
        int complete=1;
        unsigned int runs=0,previous=~0u;
        for(long s=m->first_surface_index;s<m->first_surface_index+m->surface_count;++s) {
            unsigned int owner=batch_run_owner(seen[s]);
            if(seen[s]==65535) complete=0;
            if(owner<MAXIMUM_CLUSTERS_PER_STRUCTURE) batch->single_owner_clusters[owner/32]|=1u<<(owner%32);
            if(owner!=previous) {++runs;previous=owner;}
        }
        batch->clusters_valid=complete;indexed+=complete;
        /* A run table must be materially smaller than the owner stream.
         * Fragmented materials and low memory retain the exact triangle scan. */
        if(new_model && batch->shader->base.type!=7 && runs*sizeof(*batch_index_runs)<(unsigned int)m->surface_count*2 &&
           runs<=BATCH_INDEX_RUN_BYTES/sizeof(*batch_index_runs)-total_runs) {
            batch->index_run_count=runs;total_runs+=runs;
        }
    }
    if(new_model) batch_candidates_prepare(bsp->clusters.count);
    batch_index_runs_prepare(seen,total_runs);
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
