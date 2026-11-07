/* 24 DSP voices, four bounded PCM buffers each. Pure decoding can run on a
 * second core. Voice ownership and all DSP submissions remain on the game
 * thread, joined before any source sample may be unpinned or map unloaded. */
#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "engine_audio.h"
#include "engine_parallel.h"
#include "engine_sounds.h"
#include "engine_algorithm_profile.h"

enum { SOUND_READ_IDLE,SOUND_READ_QUEUED,SOUND_READ_DONE,SOUND_READ_CHUNK=32*1024 };
static Thread sound_reader;
static LightEvent sound_reader_work;
static int sound_reader_state,sound_reader_stop,sound_reader_cancelled,sound_reader_success;
static int sound_reader_unavailable;
static unsigned char *sound_reader_buffer;
static unsigned int sound_reader_capacity;
static char sound_reader_path[128];
static struct n3ds_sound_read_key sound_reader_key;

static int same_read(const struct n3ds_sound_read_key *key)
{
    return key->map_generation==sound_reader_key.map_generation && key->handle==sound_reader_key.handle &&
        key->offset==sound_reader_key.offset && key->bytes==sound_reader_key.bytes;
}
static void sound_reader_main(void *unused)
{
    (void)unused;
    char open_path[sizeof(sound_reader_path)]={0};
    Handle file=0;
    for (;;) {
        LightEvent_Wait(&sound_reader_work);
        if (__atomic_load_n(&sound_reader_stop,__ATOMIC_ACQUIRE)) break;
        if (__atomic_load_n(&sound_reader_state,__ATOMIC_ACQUIRE)!=SOUND_READ_QUEUED) continue;
        unsigned int done=0;
        if (!__atomic_load_n(&sound_reader_cancelled,__ATOMIC_ACQUIRE) &&
            (!file || strcmp(open_path,sound_reader_path))) {
            if (file) FSFILE_Close(file);
            file=0;
            if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&file,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,NULL),
                    fsMakePath(PATH_ASCII,sound_reader_path+5),FS_OPEN_READ,0)))
                strcpy(open_path,sound_reader_path);
            else file=0;
        }
        while (file && done<sound_reader_key.bytes &&
                !__atomic_load_n(&sound_reader_cancelled,__ATOMIC_ACQUIRE)) {
            unsigned int count=sound_reader_key.bytes-done;
            if (count>SOUND_READ_CHUNK) count=SOUND_READ_CHUNK;
            u32 got=0;
            if (R_FAILED(FSFILE_Read(file,&got,(u64)sound_reader_key.offset+done,
                    sound_reader_buffer+done,count)) || !got || got>count) {
                FSFILE_Close(file);
                file=0;
                break;
            }
            done+=got;
        }
        sound_reader_success=done==sound_reader_key.bytes;
        __atomic_store_n(&sound_reader_state,SOUND_READ_DONE,__ATOMIC_RELEASE);
    }
    if (file) FSFILE_Close(file);
}
int n3ds_sound_reader_supported(void)
{
#ifdef HALO_N3DS_LOW_MEMORY_TEST
    return 0;
#else
    static int supported=-1;
    if (supported<0) {
        bool new_model=false;
        supported=R_SUCCEEDED(APT_CheckNew3DS(&new_model)) && new_model;
    }
    return supported;
#endif
}
void n3ds_sound_reader_cancel(void)
{
    __atomic_store_n(&sound_reader_cancelled,1,__ATOMIC_RELEASE);
}
void n3ds_sound_reader_discard(unsigned int map_generation,unsigned int handle)
{
    if (__atomic_load_n(&sound_reader_state,__ATOMIC_ACQUIRE)!=SOUND_READ_IDLE &&
        sound_reader_key.map_generation==map_generation && sound_reader_key.handle==handle)
        n3ds_sound_reader_cancel();
}
void n3ds_sound_reader_offer(const char *path,const struct n3ds_sound_read_key *key)
{
    if (sound_reader_unavailable || !path || strncmp(path,"sdmc:/",6) ||
        strlen(path)>=sizeof(sound_reader_path) || !key->bytes || key->bytes>N3DS_SOUND_READ_LIMIT ||
        key->offset>~0u-key->bytes) return;
    int state=__atomic_load_n(&sound_reader_state,__ATOMIC_ACQUIRE);
    if (state!=SOUND_READ_IDLE && same_read(key) &&
        !__atomic_load_n(&sound_reader_cancelled,__ATOMIC_ACQUIRE)) return;
    if (state==SOUND_READ_QUEUED) {
        /* A lower pending slot may replace the hint. Never wait for a reader
         * on the game thread or rewrite its data before it reports completion. */
        n3ds_sound_reader_cancel();
        return;
    }
    if (key->bytes>sound_reader_capacity) {
        unsigned int capacity=(key->bytes+SOUND_READ_CHUNK-1)&~(SOUND_READ_CHUNK-1u);
        unsigned char *buffer=realloc(sound_reader_buffer,capacity);
        if (!buffer) return;
        sound_reader_buffer=buffer;
        sound_reader_capacity=capacity;
    }
    if (!sound_reader) {
        LightEvent_Init(&sound_reader_work,RESET_ONESHOT);
        __atomic_store_n(&sound_reader_stop,0,__ATOMIC_RELAXED);
        s32 priority=0x30;
        svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
        priority=priority<=0x3c?priority+3:0x3f;
        /* Leave the main and system cores alone, and keep the existing
         * geometry/codec and texture helper jobs above this reader. */
        sound_reader=threadCreate(sound_reader_main,NULL,16*1024,priority,2,false);
        if (!sound_reader) {
            sound_reader_unavailable=1;
            free(sound_reader_buffer);
            sound_reader_buffer=NULL;
            sound_reader_capacity=0;
            return;
        }
    }
    strcpy(sound_reader_path,path);
    sound_reader_key=*key;
    __atomic_store_n(&sound_reader_cancelled,0,__ATOMIC_RELAXED);
    __atomic_store_n(&sound_reader_state,SOUND_READ_QUEUED,__ATOMIC_RELEASE);
    LightEvent_Signal(&sound_reader_work);
}
int n3ds_sound_reader_copy(const struct n3ds_sound_read_key *key,void *destination)
{
    int state=__atomic_load_n(&sound_reader_state,__ATOMIC_ACQUIRE);
    if (state==SOUND_READ_QUEUED) {
        /* A synchronous cache read is now authoritative. Avoid issuing
         * another speculative SD request between its own I/O operations. */
        n3ds_sound_reader_cancel();
        return 0;
    }
    if (state!=SOUND_READ_DONE || !same_read(key)) return 0;
    int success=sound_reader_success && !__atomic_load_n(&sound_reader_cancelled,__ATOMIC_ACQUIRE);
    if (success) memcpy(destination,sound_reader_buffer,key->bytes);
    __atomic_store_n(&sound_reader_state,SOUND_READ_IDLE,__ATOMIC_RELAXED);
    return success;
}
void n3ds_sound_reader_shutdown(void)
{
    if (!sound_reader) return;
    n3ds_sound_reader_cancel();
    __atomic_store_n(&sound_reader_stop,1,__ATOMIC_RELEASE);
    LightEvent_Signal(&sound_reader_work);
    if (svcWaitSynchronization(threadGetHandle(sound_reader),U64_MAX)!=0) svcBreak(USERBREAK_PANIC);
    threadFree(sound_reader);
    sound_reader=NULL;
    free(sound_reader_buffer);
    sound_reader_buffer=NULL;
    sound_reader_capacity=0;
    __atomic_store_n(&sound_reader_state,SOUND_READ_IDLE,__ATOMIC_RELAXED);
}

#define VOICES 24
#define PACKETS 4
#define FRAMES 2048
struct clip {const unsigned char *data;unsigned int bytes,offset;int channels,compression;};
struct voice {
    struct clip current,next;
    ndspWaveBuf wave[PACKETS];
    short *pcm[PACKETS];
    int outstanding,rate_valid,mix_valid;
    float rate,gains[2];
};
static struct voice voices[VOICES];
static short *storage;
static int ready,count,paused;
static unsigned int decoded_frames,underruns,submitted;
static int exit_registered;
void n3ds_log(const char *);
static void audio_exit(void)
{
    if(!ready) return;
    n3ds_audio_dispose();
    n3ds_log("EXIT: audio worker stopped before application heap release");
}
int n3ds_audio_initialize(int channels)
{
    if(ready || channels<1 || channels>VOICES || !n3ds_audio_codec_tests()) return 0;
    Result result=ndspInit();
    if(R_FAILED(result)) {char s[160];snprintf(s,sizeof(s),"AUDIO: ndspInit failed %08lx; /3ds/dspfirm.cdc is required",(unsigned long)result);n3ds_log(s);return 0;}
    storage=linearAlloc(channels*PACKETS*FRAMES*2*sizeof(short));
    if(!storage) {ndspExit();return 0;}
    memset(voices,0,sizeof(voices));count=channels;ready=1;paused=0;
    if(!exit_registered) {
        if(atexit(audio_exit)) {n3ds_audio_dispose();return 0;}
        exit_registered=1;
    }
    decoded_frames=underruns=submitted=0;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);ndspSetMasterVol(.8f);
    for(int i=0;i<count;++i) {
        ndspChnReset(i);ndspChnSetInterp(i,NDSP_INTERP_LINEAR);
        for(int p=0;p<PACKETS;++p) voices[i].pcm[p]=storage+(i*PACKETS+p)*FRAMES*2;
    }
    n3ds_log("AUDIO: native DSP ready; 24 voices, 786432 bytes streaming PCM; Xbox ADPCM codec tests PASS");return 1;
}
void n3ds_audio_stop(int channel)
{
    if(!ready || channel<0 || channel>=count) return;
    struct voice *v=voices+channel;ndspChnWaveBufClear(channel);
    memset(v->wave,0,sizeof(v->wave));memset(&v->current,0,sizeof(v->current));memset(&v->next,0,sizeof(v->next));v->outstanding=0;
    v->rate_valid=v->mix_valid=0;
}
/* Also called by libctru on abort/_exit, which can bypass atexit. Do not use
 * stdio here: newlib may already have closed it. The worker must be joined
 * before libctru unmaps the heap containing its stack and PCM buffers. */
void userAppExit(void)
{
    /* abort/_exit can bypass main and atexit. No diagnostic worker may keep
     * using SD services or its stack after libctru tears the process down. */
    extern void n3ds_log_shutdown(void) __attribute__((weak));
    extern void n3ds_stall_watch_stop(void) __attribute__((weak));
    extern void n3ds_prefetch_shutdown(void);
    extern void n3ds_texture_reader_shutdown(void) __attribute__((weak));
    n3ds_sound_reader_shutdown();
    if(n3ds_texture_reader_shutdown) n3ds_texture_reader_shutdown();
    n3ds_prefetch_shutdown();
    n3ds_parallel_shutdown();
    if(n3ds_log_shutdown) n3ds_log_shutdown();
    if(n3ds_stall_watch_stop) n3ds_stall_watch_stop();
    if(ready) {
        for(int i=0;i<count;++i) n3ds_audio_stop(i);
        ndspExit();linearFree(storage);storage=NULL;ready=0;
    }
    /* Fatal exit bypasses main: stop the GSP worker before heap unmapping too. */
    gfxExit();
}
void n3ds_audio_dispose(void)
{
    if(!ready) return;
    for(int i=0;i<count;++i) n3ds_audio_stop(i);
    ndspExit();linearFree(storage);storage=NULL;ready=0;
    char s[180];snprintf(s,sizeof(s),"AUDIO TOTAL: decoded_frames=%u packets=%u refill_underruns=%u",decoded_frames,submitted,underruns);n3ds_log(s);
}
int n3ds_audio_state(int i)
{
    if(!ready || i<0 || i>=count) return 0;
    return voices[i].next.data?2:voices[i].current.data?1:0;
}
int n3ds_audio_queue(int i,const void *data,unsigned int bytes,int channels,int compression)
{
    if(!ready || i<0 || i>=count || !data || !bytes || channels<1 || channels>2 || compression<0 || compression>1 || bytes%(channels*(compression?36:2))) return 0;
    struct voice *v=voices+i;struct clip c={data,bytes,0,channels,compression};
    if(v->current.data) v->next=c;else {v->current=c;ndspChnSetFormat(i,channels==2?NDSP_FORMAT_STEREO_PCM16:NDSP_FORMAT_MONO_PCM16);}
    return 1;
}
void n3ds_audio_properties(int i,float rate,float left,float right)
{
    if(!ready || i<0 || i>=count) return;
    struct voice *v=voices+i;
    float gains[2]={left,right};
    if (!v->rate_valid || memcmp(&v->rate,&rate,sizeof(rate))) {
        ndspChnSetRate(i,rate);
        memcpy(&v->rate,&rate,sizeof(rate));
        v->rate_valid=1;
    }
    if (!v->mix_valid || memcmp(v->gains,gains,sizeof(gains))) {
        float mix[12]={left,right};
        ndspChnSetMix(i,mix);
        memcpy(v->gains,gains,sizeof(gains));
        v->mix_valid=1;
    }
}
void n3ds_audio_pause(int p)
{
    paused=p;for(int i=0;ready && i<count;++i) ndspChnSetPaused(i,p!=0);
}
struct audio_decode_job {
    const unsigned char *source;
    short *pcm;
    unsigned int bytes,frames;
    int channels,compression,voice,packet;
};
static struct audio_decode_job decode_jobs[VOICES*PACKETS];
static int decode_range(void *context,unsigned int begin,unsigned int end)
{
    struct audio_decode_job *jobs=context;
    for(unsigned int i=begin;i<end;++i) {
        struct audio_decode_job *j=jobs+i;
        if(j->compression) j->frames=n3ds_xbox_adpcm_decode(j->source,j->bytes,j->channels,j->pcm,FRAMES*2);
        else {memcpy(j->pcm,j->source,j->bytes);j->frames=j->bytes/(2*j->channels);}
    }
    return 1;
}
void n3ds_audio_update(void)
{
    if(!ready || paused) return;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    u64 profile_start=svcGetSystemTick();
    unsigned int profile_active=0,profile_pcm=0,profile_drains=0,profile_bad=0;
    unsigned int profile_frames=0,profile_packets=0;
#endif
    unsigned int jobs=0,compressed_bytes=0;
    for(int i=0;i<count;++i) {
        struct voice *v=voices+i;
        for(int p=0;p<PACKETS;++p) if(v->wave[p].status==NDSP_WBUF_DONE) {v->wave[p].status=NDSP_WBUF_FREE;--v->outstanding;}
        if(v->current.data && v->current.offset==v->current.bytes && !v->outstanding) {
            v->current=v->next;memset(&v->next,0,sizeof(v->next));
            if(v->current.data) ndspChnSetFormat(i,v->current.channels==2?NDSP_FORMAT_STEREO_PCM16:NDSP_FORMAT_MONO_PCM16);
        }
        struct clip *c=&v->current;
        if(!c->data) continue;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        ++profile_active;
#endif
        if(c->offset && c->offset<c->bytes && !v->outstanding) {
            ++underruns;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
            ++profile_drains;
#endif
        }
        for(int p=0;p<PACKETS && c->offset<c->bytes;++p) if(v->wave[p].status==NDSP_WBUF_FREE) {
            unsigned int bytes=c->compression?(FRAMES/64)*36*c->channels:FRAMES*2*c->channels;
            if(bytes>c->bytes-c->offset) bytes=c->bytes-c->offset;
            decode_jobs[jobs++]=(struct audio_decode_job){c->data+c->offset,v->pcm[p],bytes,0,c->channels,c->compression,i,p};
            if(c->compression) compressed_bytes+=bytes;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
            else profile_pcm+=bytes;
#endif
            c->offset+=bytes;
        }
    }
    /* The codec lookup was initialized by initialize's codec tests before the
     * worker can see it. Callbacks only read encoded bytes and write disjoint
     * PCM/results; no sound-manager or ndsp state is accessed off-thread. */
    /* Tiny final packets and plain PCM copies do not repay a worker wake.
     * Count encoded work, not just descriptors, before splitting a batch. */
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    u64 profile_decode_start=svcGetSystemTick();
#endif
    if(compressed_bytes>=8192) n3ds_parallel_range(decode_range,decode_jobs,jobs,4,0);
    else decode_range(decode_jobs,0,jobs);
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    u64 profile_submit_start=svcGetSystemTick();
#endif
    unsigned int bad=0;
    for(unsigned int i=0;i<jobs;++i) if(!decode_jobs[i].frames) {
        bad|=1u<<decode_jobs[i].voice;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        ++profile_bad;
#endif
    }
    for(int i=0;i<count;++i) if(bad&(1u<<i)) {n3ds_log("AUDIO ERROR: malformed encoded block; voice stopped");n3ds_audio_stop(i);}
    for(unsigned int i=0;i<jobs;++i) {
        struct audio_decode_job *j=decode_jobs+i;
        if(bad&(1u<<j->voice)) continue;
        struct voice *v=voices+j->voice;ndspWaveBuf *wave=v->wave+j->packet;
        decoded_frames+=j->frames;++submitted;
#ifdef HALO_N3DS_ALGORITHM_PROFILE
        profile_frames+=j->frames;++profile_packets;
#endif
        memset(wave,0,sizeof(*wave));wave->data_pcm16=j->pcm;wave->nsamples=j->frames;
        DSP_FlushDataCache(j->pcm,j->frames*j->channels*2);ndspChnWaveBufAdd(j->voice,wave);++v->outstanding;
    }
#ifdef HALO_N3DS_ALGORITHM_PROFILE
    u64 profile_end=svcGetSystemTick();
    /* Owner-only aggregation after the existing decode join and DSP submit.
     * A refill drain is an observation, not a measurement of audible dropout.
     * begin/end can each call update, so these counts are updates, not frames. */
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_UPDATES,1);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_ACTIVE_VOICES,profile_active);
    n3ds_algorithm_profile_max(N3DS_ALG_IO_AUDIO_PEAK_VOICES,profile_active);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_JOBS,jobs);
    n3ds_algorithm_profile_add(compressed_bytes>=8192?N3DS_ALG_IO_AUDIO_RANGE_BATCHES:N3DS_ALG_IO_AUDIO_LOCAL_BATCHES,jobs!=0);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_PCM_BYTES,profile_pcm);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_ADPCM_BYTES,compressed_bytes);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_DECODED_FRAMES,profile_frames);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_PACKETS,profile_packets);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_BAD_JOBS,profile_bad);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_DRAINS,profile_drains);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_PLAN_TICKS,profile_decode_start-profile_start);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_DECODE_TICKS,profile_submit_start-profile_decode_start);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_SUBMIT_TICKS,profile_end-profile_submit_start);
    n3ds_algorithm_profile_add(N3DS_ALG_IO_AUDIO_TOTAL_TICKS,profile_end-profile_start);
    n3ds_algorithm_profile_max(N3DS_ALG_IO_AUDIO_PEAK_TICKS,profile_end-profile_start);
#endif
}
int n3ds_audio_parallel_tests(void)
{
    unsigned char encoded[72]={0};short expected[128];
    static short output[33][130];struct audio_decode_job jobs[33];
    if(!n3ds_audio_codec_tests()) return 0;
    encoded[0]=0xe8;encoded[1]=3;encoded[4]=0x18;encoded[5]=0xfc;
    for(unsigned int channels=1;channels<=2;++channels) {
        unsigned int bytes=36*channels;
        if(n3ds_xbox_adpcm_decode(encoded,bytes,channels,expected,128)!=64) return 0;
        for(unsigned int i=0;i<33;++i) {
            memset(output[i],0x4a,sizeof(output[i]));
            jobs[i]=(struct audio_decode_job){encoded,output[i]+1,bytes,0,channels,1,0,0};
        }
        if(!n3ds_parallel_range(decode_range,jobs,33,2,3)) return 0;
        for(unsigned int i=0;i<33;++i) if(jobs[i].frames!=64 || memcmp(output[i]+1,expected,64*channels*2) || output[i][0]!=0x4a4a || output[i][1+64*channels]!=0x4a4a) return 0;
    }
    n3ds_log("PASS: parallel audio: 66 mono/stereo buffers match serial PCM exactly; guards preserved");return 1;
}
int n3ds_audio_parallel_benchmark(void)
{
    static unsigned char encoded[32*72];
    static short output[8][FRAMES*2],reference[FRAMES*2];
    struct audio_decode_job jobs[8];
    memset(encoded,0,sizeof(encoded));
    for(unsigned int b=0;b<32;++b) {
        encoded[b*72]=0xe8;encoded[b*72+1]=3;encoded[b*72+4]=0x18;encoded[b*72+5]=0xfc;
        for(unsigned int i=8;i<72;++i) encoded[b*72+i]=(unsigned char)(b*17+i*3);
    }
    if(n3ds_xbox_adpcm_decode(encoded,sizeof(encoded),2,reference,FRAMES*2)!=FRAMES) return 0;
    u64 serial=0,parallel=0;
    for(unsigned int i=0;i<8;++i) jobs[i]=(struct audio_decode_job){encoded,output[i],sizeof(encoded),0,2,1,0,0};
    for(unsigned int repeat=0;repeat<12;++repeat) {
        u64 start=svcGetSystemTick();decode_range(jobs,0,8);serial+=svcGetSystemTick()-start;
        start=svcGetSystemTick();n3ds_parallel_range(decode_range,jobs,8,2,3);parallel+=svcGetSystemTick()-start;
        for(unsigned int i=0;i<8;++i) if(jobs[i].frames!=FRAMES || memcmp(output[i],reference,sizeof(reference))) return 0;
    }
    char message[160];double scale=1000.0/SYSCLOCK_ARM11/12;
    snprintf(message,sizeof(message),"CPU WORKER BENCH: audio_stereo_packets=8 serial_ms=%.3f parallel_ms=%.3f bit_exact=1",serial*scale,parallel*scale);n3ds_log(message);return 1;
}
