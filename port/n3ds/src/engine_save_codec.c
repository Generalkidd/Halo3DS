/* LZ4 blocks are independently decodable: no full-size temporary save image.
 * See vendor/lz4/LICENSE. Version 1.10.0, unmodified upstream codec. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "engine_parallel.h"
#define LZ4_HEAPMODE 1
#include "../vendor/lz4/lz4.c"
enum { SAVE_BLOCK=32768, SAVE_LIMIT=0x345000, SAVE_BLOCKS=(SAVE_LIMIT+SAVE_BLOCK-1)/SAVE_BLOCK };
struct save_snapshot {
    unsigned int refs,bytes,packed,checksum,raw;
    /* Packed blocks store offsets. Raw blocks store individual checksums. */
    unsigned int blocks[SAVE_BLOCKS+1];
    unsigned char *data;
};
static unsigned int raw_snapshots;
unsigned int n3ds_save_memory_crc(const void *,unsigned int,const void *,unsigned int);
static unsigned int snapshot_block_bytes(const struct save_snapshot *s,unsigned int offset)
{
    unsigned int bytes=s->bytes-offset;
    return bytes>SAVE_BLOCK?SAVE_BLOCK:bytes;
}
static unsigned int snapshot_checksum(const struct save_snapshot *s)
{
    unsigned int metadata=n3ds_save_memory_crc(&s->bytes,8,s->blocks,sizeof(s->blocks));
    return s->raw?metadata:metadata ^ n3ds_save_memory_crc(s->data,s->packed,NULL,0);
}
int n3ds_save_snapshot_validate(const void *snapshot)
{
    const struct save_snapshot *s=snapshot;
    if(!s || !s->data || !s->bytes || s->bytes>SAVE_LIMIT || !s->packed ||
       s->packed>SAVE_LIMIT+4096 || (s->raw && s->packed!=s->bytes) ||
       s->checksum!=snapshot_checksum(s)) return 0;
    if(s->raw) {
        for(unsigned int i=0,offset=0;offset<s->bytes;++i,offset+=SAVE_BLOCK)
            if(s->blocks[i]!=n3ds_save_memory_crc(s->data+offset,snapshot_block_bytes(s,offset),NULL,0)) return 0;
    }
    return 1;
}
int n3ds_input_platform_original_model(void);
unsigned long n3ds_cache_heap_headroom(void);
unsigned int n3ds_save_pack_bound(void){return LZ4_compressBound(SAVE_BLOCK);}
unsigned int n3ds_save_pack_state_bytes(void){return LZ4_sizeofState();}
unsigned int n3ds_save_pack(void *state,const void *src,unsigned int bytes,void *dst,unsigned int capacity)
{
    /* This is LZ4_compress_default's exact block compressor. Each operation
     * owns one state, reset by extState for every independently decodable block. */
    return state && bytes<=SAVE_BLOCK?LZ4_compress_fast_extState(state,src,dst,bytes,capacity,1):0;
}
int n3ds_save_unpack(const void *src,unsigned int bytes,void *dst,unsigned int size)
{return bytes && bytes<=LZ4_compressBound(SAVE_BLOCK) && size<=SAVE_BLOCK && LZ4_decompress_safe(src,dst,bytes,size)==(int)size;}
static void *create_packed_snapshot(const void *input,unsigned int bytes)
{
 if(!input || !bytes || bytes>SAVE_LIMIT)return NULL;
 struct save_snapshot *s=calloc(1,sizeof(*s));void *block=malloc(LZ4_compressBound(SAVE_BLOCK));
 void *codec=malloc(n3ds_save_pack_state_bytes());
 if(!s || !block || !codec){free(s);free(block);free(codec);return NULL;}
 unsigned int cap=0,limit=n3ds_input_platform_original_model()?1536*1024:SAVE_LIMIT+4096;
 s->refs=1;s->bytes=bytes;
 for(unsigned int offset=0,i=0;offset<bytes;offset+=SAVE_BLOCK,++i){
  unsigned int n=bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
  unsigned int packed=n3ds_save_pack(codec,(const unsigned char *)input+offset,n,block,LZ4_compressBound(SAVE_BLOCK));
  if(!packed)goto fail;
  unsigned int required=s->packed+packed;
  if(required>cap){unsigned int next=(required+65535)&~65535u;if(next>limit)next=limit;if(required>next)goto fail;
   void *larger=realloc(s->data,next);if(!larger)goto fail;s->data=larger;cap=next;}
  s->blocks[i]=s->packed;memcpy(s->data+s->packed,block,packed);s->packed=required;s->blocks[i+1]=required;
 }
 s->checksum=snapshot_checksum(s);free(codec);free(block);return s;
 fail:free(codec);free(block);free(s->data);free(s);return NULL;
}
struct raw_capture {
    struct save_snapshot *snapshot;
    const unsigned char *input;
};
static int capture_raw_blocks(void *context,unsigned int begin,unsigned int end)
{
    struct raw_capture *capture=context;
    struct save_snapshot *s=capture->snapshot;
    for(unsigned int i=begin;i<end;++i) {
        unsigned int offset=i*SAVE_BLOCK,bytes=snapshot_block_bytes(s,offset);
        memcpy(s->data+offset,capture->input+offset,bytes);
        s->blocks[i]=n3ds_save_memory_crc(s->data+offset,bytes,NULL,0);
    }
    return 1;
}

void *n3ds_save_snapshot_capture(const void *input,unsigned int bytes,void *previous)
{
    if(!input || !bytes || bytes>SAVE_LIMIT) return NULL;
    if(n3ds_input_platform_original_model()) return create_packed_snapshot(input,bytes);
    struct save_snapshot *s=previous;
    /* The writer owns an independent reference. Reuse a raw allocation only
     * when the session owns the sole reference, with an upper bound of two
     * raw images and ordinary heap headroom for map/asset allocations. */
    if(!s || !s->raw || s->refs!=1 || s->bytes!=bytes) {
        if(raw_snapshots>=2 || n3ds_cache_heap_headroom()<bytes+8u*1024*1024)
            return create_packed_snapshot(input,bytes);
        s=calloc(1,sizeof(*s));
        if(s) s->data=malloc(bytes);
        if(!s || !s->data) {free(s);return create_packed_snapshot(input,bytes);}
        ++raw_snapshots;
        s->raw=1;s->bytes=s->packed=bytes;
    }
    struct raw_capture capture={s,input};
    /* The engine CRC's lazy tables belong to the game thread. Initialize
     * before both cores read them and write their disjoint snapshot blocks. */
    n3ds_save_memory_crc(input,0,NULL,0);
    n3ds_parallel_range(capture_raw_blocks,&capture,(bytes+SAVE_BLOCK-1)/SAVE_BLOCK,8,3);
    s->checksum=snapshot_checksum(s);
    ++s->refs;
    return s;
}

void n3ds_save_snapshot_retain(const void *snapshot){if(snapshot)++((struct save_snapshot *)snapshot)->refs;}
void n3ds_save_snapshot_release(const void *snapshot)
{
    struct save_snapshot *s=(void *)snapshot;
    if(s && !--s->refs) {
        if(s->raw) --raw_snapshots;
        free(s->data);free(s);
    }
}
unsigned int n3ds_save_snapshot_size(const void *snapshot)
{const struct save_snapshot *s=snapshot;return s?s->packed+sizeof(*s):0;}
int n3ds_save_snapshot_read(const void *snapshot,unsigned int offset,void *out,unsigned int bytes)
{
 const struct save_snapshot *s=snapshot;
 if(!s || offset%SAVE_BLOCK || offset>=s->bytes || bytes!=snapshot_block_bytes(s,offset))return 0;
 unsigned int i=offset/SAVE_BLOCK;
 if(s->raw) {
  if(s->blocks[i]!=n3ds_save_memory_crc(s->data+offset,bytes,NULL,0))return 0;
  memcpy(out,s->data+offset,bytes);return 1;
 }
 unsigned int start=s->blocks[i],end=s->blocks[i+1];
 return end>start && end<=s->packed && n3ds_save_unpack(s->data+start,end-start,out,bytes);
}
int n3ds_save_snapshot_matches(const void *snapshot,const void *input)
{
 const struct save_snapshot *s=snapshot;
 if(s && s->raw)return !memcmp(s->data,input,s->bytes);
 unsigned char *block=malloc(SAVE_BLOCK);int ok=s && block;
 if(ok)for(unsigned int offset=0;offset<s->bytes;offset+=SAVE_BLOCK){unsigned int n=s->bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
  if(!n3ds_save_snapshot_read(s,offset,block,n) || memcmp(block,(const unsigned char *)input+offset,n)){ok=0;break;}}
 free(block);return ok;
}
int n3ds_save_snapshot_restore(const void *snapshot,void *output,unsigned int bytes)
{
    const struct save_snapshot *s=snapshot;
    if(!output || !s || s->bytes!=bytes || !n3ds_save_snapshot_validate(s)) return 0;
    if(s->raw) {
        /* Validation covered the entire immutable image. Restore it directly
         * without a second CRC pass or a temporary block-sized copy. */
        memcpy(output,s->data,bytes);
        return 1;
    }
    unsigned char *block=malloc(SAVE_BLOCK);
    if(!block) return 0;
    for(unsigned int offset=0;offset<bytes;offset+=SAVE_BLOCK) {
        unsigned int count=snapshot_block_bytes(s,offset);
        if(!n3ds_save_snapshot_read(s,offset,block,count)) {
            free(block);
            return -1;
        }
        memcpy((unsigned char *)output+offset,block,count);
    }
    free(block);
    return 1;
}
int n3ds_save_codec_tests(void)
{
 unsigned char *source=malloc(SAVE_BLOCK*2),*output=malloc(SAVE_BLOCK);if(!source || !output){free(source);free(output);return 0;}
 unsigned int seed=123;for(unsigned int i=0;i<SAVE_BLOCK*2;++i){seed=seed*1664525u+1013904223u;source[i]=(i%7)?0:seed>>24;}
 void *s=create_packed_snapshot(source,SAVE_BLOCK*2);int ok=s && n3ds_save_snapshot_validate(s) && n3ds_save_snapshot_matches(s,source);
 if(ok){n3ds_save_snapshot_retain(s);n3ds_save_snapshot_release(s);ok=n3ds_save_snapshot_read(s,SAVE_BLOCK,output,SAVE_BLOCK) && !memcmp(output,source+SAVE_BLOCK,SAVE_BLOCK) && !n3ds_save_snapshot_read(s,1,output,10);}
 if(ok){source[900]^=1;ok=!n3ds_save_snapshot_matches(s,source);}
 if(ok){struct save_snapshot *test=s;test->data[0]^=1;ok=!n3ds_save_snapshot_validate(s);test->data[0]^=1;ok=ok && n3ds_save_snapshot_validate(s);}
 n3ds_save_snapshot_release(s);free(source);free(output);return ok;
}
