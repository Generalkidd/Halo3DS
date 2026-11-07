/* Optional setup-generated, GPU-ready archives. Candidate failures retain the
 * legacy archive and original map decoder. Workers receive copied spans. */
#include "engine_renderer_features.h"
#if HALO_N3DS_TEXTURE_COMPARE && (HALO_N3DS_TEXTURE_STORAGE != N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE || \
    defined(HALO_N3DS_TEXTURE_PREFETCH) || defined(HALO_N3DS_PRESENT_INTERPOLATION) || defined(HALO_N3DS_ARCHIVE_VERIFY))
#error "Texture comparison requires candidate archives and a regular-only loader"
#endif
enum { TEXTURE_ARCHIVE_KIND_LEGACY = 1, TEXTURE_ARCHIVE_KIND_CANDIDATE = 4 };
enum { TEXTURE_ARCHIVE_RAW = 0, TEXTURE_ARCHIVE_LZ4 = 1, TEXTURE_ARCHIVE_LZ4_LIMIT = 256*1024 };
struct texture_archive_selection { unsigned int offset,bytes,hash,kind,format,codec; };
struct texture_archive_entry {
    unsigned int bitmap_offset,resolution,source_offset,source_bytes;
    unsigned int dimensions,offset,bytes,hash;
};
static FILE *texture_archive;
static struct texture_archive_entry *texture_archive_index;
enum { TEXTURE_ARCHIVE_LEGACY = 1, TEXTURE_ARCHIVE_FILTERED_LIGHTMAPS = 2,
    TEXTURE_ARCHIVE_CURRENT_PLANS = 3 };
static unsigned int texture_archive_count,texture_archive_generation,texture_archive_attempted,texture_archive_version;
static unsigned int texture_archive_hits,texture_archive_misses;
static unsigned int texture_archive_legacy_hits,texture_archive_candidate_hits;
#if HALO_N3DS_TEXTURE_COMPARE
static struct {
    unsigned int mode,raw,lz4,rgba8,rgb8,la8;
    unsigned long long stored_bytes,resident_bytes;
    char map[sizeof(((struct n3ds_cache_view *)0)->header.name)];
} texture_compare;
static void texture_compare_record_publish(const struct texture_archive_selection *selection,unsigned int resident_bytes)
{
    if(!texture_compare.mode || selection->kind!=TEXTURE_ARCHIVE_KIND_CANDIDATE) return;
    if(selection->codec==TEXTURE_ARCHIVE_LZ4) ++texture_compare.lz4;
    else ++texture_compare.raw;
    switch(selection->format) {
        case NATIVE_TEXTURE_FORMAT_RGBA8: ++texture_compare.rgba8;break;
        case NATIVE_TEXTURE_FORMAT_RGB8: ++texture_compare.rgb8;break;
        case NATIVE_TEXTURE_FORMAT_LA8: ++texture_compare.la8;break;
    }
    texture_compare.stored_bytes+=selection->bytes;
    texture_compare.resident_bytes+=resident_bytes;
}
#endif
static void texture_archive_record_hit(unsigned int kind)
{
    ++texture_archive_hits;
    if(kind==TEXTURE_ARCHIVE_KIND_CANDIDATE) ++texture_archive_candidate_hits;
    else ++texture_archive_legacy_hits;
}
static int texture_data_path(const struct n3ds_cache_view *view,const char *suffix,char path[128])
{
    if(!view || !view->tags || !view->header.name[0]) return 0;
    unsigned int i;
    for(i=0;i<sizeof(view->header.name) && view->header.name[i];++i) {
        char c=view->header.name[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_')) return 0;
    }
    if(i==sizeof(view->header.name)) return 0;
    int length=snprintf(path,128,"sdmc:/halo-source/%s.%s",view->header.name,suffix);
    return length>0 && length<128;
}
static unsigned int texture_hash(const void *data,unsigned int bytes)
{
    const byte *p=data;unsigned int hash=2166136261U;
    for(unsigned int i=0;i<bytes;++i) hash=(hash^p[i])*16777619U;
    return hash;
}
static void texture_archive_legacy_close(void)
{
    if(texture_archive) {n3ds_files_lock();fclose(texture_archive);n3ds_files_unlock();texture_archive=NULL;}
    free(texture_archive_index);texture_archive_index=NULL;texture_archive_count=texture_archive_attempted=texture_archive_version=0;
}
#include "engine_texture_candidate.inl"
static void texture_archive_close(void)
{
#if HALO_N3DS_TEXTURE_COMPARE
    if(texture_compare.mode) {
        char message[240];
        snprintf(message,sizeof(message),
            "TEXTURE COMPARE: map=%s mode=%u raw=%u lz4=%u rgba8=%u rgb8=%u la8=%u stored_bytes=%llu resident_bytes=%llu",
            texture_compare.map,texture_compare.mode,
            texture_compare.raw,texture_compare.lz4,texture_compare.rgba8,texture_compare.rgb8,texture_compare.la8,
            texture_compare.stored_bytes,texture_compare.resident_bytes);
        n3ds_log(message);
        memset(&texture_compare,0,sizeof(texture_compare));
    }
#endif
    texture_archive_legacy_close();
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
    texture_candidate_close();
#endif
    if(texture_archive_hits || texture_archive_misses) {
        char message[160];snprintf(message,sizeof(message),
            "TEXTURE ARCHIVE: hits=%u legacy_hits=%u candidate_hits=%u fallback=%u",
            texture_archive_hits,texture_archive_legacy_hits,texture_archive_candidate_hits,texture_archive_misses);n3ds_log(message);
    }
    texture_archive_hits=texture_archive_misses=0;
    texture_archive_legacy_hits=texture_archive_candidate_hits=0;
}
static void texture_archive_legacy_open(void)
{
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    /* Renderer initialization precedes map publication. Do not attempt any
     * archive I/O or consume the generation's attempt before tags exist. */
    if(!v || !v->tags || !v->header.name[0]) return;
    unsigned int generation=n3ds_engine_tag_generation();
    if(texture_archive_attempted && texture_archive_generation==generation) return;
    texture_archive_legacy_close();texture_archive_attempted=1;texture_archive_generation=generation;
    char path[128];if(!texture_data_path(v,"ntx",path)) return;
    unsigned int h[8];long size;
    n3ds_files_lock();FILE *f=fopen(path,"rb");
    if(!f) goto done;
    if(fread(h,1,sizeof(h),f)!=sizeof(h) || h[0]!=0x31585448U ||
       (h[1]!=TEXTURE_ARCHIVE_LEGACY && h[1]!=TEXTURE_ARCHIVE_FILTERED_LIGHTMAPS && h[1]!=TEXTURE_ARCHIVE_CURRENT_PLANS) ||
       h[2]!=texture_hash(&v->header,sizeof(v->header)) || h[3]>16384 || !h[3] || h[6] || h[7] ||
       fseek(f,0,SEEK_END) || (size=ftell(f))<0 || (unsigned long)size!=h[5] ||
       h[5]<32+h[3]*32) goto fail;
    struct texture_archive_entry *index=(malloc)(h[3]*sizeof(*index));
    if(!index) goto fail;
    if(fseek(f,32,SEEK_SET) || fread(index,32,h[3],f)!=h[3] || texture_hash(index,h[3]*32)!=h[4]) {free(index);goto fail;}
    for(unsigned int i=0;i<h[3];++i) {
        struct texture_archive_entry *r=index+i;
        unsigned int w=r->dimensions&65535,hh=r->dimensions>>16;
        if(r->bitmap_offset>v->tag_bytes || sizeof(struct bitmap_data)>v->tag_bytes-r->bitmap_offset ||
           (r->resolution!=32 && r->resolution!=64 && r->resolution!=128 && r->resolution!=256 && r->resolution!=512) ||
           w<8 || hh<8 || w>512 || hh>512 || (w&(w-1)) || (hh&(hh-1)) || r->bytes!=w*hh*4 ||
           r->offset<32+h[3]*32 || r->offset>h[5] || r->bytes>h[5]-r->offset ||
           (i && (r[-1].bitmap_offset>r->bitmap_offset || (r[-1].bitmap_offset==r->bitmap_offset && r[-1].resolution>=r->resolution)))) {
            free(index);goto fail;
        }
    }
    texture_archive=f;texture_archive_index=index;texture_archive_count=h[3];texture_archive_version=h[1];
    {char message[128];snprintf(message,sizeof(message),"TEXTURE ARCHIVE: ready version=%u entries=%u index_bytes=%u",h[1],h[3],h[3]*32);n3ds_log(message);}
    goto done;
fail:
    fclose(f);n3ds_log("TEXTURE ARCHIVE: rejected; using map textures");
done:
    n3ds_files_unlock();
}
static void texture_archive_open(void)
{
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active()) return;
#endif
    texture_archive_legacy_open();
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
    /* Candidate directory preparation belongs to regular-frame idle time.
     * A first demand before that point continues through legacy/map data. */
    if(!n3ds_gpu_frame_active()) texture_candidate_open();
#endif
}
/* Only the render thread accesses this index. Copy the result before submitting
 * a worker job so archive closure never invalidates a worker's memory. */
static const struct texture_archive_entry *texture_archive_find(const struct bitmap_data *bitmap,unsigned int resolution,
    unsigned int source_offset,unsigned int source_bytes,unsigned int width,unsigned int height,int lightmap)
{
    if(!texture_archive || texture_archive_generation!=n3ds_engine_tag_generation() ||
       (lightmap && texture_archive_version<TEXTURE_ARCHIVE_FILTERED_LIGHTMAPS)) return NULL;
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    unsigned int key=(const byte *)bitmap-v->tags,lo=0,hi=texture_archive_count;
    while(lo<hi) {unsigned int mid=lo+(hi-lo)/2;const struct texture_archive_entry *r=texture_archive_index+mid;
        if(r->bitmap_offset<key || (r->bitmap_offset==key && r->resolution<resolution)) lo=mid+1;else hi=mid;}
    if(lo==texture_archive_count) return NULL;
    const struct texture_archive_entry *r=texture_archive_index+lo;
    if(r->bitmap_offset!=key || r->resolution!=resolution || r->source_offset!=source_offset ||
       r->source_bytes!=source_bytes || r->dimensions!=(width|(height<<16))) return NULL;
    return r;
}
static void texture_archive_reject_payload(unsigned int kind)
{
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
    if(kind==TEXTURE_ARCHIVE_KIND_CANDIDATE) {
        texture_candidate_close();texture_candidate_attempted=1;
        texture_candidate_generation=n3ds_engine_tag_generation();
        n3ds_log("TEXTURE CANDIDATE: payload rejected; using legacy/map textures");
        return;
    }
#endif
    if(kind!=TEXTURE_ARCHIVE_KIND_LEGACY) return;
    texture_archive_legacy_close();texture_archive_attempted=1;texture_archive_generation=n3ds_engine_tag_generation();
    n3ds_log("TEXTURE ARCHIVE: payload rejected; using map textures");
}
static int texture_archive_select(const struct bitmap_data *bitmap,unsigned int resolution,
    unsigned int source_offset,unsigned int source_bytes,unsigned int width,unsigned int height,int lightmap,
    unsigned int source_mip,unsigned int source_format,unsigned int source_width,unsigned int source_height,
    int allow_candidate,struct texture_archive_selection *selection)
{
    if(!selection) return 0;
    memset(selection,0,sizeof(*selection));
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    if(!bitmap || !v || !v->tags || n3ds_cache_resolve(v,bitmap,sizeof(*bitmap))!=bitmap) return 0;
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
    if(allow_candidate && texture_candidate_select(bitmap,resolution,source_offset,source_bytes,width,height,lightmap,
            source_mip,source_format,source_width,source_height,selection)) return 1;
#else
    (void)source_mip;(void)source_format;(void)source_width;(void)source_height;(void)allow_candidate;
#endif
    const struct texture_archive_entry *r=texture_archive_find(bitmap,resolution,source_offset,source_bytes,width,height,lightmap);
    if(!r) return 0;
    *selection=(struct texture_archive_selection){
        r->offset,r->bytes,r->hash,TEXTURE_ARCHIVE_KIND_LEGACY,NATIVE_TEXTURE_FORMAT_RGBA8,TEXTURE_ARCHIVE_RAW};
    return 1;
}
static unsigned int texture_archive_read_span(const struct texture_archive_selection *selected,void *out)
{
    FILE *file=texture_archive;
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
    if(selected->kind==TEXTURE_ARCHIVE_KIND_CANDIDATE) file=texture_candidate;
#endif
    unsigned int got=0;
    n3ds_files_lock();
    if(file && !fseek(file,selected->offset,SEEK_SET)) got=fread(out,1,selected->bytes,file);
    n3ds_files_unlock();
    return got;
}
static int texture_archive_read(const struct bitmap_data *bitmap,unsigned int resolution,
    unsigned int source_offset,unsigned int source_bytes,unsigned int width,unsigned int height,int lightmap,
    unsigned int source_mip,unsigned int source_format,unsigned int source_width,unsigned int source_height,
    int allow_candidate,void *out,unsigned int *bytes_read)
{
    *bytes_read=0;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active()) return 0;
#endif
    texture_archive_open();
    /* At most candidate then legacy. A failed read has only touched the same
     * unpublished RGBA8 destination; overwrite it before publishing anything. */
    for(unsigned int attempt=0;attempt<2;++attempt) {
        struct texture_archive_selection selected;
        if(!texture_archive_select(bitmap,resolution,source_offset,source_bytes,width,height,lightmap,
                source_mip,source_format,source_width,source_height,allow_candidate,&selected) ||
                selected.format!=NATIVE_TEXTURE_FORMAT_RGBA8 || selected.codec!=TEXTURE_ARCHIVE_RAW) break;
        unsigned int got=texture_archive_read_span(&selected,out);
        *bytes_read+=got;
        if(got==selected.bytes && texture_hash(out,selected.bytes)==selected.hash) {
            texture_archive_record_hit(selected.kind);
            return 1;
        }
        texture_archive_reject_payload(selected.kind);
        if(selected.kind!=TEXTURE_ARCHIVE_KIND_CANDIDATE) break;
    }
    ++texture_archive_misses;return 0;
}
