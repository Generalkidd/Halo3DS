#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "engine_files.h"
#include "engine_input.h"

#define N3DS_INPUT_REPLAY_MAX_FRAMES 12000u
enum { N3DS_INPUT_REPLAY_HEADER_SIZE=24, N3DS_INPUT_REPLAY_SAMPLE_SIZE=16 };

struct n3ds_input_replay {
    struct native_input_sample *samples;
    unsigned int frames, seed, checksum;
};

static unsigned int n3ds_replay_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1]<<8);
}
static uint32_t n3ds_replay_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
        ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static short n3ds_replay_s16(const unsigned char *p)
{
    unsigned int value=n3ds_replay_u16(p);
    return (short)(value<32768u?(int)value:(int)value-65536);
}
static uint32_t n3ds_replay_checksum(const unsigned char *header,
    const unsigned char *payload,unsigned int length)
{
    uint32_t value=2166136261u;
    unsigned int i;
    for(i=0;i<20;++i) value=(value^header[i])*16777619u;
    for(i=0;i<length;++i) value=(value^payload[i])*16777619u;
    return value;
}
static void n3ds_input_replay_dispose(struct n3ds_input_replay *replay)
{
    free(replay->samples);
    memset(replay,0,sizeof(*replay));
}
static int n3ds_input_replay_load(struct n3ds_input_replay *replay,unsigned int expected_frames)
{
    unsigned char header[N3DS_INPUT_REPLAY_HEADER_SIZE],*payload=NULL;
    struct native_input_sample *samples=NULL;
    unsigned int handle=0,size,frames,seed,expected_checksum,payload_size,i;
    const char *error="INPUT REPLAY FAIL: unknown error";
    char message[160];
    handle=n3ds_file_open("t:\\frame-input.replay",1);
    if(!handle) {error="INPUT REPLAY FAIL: cannot open t:\\frame-input.replay";goto fail;}
    size=n3ds_file_size(handle);
    if(size<N3DS_INPUT_REPLAY_HEADER_SIZE ||
       size>N3DS_INPUT_REPLAY_HEADER_SIZE+N3DS_INPUT_REPLAY_MAX_FRAMES*N3DS_INPUT_REPLAY_SAMPLE_SIZE) {
        error="INPUT REPLAY FAIL: file size is outside the allowed bounds";goto fail;
    }
    if(!n3ds_file_read(handle,header,sizeof(header))) {
        error="INPUT REPLAY FAIL: cannot read the complete header";goto fail;
    }
    if(memcmp(header,"H3IR",4) || n3ds_replay_u32(header+4)!=1 || n3ds_replay_u32(header+16)) {
        error="INPUT REPLAY FAIL: invalid or unsupported header";goto fail;
    }
    frames=n3ds_replay_u32(header+8);
    seed=n3ds_replay_u32(header+12);
    expected_checksum=n3ds_replay_u32(header+20);
    if(!frames || frames>N3DS_INPUT_REPLAY_MAX_FRAMES || frames!=expected_frames) {
        error="INPUT REPLAY FAIL: frame count does not match this bounded build";goto fail;
    }
    payload_size=frames*N3DS_INPUT_REPLAY_SAMPLE_SIZE;
    if(size!=N3DS_INPUT_REPLAY_HEADER_SIZE+payload_size) {
        error="INPUT REPLAY FAIL: file length does not match its frame count";goto fail;
    }
    payload=malloc(payload_size);
    if(!payload) {error="INPUT REPLAY FAIL: cannot allocate input payload";goto fail;}
    if(!n3ds_file_read(handle,payload,payload_size)) {
        error="INPUT REPLAY FAIL: cannot read the complete input payload";goto fail;
    }
    if(!n3ds_file_close(handle)) {
        handle=0;error="INPUT REPLAY FAIL: cannot close the input file";goto fail;
    }
    handle=0;
    if(n3ds_replay_checksum(header,payload,payload_size)!=expected_checksum) {
        error="INPUT REPLAY FAIL: checksum mismatch";goto fail;
    }
    samples=malloc(frames*sizeof(*samples));
    if(!samples) {error="INPUT REPLAY FAIL: cannot allocate frame samples";goto fail;}
    for(i=0;i<frames;++i) {
        const unsigned char *p=payload+i*N3DS_INPUT_REPLAY_SAMPLE_SIZE;
        struct native_input_sample *sample=&samples[i];
        sample->held=n3ds_replay_u32(p);
        sample->circle_x=n3ds_replay_s16(p+4);sample->circle_y=n3ds_replay_s16(p+6);
        sample->cstick_x=n3ds_replay_s16(p+8);sample->cstick_y=n3ds_replay_s16(p+10);
        sample->touch_x=n3ds_replay_u16(p+12);sample->touch_y=n3ds_replay_u16(p+14);
        if(((sample->held&NATIVE_KEY_TOUCH) && (sample->touch_x>=320 || sample->touch_y>=240)) ||
           (sample->held&(NATIVE_KEY_START|NATIVE_KEY_SELECT))==(NATIVE_KEY_START|NATIVE_KEY_SELECT)) {
            error="INPUT REPLAY FAIL: invalid touch or physical stop chord in input";goto fail;
        }
    }
    free(payload);
    replay->samples=samples;replay->frames=frames;replay->seed=seed;replay->checksum=expected_checksum;
    snprintf(message,sizeof(message),"INPUT REPLAY START: version=1 frames=%u seed=%08x checksum=%08x",
        frames,seed,expected_checksum);
    n3ds_log(message);
    return 1;
fail:
    if(handle && !n3ds_file_close(handle)) n3ds_log("INPUT REPLAY FAIL: cannot close the input file");
    free(payload);free(samples);n3ds_log(error);
    return 0;
}
