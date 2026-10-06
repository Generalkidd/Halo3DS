/* Bounded, optional copies of validated on-disk BSP bytes on New models.
 * CPU/GPU runtime pointers are never cached. Every reuse still validates the
 * map CRC and relocation sidecar before publishing the new section. */
enum { BSP_READ_SLOTS=16, BSP_READ_BUDGET=24*1024*1024 };
static struct bsp_read_entry {
    byte *data;
    unsigned long offset,bytes,age;
} bsp_reads[BSP_READ_SLOTS];
static unsigned long bsp_read_bytes,bsp_read_age,bsp_read_hits;
static byte *bsp_pending;
static unsigned long bsp_pending_offset,bsp_pending_bytes;
extern int n3ds_prefetch_begin(const char *,void *,unsigned int,unsigned int);
extern int n3ds_prefetch_collect(int);
extern void n3ds_prefetch_shutdown(void);
static void bsp_reads_clear(void)
{
    n3ds_prefetch_shutdown();free(bsp_pending);bsp_pending=NULL;bsp_pending_bytes=0;
    for(unsigned int i=0;i<BSP_READ_SLOTS;++i) {free(bsp_reads[i].data);memset(bsp_reads+i,0,sizeof(*bsp_reads));}
    bsp_read_bytes=bsp_read_age=bsp_read_hits=0;
}
static struct bsp_read_entry *bsp_read_find(unsigned long offset,unsigned long bytes)
{
    for(unsigned int i=0;i<BSP_READ_SLOTS;++i)
        if(bsp_reads[i].data && bsp_reads[i].offset==offset && bsp_reads[i].bytes==bytes) {
            bsp_reads[i].age=++bsp_read_age;++bsp_read_hits;return bsp_reads+i;
        }
    return NULL;
}
static byte *bsp_read_reserve(unsigned long bytes)
{
    extern int n3ds_input_platform_original_model(void);
    extern unsigned long n3ds_cache_heap_headroom(void);
    if(n3ds_input_platform_original_model() || bytes>BSP_READ_BUDGET) return NULL;
    while(bytes>BSP_READ_BUDGET-bsp_read_bytes-bsp_pending_bytes || n3ds_cache_heap_headroom()<bytes+16*1024*1024) {
        struct bsp_read_entry *oldest=NULL;
        for(unsigned int i=0;i<BSP_READ_SLOTS;++i) if(bsp_reads[i].data && (!oldest || bsp_reads[i].age<oldest->age)) oldest=bsp_reads+i;
        if(!oldest) return NULL;
        bsp_read_bytes-=oldest->bytes;free(oldest->data);memset(oldest,0,sizeof(*oldest));
    }
    return (malloc)(bytes);
}
static void bsp_read_publish(byte *data,unsigned long offset,unsigned long bytes)
{
    if(!data) return;
    for(unsigned int i=0;i<BSP_READ_SLOTS;++i) if(!bsp_reads[i].data) {
        bsp_reads[i]=(struct bsp_read_entry){data,offset,bytes,++bsp_read_age};bsp_read_bytes+=bytes;return;
    }
    free(data);
}
static void bsp_prefetch_collect(unsigned long requested)
{
    if(!bsp_pending) return;
    int result=n3ds_prefetch_collect(requested==bsp_pending_offset);
    if(!result) return;
    if(result>0) {
        bsp_read_publish(bsp_pending,bsp_pending_offset,bsp_pending_bytes);
        char message[128];snprintf(message,sizeof(message),"BSP PREFETCH: completed bytes=%lu; CRC and relocation still checked on use",bsp_pending_bytes);n3ds_log(message);
    } else free(bsp_pending);
    bsp_pending=NULL;bsp_pending_bytes=0;
}
static void bsp_prefetch_next(const struct scenario_structure_bsp_reference *references,long count,long current,const char *path)
{
    extern int n3ds_input_platform_original_model(void);
    extern unsigned long n3ds_cache_heap_headroom(void);
    if(n3ds_input_platform_original_model() || current+1>=count) return;
    bsp_prefetch_collect(0);
    if(bsp_pending) return;
    const struct scenario_structure_bsp_reference *next=references+current+1;
    unsigned long bytes=next->file_size,offset=next->file_offset;
    if(bytes<24 || bytes>0x1600000 || offset<2048 || bytes>BSP_READ_BUDGET-bsp_read_bytes || n3ds_cache_heap_headroom()<bytes+16*1024*1024) return;
    for(unsigned int i=0;i<BSP_READ_SLOTS;++i) if(bsp_reads[i].data && bsp_reads[i].offset==offset && bsp_reads[i].bytes==bytes) return;
    byte *data=(malloc)(bytes);if(!data) return;
    if(!n3ds_prefetch_begin(path,data,offset,bytes)) {free(data);return;}
    bsp_pending=data;bsp_pending_bytes=bytes;bsp_pending_offset=offset;
    char message[128];snprintf(message,sizeof(message),"BSP PREFETCH: queued section=%ld bytes=%lu; low-priority bounded read-ahead",current+1,bytes);n3ds_log(message);
}
