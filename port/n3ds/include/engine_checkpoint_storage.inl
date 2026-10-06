/* Portable checksummed saves: typed pointers become region-relative values.
 * Files are streamed in bounded blocks; the prior complete save is retained. */
#include "cseries.h"
#include "game_state.h"
#include "crc.h"
#include "interface/player_ui.h"
#include "engine_files.h"
#include "engine_checkpoint.h"
#include "engine_threads.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "game/game.h"
#include "math/random_math.h"
void n3ds_log(const char *);
void *n3ds_game_state_identity(unsigned long long *);
void n3ds_state_flush(void *,unsigned int);
extern byte __start__[],__end__[],__exidx_start[];
#ifndef HALO_N3DS_CHECKPOINT_BUILD
#define HALO_N3DS_CHECKPOINT_BUILD 0u
#endif
enum { STORAGE_MAGIC=0x53503348, STORAGE_VERSION=3, STORAGE_LIMIT=0x345000, MAX_POINTERS=16384 };
struct checkpoint_header {
    unsigned int magic,version,header_bytes,payload_bytes;
    unsigned int engine_header_bytes,checksum_offset,pointer_count,payload_crc;
    unsigned int build,tag_bytes,bsp_bytes,image_bytes;
    int bsp;
    unsigned int seed,header_crc,reserved;
};
struct pointer_record {unsigned int offset,target;};
struct regions {unsigned long base[5],size[5];};
static struct {byte *arena;struct regions regions;struct pointer_record *records;unsigned int count;unsigned long callbacks[2];} writing;
static int path_for_directory(const char *directory,char path[256])
{
    char active[256];
    if(!directory) {if(!player_ui_get_path_to_local_player_profile_directory(0,active)) return 0;directory=active;}
    unsigned int n=strlen(directory);
    if(n<3 || n>220 || directory[1]!=':' || (directory[0]!='u' && directory[0]!='U')) return 0;
    snprintf(path,256,"%s%ssavegame.n3ds",directory,directory[n-1]=='\\' || directory[n-1]=='/'?"":"\\");return 1;
}
const char *game_state_get_persistent_storage_filename(void) {return "savegame.n3ds";}
void game_state_create_persistent_storage(const char *directory)
{
    char path[256];struct native_file_info info;
    if(!path_for_directory(directory,path)) return;
    n3ds_files_lock();
    if(!n3ds_file_stat(path,&info)) n3ds_file_create(path,0);
    n3ds_files_unlock();
}
static unsigned int checksum(const void *p,unsigned int size)
{unsigned long c;crc_new(&c);crc_checksum_buffer(&c,p,size);return c;}
static unsigned int image_identity(void)
{
    /* For an unusual pointer into executable data, accept only the same image.
     * Ordinary checkpoints use symbolic callbacks and need no image identity.
     * Skip crt0's loader-owned header when a builder hasn't supplied an ID. */
    static unsigned int value;
    if(HALO_N3DS_CHECKPOINT_BUILD) return HALO_N3DS_CHECKPOINT_BUILD;
    if(!value) {value=checksum(__start__+4096,__exidx_start-__start__-4096);if(!value)value=1;}
    return value;
}
static void current_regions(struct regions *r)
{
    unsigned long long generation;memset(r,0,sizeof(*r));
    r->base[1]=(unsigned long)n3ds_game_state_identity(&generation);r->size[1]=STORAGE_LIMIT;
    for(int i=0;i<2;++i) {const struct n3ds_cache_view *v=n3ds_engine_cache_view(i);
        if(v) {r->base[i+2]=(unsigned long)v->tags;r->size[i+2]=v->tags?v->tag_bytes:0;}}
    r->base[4]=(unsigned long)__start__;r->size[4]=__end__-__start__;
}
static int encode_pointer(void *field,int clear)
{
    unsigned long offset=(byte *)field-writing.arena,value;memcpy(&value,field,4);
    if((offset&3) || offset>STORAGE_LIMIT-4 || writing.count==MAX_POINTERS) return 0;
    unsigned int target=0;
    if(value && !clear) {
        for(unsigned int i=1;i<=2;++i) if(value==writing.callbacks[i-1]) {target=0x50000000u|i;break;}
        unsigned int kind;
        for(kind=1;!target && kind<=4;++kind) if(value>=writing.regions.base[kind] &&
            value-writing.regions.base[kind]<writing.regions.size[kind]) {
            target=(kind<<28)|(value-writing.regions.base[kind]);break;
        }
        if(!target) {char message[128];snprintf(message,sizeof(message),"CHECKPOINT: unsupported pointer field=%lu value=%08lx; previous save retained",offset,value);n3ds_log(message);return 0;}
    }
    writing.records[writing.count++]=(struct pointer_record){offset,target};return 1;
}
static int compare_records(const void *a,const void *b)
{unsigned int x=((const struct pointer_record *)a)->offset,y=((const struct pointer_record *)b)->offset;return x<y?-1:x>y;}
static int decode_pointer(unsigned int target,const struct regions *r,unsigned int *value)
{
    if(!target) {*value=0;return 1;}
    unsigned int kind=target>>28,offset=target&0xfffffff;
    if(kind==5) {*value=n3ds_checkpoint_callback(offset);return *value!=0;}
    if(kind<1 || kind>4 || !r->base[kind] || offset>=r->size[kind]) return 0;
    *value=r->base[kind]+offset;return 1;
}
static unsigned int payload_storage_bytes(const struct checkpoint_header *h)
{return h->version==2?h->payload_bytes:h->reserved;}
static int header_valid(struct checkpoint_header *h,unsigned int size)
{
    unsigned int stored=h->header_crc;h->header_crc=0;unsigned int actual=checksum(h,sizeof(*h));h->header_crc=stored;
    return h->magic==STORAGE_MAGIC && (h->version==2 || h->version==STORAGE_VERSION) && stored==actual &&
        h->header_bytes==sizeof(*h) && h->payload_bytes==STORAGE_LIMIT &&
        h->engine_header_bytes==sizeof(struct game_state_header) && h->checksum_offset==offsetof(struct game_state_header,checksum) &&
        h->pointer_count<=MAX_POINTERS && h->bsp>=0 && h->bsp<64 &&
        (h->version==2?!h->reserved:(h->reserved>0 && h->reserved<STORAGE_LIMIT+32768)) &&
        size==sizeof(*h)+payload_storage_bytes(h)+h->pointer_count*sizeof(struct pointer_record);
}
/* The stream cursor is sequential; even large cards only read packed bytes.
 * Every block has an independent bound, and the logical CRC covers the entire
 * decoded image plus its pointer table before a save can be offered to Resume. */
static int read_payload_block(unsigned int handle,const struct checkpoint_header *h,
    byte *block,byte *packed,unsigned int n,unsigned int *consumed)
{
    if(h->version==2) {*consumed+=n;return n3ds_file_read(handle,block,n);}
    unsigned int count;
    if(*consumed>h->reserved || h->reserved-*consumed<4 || !n3ds_file_read(handle,&count,4) ||
       !count || count>n3ds_save_pack_bound() || count>h->reserved-*consumed-4) return 0;
    *consumed+=count+4;
    return n3ds_file_read(handle,packed,count) && n3ds_save_unpack(packed,count,block,n);
}
static unsigned int open_valid(const char *path,struct checkpoint_header *h,struct game_state_header *engine,boolean *corrupted)
{
    unsigned int handle=n3ds_file_open(path,1),size,consumed=0;unsigned long crc;
    byte *block=NULL,*packed=NULL;
    if(!handle) return 0;
    size=n3ds_file_size(handle);
    if(!size) goto absent;
    if(size<sizeof(*h) || !n3ds_file_read(handle,h,sizeof(*h))) goto damaged;
    if(h->magic!=STORAGE_MAGIC || (h->version!=2 && h->version!=STORAGE_VERSION)) goto absent;
    if(!header_valid(h,size)) goto damaged;
    if(h->build && h->build!=image_identity()) goto absent;
    block=(malloc)(32768);packed=(malloc)(n3ds_save_pack_bound());
    if(!block || !packed) goto absent;
    crc_new(&crc);
    for(unsigned int offset=0;offset<STORAGE_LIMIT;offset+=32768) {
        unsigned int n=MIN(32768u,STORAGE_LIMIT-offset);
        if(!read_payload_block(handle,h,block,packed,n,&consumed)) goto damaged;
        if(!offset) memcpy(engine,block,sizeof(*engine));
        crc_checksum_buffer(&crc,block,n);
    }
    if(consumed!=payload_storage_bytes(h)) goto damaged;
    for(unsigned int offset=0,bytes=h->pointer_count*sizeof(struct pointer_record);offset<bytes;) {
        unsigned int n=MIN(32768u,bytes-offset);
        if(!n3ds_file_read(handle,block,n)) goto damaged;
        crc_checksum_buffer(&crc,block,n);offset+=n;
    }
    if(crc!=h->payload_crc || !memchr(engine->map_name,0,sizeof(engine->map_name)) ||
       strncmp(engine->map_name,"levels\\",7) || engine->difficulty<0 || engine->difficulty>3 || engine->player_count!=1) goto damaged;
    free(block);free(packed);return handle;
damaged:
    if(corrupted) *corrupted=TRUE;
absent:
    free(block);free(packed);n3ds_file_close(handle);return 0;
}
static unsigned int open_checkpoint(char path[256],struct checkpoint_header *h,struct game_state_header *engine,boolean *corrupted)
{
    if(corrupted) *corrupted=FALSE;
    if(!path_for_directory(NULL,path)) return 0;
    unsigned int handle=open_valid(path,h,engine,corrupted);
    if(!handle) {strcat(path,".bak");handle=open_valid(path,h,engine,corrupted);
        if(handle) n3ds_log("CHECKPOINT: using previous complete checkpoint backup");}
    if(handle && corrupted) *corrupted=FALSE;
    return handle;
}
boolean game_state_read_header_from_persistent_storage(void *buffer,unsigned long *field,long header_bytes,long bytes,boolean *corrupted)
{
    n3ds_checkpoint_finish();
    char path[256];struct checkpoint_header h;struct game_state_header engine;
    if(corrupted) *corrupted=FALSE;
    if(!buffer || field!=(unsigned long *)((byte *)buffer+offsetof(struct game_state_header,checksum)) ||
       header_bytes!=sizeof(engine) || bytes!=STORAGE_LIMIT) return FALSE;
    n3ds_files_lock();unsigned int handle=open_checkpoint(path,&h,&engine,corrupted);
    int ok=handle && n3ds_file_close(handle);
    if(ok) {memcpy(buffer,&engine,sizeof(engine));*field=0;}
    n3ds_files_unlock();return ok;
}
struct checkpoint_job_data {
    unsigned int thread;int threaded;
    const void *snapshot;const byte *raw;
    struct pointer_record *records;struct checkpoint_header header;
    char path[256],map[256];long long started;
};
static struct checkpoint_job_data checkpoint_job,checkpoint_pending;
static int checkpoint_just_captured;
long long n3ds_engine_ticks(void),n3ds_engine_tick_frequency(void);
void n3ds_engine_sleep(unsigned int);
static unsigned int checkpoint_write_worker(void *argument)
{
    struct checkpoint_job_data *job=argument;
    const char *path=job->path;char temp[256],backup[256];
    struct checkpoint_header h=job->header;
    unsigned int handle=0,next=0;int ok=0;unsigned long crc;
    byte *block=(malloc)(32768),*packed=(malloc)(n3ds_save_pack_bound());
    if(!block || !packed)goto done;
    snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(backup,sizeof(backup),"%s.bak",path);
    if(!n3ds_file_create(temp,0) || !(handle=n3ds_file_open(temp,3)) || !n3ds_file_resize(handle,0) || !n3ds_file_write_buffered(handle,&h,sizeof(h))) goto done;
    crc_new(&crc);h.reserved=0;
    for(unsigned int offset=0;offset<STORAGE_LIMIT;) {
        unsigned int n=MIN(32768u,STORAGE_LIMIT-offset);
        if(job->snapshot) {if(!n3ds_save_snapshot_read(job->snapshot,offset,block,n))goto done;}
        else memcpy(block,job->raw+offset,n);
        if(!offset) memset(block+h.checksum_offset,0,4);
        while(next<h.pointer_count && job->records[next].offset<offset+n) {
            memcpy(block+job->records[next].offset-offset,&job->records[next].target,4);++next;
        }
        crc_checksum_buffer(&crc,block,n);
        unsigned int count=n3ds_save_pack(block,n,packed,n3ds_save_pack_bound());
        if(!count || !n3ds_file_write_buffered(handle,&count,4) || !n3ds_file_write_buffered(handle,packed,count)) goto done;
        h.reserved+=count+4;offset+=n;
        if(job->threaded) n3ds_engine_sleep(1);
    }
    crc_checksum_buffer(&crc,job->records,h.pointer_count*sizeof(*job->records));h.payload_crc=crc;h.header_crc=checksum(&h,sizeof(h));
    if(!n3ds_file_write_buffered(handle,job->records,h.pointer_count*sizeof(*job->records)) || !n3ds_file_write_at(handle,0,&h,sizeof(h))) goto done;
    ok=n3ds_file_close(handle);handle=0;if(!ok) goto done;ok=0;
    struct checkpoint_header old;struct game_state_header old_engine;
    unsigned int old_handle=open_valid(path,&old,&old_engine,NULL);
    if(old_handle) {
        if(!n3ds_file_close(old_handle)) goto done;
        struct native_file_info info;
        if(n3ds_file_stat(backup,&info) && !n3ds_file_delete(backup,0)) goto done;
        if(!n3ds_file_rename(path,backup)) goto done;
    } else {struct native_file_info info;if(n3ds_file_stat(path,&info) && !n3ds_file_delete(path,0)) goto done;}
    ok=n3ds_file_rename(temp,path);job->header.reserved=h.reserved;
done:
    if(handle) n3ds_file_close(handle);
    free(block);free(packed);return ok;
}
static void release_job(struct checkpoint_job_data *job)
{n3ds_save_snapshot_release(job->snapshot);free(job->records);memset(job,0,sizeof(*job));}
static void checkpoint_completed(unsigned int ok)
{
    char message[384];
    snprintf(message,sizeof(message),"CHECKPOINT: %s map=%s bsp=%d pointers=%u packed_bytes=%u background=%d elapsed_ms=%.1f",
        ok?"saved":"not saved; previous retained",checkpoint_job.map,checkpoint_job.header.bsp,checkpoint_job.header.pointer_count,
        checkpoint_job.header.reserved,checkpoint_job.threaded,(n3ds_engine_ticks()-checkpoint_job.started)*1000./n3ds_engine_tick_frequency());
    n3ds_log(message);release_job(&checkpoint_job);
}
static void start_checkpoint_job(void)
{
    if(checkpoint_job.snapshot) {checkpoint_job.threaded=1;checkpoint_job.thread=n3ds_thread_create(2,checkpoint_write_worker,&checkpoint_job);}
    if(!checkpoint_job.thread) {checkpoint_job.threaded=0;checkpoint_completed(checkpoint_write_worker(&checkpoint_job));}
}
void n3ds_checkpoint_poll(void)
{
    unsigned int result;
    if(checkpoint_job.thread && n3ds_thread_result(checkpoint_job.thread,&result)) {
        assert(n3ds_thread_close(checkpoint_job.thread));checkpoint_job.thread=0;checkpoint_completed(result);
    }
    if(!checkpoint_job.thread && checkpoint_pending.records) {
        checkpoint_job=checkpoint_pending;memset(&checkpoint_pending,0,sizeof(checkpoint_pending));start_checkpoint_job();
    }
}
void n3ds_checkpoint_finish(void)
{
    do {n3ds_checkpoint_poll();if(checkpoint_job.thread)n3ds_engine_sleep(1);} while(checkpoint_job.thread || checkpoint_pending.records);
}
void game_state_write_to_persistent_storage(void *buffer,unsigned long *field,long header_bytes,long bytes)
{
    n3ds_checkpoint_poll();
    long long begin=n3ds_engine_ticks();
    char path[256];struct checkpoint_header h={0};
    if(bytes!=STORAGE_LIMIT || header_bytes!=sizeof(struct game_state_header) || !path_for_directory(NULL,path)) return;
    current_regions(&writing.regions);writing.arena=(void *)writing.regions.base[1];writing.count=0;
    for(unsigned int i=0;i<2;++i) writing.callbacks[i]=n3ds_checkpoint_callback(i+1);
    if(buffer!=writing.arena || field!=(unsigned long *)((byte *)buffer+offsetof(struct game_state_header,checksum))) return;
    const struct game_state_header *engine=buffer;
    if(engine->player_count!=1 || !strncmp(engine->map_name,"levels\\ui\\",10)) return;
    writing.records=(malloc)(MAX_POINTERS*sizeof(struct pointer_record));
    if(!writing.records) {n3ds_log("CHECKPOINT: insufficient memory; previous save retained");return;}
    if(!n3ds_checkpoint_visit(encode_pointer)) goto rejected;
    qsort(writing.records,writing.count,sizeof(*writing.records),compare_records);
    for(unsigned int i=1;i<writing.count;++i) if(writing.records[i-1].offset==writing.records[i].offset) goto rejected;
    h.magic=STORAGE_MAGIC;h.version=STORAGE_VERSION;h.header_bytes=sizeof(h);h.payload_bytes=bytes;
    h.engine_header_bytes=header_bytes;h.checksum_offset=offsetof(struct game_state_header,checksum);h.pointer_count=writing.count;
    for(unsigned int i=0;i<writing.count;++i) if((writing.records[i].target>>28)==4) h.build=image_identity();
    h.tag_bytes=writing.regions.size[2];h.bsp_bytes=writing.regions.size[3];h.image_bytes=writing.regions.size[4];
    h.bsp=global_structure_bsp_index_get();h.seed=get_random_seed();
    const void *snapshot=n3ds_game_state_snapshot();
    if(snapshot && (checkpoint_just_captured || n3ds_save_snapshot_matches(snapshot,buffer))) n3ds_save_snapshot_retain(snapshot);
    else snapshot=n3ds_save_snapshot_create(buffer,bytes);
    struct checkpoint_job_data job={0};job.header=h;job.started=begin;job.records=writing.records;writing.records=NULL;
    job.snapshot=snapshot;job.raw=snapshot?NULL:buffer;
    strcpy(job.path,path);strcpy(job.map,engine->map_name);
    if(!snapshot) {
        /* Allocation failure keeps a correct, bounded synchronous fallback.
         * Never let a worker refer to a live, mutable game-state arena. */
        n3ds_checkpoint_finish();checkpoint_job=job;start_checkpoint_job();
    } else if(checkpoint_job.thread) {
        release_job(&checkpoint_pending);checkpoint_pending=job;
        n3ds_log("CHECKPOINT: newest immutable checkpoint queued without waiting for SD writer");
    } else {checkpoint_job=job;start_checkpoint_job();}
    char message[128];snprintf(message,sizeof(message),"CHECKPOINT: immutable preparation_ms=%.3f",(n3ds_engine_ticks()-begin)*1000./n3ds_engine_tick_frequency());n3ds_log(message);
    return;
rejected:
    free(writing.records);writing.records=NULL;n3ds_log("CHECKPOINT: pointer validation failed; previous save retained");
}
void n3ds_checkpoint_write_captured(void *buffer,unsigned long *field,long header_bytes,long bytes)
{
    /* Only the immediate capture owner can assert this: no simulation or
     * after-load hooks have run since the immutable snapshot was made. */
    checkpoint_just_captured=1;
    game_state_write_to_persistent_storage(buffer,field,header_bytes,bytes);
    checkpoint_just_captured=0;
}
void game_state_read_from_persistent_storage(void *buffer,long bytes)
{
    n3ds_checkpoint_finish();
    char path[256];struct checkpoint_header h;struct game_state_header engine;struct regions r;
    struct pointer_record *records=NULL;byte *block=NULL,*packed=NULL;unsigned int next=0,consumed=0;int ok=0;
    n3ds_files_lock();unsigned int handle=open_checkpoint(path,&h,&engine,NULL);
    if(!handle || bytes!=STORAGE_LIMIT) goto done;
    records=(malloc)(h.pointer_count*sizeof(*records));if(!records) goto done;
    if(!n3ds_file_read_at(handle,sizeof(h)+payload_storage_bytes(&h),records,h.pointer_count*sizeof(*records))) goto done;
    block=(malloc)(32768);packed=(malloc)(n3ds_save_pack_bound());if(!block || !packed)goto done;
    current_regions(&r);
    if(buffer!=(void *)r.base[1] || (h.build && h.image_bytes!=r.size[4]) || h.tag_bytes!=r.size[2] ||
       !global_scenario_try_and_get() || h.bsp>=global_scenario_get()->structure_bsp_references.count) goto done;
    struct regions bounds=r;bounds.base[3]=1;bounds.size[3]=h.bsp_bytes;
    for(unsigned int i=0;i<h.pointer_count;++i) {
        unsigned int value;
        if((records[i].offset&3) || records[i].offset<sizeof(engine) || records[i].offset>STORAGE_LIMIT-4 ||
           (i && records[i-1].offset>=records[i].offset) || (!h.build && records[i].target>>28==4) ||
           !decode_pointer(records[i].target,&bounds,&value)) goto done;
    }
    if(global_structure_bsp_index_get()!=h.bsp && !scenario_switch_structure_bsp(h.bsp)) goto done;
    current_regions(&r);if(h.bsp_bytes!=r.size[3]) goto done;
    for(unsigned int i=0;i<h.pointer_count;++i) if(!decode_pointer(records[i].target,&r,&records[i].target)) goto done;
    if(!n3ds_file_seek(handle,sizeof(h)))goto done;
    for(unsigned int offset=0;offset<(unsigned int)bytes;) {
        unsigned int n=MIN(32768u,(unsigned int)bytes-offset);
        if(!read_payload_block(handle,&h,block,packed,n,&consumed)) goto done;
        while(next<h.pointer_count && records[next].offset<offset+n) {
            memcpy(block+records[next].offset-offset,&records[next].target,4);++next;
        }
        memcpy((byte *)buffer+offset,block,n);offset+=n;
    }
    set_random_seed(h.seed);n3ds_state_flush(buffer,bytes);ok=1;
done:
    if(handle && !n3ds_file_close(handle)) ok=0;
    free(records);free(block);free(packed);n3ds_files_unlock();
    if(!ok) {vhalt("Campaign storage: incompatible or damaged checkpoint during restore");}
    else {char message[128];snprintf(message,sizeof(message),"CHECKPOINT: resumed map=%s bsp=%d",engine.map_name,h.bsp);n3ds_log(message);}
}
