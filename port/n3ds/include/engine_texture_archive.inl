/* Optional setup-generated, GPU-ready RGBA8 archive. Never written by the
 * console. Bad/missing files fall back to the original map decoder. */
struct texture_archive_entry {
    unsigned int bitmap_offset,resolution,source_offset,source_bytes;
    unsigned int dimensions,offset,bytes,hash;
};
static FILE *texture_archive;
static struct texture_archive_entry *texture_archive_index;
static unsigned int texture_archive_count,texture_archive_generation,texture_archive_attempted;
static unsigned int texture_archive_hits,texture_archive_misses;
static unsigned int texture_hash(const void *data,unsigned int bytes)
{
    const byte *p=data;unsigned int hash=2166136261U;
    for(unsigned int i=0;i<bytes;++i) hash=(hash^p[i])*16777619U;
    return hash;
}
static void texture_archive_close(void)
{
    if(texture_archive) {n3ds_files_lock();fclose(texture_archive);n3ds_files_unlock();texture_archive=NULL;}
    free(texture_archive_index);texture_archive_index=NULL;texture_archive_count=texture_archive_attempted=0;
    if(texture_archive_hits || texture_archive_misses) {
        char message[128];snprintf(message,sizeof(message),"TEXTURE ARCHIVE: hits=%u fallback=%u",texture_archive_hits,texture_archive_misses);n3ds_log(message);
    }
    texture_archive_hits=texture_archive_misses=0;
}
static void texture_archive_open(void)
{
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    unsigned int generation=n3ds_engine_tag_generation();
    if(texture_archive_attempted && texture_archive_generation==generation) return;
    texture_archive_close();texture_archive_attempted=1;texture_archive_generation=generation;
    if(!v || !v->tags) return;
    for(unsigned int i=0;v->header.name[i];++i) {
        char c=v->header.name[i];if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_')) return;
    }
    char path[128];snprintf(path,sizeof(path),"sdmc:/halo-source/%s.ntx",v->header.name);
    unsigned int h[8];long size;
    n3ds_files_lock();FILE *f=fopen(path,"rb");
    if(!f) goto done;
    if(fread(h,1,sizeof(h),f)!=sizeof(h) || h[0]!=0x31585448U || h[1]!=1 ||
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
    texture_archive=f;texture_archive_index=index;texture_archive_count=h[3];
    {char message[128];snprintf(message,sizeof(message),"TEXTURE ARCHIVE: ready entries=%u index_bytes=%u",h[3],h[3]*32);n3ds_log(message);}
    goto done;
fail:
    fclose(f);n3ds_log("TEXTURE ARCHIVE: rejected; using map textures");
done:
    n3ds_files_unlock();
}
static int texture_archive_read(const struct bitmap_data *bitmap,unsigned int resolution,
    unsigned int source_offset,unsigned int source_bytes,unsigned int width,unsigned int height,void *out)
{
    texture_archive_open();
    if(!texture_archive) return 0;
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    unsigned int key=(const byte *)bitmap-v->tags,lo=0,hi=texture_archive_count;
    while(lo<hi) {unsigned int mid=lo+(hi-lo)/2;const struct texture_archive_entry *r=texture_archive_index+mid;
        if(r->bitmap_offset<key || (r->bitmap_offset==key && r->resolution<resolution)) lo=mid+1;else hi=mid;}
    if(lo==texture_archive_count) {++texture_archive_misses;return 0;}
    const struct texture_archive_entry *r=texture_archive_index+lo;
    if(r->bitmap_offset!=key || r->resolution!=resolution || r->source_offset!=source_offset ||
       r->source_bytes!=source_bytes || r->dimensions!=(width|(height<<16))) {++texture_archive_misses;return 0;}
    n3ds_files_lock();int ok=!fseek(texture_archive,r->offset,SEEK_SET) && fread(out,1,r->bytes,texture_archive)==r->bytes;
    n3ds_files_unlock();
    if(ok && texture_hash(out,r->bytes)==r->hash) {++texture_archive_hits;return 1;}
    texture_archive_close();texture_archive_attempted=1;texture_archive_generation=n3ds_engine_tag_generation();
    n3ds_log("TEXTURE ARCHIVE: payload rejected; using map textures");return 0;
}
