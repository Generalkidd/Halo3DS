#include "cseries.h"
#include <xtl.h>
#include "saved games/saved_game_files.h"
#include "bungie_net/common/thread.h"
#include "engine_signatures.h"
#include "engine_signature_vectors.h"
#include "engine_threads.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep_milliseconds(long milliseconds);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
struct signature_work { const unsigned char *data; unsigned int index,failed; };
static unsigned long __stdcall signature_worker(void *argument)
{
    struct signature_work *work=argument;
    for(unsigned int v=work->index;v<NUMBEROF(signature_vectors);v+=4) {
        HANDLE handle=XCalculateSignatureBegin(0); XCALCSIG_SIGNATURE result;
        if(handle==INVALID_HANDLE_VALUE) { ++work->failed; break; }
        for(unsigned int offset=0;offset<signature_vectors[v].bytes;) {
            unsigned int bytes=(offset%71)+1;
            if(bytes>signature_vectors[v].bytes-offset) bytes=signature_vectors[v].bytes-offset;
            if(XCalculateSignatureUpdate(handle,work->data+offset,bytes)) ++work->failed;
            offset+=bytes;
        }
        if(XCalculateSignatureEnd(handle,&result) || memcmp(result.Signature,signature_vectors[v].digest,20)) ++work->failed;
    }
    return work->failed;
}
int halo_engine_signature_tests(void)
{
    unsigned char *data=malloc(65535); XCALCSIG_SIGNATURE result;
    HANDLE handles[16],stale; struct signature_work work[4]; struct thread_reference *threads[4];
#define CHECK(x) do { if(!(x)) { n3ds_log("SIGNATURE ENGINE FAIL: " #x); return 1; } } while(0)
    CHECK(data && !n3ds_signature_count());
    for(unsigned int i=0;i<65535;++i) data[i]=(i*29+7)&255;
    CHECK(XCalculateSignatureBegin(XCALCSIG_FLAG_NON_ROAMABLE)==INVALID_HANDLE_VALUE);
    for(unsigned int v=0;v<NUMBEROF(signature_vectors);++v) {
        saved_game_file_generate_checksum(data,(word)signature_vectors[v].bytes,&result);
        CHECK(!memcmp(result.Signature,signature_vectors[v].digest,20));
    }
    for(unsigned int i=0;i<4;++i) {
        work[i]=(struct signature_work){.data=data,.index=i};
        CHECK(create_thread(0,signature_worker,&work[i],&threads[i]));
    }
    for(unsigned int i=0;i<4;++i) {
        long long end=n3ds_engine_ticks()+15*n3ds_engine_tick_frequency();
        while(!thread_has_exited(threads[i])) { CHECK(n3ds_engine_ticks()<end); n3ds_engine_sleep_milliseconds(1); }
        CHECK(!work[i].failed); dispose_thread(threads[i]);
    }
    for(unsigned int i=0;i<16;++i) { handles[i]=XCalculateSignatureBegin(0); CHECK(handles[i]!=INVALID_HANDLE_VALUE); }
    CHECK(n3ds_signature_count()==16 && XCalculateSignatureBegin(0)==INVALID_HANDLE_VALUE);
    stale=handles[0]; CHECK(!XCalculateSignatureEnd(stale,NULL)); handles[0]=XCalculateSignatureBegin(0);
    CHECK(handles[0]!=INVALID_HANDLE_VALUE && handles[0]!=stale);
    CHECK(XCalculateSignatureUpdate(stale,data,1)==ERROR_INVALID_HANDLE && XCalculateSignatureEnd(stale,&result)==ERROR_INVALID_HANDLE);
    CHECK(XCalculateSignatureUpdate(handles[0],NULL,1)==ERROR_INVALID_PARAMETER);
    CHECK(!XCalculateSignatureUpdate(handles[0],NULL,0));
    CHECK(!XCalculateSignatureEnd(handles[0],&result) && !memcmp(result.Signature,signature_vectors[0].digest,20));
    for(unsigned int i=1;i<16;++i) CHECK(!XCalculateSignatureEnd(handles[i],NULL));
    CHECK(!n3ds_signature_count() && !n3ds_thread_count());
    free(data);
    n3ds_log("PASS: original save checksum matches 18 independent SHA-1 references through 65535 bytes; 4 chunked workers, 16 slots and stale/error/cancel cleanup");
    return 0;
}
