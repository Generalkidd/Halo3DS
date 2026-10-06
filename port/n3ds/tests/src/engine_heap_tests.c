/* Focused executable only: exercise actual engine arrays and inject one libc
 * allocation failure without exhausting the process or replacing engine code. */
#include "cseries.h"
#include "array.h"
void n3ds_log(const char *message);
void *__real_realloc(void *pointer,size_t size);
static boolean fail_next_resize;
static unsigned int injected_failures;
void *__wrap_realloc(void *pointer,size_t size)
{
    if(fail_next_resize) {
        fail_next_resize=FALSE; ++injected_failures;
        return NULL;
    }
    return __real_realloc(pointer,size);
}
#define VERIFY(x) do { if(!(x)) { n3ds_log("FAIL: native heap: " #x); return 1; } } while(0)
int halo_engine_heap_tests(void)
{
    struct dynamic_array array;
    unsigned int i;
    byte *buffer=debug_malloc(32,TRUE,__FILE__,__LINE__),*resized;
    for(i=0;i<32;++i) { VERIFY(buffer[i]==0); buffer[i]=(byte)(i^0x5a); }
    resized=debug_realloc(buffer,4096,__FILE__,__LINE__); VERIFY(resized); buffer=resized;
    for(i=0;i<32;++i) VERIFY(buffer[i]==(byte)(i^0x5a));
    resized=debug_realloc(buffer,16,__FILE__,__LINE__); VERIFY(resized); buffer=resized;
    for(i=0;i<16;++i) VERIFY(buffer[i]==(byte)(i^0x5a));
    fail_next_resize=TRUE;
    VERIFY(!debug_realloc(buffer,8192,__FILE__,__LINE__));
    VERIFY(!fail_next_resize && injected_failures==1);
    for(i=0;i<16;++i) VERIFY(buffer[i]==(byte)(i^0x5a));
    VERIFY(!debug_realloc(buffer,0,__FILE__,__LINE__));
    buffer=debug_realloc(NULL,32,__FILE__,__LINE__); VERIFY(buffer);
    memset(buffer,0x33,32); debug_free(buffer,__FILE__,__LINE__);

    dynamic_array_new(&array,sizeof(long));
    VERIFY(dynamic_array_resize(&array,3));
    for(i=0;i<3;++i) {
        long *value=dynamic_array_get_element(&array,i,sizeof(long));
        VERIFY(*value==0); *value=100+i;
    }
    VERIFY(dynamic_array_add_element(&array)==3);
    VERIFY(*(long *)dynamic_array_get_element(&array,3,sizeof(long))==0);
    *(long *)dynamic_array_get_element(&array,3,sizeof(long))=103;
    buffer=array.elements;
    fail_next_resize=TRUE;
    VERIFY(!dynamic_array_resize(&array,100));
    VERIFY(!fail_next_resize && injected_failures==2);
    VERIFY(array.count==4 && array.elements==buffer);
    for(i=0;i<4;++i) VERIFY(*(long *)dynamic_array_get_element(&array,i,sizeof(long))==100+i);
    dynamic_array_delete_element(&array,1);
    VERIFY(array.count==3);
    VERIFY(*(long *)dynamic_array_get_element(&array,0,sizeof(long))==100);
    VERIFY(*(long *)dynamic_array_get_element(&array,1,sizeof(long))==102);
    VERIFY(*(long *)dynamic_array_get_element(&array,2,sizeof(long))==103);
    VERIFY(dynamic_array_resize(&array,8));
    for(i=3;i<8;++i) VERIFY(*(long *)dynamic_array_get_element(&array,i,sizeof(long))==0);
    VERIFY(dynamic_array_resize(&array,0) && !array.elements && !array.count);
    VERIFY(dynamic_array_add_element(&array)==0);
    dynamic_array_delete(&array);
    VERIFY(array.count==NONE && array.element_size==NONE && !array.elements);
    n3ds_log("PASS: native heap resize preserves bytes across growth/shrink, frees on zero, interoperates with malloc/free and preserves original dynamic arrays on injected allocation failure");
    return 0;
}
