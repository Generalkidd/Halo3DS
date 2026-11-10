/* Circle Pad Pro input, using the documented ir:USER protocol.
 * Protocol references and hardware validation limits are in DEVELOPMENT.md.
 * Byte decoding deliberately avoids packed structs and unaligned float loads. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "engine_input.h"

struct cpp_calibration { unsigned int x, y; float sx, sy; };
static unsigned int cpp_u16(const unsigned char *p) { return p[0] | p[1]<<8; }
static unsigned char cpp_crc(const unsigned char *p, size_t n)
{
    unsigned int crc=0;
    while(n--) {
        crc^=*p++;
        for(int bit=0;bit<8;++bit) crc=((crc<<1)^((crc&128)?7:0))&255;
    }
    return (unsigned char)crc;
}
static int cpp_calibrate(const unsigned char *p, size_t size, unsigned int offset,
                         struct cpp_calibration *out)
{
    if(size!=69 || p[0]!=0x11 || cpp_u16(p+1)!=offset || cpp_u16(p+3)!=64)return 0;
    for(int i=0;i<4;++i) {
        const unsigned char *c=p+5+i*16;
        struct cpp_calibration v;
        if(cpp_crc(c,15)!=c[15])continue;
        v.x=c[1]|((c[2]&15)<<8);v.y=(c[2]>>4)|(c[3]<<4);
        memcpy(&v.sx,c+4,4);memcpy(&v.sy,c+8,4);
        if(!v.x || v.x>=4095 || !v.y || v.y>=4095 ||
           !isfinite(v.sx) || !isfinite(v.sy) || v.sx<=0 || v.sy<=0 ||
           v.sx>64 || v.sy>64)continue;
        *out=v;return 1;
    }
    return 0;
}
static short cpp_axis(unsigned int raw, unsigned int center, float scale)
{
    /* Factory calibration returns approximately +/-1248; HID uses +/-156. */
    float v=((int)raw-(int)center)*scale*.125f;
    return v>156?156:v<-156?-156:(short)v;
}
static int cpp_input(const unsigned char *p, size_t size,
                     const struct cpp_calibration *c, short *x, short *y)
{
    if(size!=6 || p[0]!=0x10)return 0;
    *x=cpp_axis(p[1]|((p[2]&15)<<8),c->x,c->sx);
    *y=cpp_axis((p[2]>>4)|(p[3]<<4),c->y,c->sy);
    return 1;
}
/* Unwrap only a fully validated packet; malformed lengths cannot cross the
 * receive ring into send metadata or overrun the caller's buffer. */
static int cpp_packet(const volatile unsigned char *ring, unsigned int ring_size,
                      unsigned int offset, unsigned int size, unsigned char *out,
                      unsigned int capacity)
{
    unsigned char packet[80];
    if(!ring_size || offset>=ring_size || size<4 || size>sizeof(packet) || size>ring_size)return 0;
    for(unsigned int i=0;i<size;++i)packet[i]=ring[(offset+i)%ring_size];
    unsigned int header=(packet[2]&64)?4:3;
    unsigned int length=packet[2]&63;
    if(header==4)length=(length<<8)|packet[3];
    if(packet[0]!=0xa5 || length>capacity || length+header+1!=size || cpp_crc(packet,size))return 0;
    memcpy(out,packet+header,length);return (int)length;
}

#ifndef HALO_CPP_PROTOCOL_TEST
#include <3ds.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>

enum { CPP_MEMORY=4096, CPP_RECV=2048, CPP_SEND=1024, CPP_PACKETS=32 };
#define CPP_MS 1000000LL
static Thread cpp_thread;
static LightLock cpp_lock;
static Handle cpp_wake;
static aptHookCookie cpp_hook;
static int cpp_enabled,cpp_suspended,cpp_stopping,cpp_failed;
static unsigned int cpp_epoch;
static short cpp_x,cpp_y;
static u64 cpp_sample_tick;
void n3ds_log(const char *message);

struct cpp_session {
    Handle service,memory,receive,connection;
    volatile unsigned char *shared;
    int initialized;
};
static Result cpp_command(struct cpp_session *s, unsigned int id, int words, u32 value)
{
    u32 *c=getThreadCommandBuffer();c[0]=IPC_MakeHeader(id,words,0);c[1]=value;
    Result r=svcSendSyncRequest(s->service);return R_FAILED(r)?r:(Result)c[1];
}
static Result cpp_event(struct cpp_session *s,unsigned int id,Handle *event)
{
    Result r=cpp_command(s,id,0,0);
    if(R_SUCCEEDED(r))*event=getThreadCommandBuffer()[3];
    return r;
}
static Result cpp_send(struct cpp_session *s,const unsigned char *data,unsigned int size)
{
    u32 *c=getThreadCommandBuffer();c[0]=IPC_MakeHeader(0xd,1,2);
    c[1]=size;c[2]=IPC_Desc_StaticBuffer(size,0);c[3]=(u32)data;
    Result r=svcSendSyncRequest(s->service);return R_FAILED(r)?r:(Result)c[1];
}
static void cpp_neutral(void)
{
    LightLock_Lock(&cpp_lock);cpp_x=cpp_y=0;cpp_sample_tick=0;LightLock_Unlock(&cpp_lock);
}
static int cpp_wanted(unsigned int epoch)
{
    LightLock_Lock(&cpp_lock);
    int yes=cpp_enabled && !cpp_suspended && !cpp_stopping && epoch==cpp_epoch;
    LightLock_Unlock(&cpp_lock);return yes;
}
static void cpp_close(struct cpp_session *s)
{
    cpp_neutral();
    if(s->initialized) {cpp_command(s,9,0,0);cpp_command(s,2,0,0);}
    if(s->receive)svcCloseHandle(s->receive);
    if(s->connection)svcCloseHandle(s->connection);
    if(s->memory)svcCloseHandle(s->memory);
    if(s->service)svcCloseHandle(s->service);
    free((void *)s->shared);memset(s,0,sizeof(*s));
}
static Result cpp_open(struct cpp_session *s)
{
    Result r=srvGetServiceHandle(&s->service,"ir:USER");if(R_FAILED(r))return r;
    s->shared=memalign(4096,CPP_MEMORY);if(!s->shared)return -1;
    memset((void *)s->shared,0,CPP_MEMORY);
    r=svcCreateMemoryBlock(&s->memory,(u32)s->shared,CPP_MEMORY,MEMPERM_READ,MEMPERM_READWRITE);
    if(R_FAILED(r))return r;
    u32 *c=getThreadCommandBuffer();c[0]=IPC_MakeHeader(0x18,6,2);
    c[1]=CPP_MEMORY;c[2]=CPP_RECV;c[3]=CPP_PACKETS;
    c[4]=CPP_SEND;c[5]=CPP_PACKETS;c[6]=4;c[7]=IPC_Desc_SharedHandles(1);c[8]=s->memory;
    r=svcSendSyncRequest(s->service);if(R_FAILED(r))return r;
    r=(Result)c[1];if(R_FAILED(r))return r;s->initialized=1;
    r=cpp_event(s,0xa,&s->receive);if(R_FAILED(r))return r;
    return cpp_event(s,0xc,&s->connection);
}
static int cpp_wait(Handle event,s64 timeout)
{
    Handle events[2]={cpp_wake,event};s32 which=-1;
    Result r=svcWaitSynchronizationN(&which,events,2,false,timeout);
    return R_SUCCEEDED(r) && which==1;
}
static int cpp_receive(struct cpp_session *s,unsigned char *out,unsigned int capacity)
{
    const volatile u32 *info=(const volatile u32 *)(s->shared+0x10);
    u32 index=info[0],count=info[2];
    if(!count)return 0;
    if(index>=CPP_PACKETS || count>CPP_PACKETS)return -1;
    const volatile u32 *p=(const volatile u32 *)(s->shared+0x20+index*8);
    int n=cpp_packet(s->shared+0x20+CPP_PACKETS*8,CPP_RECV-CPP_PACKETS*8,p[0],p[1],out,capacity);
    if(R_FAILED(cpp_command(s,0x19,1,1)))return -1;
    return n;
}
static int cpp_read_calibration(struct cpp_session *s,unsigned int epoch,struct cpp_calibration *cal)
{
    /* Read-only requests: never reset or write the accessory's calibration.
     * Try the factory block, then bounded alternate calibration blocks. */
    for(unsigned int block=0;block<65 && cpp_wanted(epoch);++block) {
        unsigned int offset=block?0x400+(block-1)*16:0;
        unsigned char request[6]={2,100,(unsigned char)offset,(unsigned char)(offset>>8),64,0};
        for(int attempt=0;attempt<2 && cpp_wanted(epoch);++attempt) {
            if(R_FAILED(cpp_send(s,request,sizeof(request))))return 0;
            u64 deadline=svcGetSystemTick()+SYSCLOCK_ARM11/5;
            do {
                unsigned char packet[69];int n=cpp_receive(s,packet,sizeof(packet));
                if(n<0)return 0;
                if(n==69 && packet[0]==0x11 && cpp_u16(packet+1)==offset) {
                    if(cpp_calibrate(packet,n,offset,cal))return 1;
                    break;
                }
                cpp_wait(s->receive,20*CPP_MS);
            } while(cpp_wanted(epoch) && svcGetSystemTick()<deadline);
        }
    }
    return 0;
}
static void cpp_run(struct cpp_session *s,unsigned int epoch)
{
    for(int attempt=0;attempt<4 && cpp_wanted(epoch);++attempt) {
        if(R_FAILED(cpp_command(s,6,1,1)))return;
        cpp_wait(s->connection,20*CPP_MS);
        if(s->shared[8]==2)break;
        cpp_command(s,9,0,0);
    }
    if(!cpp_wanted(epoch) || s->shared[8]!=2)return;
    struct cpp_calibration calibration;
    if(!cpp_read_calibration(s,epoch,&calibration))return;
    const unsigned char request[3]={1,20,0x87};
    u64 last_input=svcGetSystemTick(),last_request=0;
    int announced=0;
    while(cpp_wanted(epoch) && s->shared[8]==2) {
        u64 now=svcGetSystemTick();
        if(!last_request || now-last_request>SYSCLOCK_ARM11/10) {
            if(R_FAILED(cpp_send(s,request,sizeof(request))))break;
            last_request=now;
        }
        /* Drain bounded batches so returning from a debugger/app menu cannot
         * leave several seconds of old stick movement queued up. */
        for(int i=0;i<CPP_PACKETS;++i) {
            unsigned char packet[69];short x,y;
            int n=cpp_receive(s,packet,sizeof(packet));if(n<0)return;
            if(!n)break;
            if(cpp_input(packet,n,&calibration,&x,&y)) {
                last_input=svcGetSystemTick();
                LightLock_Lock(&cpp_lock);
                if(cpp_enabled && !cpp_suspended && epoch==cpp_epoch) {
                    cpp_x=x;cpp_y=y;cpp_sample_tick=last_input;
                }
                LightLock_Unlock(&cpp_lock);
                if(!announced){n3ds_log("CIRCLE PAD PRO: calibrated right stick connected");announced=1;}
            }
        }
        if(svcGetSystemTick()-last_input>SYSCLOCK_ARM11/5)break;
        cpp_wait(s->receive,20*CPP_MS);
    }
    if(announced)n3ds_log("CIRCLE PAD PRO: disconnected or suspended");
}
static void cpp_worker(void *unused)
{
    (void)unused;int reported_error=0;
    for(;;) {
        LightLock_Lock(&cpp_lock);int stop=cpp_stopping;unsigned int epoch=cpp_epoch;LightLock_Unlock(&cpp_lock);
        if(stop)break;
        if(cpp_wanted(epoch)) {
            struct cpp_session s={0};Result r=cpp_open(&s);
            if(R_SUCCEEDED(r))cpp_run(&s,epoch);
            else if(!reported_error) {
                char message[96];snprintf(message,sizeof(message),"CIRCLE PAD PRO: optional IR service unavailable (%08lx)",(unsigned long)r);
                n3ds_log(message);reported_error=1;
            }
            cpp_close(&s);
        }
        svcWaitSynchronization(cpp_wake,1000*CPP_MS);
    }
}
static void cpp_app_event(APT_HookType event,void *unused)
{
    (void)unused;
    LightLock_Lock(&cpp_lock);
    if(event==APTHOOK_ONSLEEP || event==APTHOOK_ONSUSPEND || event==APTHOOK_ONEXIT) {
        cpp_suspended|=event==APTHOOK_ONSLEEP?1:event==APTHOOK_ONSUSPEND?2:4;
        ++cpp_epoch;cpp_sample_tick=0;cpp_x=cpp_y=0;
    } else if(event==APTHOOK_ONWAKEUP)cpp_suspended&=~1;
    else if(event==APTHOOK_ONRESTORE)cpp_suspended&=~2;
    LightLock_Unlock(&cpp_lock);svcSignalEvent(cpp_wake);
}
void n3ds_cpp_dispose(void)
{
    if(cpp_thread) {
        aptUnhook(&cpp_hook);
        LightLock_Lock(&cpp_lock);cpp_stopping=1;LightLock_Unlock(&cpp_lock);
        svcSignalEvent(cpp_wake);
        /* Worker waits are interruptible. Join before releasing shared memory;
         * never detach a thread still holding IR service handles. */
        threadJoin(cpp_thread,U64_MAX);threadFree(cpp_thread);cpp_thread=NULL;
        svcCloseHandle(cpp_wake);cpp_wake=0;
    }
    cpp_failed=cpp_enabled=cpp_stopping=cpp_suspended=0;
    cpp_sample_tick=0;cpp_x=cpp_y=0;
}
void n3ds_cpp_poll(struct native_input_sample *sample,int enabled)
{
    if(!cpp_thread) {
        if(!enabled || cpp_failed)return;
        LightLock_Init(&cpp_lock);cpp_enabled=1;cpp_stopping=cpp_suspended=0;++cpp_epoch;
        Result r=svcCreateEvent(&cpp_wake,RESET_ONESHOT);
        if(R_SUCCEEDED(r))cpp_thread=threadCreate(cpp_worker,NULL,8192,0x30,0,false);
        if(!cpp_thread) {
            if(cpp_wake)svcCloseHandle(cpp_wake);cpp_wake=0;cpp_failed=1;
            n3ds_log("CIRCLE PAD PRO: optional worker unavailable; normal controls retained");return;
        }
        aptHook(&cpp_hook,cpp_app_event,NULL);
    }
    LightLock_Lock(&cpp_lock);
    int changed=cpp_enabled!=!!enabled;
    if(changed){cpp_enabled=!!enabled;++cpp_epoch;cpp_x=cpp_y=0;cpp_sample_tick=0;}
    if(enabled && !cpp_suspended && cpp_sample_tick &&
       svcGetSystemTick()-cpp_sample_tick<=SYSCLOCK_ARM11/5) {
        sample->cstick_x=cpp_x;sample->cstick_y=cpp_y;
    }
    LightLock_Unlock(&cpp_lock);
    if(changed)svcSignalEvent(cpp_wake);
}
#endif
