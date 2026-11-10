/* New3DS integration additions by Kevin Boggs. */
static Thread sound_reader;
static Handle sound_reader_thread,sound_reader_file;
static LightEvent sound_start,sound_wake;
static unsigned char *sound_destination;
static u32 sound_read_offset,sound_read_bytes;
static int sound_cancel,sound_urgent,sound_result,sound_exit_registered;
void n3ds_sound_read_shutdown(void);
/* Kevin Boggs: sound_read_worker */
static void sound_read_worker(void *unused)
{
    (void)unused;LightEvent_Wait(&sound_start);
    /* Sound samples use a separate read-only handle; never hold the shared
     * map/texture stream lock while waiting on SD reads. */
    
    u32 done=0;
    while(done<sound_read_bytes && !__atomic_load_n(&sound_cancel,__ATOMIC_ACQUIRE)) {
        u32 bytes=sound_read_bytes-done,got=0;if(bytes>16384) bytes=16384;
        if(R_FAILED(FSFILE_Read(sound_reader_file,&got,(u64)sound_read_offset+done,sound_destination+done,bytes)) || !got || got>bytes) break;
        done+=got;
        if(!__atomic_load_n(&sound_urgent,__ATOMIC_ACQUIRE)) LightEvent_WaitTimeout(&sound_wake,250000LL);
    }
    sound_result=done==sound_read_bytes;
}
/* Kevin Boggs: n3ds_sound_read_begin */
int n3ds_sound_read_begin(const char *path,void *data,unsigned int offset,unsigned int bytes)
{
    if(sound_reader || !path || strncmp(path,"sdmc:/",6) || !data || !bytes) return 0;
    if(R_FAILED(FSUSER_OpenFileDirectly(&sound_reader_file,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,NULL),fsMakePath(PATH_ASCII,path+5),FS_OPEN_READ,0))) return 0;
    u64 size=0;
    if(R_FAILED(FSFILE_GetSize(sound_reader_file,&size)) || offset>size || bytes>size-offset) {FSFILE_Close(sound_reader_file);sound_reader_file=0;return 0;}
    sound_destination=data;sound_read_offset=offset;sound_read_bytes=bytes;sound_cancel=sound_urgent=sound_result=0;
    LightEvent_Init(&sound_start,RESET_ONESHOT);LightEvent_Init(&sound_wake,RESET_ONESHOT);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    sound_reader=threadCreate(sound_read_worker,NULL,16384,priority<0x3d?priority+2:priority,-2,false);
    if(!sound_reader) {FSFILE_Close(sound_reader_file);sound_reader_file=0;sound_destination=NULL;return 0;}
    sound_reader_thread=threadGetHandle(sound_reader);
    if(!sound_exit_registered) {atexit(n3ds_sound_read_shutdown);sound_exit_registered=1;}
    LightEvent_Signal(&sound_start);return 1;
}
/* 0 = still running, 1 = full read, -1 = failed/cancelled. */
/* Kevin Boggs: n3ds_sound_read_collect */
int n3ds_sound_read_collect(int wait)
{
    if(!sound_reader) return -1;
    if(wait) {__atomic_store_n(&sound_urgent,1,__ATOMIC_RELEASE);LightEvent_Signal(&sound_wake);}
    Result ready=svcWaitSynchronization(sound_reader_thread,wait?U64_MAX:0);
    if(ready!=0) return 0;
    int success=sound_result;threadFree(sound_reader);sound_reader=NULL;sound_reader_thread=0;
    FSFILE_Close(sound_reader_file);sound_reader_file=0;sound_destination=NULL;
    return success?1:-1;
}
/* Kevin Boggs: n3ds_sound_read_shutdown */
void n3ds_sound_read_shutdown(void)
{
    if(!sound_reader) return;
    __atomic_store_n(&sound_cancel,1,__ATOMIC_RELEASE);LightEvent_Signal(&sound_wake);
    if(svcWaitSynchronization(sound_reader_thread,U64_MAX)!=0) svcBreak(USERBREAK_PANIC);
    threadFree(sound_reader);sound_reader=NULL;sound_reader_thread=0;FSFILE_Close(sound_reader_file);sound_reader_file=0;sound_destination=NULL;
}
