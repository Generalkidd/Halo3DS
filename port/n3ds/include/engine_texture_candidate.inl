/* Optional .ntx4 reader. Schema 1 retains the original RGBA8 2D contract.
 * Schema 2 selects one of three independent representations per map. */
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
#include "engine_texture_catalog.h"
enum { TEXTURE_CANDIDATE_HEADER_BYTES=128, TEXTURE_CANDIDATE_ENTRY_BYTES=96,
    TEXTURE_CANDIDATE_MAX_ENTRIES=16384,
    TEXTURE_CANDIDATE_PAYLOAD_START=128+16384*96 };
static FILE *texture_candidate;
static byte *texture_candidate_index;
static unsigned int texture_candidate_count,texture_candidate_attempted,texture_candidate_generation;
static unsigned int texture_candidate_u32(const byte *p)
{
    return (unsigned int)p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24;
}
static void texture_candidate_close(void)
{
    if(texture_candidate) {n3ds_files_lock();fclose(texture_candidate);n3ds_files_unlock();texture_candidate=NULL;}
    free(texture_candidate_index);texture_candidate_index=NULL;
    texture_candidate_count=texture_candidate_attempted=0;
}
static int texture_candidate_identity(const struct n3ds_cache_view *v,const byte *sha,unsigned int bytes)
{
    /* Setup hashes the full expanded map while preparing the archive. Runtime
     * compares that recorded identity to the compiled catalogue, then checks
     * the live header and the already-validated, pre-relocation tag CRC. It
     * never adds whole-map hashing to texture demand or archive opening. */
    for(unsigned int i=0;i<NUMBEROF(texture_archive_catalog);++i) {
        const struct texture_archive_catalog_entry *r=texture_archive_catalog+i;
        if(r->bytes==bytes && !strcmp(r->name,v->header.name) && !memcmp(r->sha256,sha,32)) return 1;
    }
    return 0;
}
#if HALO_N3DS_TEXTURE_COMPARE
static int texture_candidate_compact_allowed(const char *map,unsigned int bitmap_offset)
{
    /* Catalogued retail BSP lightmaps with no CPU readers of the native
     * image. Original light sampling uses base_address, which stays NULL
     * for both native representations. Match setup's eligibility. */
    static const unsigned int a10[][2]={
        {0xd6e94c,30},{0xd73288,38},{0xd75488,36},{0xd77270,38},{0xd7867c,50},
        {0xd7a0bc,47},{0xd7ca60,54},{0xd7d6ac,7},{0xd801bc,9}
    };
    static const unsigned int b30[][2]={{0xeeee8c,26},{0xefcc80,29}};
    const unsigned int (*ranges)[2];
    unsigned int count;
    if(!strcmp(map,"a10")) {ranges=a10;count=NUMBEROF(a10);}
    else if(!strcmp(map,"b30")) {ranges=b30;count=NUMBEROF(b30);}
    else return 0;
    for(unsigned int i=0;i<count;++i) {
        if(bitmap_offset<ranges[i][0]) continue;
        unsigned int distance=bitmap_offset-ranges[i][0];
        if(distance%sizeof(struct bitmap_data)==0 && distance/sizeof(struct bitmap_data)<ranges[i][1]) return 1;
    }
    return 0;
}
#endif
static int texture_candidate_entry_valid(const struct n3ds_cache_view *v,const byte *r,
    unsigned int file_bytes,unsigned int mode)
{
    unsigned int bitmap_offset=texture_candidate_u32(r),resolution=texture_candidate_u32(r+4);
    unsigned int source_offset=texture_candidate_u32(r+8),source_bytes=texture_candidate_u32(r+12);
    unsigned int width=texture_candidate_u32(r+16),height=texture_candidate_u32(r+20);
    unsigned int mip=texture_candidate_u32(r+36),resident=texture_candidate_u32(r+48);
    unsigned int offset=texture_candidate_u32(r+56),bytes=texture_candidate_u32(r+60);
    unsigned int format=texture_candidate_u32(r+68),sw=texture_candidate_u32(r+72),sh=texture_candidate_u32(r+76);
    unsigned int lightmap=texture_candidate_u32(r+84),native_format=texture_candidate_u32(r+24);
    unsigned int cpu=texture_candidate_u32(r+44),codec=texture_candidate_u32(r+88);
    unsigned int native_stride=4;
    if(native_format==NATIVE_TEXTURE_FORMAT_RGBA8) {
        if(cpu!=(NATIVE_TEXTURE_CPU_RGBA8|NATIVE_TEXTURE_CPU_UNKNOWN)) return 0;
    }
#if HALO_N3DS_TEXTURE_COMPARE
    else {
        if(mode!=3 || cpu || !lightmap || format!=6 ||
            !texture_candidate_compact_allowed(v->header.name,bitmap_offset)) return 0;
        if(native_format==NATIVE_TEXTURE_FORMAT_RGB8) native_stride=3;
        else if(native_format==NATIVE_TEXTURE_FORMAT_LA8) native_stride=2;
        else return 0;
    }
#else
    else return 0;
#endif
    if(bitmap_offset>v->tag_bytes || sizeof(struct bitmap_data)>v->tag_bytes-bitmap_offset ||
       (resolution!=32 && resolution!=64 && resolution!=128 && resolution!=256 && resolution!=512) ||
       width<8 || height<8 || width>512 || height>512 || (width&(width-1)) || (height&(height-1)) ||
       texture_candidate_u32(r+28)!=1 || texture_candidate_u32(r+32)!=1 ||
       mip>10 || texture_candidate_u32(r+40)!=NATIVE_TEXTURE_CONVERSION_RGBA8 ||
       sw<4 || sh<4 || sw>1024 || sh>1024 || (sw&(sw-1)) || (sh&(sh-1)) ||
       width!=MAX(8,MIN(sw,resolution)) || height!=MAX(8,MIN(sh,resolution)) ||
       lightmap>1 || (lightmap && format!=6) || resident!=width*height*native_stride || !bytes ||
       offset<TEXTURE_CANDIDATE_PAYLOAD_START || (offset&3) || offset>file_bytes || bytes>file_bytes-offset ||
       source_offset<2048 || source_offset>(unsigned int)v->header.file_length ||
       source_bytes>(unsigned int)v->header.file_length-source_offset) return 0;
    if(codec==TEXTURE_ARCHIVE_RAW) {
        if(bytes!=resident) return 0;
    } else if(mode!=2 || codec!=TEXTURE_ARCHIVE_LZ4 || bytes>=resident || bytes>TEXTURE_ARCHIVE_LZ4_LIMIT) return 0;
    unsigned int canonical_hash=texture_candidate_u32(r+92);
    if(mode==0 ? canonical_hash!=0 : native_format==NATIVE_TEXTURE_FORMAT_RGBA8 &&
        canonical_hash!=texture_candidate_u32(r+64)) return 0;
    int raw=format<=3 || format==6 || (format>=8 && format<=11) || format==17;
    if(!raw && (format<14 || format>16)) return 0;
    unsigned int stride=(format<=2 || format==17)?1:format>=10?4:2;
    unsigned int expected=raw?sw*sh*stride:((sw+3)/4)*((sh+3)/4)*(format==14?8:16);
    if(source_bytes!=expected || texture_candidate_u32(r+52)!=((source_bytes+3)&~3u)+(lightmap?width*height*2:0)) return 0;
    /* These first 32 bytes contain only source metadata. The following tag,
     * slot, hardware and base pointers are relocated/mutated at runtime. */
    const byte *bitmap=v->tags+bitmap_offset;
    if(texture_hash(bitmap,32)!=texture_candidate_u32(r+80)) return 0;
    return 1;
}
static void texture_candidate_open(void)
{
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    if(!v || !v->tags || !v->header.name[0] || n3ds_gpu_frame_active()) return;
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active()) return;
#endif
    unsigned int generation=n3ds_engine_tag_generation();
    if(texture_candidate_attempted && texture_candidate_generation==generation) return;
    texture_candidate_close();texture_candidate_attempted=1;texture_candidate_generation=generation;
    char path[128];if(!texture_data_path(v,"ntx4",path)) return;
    byte h[TEXTURE_CANDIDATE_HEADER_BYTES];byte *index=NULL;long size;
    n3ds_files_lock();FILE *f=fopen(path,"rb");
    if(!f) goto done;
    if(fread(h,1,sizeof(h),f)!=sizeof(h)) goto fail;
    unsigned int count=texture_candidate_u32(h+40),file_bytes=texture_candidate_u32(h+56);
    unsigned int header_hash=texture_candidate_u32(h+96);
    unsigned int schema=texture_candidate_u32(h+4),mode=texture_candidate_u32(h+100);
    memset(h+96,0,4);
    if(schema==1) {
        if(mode) goto fail;
    }
#if HALO_N3DS_TEXTURE_COMPARE
    else if(schema!=2 || mode<1 || mode>3) goto fail;
#else
    else goto fail;
#endif
    if(texture_candidate_u32(h)!=0x34585448U ||
       texture_candidate_u32(h+8)!=TEXTURE_CANDIDATE_HEADER_BYTES ||
       texture_candidate_u32(h+12)!=TEXTURE_CANDIDATE_ENTRY_BYTES ||
       texture_candidate_u32(h+16)!=1 || texture_candidate_u32(h+20)!=0x01020304U ||
       texture_candidate_u32(h+24)!=(unsigned int)v->header.file_length ||
       texture_candidate_u32(h+28)!=texture_hash(&v->header,sizeof(v->header)) ||
       texture_candidate_u32(h+32)!=v->tag_bytes || texture_candidate_u32(h+36)!=v->source_tags_crc ||
       !count || count>TEXTURE_CANDIDATE_MAX_ENTRIES || texture_candidate_u32(h+44)!=sizeof(h) ||
       texture_candidate_u32(h+48)!=count*TEXTURE_CANDIDATE_ENTRY_BYTES ||
       texture_candidate_u32(h+52)!=TEXTURE_CANDIDATE_PAYLOAD_START ||
       file_bytes<TEXTURE_CANDIDATE_PAYLOAD_START || file_bytes>0x7fffffffU ||
       !texture_candidate_identity(v,h+64,texture_candidate_u32(h+24)) ||
       texture_hash(h,sizeof(h))!=header_hash || fseek(f,0,SEEK_END) ||
       (size=ftell(f))<0 || (unsigned long)size!=file_bytes) goto fail;
    for(unsigned int i=104;i<sizeof(h);++i) if(h[i]) goto fail;
    unsigned int index_bytes=count*TEXTURE_CANDIDATE_ENTRY_BYTES;
    index=(malloc)(index_bytes);if(!index) goto fail;
    if(fseek(f,sizeof(h),SEEK_SET) || fread(index,1,index_bytes,f)!=index_bytes ||
       texture_hash(index,index_bytes)!=texture_candidate_u32(h+60)) goto fail;
    for(unsigned int i=0;i<count;++i) {
        const byte *r=index+i*TEXTURE_CANDIDATE_ENTRY_BYTES;
        if(!texture_candidate_entry_valid(v,r,file_bytes,mode)) goto fail;
        if(i) {
            unsigned int key=texture_candidate_u32(r),previous=texture_candidate_u32(r-TEXTURE_CANDIDATE_ENTRY_BYTES);
            if(previous>key || (previous==key &&
               texture_candidate_u32(r-TEXTURE_CANDIDATE_ENTRY_BYTES+4)>=texture_candidate_u32(r+4))) goto fail;
        }
    }
    texture_candidate=f;texture_candidate_index=index;texture_candidate_count=count;
#if HALO_N3DS_TEXTURE_COMPARE
    texture_compare.mode=mode;
    memcpy(texture_compare.map,v->header.name,sizeof(texture_compare.map));
#endif
    {char message[144];snprintf(message,sizeof(message),"TEXTURE CANDIDATE: ready map=%s schema=%u mode=%u entries=%u index_bytes=%u",
        v->header.name,schema,mode,count,index_bytes);n3ds_log(message);}
    goto done;
fail:
    free(index);fclose(f);n3ds_log("TEXTURE CANDIDATE: rejected; using legacy/map textures");
done:
    n3ds_files_unlock();
}
static int texture_candidate_select(const struct bitmap_data *bitmap,unsigned int resolution,
    unsigned int source_offset,unsigned int source_bytes,unsigned int width,unsigned int height,int lightmap,
    unsigned int source_mip,unsigned int source_format,unsigned int source_width,unsigned int source_height,
    struct texture_archive_selection *selection)
{
    if(!texture_candidate || texture_candidate_generation!=n3ds_engine_tag_generation()) return 0;
    const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);
    unsigned int key=(const byte *)bitmap-v->tags,lo=0,hi=texture_candidate_count;
    while(lo<hi) {
        unsigned int mid=lo+(hi-lo)/2;const byte *r=texture_candidate_index+mid*TEXTURE_CANDIDATE_ENTRY_BYTES;
        unsigned int entry_key=texture_candidate_u32(r);
        if(entry_key<key || (entry_key==key && texture_candidate_u32(r+4)<resolution)) lo=mid+1;else hi=mid;
    }
    if(lo==texture_candidate_count) return 0;
    const byte *r=texture_candidate_index+lo*TEXTURE_CANDIDATE_ENTRY_BYTES;
    if(texture_candidate_u32(r)!=key || texture_candidate_u32(r+4)!=resolution ||
       texture_candidate_u32(r+8)!=source_offset || texture_candidate_u32(r+12)!=source_bytes ||
       texture_candidate_u32(r+16)!=width || texture_candidate_u32(r+20)!=height ||
       texture_candidate_u32(r+36)!=source_mip || texture_candidate_u32(r+68)!=source_format ||
       texture_candidate_u32(r+72)!=source_width || texture_candidate_u32(r+76)!=source_height ||
       texture_candidate_u32(r+80)!=texture_hash(bitmap,32) || texture_candidate_u32(r+84)!=(unsigned int)lightmap) return 0;
    *selection=(struct texture_archive_selection){texture_candidate_u32(r+56),texture_candidate_u32(r+60),
        texture_candidate_u32(r+64),TEXTURE_ARCHIVE_KIND_CANDIDATE,texture_candidate_u32(r+24),texture_candidate_u32(r+88)};
    return 1;
}
#endif
