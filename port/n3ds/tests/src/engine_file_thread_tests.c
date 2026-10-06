#include "cseries.h"
#include "tag_files/files.h"
#include "bungie_net/common/thread.h"
#include "engine_files.h"
#include "engine_threads.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);

enum { WORKERS=4, BLOCK_BYTES=64, BLOCKS=8 };
struct file_work {
    const struct file_reference *shared;
    unsigned int *ready,*release;
    unsigned int index,mode,errors,opened;
};
static int wait_value(unsigned int *value,unsigned int minimum)
{
    long long deadline=n3ds_engine_ticks()+15*n3ds_engine_tick_frequency();
    while(__atomic_load_n(value,__ATOMIC_ACQUIRE)<minimum) {
        if(n3ds_engine_ticks()>deadline) return 0;
        n3ds_engine_sleep_milliseconds(1);
    }
    return 1;
}
static unsigned long __stdcall file_worker(void *argument)
{
    struct file_work *w=argument;
    char data[BLOCK_BYTES],actual[BLOCK_BYTES];
    if(w->mode==0) {
        __atomic_fetch_add(w->ready,1,__ATOMIC_RELEASE);
        if(!wait_value(w->release,1)) return ++w->errors;
        for(unsigned int i=0;i<BLOCKS;++i) {
            unsigned int offset=(i*WORKERS+w->index)*BLOCK_BYTES;
            memset(data,17+w->index*32+i,sizeof(data));
            if(!file_write_to_position(w->shared,offset,sizeof(data),data)) ++w->errors;
            n3ds_engine_sleep_milliseconds(1);
            if(!file_read_from_position(w->shared,offset,sizeof(actual),actual) || memcmp(data,actual,sizeof(data))) ++w->errors;
        }
    } else if(w->mode==1) {
        unsigned int handle;
        __atomic_fetch_add(w->ready,1,__ATOMIC_RELEASE);
        if(!wait_value(w->release,1)) return ++w->errors;
        handle=n3ds_file_open("z:\\native-thread-io\\data.bin",3);
        w->opened=handle!=0;
        __atomic_fetch_add(w->ready,1,__ATOMIC_RELEASE);
        /* The winner retains exclusive ownership until everyone has tried. */
        if(!wait_value(w->release,2)) ++w->errors;
        if(handle && !n3ds_file_close(handle)) ++w->errors;
    } else if(w->mode==2) {
        struct file_reference foreign,entry;
        struct native_file_info info;
        unsigned int count=n3ds_files_directory_count();
        file_reference_create_from_path(&foreign,"d:\\maps",TRUE);
        find_files_start(0,&foreign); /* Must not replace main's search metadata. */
        if(find_files_next(&entry,NULL)) ++w->errors;
        if(n3ds_files_find_next(actual,sizeof(actual),&info)!=-1) ++w->errors;
        n3ds_files_find_end(); /* Foreign cancellation must also leave it intact. */
        if(n3ds_files_directory_count()!=count) ++w->errors;
    } else {
        struct file_reference directory,entry;
        file_reference_create_from_path(&directory,"z:\\native-thread-io",TRUE);
        find_files_start(0,&directory);
        if(!find_files_next(&entry,NULL) || !n3ds_files_directory_count()) ++w->errors;
        /* Return before exhausting the walk: native worker exit must close it. */
    }
    return w->errors;
}
static int join_worker(struct thread_reference *thread,struct file_work *work)
{
    unsigned int result;
    long long deadline=n3ds_engine_ticks()+15*n3ds_engine_tick_frequency();
    while(!thread_has_exited(thread)) {
        if(n3ds_engine_ticks()>deadline) return 0;
        n3ds_engine_sleep_milliseconds(1);
    }
    int success=n3ds_thread_result((unsigned long)thread,&result) && !result && !work->errors;
    dispose_thread(thread); return success;
}
int halo_engine_file_thread_tests(void)
{
    struct file_reference root,file,entry;
    struct thread_reference *threads[WORKERS];
    struct file_work work[WORKERS];
    unsigned int ready=0,release=0,opened=0;
    char data[BLOCK_BYTES],expected[BLOCK_BYTES],name[256];
#define CHECK(x) do { if(!(x)) { n3ds_log("FILE THREAD FAIL: " #x); return 1; } } while(0)
    CHECK(!n3ds_files_open_count() && !n3ds_files_directory_count() && !n3ds_thread_count());
    file_reference_create_from_path(&root,"z:\\native-thread-io",TRUE);
    file_reference_create_from_path(&file,"z:\\native-thread-io\\data.bin",FALSE);
    CHECK(!file_exists(&root) && file_create(&root) && file_create(&file));
    CHECK(file_open(&file,3) && file_set_eof(&file,WORKERS*BLOCKS*BLOCK_BYTES));
    for(unsigned int i=0;i<WORKERS;++i) {
        work[i]=(struct file_work){.shared=&file,.ready=&ready,.release=&release,.index=i};
        CHECK(create_thread(0,file_worker,&work[i],&threads[i]));
    }
    CHECK(wait_value(&ready,WORKERS)); __atomic_store_n(&release,1,__ATOMIC_RELEASE);
    for(unsigned int i=0;i<WORKERS;++i) CHECK(join_worker(threads[i],&work[i]));
    CHECK(file_get_eof(&file)==WORKERS*BLOCKS*BLOCK_BYTES);
    for(unsigned int i=0;i<BLOCKS;++i) for(unsigned int j=0;j<WORKERS;++j) {
        memset(expected,17+j*32+i,sizeof(expected));
        CHECK(file_read_from_position(&file,(i*WORKERS+j)*BLOCK_BYTES,sizeof(data),data) && !memcmp(data,expected,sizeof(data)));
    }
    CHECK(file_close(&file));
    n3ds_log("PASS: 4 native workers share original file API; 32 positioned writes and interleaved reads preserve all 2048 bytes");
    ready=release=0;
    for(unsigned int i=0;i<WORKERS;++i) {
        work[i]=(struct file_work){.ready=&ready,.release=&release,.mode=1};
        CHECK(create_thread(0,file_worker,&work[i],&threads[i]));
    }
    CHECK(wait_value(&ready,WORKERS)); __atomic_store_n(&release,1,__ATOMIC_RELEASE);
    CHECK(wait_value(&ready,2*WORKERS) && n3ds_files_open_count()==1);
    __atomic_store_n(&release,2,__ATOMIC_RELEASE);
    for(unsigned int i=0;i<WORKERS;++i) { CHECK(join_worker(threads[i],&work[i])); opened+=work[i].opened; }
    CHECK(opened==1 && !n3ds_files_open_count());
    find_files_start(0,&root);
    work[0]=(struct file_work){.mode=2};
    CHECK(create_thread(0,file_worker,&work[0],&threads[0]) && join_worker(threads[0],&work[0]));
    CHECK(find_files_next(&entry,NULL));
    file_reference_get_name(&entry,FLAG(_name_filename_bit)|FLAG(_name_extension_bit),name);
    CHECK(!strcmp(name,"data.bin") && file_exists(&entry) && !find_files_next(&entry,NULL));
    work[0]=(struct file_work){.mode=3};
    CHECK(create_thread(0,file_worker,&work[0],&threads[0]) && join_worker(threads[0],&work[0]));
    CHECK(!n3ds_files_directory_count());
    find_files_start(0,&root); CHECK(find_files_next(&entry,NULL) && !find_files_next(&entry,NULL));
    CHECK(file_delete(&file) && file_delete(&root));
    CHECK(!n3ds_files_open_count() && !n3ds_files_directory_count() && !n3ds_thread_count());
    n3ds_log("PASS: concurrent exclusive open has one owner; foreign enumeration rejected, abandoned walk reclaimed and file resources released");
    return 0;
}
