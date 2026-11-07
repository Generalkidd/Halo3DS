/* Bounded NXM1 fixture pack. A single BGR8 source chain supplies each mode. */
enum { LOD_PACK_HEADER_BYTES=32, LOD_PACK_ENTRY_BYTES=64, LOD_PACK_CAPACITY=4,
    LOD_PACK_CHAIN_BYTES=16320, LOD_PACK_TOP_BYTES=12288 };

static unsigned int lod_pack_word(const unsigned char *bytes)
{
    return (unsigned int)bytes[0]|(unsigned int)bytes[1]<<8|
        (unsigned int)bytes[2]<<16|(unsigned int)bytes[3]<<24;
}

static unsigned int lod_pack_hash(const void *data,unsigned int bytes)
{
    const unsigned char *source=data;
    unsigned int hash=2166136261u;
    for(unsigned int i=0;i<bytes;++i) hash=(hash^source[i])*16777619u;
    return hash;
}

static int lod_pack_entry_valid(const unsigned char *entry,unsigned int expected_offset)
{
    const unsigned char *end=memchr(entry,0,32);
    if(!end || end==entry || lod_pack_word(entry+32)!=expected_offset ||
       lod_pack_word(entry+36)!=LOD_PACK_CHAIN_BYTES || (lod_pack_word(entry+44)&3) ||
       lod_pack_word(entry+48)>10 || lod_pack_word(entry+52)!=64 ||
       lod_pack_word(entry+56)!=64 || lod_pack_word(entry+60)!=4) return 0;
    for(const unsigned char *p=entry;p<end;++p) if(*p<32 || *p>126) return 0;
    for(const unsigned char *p=end;p<entry+32;++p) if(*p) return 0;
    return 1;
}

int n3ds_gpu_texture_lod_load(const char *path,unsigned int mode,void *textures[4],unsigned int *count)
{
    static const struct { unsigned int size,levels,offset; } tiers[]={
        {64,1,0},{64,4,0},{32,3,LOD_PACK_TOP_BYTES}
    };
    if(!textures || !count) return 0;
    *count=0;
    memset(textures,0,LOD_PACK_CAPACITY*sizeof(*textures));
    if(!path || mode>=sizeof(tiers)/sizeof(tiers[0])) return 0;
    FILE *file=fopen(path,"rb");
    if(!file) return 0;

    unsigned char header[LOD_PACK_HEADER_BYTES];
    unsigned char entries[LOD_PACK_CAPACITY][LOD_PACK_ENTRY_BYTES];
    unsigned char *chain=NULL;
    int success=0;
    if(fread(header,1,sizeof(header),file)!=sizeof(header)) goto done;
    unsigned int images=lod_pack_word(header+8);
    if(!images || images>LOD_PACK_CAPACITY) goto done;
    unsigned int index_bytes=images*LOD_PACK_ENTRY_BYTES;
    unsigned int payload_start=LOD_PACK_HEADER_BYTES+index_bytes;
    unsigned int file_bytes=payload_start+images*LOD_PACK_CHAIN_BYTES;
    if(lod_pack_word(header)!=0x314d584eu || lod_pack_word(header+4)!=1 ||
       lod_pack_word(header+12)!=LOD_PACK_ENTRY_BYTES ||
       lod_pack_word(header+16)!=NATIVE_TEXTURE_FORMAT_RGB8 ||
       lod_pack_word(header+20)!=payload_start || lod_pack_word(header+24)!=file_bytes ||
       fread(entries,1,index_bytes,file)!=index_bytes ||
       lod_pack_hash(entries,index_bytes)!=lod_pack_word(header+28)) goto done;
    for(unsigned int i=0;i<images;++i) {
        if(!lod_pack_entry_valid(entries[i],payload_start+i*LOD_PACK_CHAIN_BYTES)) goto done;
        for(unsigned int previous=0;previous<i;++previous)
            if(!memcmp(entries[previous],entries[i],32)) goto done;
    }

    /* Validate the same complete source bytes in every mode. Scratch is
     * shared serially; only the selected level(s) take GPU-readable memory. */
    chain=malloc(LOD_PACK_CHAIN_BYTES);
    if(!chain) goto done;
    unsigned int size=tiers[mode].size,levels=tiers[mode].levels,offset=tiers[mode].offset;
    unsigned int resident_bytes=n3ds_texture_rgb8_mip_bytes(size,size,levels);
    unsigned int linear_bytes=0;
    if(!resident_bytes || resident_bytes>LOD_PACK_CHAIN_BYTES-offset) goto done;
    for(unsigned int i=0;i<images;++i) {
        if(fread(chain,1,LOD_PACK_CHAIN_BYTES,file)!=LOD_PACK_CHAIN_BYTES ||
           lod_pack_hash(chain,LOD_PACK_CHAIN_BYTES)!=lod_pack_word(entries[i]+40)) goto done;
        struct native_texture_descriptor descriptor;
        unsigned int source_mip=lod_pack_word(entries[i]+48)+(mode==2);
        if(!n3ds_texture_descriptor_rgb8_mips(&descriptor,size,size,levels,source_mip,
                resident_bytes,LOD_PACK_CHAIN_BYTES)) goto done;
        struct native_texture_span spans[6];
        textures[i]=n3ds_gpu_texture_reserve_descriptor(&descriptor,spans);
        if(!textures[i] || spans[0].bytes!=resident_bytes) goto done;
        size_t allocated=linearGetSize(spans[0].data);
        if(allocated<resident_bytes) goto done;
        linear_bytes+=allocated;
        memcpy(spans[0].data,chain+offset,resident_bytes);
        if(!n3ds_gpu_texture_finalize_descriptor(textures[i],&descriptor)) goto done;
    }
    if(fgetc(file)!=EOF || ferror(file)) goto done;
    *count=images;
    success=1;
done:
    free(chain);
    if(fclose(file)) success=0;
    if(!success) {
        for(unsigned int i=0;i<LOD_PACK_CAPACITY;++i) {
            n3ds_gpu_texture_abandon(textures[i]);
            textures[i]=NULL;
        }
        *count=0;
        return 0;
    }
    void n3ds_log(const char *);
    char message[240];
    for(unsigned int i=0;i<images;++i) {
        n3ds_gpu_texture_publish_immutable(textures[i]);
        snprintf(message,sizeof(message),
            "TEXTURE LOD ASSET: mode=%u label=%s bitmap_offset=%08x source_mip=%u size=%u levels=%u resident_bytes=%u chain_fnv=%08x",
            mode,(const char *)entries[i],lod_pack_word(entries[i]+44),
            lod_pack_word(entries[i]+48)+(mode==2),size,levels,resident_bytes,lod_pack_word(entries[i]+40));
        n3ds_log(message);
    }
    snprintf(message,sizeof(message),"TEXTURE LOD PACK: mode=%u textures=%u resident_bytes=%u",
        mode,images,images*resident_bytes);
    n3ds_log(message);
    snprintf(message,sizeof(message),"TEXTURE LOD LINEAR: mode=%u allocation_bytes=%u",
        mode,linear_bytes);
    n3ds_log(message);
    return 1;
}
