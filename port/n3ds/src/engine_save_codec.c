/* New3DS integration additions by Kevin Boggs.
 * Independently compressed, immutable blocks shared by consecutive snapshots.
 * See vendor/lz4/LICENSE (unmodified upstream codec). The disk format is unchanged. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define LZ4_HEAPMODE 1
#include "../vendor/lz4/lz4.c"
enum { SAVE_BLOCK=32768, SAVE_LIMIT=0x345000, SAVE_BLOCKS=(SAVE_LIMIT+SAVE_BLOCK-1)/SAVE_BLOCK };
struct save_block { unsigned int refs,packed,checksum; unsigned char data[]; };
struct save_snapshot {
    unsigned int refs,bytes,packed,checksum;
    struct save_block *blocks[SAVE_BLOCKS];
};
unsigned int n3ds_save_memory_crc(const void *,unsigned int,const void *,unsigned int);
int n3ds_input_platform_original_model(void);
void n3ds_save_snapshot_release(const void *);

/* Kevin Boggs: snapshot_checksum */
static unsigned int snapshot_checksum(const struct save_snapshot *s)
{
    unsigned int metadata[2+SAVE_BLOCKS*2]={s->bytes,s->packed};
    for(unsigned int i=0;i<(s->bytes+SAVE_BLOCK-1)/SAVE_BLOCK;++i) {
        metadata[2+i*2]=s->blocks[i]->packed;
        metadata[3+i*2]=s->blocks[i]->checksum;
    }
    return n3ds_save_memory_crc(metadata,sizeof(metadata),NULL,0);
}
/* Kevin Boggs: n3ds_save_snapshot_validate */
int n3ds_save_snapshot_validate(const void *snapshot)
{
    const struct save_snapshot *s=snapshot;
    if(!s || !s->bytes || s->bytes>SAVE_LIMIT || s->packed>SAVE_LIMIT+4096)return 0;
    unsigned int total=0;
    for(unsigned int i=0;i<(s->bytes+SAVE_BLOCK-1)/SAVE_BLOCK;++i) {
        const struct save_block *b=s->blocks[i];
        if(!b || !b->packed || b->packed>(unsigned int)LZ4_compressBound(SAVE_BLOCK) ||
           b->checksum!=n3ds_save_memory_crc(b->data,b->packed,NULL,0))return 0;
        total+=b->packed;
    }
    return total==s->packed && s->checksum==snapshot_checksum(s);
}
unsigned int n3ds_save_pack_bound(void){return LZ4_compressBound(SAVE_BLOCK);}
unsigned int n3ds_save_pack(const void *src,unsigned int bytes,void *dst,unsigned int capacity)
{return bytes<=SAVE_BLOCK?LZ4_compress_default(src,dst,bytes,capacity):0;}
int n3ds_save_unpack(const void *src,unsigned int bytes,void *dst,unsigned int size)
{return bytes && bytes<=LZ4_compressBound(SAVE_BLOCK) && size<=SAVE_BLOCK && LZ4_decompress_safe(src,dst,bytes,size)==(int)size;}
/* Kevin Boggs: n3ds_save_snapshot_read */
int n3ds_save_snapshot_read(const void *snapshot,unsigned int offset,void *out,unsigned int bytes)
{
    const struct save_snapshot *s=snapshot;
    if(!s || offset%SAVE_BLOCK || offset>=s->bytes || bytes!=(s->bytes-offset>SAVE_BLOCK?SAVE_BLOCK:s->bytes-offset))return 0;
    const struct save_block *b=s->blocks[offset/SAVE_BLOCK];
    return b && n3ds_save_unpack(b->data,b->packed,out,bytes);
}
/* Kevin Boggs: snapshot_zero_block */
static int snapshot_zero_block(const unsigned char *source,unsigned int bytes)
{
    /* The arena and block offsets are aligned, but retain a portable fallback
     * for callers supplying another buffer. Word loads avoid bytewise scans. */
    while(bytes && ((uintptr_t)source&3u)){if(*source++)return 0;--bytes;}
    while(bytes>=16) {
        const uint32_t *words=(const uint32_t *)(const void *)source;
        if(words[0]|words[1]|words[2]|words[3])return 0;
        source+=16;bytes-=16;
    }
    while(bytes--){if(*source++)return 0;}
    return 1;
}
/* Kevin Boggs: n3ds_save_snapshot_create_reusing */
void *n3ds_save_snapshot_create_reusing(const void *input,unsigned int bytes,const void *previous)
{
    if(!input || !bytes || bytes>SAVE_LIMIT)return NULL;
    const struct save_snapshot *old=previous;
    if(old && old->bytes!=bytes)old=NULL;
    struct save_snapshot *s=calloc(1,sizeof(*s));
    unsigned char *packed=malloc(LZ4_compressBound(SAVE_BLOCK));
    struct save_block *zero_block=NULL;
    LZ4_stream_t *codec=malloc(sizeof(*codec));
    if(!s || !packed || !codec || !LZ4_initStream(codec,sizeof(*codec))) {
        free(s);free(packed);free(codec);return NULL;
    }
    unsigned int limit=n3ds_input_platform_original_model()?1536*1024:SAVE_LIMIT+4096;
    s->refs=1;s->bytes=bytes;
    for(unsigned int offset=0,i=0;offset<bytes;offset+=SAVE_BLOCK,++i) {
        unsigned int n=bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
        const unsigned char *source=(const unsigned char *)input+offset;
        int zero=n==SAVE_BLOCK && snapshot_zero_block(source,n);
        /* Empty arena blocks dominate campaign saves. Reuse them without a
         * decompress/compare pass over several megabytes. Other blocks are
         * compared only after compression, when the buffers are much smaller. */
        if(zero && zero_block) {
            s->blocks[i]=zero_block;++zero_block->refs;
        } else {
            unsigned int count=LZ4_compress_fast_extState_fastReset(codec,(const char *)source,(char *)packed,n,LZ4_compressBound(SAVE_BLOCK),1);
            if(!count || s->packed+count>limit)goto fail;
            unsigned int crc=n3ds_save_memory_crc(packed,count,NULL,0);
            struct save_block *b=old?old->blocks[i]:NULL;
            if(b && b->packed==count && b->checksum==crc && !memcmp(b->data,packed,count)) {
                ++b->refs;
            } else {
                b=malloc(sizeof(*b)+count);if(!b)goto fail;
                b->refs=1;b->packed=count;memcpy(b->data,packed,count);b->checksum=crc;
            }
            s->blocks[i]=b;
            if(zero)zero_block=b;
        }
        s->packed+=s->blocks[i]->packed;
        if(s->packed>limit)goto fail;
    }
    s->checksum=snapshot_checksum(s);free(packed);free(codec);return s;
fail:
    free(packed);free(codec);n3ds_save_snapshot_release(s);return NULL;
}
void *n3ds_save_snapshot_create(const void *input,unsigned int bytes)
{return n3ds_save_snapshot_create_reusing(input,bytes,NULL);}
void n3ds_save_snapshot_retain(const void *snapshot){if(snapshot)++((struct save_snapshot *)snapshot)->refs;}
/* Kevin Boggs: n3ds_save_snapshot_release */
void n3ds_save_snapshot_release(const void *snapshot)
{
    struct save_snapshot *s=(void *)snapshot;
    /* Snapshot/block reference counts are owned by the main thread; workers
     * only read their retained immutable snapshot, and are joined before release. */
    if(s && !--s->refs) {
        for(unsigned int i=0;i<SAVE_BLOCKS;++i)if(s->blocks[i] && !--s->blocks[i]->refs)free(s->blocks[i]);
        free(s);
    }
}
unsigned int n3ds_save_snapshot_size(const void *snapshot)
{const struct save_snapshot *s=snapshot;return s?s->packed+sizeof(*s)+((s->bytes+SAVE_BLOCK-1)/SAVE_BLOCK)*sizeof(struct save_block):0;}
int n3ds_save_snapshot_matches(const void *snapshot,const void *input)
{
    const struct save_snapshot *s=snapshot;unsigned char *block=malloc(SAVE_BLOCK);int ok=s && block;
    if(ok)for(unsigned int offset=0;offset<s->bytes;offset+=SAVE_BLOCK){unsigned int n=s->bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
        if(!n3ds_save_snapshot_read(s,offset,block,n) || memcmp(block,(const unsigned char *)input+offset,n)){ok=0;break;}}
    free(block);return ok;
}
/* Kevin Boggs: n3ds_save_codec_tests */
int n3ds_save_codec_tests(void)
{
    unsigned int bytes=SAVE_BLOCK*2+113;
    unsigned char *source=malloc(bytes),*output=malloc(SAVE_BLOCK);
    if(!source || !output){free(source);free(output);return 0;}
    unsigned int seed=123;for(unsigned int i=0;i<bytes;++i){seed=seed*1664525u+1013904223u;source[i]=(i%7)?0:seed>>24;}
    struct save_snapshot *s=n3ds_save_snapshot_create(source,bytes);
    int ok=s && n3ds_save_snapshot_validate(s) && n3ds_save_snapshot_matches(s,source);
    struct save_snapshot *next=NULL;
    if(ok){source[SAVE_BLOCK+900]^=1;next=n3ds_save_snapshot_create_reusing(source,bytes,s);
        ok=next && n3ds_save_snapshot_validate(next) && n3ds_save_snapshot_matches(next,source) &&
           next->blocks[0]==s->blocks[0] && next->blocks[1]!=s->blocks[1] && next->blocks[2]==s->blocks[2] &&
           !n3ds_save_snapshot_matches(s,source);}
    if(ok){n3ds_save_snapshot_retain(s);n3ds_save_snapshot_release(s);
        ok=n3ds_save_snapshot_read(next,SAVE_BLOCK,output,SAVE_BLOCK) && !memcmp(output,source+SAVE_BLOCK,SAVE_BLOCK) && !n3ds_save_snapshot_read(s,1,output,10);}
    n3ds_save_snapshot_release(s);
    if(ok){ok=n3ds_save_snapshot_validate(next) && n3ds_save_snapshot_matches(next,source);
        next->blocks[0]->data[0]^=1;ok=ok && !n3ds_save_snapshot_validate(next);next->blocks[0]->data[0]^=1;
        ok=ok && n3ds_save_snapshot_validate(next);}
    n3ds_save_snapshot_release(next);free(source);free(output);return ok;
}
