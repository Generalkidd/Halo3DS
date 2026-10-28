#include "cseries.h"
#include "text/unicode.h"

void n3ds_log(const char *message);
#define CHECK(e) do { if(!(e)) { n3ds_log("WIDE ABI FAIL: " #e); return 1; } } while(0)

int halo_engine_wide_abi_tests(void)
{
    static const wchar_t root[] __attribute__((aligned(4)))={'u',':','\\',0,'x','y','z','x',0,0};
    static const wchar_t even[] __attribute__((aligned(4)))={'a','b',0,'x','y','z',0,0};
    static const wchar_t empty[] __attribute__((aligned(4)))={0,'x','y','z',0,0};
    static const wchar_t non_ascii[] __attribute__((aligned(4)))={0x00e9,0,0,0};
    char ascii[8];

    CHECK(ustrlen(root)==3 && ustrlen(even)==2 && ustrlen(empty)==0);
    CHECK(wide_to_ascii(root,ascii,sizeof(ascii))==ascii && !strcmp(ascii,"u:\\"));
    CHECK(wide_to_ascii(even,ascii,sizeof(ascii))==ascii && !strcmp(ascii,"ab"));
    CHECK(wide_to_ascii(empty,ascii,sizeof(ascii))==ascii && !ascii[0]);
    CHECK(!wide_to_ascii(root,ascii,3));
    CHECK(!wide_to_ascii(non_ascii,ascii,sizeof(ascii)));
    n3ds_log("PASS: 16-bit wide lengths and save-root ASCII conversion");
    return 0;
}
