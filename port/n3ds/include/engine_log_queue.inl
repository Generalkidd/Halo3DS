/* Keep routine SD diagnostics off the frame thread. The lock protects only
 * RAM copies: the writer never holds it while waiting for the filesystem. */
enum { LOG_QUEUE_BYTES=32768, LOG_WRITE_BYTES=4096 };
static char log_queue[LOG_QUEUE_BYTES];
static unsigned int log_read,log_write,log_count,log_dropped;
static LightLock log_lock;
static Thread log_thread;
static Handle log_handle;
static u64 log_offset;
static unsigned int log_io_errors;
static int log_stop;
static int log_attempted;
static void log_async_finish(void);
/* A worker must not enter newlib's FILE/heap locks while the game reclaims
 * graphics/cache memory. The pre-opened native file needs only stack IPC. */
static void log_write_block(const char *data,unsigned int bytes)
{
    while(bytes) {
        u32 written=0;
        Result result=FSFILE_Write(log_handle,&written,log_offset,data,bytes,FS_WRITE_FLUSH);
        if(R_FAILED(result) || !written || written>bytes) {++log_io_errors;return;}
        log_offset+=written;data+=written;bytes-=written;
    }
}
static void log_writer(void *unused)
{
    char block[LOG_WRITE_BYTES];
    static const char started[]="LOG QUEUE: native SD writer active; no worker FILE or heap calls\n";
    log_write_block(started,sizeof(started)-1);
    for(;;) {
        LightLock_Lock(&log_lock);
        unsigned int bytes=log_count>sizeof(block)?sizeof(block):log_count;
        for(unsigned int i=0;i<bytes;++i) block[i]=log_queue[(log_read+i)%LOG_QUEUE_BYTES];
        log_read=(log_read+bytes)%LOG_QUEUE_BYTES;log_count-=bytes;
        int done=log_stop && !bytes;
        LightLock_Unlock(&log_lock);
        if(done) break;
        if(bytes) log_write_block(block,bytes);
        else svcSleepThread(100000000LL);
    }
}
static void log_async_start(void)
{
    if(!log_file || log_thread || __sync_lock_test_and_set(&log_attempted,1)) return;
    /* Called once, after the loading console transfers to GPU ownership.
     * All subsequent normal writes belong to this one worker. */
    fflush(log_file);
    Result opened=FSUSER_OpenFileDirectly(&log_handle,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,NULL),
        fsMakePath(PATH_ASCII,"/halo-source-engine.log"),FS_OPEN_WRITE,0);
    if(R_FAILED(opened)) {log_handle=0;return;}
    if(R_FAILED(FSFILE_GetSize(log_handle,&log_offset))) {FSFILE_Close(log_handle);log_handle=0;return;}
    fclose(log_file);log_file=NULL;LightLock_Init(&log_lock);
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    log_thread=threadCreate(log_writer,NULL,16384,priority<0x3e?priority+1:priority,-2,false);
    if(log_thread) atexit(log_async_finish);
    if(!log_thread) {
        FSFILE_Close(log_handle);log_handle=0;
        log_file=fopen("sdmc:/halo-source-engine.log","a");
        if(log_file) setvbuf(log_file,log_buffer,_IOFBF,sizeof(log_buffer));
    }
}
static void log_enqueue(const char *message)
{
    size_t bytes=strlen(message);
    LightLock_Lock(&log_lock);
    if(bytes+1<=LOG_QUEUE_BYTES-log_count) {
        for(size_t i=0;i<bytes;++i) {log_queue[log_write]=message[i];log_write=(log_write+1)%LOG_QUEUE_BYTES;}
        log_queue[log_write]='\n';log_write=(log_write+1)%LOG_QUEUE_BYTES;log_count+=bytes+1;
    } else ++log_dropped; /* Diagnostics must not block gameplay on a slow SD. */
    LightLock_Unlock(&log_lock);
}
static void log_async_finish(void)
{
    if(!log_thread) return;
    /* libctru marks 'finished' before svcExitThread. Capture the handle while
     * the worker is alive, then wait for kernel completion before freeing its
     * stack; threadJoin's early finished check is not that completion fence. */
    Handle handle=threadGetHandle(log_thread);
    LightLock_Lock(&log_lock);log_stop=1;LightLock_Unlock(&log_lock);
    if(svcWaitSynchronization(handle,U64_MAX)!=0) svcBreak(USERBREAK_PANIC);
    threadFree(log_thread);log_thread=NULL;
    char text[160];snprintf(text,sizeof(text),"LOG QUEUE: dropped=%u io_errors=%u; routine writes ran off the frame thread\n",log_dropped,log_io_errors);
    log_write_block(text,strlen(text));FSFILE_Close(log_handle);log_handle=0;
}
