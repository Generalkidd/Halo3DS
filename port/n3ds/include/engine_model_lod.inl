/* New3DS integration additions by Kevin Boggs.
 * Original-model-only, setup-generated visual LODs. All disk I/O happens at
 * tag load. First-person, UI, special shader and New-model paths stay stock.
 * Missing, incompatible or oversized packs fall back without changing maps. */
#define MODEL_LOD_PACK_BYTES (512u * 1024u)
#define MODEL_LOD_VERTEX_BYTES (256u * 1024u)
struct native_lod_entry {
    unsigned int vertex_offset, index_offset, source_vertices, source_triangles;
    unsigned int vertices, indices, offset, source_hash, payload_hash, strip;
};
struct native_lod_mesh {
    const struct native_lod_entry *entry;
    struct vertex_buffer vertices;
    struct triangle_buffer triangles;
    unsigned int saved_triangles;
    int tried;
};
static byte *native_lod_pack;
static unsigned int native_lod_count, native_lod_bytes, native_lod_hits, native_lod_saved;
static struct native_lod_mesh native_lod_meshes[128];

/* Kevin Boggs: native_lod_hash */
static unsigned int native_lod_hash(unsigned int hash,const void *data,unsigned int size)
{
    const byte *p=data;
    for(unsigned int i=0;i<size;++i)hash=(hash^p[i])*16777619u;
    return hash;
}

/* Kevin Boggs: native_lod_meshes_release */
static void native_lod_meshes_release(void)
{
    for(unsigned int i=0;i<128;++i)free(native_lod_meshes[i].vertices.base_address);
    memset(native_lod_meshes,0,sizeof(native_lod_meshes));native_lod_bytes=0;
}

/* Kevin Boggs: n3ds_model_lod_close */
void n3ds_model_lod_close(void)
{
    native_lod_meshes_release();free(native_lod_pack);native_lod_pack=NULL;native_lod_count=0;
    if(native_lod_hits) {
        char line[128];snprintf(line,sizeof(line),"ORIGINAL MODEL LODS: draws=%u triangles_saved=%u",native_lod_hits,native_lod_saved);n3ds_log(line);
    }
    native_lod_hits=native_lod_saved=0;
}

/* Kevin Boggs: n3ds_model_lod_prepare */
void n3ds_model_lod_prepare(void)
{
    extern int n3ds_input_platform_original_model(void);
    extern void n3ds_files_lock(void),n3ds_files_unlock(void);
    n3ds_model_lod_close();
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    if(!n3ds_input_platform_original_model() || !v || !v->tags || v->header.version!=5 || !strcmp(v->header.name,"ui"))return;
    for(unsigned int i=0;i<sizeof(v->header.name);++i) {
        char c=v->header.name[i];if(!c)break;
        if(i==sizeof(v->header.name)-1 || !((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return;
    }
    char path[128];snprintf(path,sizeof(path),"sdmc:/halo-source/%s.hld",v->header.name);
    unsigned int h[8];byte *pack=NULL;
    n3ds_files_lock();FILE *file=fopen(path,"rb");
    if(!file){n3ds_files_unlock();return;}
    if(fread(h,1,32,file)!=32 || h[0]!=0x31444c48u || h[1]!=1 || h[2]!=native_lod_hash(2166136261u,&v->header,sizeof(v->header)) ||
       h[3]>4096 || !h[3] || h[5]>MODEL_LOD_PACK_BYTES || h[5]<32+h[3]*40 || h[6] || h[7] ||
       fseek(file,0,SEEK_END) || ftell(file)!=(long)h[5] || fseek(file,0,SEEK_SET))goto done;
    pack=(malloc)(h[5]);if(!pack || fread(pack,1,h[5],file)!=h[5])goto done;
    if(native_lod_hash(2166136261u,pack+32,h[3]*40)!=h[4])goto done;
    struct native_lod_entry *entries=(void *)(pack+32);
    for(unsigned int i=0;i<h[3];++i) {
        const struct native_lod_entry *r=entries+i;
        if(!r->source_vertices || r->source_vertices>8192 || r->source_triangles>65535 || !r->vertices || r->vertices>=r->source_vertices ||
           r->indices<3 || r->indices%3 || r->indices>r->source_triangles*3 || r->strip!=1 || r->offset&1u ||
           r->offset<32+h[3]*40 || r->offset>h[5] || (r->vertices+r->indices)*2>h[5]-r->offset ||
           r->vertex_offset>v->tag_bytes || r->source_vertices*32>v->tag_bytes-r->vertex_offset ||
           r->index_offset>v->tag_bytes || (r->source_triangles+2)*2>v->tag_bytes-r->index_offset ||
           (i && (r[-1].vertex_offset>r->vertex_offset || (r[-1].vertex_offset==r->vertex_offset && r[-1].index_offset>=r->index_offset))))goto done;
        const unsigned short *remap=(const void *)(pack+r->offset),*indices=remap+r->vertices;
        if(native_lod_hash(2166136261u,remap,(r->vertices+r->indices)*2)!=r->payload_hash)goto done;
        for(unsigned int k=0;k<r->vertices;++k)if(remap[k]>=r->source_vertices)goto done;
        for(unsigned int k=0;k<r->indices;++k)if(indices[k]>=r->vertices)goto done;
    }
    native_lod_pack=pack;pack=NULL;native_lod_count=h[3];
    {char line[128];snprintf(line,sizeof(line),"ORIGINAL MODEL LODS: ready meshes=%u bytes=%u",h[3],h[5]);n3ds_log(line);}
done:
    free(pack);fclose(file);n3ds_files_unlock();
}

/* Kevin Boggs: native_lod_get */
static struct native_lod_mesh *native_lod_get(const struct vertex_buffer *vertices,
    const struct triangle_buffer *triangles)
{
    if(!native_lod_pack || first_person_active || vertices->type!=5 || triangles->type!=1)return NULL;
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    if(!v || n3ds_cache_resolve(v,vertices->base_address,vertices->count*32)!=vertices->base_address ||
       n3ds_cache_resolve(v,triangles->base_address,(triangles->count+2)*2)!=triangles->base_address)return NULL;
    unsigned int vo=(const byte *)vertices->base_address-v->tags,io=(const byte *)triangles->base_address-v->tags;
    const struct native_lod_entry *entries=(const void *)(native_lod_pack+32);
    unsigned int lo=0,hi=native_lod_count;
    while(lo<hi){unsigned int m=lo+(hi-lo)/2;if(entries[m].vertex_offset<vo || (entries[m].vertex_offset==vo && entries[m].index_offset<io))lo=m+1;else hi=m;}
    if(lo==native_lod_count)return NULL;
    const struct native_lod_entry *r=entries+lo;
    if(r->vertex_offset!=vo || r->index_offset!=io || r->source_vertices!=(unsigned int)vertices->count || r->source_triangles!=(unsigned int)triangles->count)return NULL;
    struct native_lod_mesh *slot=NULL;
    for(unsigned int i=0;i<128;++i) {
        struct native_lod_mesh *s=native_lod_meshes+i;
        if(s->entry==r)return s->vertices.base_address?s:NULL;
        if(!s->tried && !slot)slot=s;
    }
    if(!slot)return NULL;
    slot->tried=1;slot->entry=r;
    unsigned int bytes=r->vertices*32;
    if(bytes>MODEL_LOD_VERTEX_BYTES-native_lod_bytes)return NULL;
    unsigned int hash=native_lod_hash(2166136261u,vertices->base_address,r->source_vertices*32);
    if(native_lod_hash(hash,triangles->base_address,(r->source_triangles+2)*2)!=r->source_hash)return NULL;
    /* Strip connectors are already discarded by the stock renderer. Count
     * only nondegenerate triangles when reporting additional LOD savings. */
    const unsigned short *strip=triangles->base_address;
    unsigned int original_triangles=0;
    for(unsigned int i=0;i<r->source_triangles;++i)
        if(strip[i]!=strip[i+1] && strip[i]!=strip[i+2] && strip[i+1]!=strip[i+2])++original_triangles;
    if(r->indices/3>=original_triangles)return NULL;
    slot->saved_triangles=original_triangles-r->indices/3;
    struct model_vertex_compressed *out=(malloc)(bytes);if(!out)return NULL;
    const unsigned short *remap=(const void *)(native_lod_pack+r->offset);
    const struct model_vertex_compressed *source=vertices->base_address;
    for(unsigned int i=0;i<r->vertices;++i)out[i]=source[remap[i]];
    slot->vertices=(struct vertex_buffer){.type=5,.count=r->vertices,.base_address=out};
    slot->triangles=(struct triangle_buffer){.type=0,.count=r->indices/3,.base_address=(void *)(remap+r->vertices)};
    native_lod_bytes+=bytes;return slot;
}
