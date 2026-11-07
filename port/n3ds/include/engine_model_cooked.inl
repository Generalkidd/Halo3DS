/* This archive owns CPU-only integer plans. The decoded ARM stream still
 * supplies every vertex attribute and the existing resident cache owns GPU
 * copies. Missing or invalid plans leave the normal compactor available. */
enum {
    MODEL_COOKED_MAGIC=0x31474d4e,
    MODEL_COOKED_VERSION=1,
    MODEL_COOKED_MAX_ENTRIES=512,
    MODEL_COOKED_EXACT=HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
};

struct model_cooked_header {
    uint32_t magic,version,policy,tag_bytes,tags_crc,header_crc,count,payload_bytes,payload_crc;
};
struct model_cooked_record {
    uint32_t key[5];
    uint32_t corners,used,payload_offset;
};
_Static_assert(sizeof(struct model_cooked_header)==36,"NMG1 header layout");
_Static_assert(sizeof(struct model_cooked_record)==32,"NMG1 record layout");

static struct {
    const byte *tags;
    unsigned int tag_bytes,count;
    struct model_cooked_record *entries;
} model_cooked;

static void model_cooked_release(void)
{
    free(model_cooked.entries);
    memset(&model_cooked,0,sizeof(model_cooked));
}

static uint32_t model_cooked_crc(const void *bytes,unsigned int count)
{
    unsigned long crc;
    crc_new(&crc);
    crc_checksum_buffer(&crc,bytes,count);
    return crc^0xffffffffu;
}

static int model_cooked_key_compare(const uint32_t a[5],const uint32_t b[5])
{
    for(unsigned int i=0;i<5;++i)if(a[i]!=b[i])return a[i]<b[i]?-1:1;
    return 0;
}

static int model_cooked_validate_archive(const struct model_cooked_header *header,
    const struct model_cooked_record *entries)
{
    unsigned int cursor=header->count*sizeof(*entries);
    for(unsigned int i=0;i<header->count;++i) {
        const struct model_cooked_record *entry=entries+i;
        unsigned int vertices=entry->key[2],triangles=entry->key[3],type=entry->key[4];
        if(!vertices || vertices>65535 || !triangles || triangles>65535 || type>1 ||
           entry->key[0]>=header->tag_bytes || entry->key[1]>=header->tag_bytes ||
           vertices*32>header->tag_bytes-entry->key[0] ||
           (type?(triangles+2)*2:triangles*6)>header->tag_bytes-entry->key[1] ||
           !entry->corners || entry->corners>triangles*3 || entry->corners%3 ||
           !entry->used || entry->used>vertices || entry->used>entry->corners ||
           entry->payload_offset!=cursor ||
           (i && model_cooked_key_compare(entries[i-1].key,entry->key)>=0))return 0;
        unsigned int bytes=(entry->used+entry->corners)*sizeof(unsigned short);
        if(bytes>header->payload_bytes-cursor)return 0;
        cursor+=bytes;
    }
    return cursor==header->payload_bytes;
}

void n3ds_model_geometry_load(const char *map_name,const struct n3ds_cache_view *view)
{
    model_cooked_release();
    char path[96];
    snprintf(path,sizeof(path),"sdmc:/halo-source/%s.nmg",map_name);
    FILE *file=fopen(path,"rb");
    if(!file)return;
    struct model_cooked_header header;
    struct model_cooked_record *entries=NULL;
    extern int n3ds_input_platform_original_model(void);
    unsigned int budget=n3ds_input_platform_original_model()?64*1024:256*1024;
    if(fread(&header,1,sizeof(header),file)!=sizeof(header) ||
       header.magic!=MODEL_COOKED_MAGIC || header.version!=MODEL_COOKED_VERSION ||
       header.policy!=MODEL_COOKED_EXACT || header.tag_bytes!=view->tag_bytes ||
       header.tags_crc!=view->source_tags_crc || header.header_crc!=model_cooked_crc(&view->header,sizeof(view->header)) ||
       header.count>MODEL_COOKED_MAX_ENTRIES || header.payload_bytes>budget-sizeof(header) ||
       header.payload_bytes<header.count*sizeof(*entries))goto done;
    if(!header.count)goto done;
    entries=(malloc)(header.payload_bytes);
    if(!entries || fread(entries,1,header.payload_bytes,file)!=header.payload_bytes ||
       fgetc(file)!=EOF || ferror(file) || header.payload_crc!=model_cooked_crc(entries,header.payload_bytes) ||
       !model_cooked_validate_archive(&header,entries))goto done;
    model_cooked.tags=view->tags;
    model_cooked.tag_bytes=view->tag_bytes;
    model_cooked.count=header.count;
    model_cooked.entries=entries;
    entries=NULL;
    char message[112];
    snprintf(message,sizeof(message),"MODEL COOKED: loaded %u plans, bytes=%lu policy=%u",
        model_cooked.count,(unsigned long)header.payload_bytes,(unsigned int)header.policy);
    n3ds_log(message);
done:
    if(!model_cooked.entries)n3ds_log("MODEL COOKED: archive unavailable or incompatible; using runtime");
    free(entries);
    fclose(file);
}

static const struct model_cooked_record *model_cooked_find(const void *source,const void *indices,
    unsigned int vertices,unsigned int triangles,unsigned int type,unsigned int corners)
{
    if(!model_cooked.entries)return NULL;
    uintptr_t base=(uintptr_t)model_cooked.tags,vertex=(uintptr_t)source,index=(uintptr_t)indices;
    if(vertex<base || vertex-base>=model_cooked.tag_bytes || index<base || index-base>=model_cooked.tag_bytes)return NULL;
    uint32_t key[]={vertex-base,index-base,vertices,triangles,type};
    unsigned int begin=0,end=model_cooked.count;
    while(begin<end) {
        unsigned int middle=begin+(end-begin)/2;
        const struct model_cooked_record *entry=model_cooked.entries+middle;
        int order=model_cooked_key_compare(key,entry->key);
        if(order<0)end=middle;
        else if(order>0)begin=middle+1;
        else return entry->corners==corners?entry:NULL;
    }
    return NULL;
}

static const unsigned short *model_cooked_remap(const struct model_cooked_record *entry,
    const struct decoded_stream *stream,unsigned int stride,const unsigned short *indices,
    unsigned short *remap,unsigned int *referenced)
{
    const unsigned short *first_use=(const void *)((const byte *)model_cooked.entries+entry->payload_offset);
    const unsigned short *output=first_use+entry->used;
    unsigned int next=0;
    *referenced=0;
    for(unsigned int i=0;i<entry->used;++i)
        if(first_use[i]>=stream->count || (!MODEL_COOKED_EXACT && i && first_use[i-1]>=first_use[i]))return NULL;
    for(unsigned int i=0;i<entry->corners;++i) {
        unsigned int source=indices[i],dest=output[i];
        if(source>=stream->count || dest>=entry->used)return NULL;
        unsigned int rep=first_use[dest];
        if(MODEL_COOKED_EXACT) {
            if(dest>next || (dest==next && rep!=source))return NULL;
            if(dest==next)++next;
        } else if(rep!=source)return NULL;
        if(remap[source]!=65535) {
            if(remap[source]!=dest)return NULL;
            continue;
        }
        if(source!=rep && (memcmp(stream->address+source,stream->address+rep,sizeof(*stream->address)) ||
           memcmp((const byte *)stream->local+source*stride,(const byte *)stream->local+rep*stride,stride)))return NULL;
        remap[source]=dest;
        ++*referenced;
    }
    return (MODEL_COOKED_EXACT?next:*referenced)==entry->used?first_use:NULL;
}
