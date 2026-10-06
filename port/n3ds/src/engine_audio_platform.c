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
#define VOICES 24
#define PACKETS 4
#define FRAMES 2048
struct clip {const unsigned char *data;unsigned int bytes,offset;int channels,compression;};
struct voice {struct clip current,next;ndspWaveBuf wave[PACKETS];short *pcm[PACKETS];int outstanding;};
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
    float mix[12]={0};mix[0]=left;mix[1]=right;
    ndspChnSetRate(i,rate);ndspChnSetMix(i,mix);
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
        if(c->offset && c->offset<c->bytes && !v->outstanding) ++underruns;
        for(int p=0;p<PACKETS && c->offset<c->bytes;++p) if(v->wave[p].status==NDSP_WBUF_FREE) {
            unsigned int bytes=c->compression?(FRAMES/64)*36*c->channels:FRAMES*2*c->channels;
            if(bytes>c->bytes-c->offset) bytes=c->bytes-c->offset;
            decode_jobs[jobs++]=(struct audio_decode_job){c->data+c->offset,v->pcm[p],bytes,0,c->channels,c->compression,i,p};
            if(c->compression) compressed_bytes+=bytes;
            c->offset+=bytes;
        }
    }
    /* The codec lookup was initialized by initialize's codec tests before the
     * worker can see it. Callbacks only read encoded bytes and write disjoint
     * PCM/results; no sound-manager or ndsp state is accessed off-thread. */
    /* Tiny final packets and plain PCM copies do not repay a worker wake.
     * Count encoded work, not just descriptors, before splitting a batch. */
    if(compressed_bytes>=8192) n3ds_parallel_range(decode_range,decode_jobs,jobs,4,0);
    else decode_range(decode_jobs,0,jobs);
    unsigned int bad=0;
    for(unsigned int i=0;i<jobs;++i) if(!decode_jobs[i].frames) bad|=1u<<decode_jobs[i].voice;
    for(int i=0;i<count;++i) if(bad&(1u<<i)) {n3ds_log("AUDIO ERROR: malformed encoded block; voice stopped");n3ds_audio_stop(i);}
    for(unsigned int i=0;i<jobs;++i) {
        struct audio_decode_job *j=decode_jobs+i;
        if(bad&(1u<<j->voice)) continue;
        struct voice *v=voices+j->voice;ndspWaveBuf *wave=v->wave+j->packet;
        decoded_frames+=j->frames;++submitted;
        memset(wave,0,sizeof(*wave));wave->data_pcm16=j->pcm;wave->nsamples=j->frames;
        DSP_FlushDataCache(j->pcm,j->frames*j->channels*2);ndspChnWaveBufAdd(j->voice,wave);++v->outstanding;
    }
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
