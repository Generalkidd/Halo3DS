/* Compile the real adapters with allocation observations at their boundary.
 * libc's internal allocations are intentionally outside this measurement. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int allocations,releases,allocation_attempts;
static int fail_allocation;
static void *format_malloc(size_t bytes)
{
    ++allocation_attempts;
    if(fail_allocation)return NULL;
    void *result=malloc(bytes);assert(result);++allocations;return result;
}
static void format_free(void *pointer)
{if(pointer)++releases;free(pointer);}
#define malloc format_malloc
#define free format_free
#include "../../src/engine_crt.c"
#undef malloc
#undef free

static void check_parser(const char *text)
{
    /* format_copy is the unchanged, formerly unconditional conversion. */
    char *reference=format_copy(text),*owned;
    unsigned int before=allocations;
    const char *actual=format_prepare(text,&owned);
    assert(reference && actual && !strcmp(reference,actual));
    if(!strcmp(reference,text))assert(actual==text && !owned && allocations==before);
    else assert(actual==owned && allocations==before+1);
    format_free(owned);format_free(reference);
}
static void check_output(const char *format,...)
{
    char expected[2048],actual[2048];
    char *reference=format_copy(format);assert(reference);
    unsigned int translated=strcmp(reference,format)!=0;
    const size_t sizes[]={0,1,2,7,64,sizeof(actual)};
    va_list args,a,b;va_start(args,format);
    for(unsigned int i=0;i<sizeof(sizes)/sizeof(*sizes);++i) {
        memset(expected,0xA5,sizeof(expected));memset(actual,0xA5,sizeof(actual));
        va_copy(a,args);va_copy(b,args);
        int er=vsnprintf(sizes[i]?expected:NULL,sizes[i],reference,a);
        unsigned int before=allocations;
        int ar=halo_n3ds_vsnprintf(sizes[i]?actual:NULL,sizes[i],format,b);
        assert(er==ar && !memcmp(expected,actual,sizeof(actual)) && allocations==before+translated);
        va_end(a);va_end(b);
    }
    va_copy(a,args);va_copy(b,args);
    int er=vsprintf(expected,reference,a);
    unsigned int before=allocations;
    int ar=halo_n3ds_vsprintf(actual,format,b);
    assert(er==ar && !strcmp(expected,actual) && allocations==before+translated);
    va_end(a);va_end(b);
    FILE *ef=tmpfile(),*af=tmpfile();assert(ef && af);
    va_copy(a,args);va_copy(b,args);
    er=vfprintf(ef,reference,a);before=allocations;
    ar=halo_n3ds_vfprintf(af,format,b);
    assert(er==ar && allocations==before+translated);
    va_end(a);va_end(b);va_end(args);
    rewind(ef);rewind(af);
    size_t eb=fread(expected,1,sizeof(expected),ef),ab=fread(actual,1,sizeof(actual),af);
    assert(eb==ab && !memcmp(expected,actual,eb));
    fclose(ef);fclose(af);format_free(reference);
}
int main(void)
{
    const char *grammar[]={"","%","%%","%%%","%%%%I64d","%%I32d %I64x",
        "%I","%I6","%I64","%I32","%+ #08.*I64d","%hI64d","%1$I64d",
        "%I64I32","%-%I32x","plain I64 and I32","%lld %zu %.2f"};
    for(unsigned int i=0;i<sizeof(grammar)/sizeof(*grammar);++i)check_parser(grammar[i]);
    uint32_t seed=0x31564a11;
    const char alphabet[]="%I6432-+ #09.*hl$diX";
    char text[80];
    for(unsigned int test=0;test<50000;++test) {
        seed=seed*1664525u+1013904223u;
        unsigned int length=seed%sizeof(text);
        for(unsigned int i=0;i<length;++i) {
            seed=seed*1664525u+1013904223u;text[i]=alphabet[seed%(sizeof(alphabet)-1)];
        }
        text[length]=0;check_parser(text);
    }
    check_output("plain UTF-8: \xc3\xa9 %% I64/I32");
    check_output("%s %d %+08x %.3f", "HUD",-17,0x123u,1.25);
    check_output("%*.*s %*.*f",8,3,"abcdef",12,5,0.125);
    check_output("%lld %llu %zu",-9223372036854775807LL,18446744073709551615ULL,(size_t)99);
    check_output("%I64d %#I64x %I32u",-9223372036854775807LL,0xfedcba9876543210ULL,77u);
    check_output("%+020.*I64d %%I64x %I32d",4,-42LL,-9);
    char output[8192],long_format[4096];
    unsigned int before=allocations;
    assert(halo_n3ds_snprintf(output,sizeof(output),"%d:%s",7,"ok")==4 && !strcmp(output,"7:ok"));
    assert(halo_n3ds_sprintf(output,"%s/%u","value",3u)==7 && !strcmp(output,"value/3"));
    int count=-1;assert(halo_n3ds_snprintf(output,sizeof(output),"ab%ncd",&count)==4 && count==2);
    assert(allocations==before);
    memset(long_format,'A',sizeof(long_format));long_format[sizeof(long_format)-1]=0;
    assert(halo_n3ds_snprintf(output,sizeof(output),long_format)==4095 && allocations==before);
    memcpy(long_format+4089,"%I64d",6);
    assert(halo_n3ds_snprintf(output,sizeof(output),long_format,123LL)==4092 && allocations==before+1);
    fail_allocation=1;before=allocation_attempts;
    assert(halo_n3ds_snprintf(output,sizeof(output),"ordinary %d",5)==10 && allocation_attempts==before);
    strcpy(output,"unchanged");
    assert(halo_n3ds_snprintf(output,sizeof(output),"%I64d",5LL)==-1 && !strcmp(output,"unchanged"));
    assert(allocation_attempts==before+1);
    fail_allocation=0;
    assert(allocations==releases);
    before=allocations;
    for(unsigned int i=0;i<10000;++i)halo_n3ds_snprintf(output,sizeof(output),"frame=%u value=%.2f",i,1.25);
    assert(allocations==before);
    puts("PASS: CRT formats: 50000 grammar cases, libc output/return/truncation parity, varargs/streams, allocation failure and 10000 ordinary calls without adapter allocation");
}
