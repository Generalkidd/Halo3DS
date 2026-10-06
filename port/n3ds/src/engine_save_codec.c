/* LZ4 blocks are independently decodable: no full-size temporary save image.
 * See vendor/lz4/LICENSE. Version 1.10.0, unmodified upstream codec. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define LZ4_HEAPMODE 1
#include "../vendor/lz4/lz4.c"
enum { SAVE_BLOCK=32768, SAVE_LIMIT=0x345000, SAVE_BLOCKS=(SAVE_LIMIT+SAVE_BLOCK-1)/SAVE_BLOCK };
struct save_snapshot { unsigned int refs,bytes,packed,checksum;unsigned int offsets[SAVE_BLOCKS+1];unsigned char *data; };
unsigned int n3ds_save_memory_crc(const void *,unsigned int,const void *,unsigned int);
static unsigned int snapshot_checksum(const struct save_snapshot *s)
{return n3ds_save_memory_crc(&s->bytes,8,s->offsets,sizeof(s->offsets)) ^ n3ds_save_memory_crc(s->data,s->packed,NULL,0);}
int n3ds_save_snapshot_validate(const void *snapshot)
{const struct save_snapshot *s=snapshot;return s && s->bytes>0 && s->bytes<=SAVE_LIMIT && s->packed<=SAVE_LIMIT+4096 && s->checksum==snapshot_checksum(s);}
int n3ds_input_platform_original_model(void);
unsigned int n3ds_save_pack_bound(void){return LZ4_compressBound(SAVE_BLOCK);}
unsigned int n3ds_save_pack(const void *src,unsigned int bytes,void *dst,unsigned int capacity)
{return bytes<=SAVE_BLOCK?LZ4_compress_default(src,dst,bytes,capacity):0;}
int n3ds_save_unpack(const void *src,unsigned int bytes,void *dst,unsigned int size)
{return bytes && bytes<=LZ4_compressBound(SAVE_BLOCK) && size<=SAVE_BLOCK && LZ4_decompress_safe(src,dst,bytes,size)==(int)size;}
void *n3ds_save_snapshot_create(const void *input,unsigned int bytes)
{
 if(!input || !bytes || bytes>SAVE_LIMIT)return NULL;
 struct save_snapshot *s=calloc(1,sizeof(*s));void *block=malloc(LZ4_compressBound(SAVE_BLOCK));
 if(!s || !block){free(s);free(block);return NULL;}
 unsigned int cap=0,limit=n3ds_input_platform_original_model()?1536*1024:SAVE_LIMIT+4096;
 s->refs=1;s->bytes=bytes;
 for(unsigned int offset=0,i=0;offset<bytes;offset+=SAVE_BLOCK,++i){
  unsigned int n=bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
  unsigned int packed=n3ds_save_pack((const unsigned char *)input+offset,n,block,LZ4_compressBound(SAVE_BLOCK));
  if(!packed)goto fail;
  unsigned int required=s->packed+packed;
  if(required>cap){unsigned int next=(required+65535)&~65535u;if(next>limit)next=limit;if(required>next)goto fail;
   void *larger=realloc(s->data,next);if(!larger)goto fail;s->data=larger;cap=next;}
  s->offsets[i]=s->packed;memcpy(s->data+s->packed,block,packed);s->packed=required;s->offsets[i+1]=required;
 }
 s->checksum=snapshot_checksum(s);free(block);return s;
 fail:free(block);free(s->data);free(s);return NULL;
}
void n3ds_save_snapshot_retain(const void *snapshot){if(snapshot)++((struct save_snapshot *)snapshot)->refs;}
void n3ds_save_snapshot_release(const void *snapshot)
{struct save_snapshot *s=(void *)snapshot;if(s && !--s->refs){free(s->data);free(s);}}
unsigned int n3ds_save_snapshot_size(const void *snapshot)
{const struct save_snapshot *s=snapshot;return s?s->packed+sizeof(*s):0;}
int n3ds_save_snapshot_read(const void *snapshot,unsigned int offset,void *out,unsigned int bytes)
{
 const struct save_snapshot *s=snapshot;
 if(!s || offset%SAVE_BLOCK || offset>=s->bytes || bytes!=(s->bytes-offset>SAVE_BLOCK?SAVE_BLOCK:s->bytes-offset))return 0;
 unsigned int i=offset/SAVE_BLOCK,start=s->offsets[i],end=s->offsets[i+1];
 return end>start && end<=s->packed && n3ds_save_unpack(s->data+start,end-start,out,bytes);
}
int n3ds_save_snapshot_matches(const void *snapshot,const void *input)
{
 const struct save_snapshot *s=snapshot;unsigned char *block=malloc(SAVE_BLOCK);int ok=s && block;
 if(ok)for(unsigned int offset=0;offset<s->bytes;offset+=SAVE_BLOCK){unsigned int n=s->bytes-offset;if(n>SAVE_BLOCK)n=SAVE_BLOCK;
  if(!n3ds_save_snapshot_read(s,offset,block,n) || memcmp(block,(const unsigned char *)input+offset,n)){ok=0;break;}}
 free(block);return ok;
}
int n3ds_save_codec_tests(void)
{
 unsigned char *source=malloc(SAVE_BLOCK*2),*output=malloc(SAVE_BLOCK);if(!source || !output){free(source);free(output);return 0;}
 unsigned int seed=123;for(unsigned int i=0;i<SAVE_BLOCK*2;++i){seed=seed*1664525u+1013904223u;source[i]=(i%7)?0:seed>>24;}
 void *s=n3ds_save_snapshot_create(source,SAVE_BLOCK*2);int ok=s && n3ds_save_snapshot_validate(s) && n3ds_save_snapshot_matches(s,source);
 if(ok){n3ds_save_snapshot_retain(s);n3ds_save_snapshot_release(s);ok=n3ds_save_snapshot_read(s,SAVE_BLOCK,output,SAVE_BLOCK) && !memcmp(output,source+SAVE_BLOCK,SAVE_BLOCK) && !n3ds_save_snapshot_read(s,1,output,10);}
 if(ok){source[900]^=1;ok=!n3ds_save_snapshot_matches(s,source);}
 if(ok){struct save_snapshot *test=s;test->data[0]^=1;ok=!n3ds_save_snapshot_validate(s);test->data[0]^=1;ok=ok && n3ds_save_snapshot_validate(s);}
 n3ds_save_snapshot_release(s);free(source);free(output);return ok;
}
