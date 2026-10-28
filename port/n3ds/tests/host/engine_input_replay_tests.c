#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/engine_files.h"

static unsigned char *file_data;
static unsigned int file_size,file_position,read_calls,close_calls;
static int fail_open,fail_read,fail_close;
static char last_log[256];
void n3ds_log(const char *message) { snprintf(last_log,sizeof(last_log),"%s",message); }
unsigned int n3ds_file_open(const char *path,unsigned int permissions)
{
    if(strcmp(path,"t:\\frame-input.replay") || permissions!=1 || fail_open) return 0;
    file_position=0;return 1;
}
unsigned int n3ds_file_size(unsigned int handle) { return handle==1?file_size:~0u; }
int n3ds_file_read(unsigned int handle,void *buffer,unsigned int size)
{
    ++read_calls;
    if(handle!=1 || fail_read==(int)read_calls || size>file_size-file_position) return 0;
    memcpy(buffer,file_data+file_position,size);file_position+=size;return 1;
}
int n3ds_file_close(unsigned int handle) { ++close_calls;return handle==1 && !fail_close; }
#include "../../include/engine_input_replay.inl"

#define CHECK(test) do {if(!(test)){fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#test,last_log);return 1;}}while(0)
static void put16(unsigned char *p,unsigned int value)
{
    p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);
}
static void put32(unsigned char *p,unsigned int value)
{
    put16(p,value);put16(p+2,value>>16);
}
static void reset_mock(unsigned char *data,unsigned int size)
{
    file_data=data;file_size=size;file_position=read_calls=close_calls=0;
    fail_open=fail_read=fail_close=0;last_log[0]=0;
}
static void update_checksum(unsigned char *data)
{
    put32(data+20,n3ds_replay_checksum(data,data+N3DS_INPUT_REPLAY_HEADER_SIZE,2*N3DS_INPUT_REPLAY_SAMPLE_SIZE));
}
static void make_fixture(unsigned char *data)
{
    unsigned char *first=data+N3DS_INPUT_REPLAY_HEADER_SIZE,*second=first+N3DS_INPUT_REPLAY_SAMPLE_SIZE;
    memset(data,0,N3DS_INPUT_REPLAY_HEADER_SIZE+2*N3DS_INPUT_REPLAY_SAMPLE_SIZE);
    memcpy(data,"H3IR",4);put32(data+4,1);put32(data+8,2);put32(data+12,0x12345678u);
    put32(first,0x40000000u);put16(first+4,0x8000);put16(first+6,0x7fff);
    put16(first+8,0xfffe);put16(first+10,3);put16(first+12,65535);put16(first+14,65535);
    put32(second,NATIVE_KEY_TOUCH|NATIVE_KEY_X);put16(second+4,156);
    put16(second+6,(unsigned int)-156);put16(second+12,319);put16(second+14,239);
    update_checksum(data);
}
static int reject(unsigned char *data,unsigned int size,unsigned int expected,const char *reason)
{
    struct n3ds_input_replay replay={0};
    reset_mock(data,size);
    if(n3ds_input_replay_load(&replay,expected) || replay.samples || close_calls!=1 || !strstr(last_log,reason)) {
        fprintf(stderr,"Expected rejection %s: %s\n",reason,last_log);return 1;
    }
    return 0;
}
static int test_loader(void)
{
    unsigned char data[N3DS_INPUT_REPLAY_HEADER_SIZE+2*N3DS_INPUT_REPLAY_SAMPLE_SIZE];
    struct n3ds_input_replay replay={0};
    make_fixture(data);reset_mock(data,sizeof(data));
    CHECK(n3ds_input_replay_load(&replay,2));
    CHECK(replay.frames==2 && replay.seed==0x12345678u && close_calls==1);
    CHECK(replay.samples[0].held==0x40000000u && replay.samples[0].circle_x==-32768);
    CHECK(replay.samples[0].circle_y==32767 && replay.samples[0].cstick_x==-2);
    CHECK(replay.samples[0].cstick_y==3 && replay.samples[0].touch_x==65535);
    CHECK(replay.samples[1].held==(NATIVE_KEY_TOUCH|NATIVE_KEY_X));
    CHECK(replay.samples[1].circle_y==-156 && replay.samples[1].touch_x==319 && replay.samples[1].touch_y==239);
    n3ds_input_replay_dispose(&replay);CHECK(!replay.samples && !replay.frames);

    reset_mock(data,sizeof(data));fail_open=1;
    CHECK(!n3ds_input_replay_load(&replay,2) && !close_calls && strstr(last_log,"cannot open"));
    reset_mock(data,sizeof(data));fail_read=1;
    CHECK(!n3ds_input_replay_load(&replay,2) && close_calls==1 && strstr(last_log,"header"));
    reset_mock(data,sizeof(data));fail_read=2;
    CHECK(!n3ds_input_replay_load(&replay,2) && close_calls==1 && strstr(last_log,"payload"));
    reset_mock(data,sizeof(data));fail_close=1;
    CHECK(!n3ds_input_replay_load(&replay,2) && close_calls==1 && strstr(last_log,"close"));
    CHECK(!reject(data,10,2,"file size"));
    CHECK(!reject(data,N3DS_INPUT_REPLAY_HEADER_SIZE+N3DS_INPUT_REPLAY_MAX_FRAMES*N3DS_INPUT_REPLAY_SAMPLE_SIZE+1,2,"file size"));
    CHECK(!reject(data,sizeof(data)-1,2,"file length"));
    CHECK(!reject(data,sizeof(data),3,"frame count"));
    data[0]='X';CHECK(!reject(data,sizeof(data),2,"header"));make_fixture(data);
    put32(data+4,2);CHECK(!reject(data,sizeof(data),2,"header"));make_fixture(data);
    put32(data+16,1);CHECK(!reject(data,sizeof(data),2,"header"));make_fixture(data);
    put32(data+8,0);CHECK(!reject(data,sizeof(data),2,"frame count"));make_fixture(data);
    put32(data+8,N3DS_INPUT_REPLAY_MAX_FRAMES+1);CHECK(!reject(data,sizeof(data),2,"frame count"));make_fixture(data);
    data[24]^=1;CHECK(!reject(data,sizeof(data),2,"checksum"));make_fixture(data);
    put16(data+N3DS_INPUT_REPLAY_HEADER_SIZE+N3DS_INPUT_REPLAY_SAMPLE_SIZE+12,320);
    update_checksum(data);CHECK(!reject(data,sizeof(data),2,"invalid touch"));make_fixture(data);
    put32(data+N3DS_INPUT_REPLAY_HEADER_SIZE,NATIVE_KEY_START|NATIVE_KEY_SELECT);
    update_checksum(data);CHECK(!reject(data,sizeof(data),2,"stop chord"));
    puts("PASS: frame replay loader validates inputs, bounds, lifecycle and I/O failures");
    return 0;
}
static int test_fixture_file(const char *path,unsigned int expected)
{
    FILE *stream=fopen(path,"rb");unsigned char *data;long length;
    struct n3ds_input_replay replay={0};
    CHECK(stream && !fseek(stream,0,SEEK_END) && (length=ftell(stream))>=0 && !fseek(stream,0,SEEK_SET));
    CHECK((unsigned long)length<=N3DS_INPUT_REPLAY_HEADER_SIZE+N3DS_INPUT_REPLAY_MAX_FRAMES*N3DS_INPUT_REPLAY_SAMPLE_SIZE);
    data=malloc((size_t)length);CHECK(data && fread(data,1,(size_t)length,stream)==(size_t)length && !fclose(stream));
    reset_mock(data,(unsigned int)length);CHECK(n3ds_input_replay_load(&replay,expected));
    printf("PASS: external fixture frames=%u seed=%08x checksum=%08x\n",replay.frames,replay.seed,replay.checksum);
    n3ds_input_replay_dispose(&replay);free(data);return 0;
}
int main(int argc,char **argv)
{
    if(argc==3) {
        char *end;unsigned long count=strtoul(argv[2],&end,10);
        if(!argv[2][0] || *end || !count || count>N3DS_INPUT_REPLAY_MAX_FRAMES) return 2;
        return test_fixture_file(argv[1],(unsigned int)count);
    }
    if(argc!=1) return 2;
    return test_loader();
}
