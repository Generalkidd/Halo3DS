#include "cseries.h"
#include <xtl.h>
#include "cseries_windows.h"
#include "engine_files.h"
const char *shell_get_command_line(void);
void shell_idle(void);
void n3ds_system_write_test_arm(int mode);
int n3ds_system_write_test_complete(int expect_failure);
void n3ds_log(const char *message);
#define CHECK(x) do { if(!(x)) { n3ds_log("FAIL: native system: " #x); return 1; } } while(0)
static int matches(const char *path,const byte *bytes,unsigned int size)
{
    byte buffer[257]; unsigned int offset=0;
    unsigned int file=n3ds_file_open(path,1); if(!file) return 0;
    int good=n3ds_file_size(file)==size;
    while(good && offset<size) {
        unsigned int count=size-offset; if(count>sizeof(buffer)) count=sizeof(buffer);
        good=n3ds_file_read(file,buffer,count) && !memcmp(buffer,bytes+offset,count); offset+=count;
    }
    return n3ds_file_close(file) && good;
}
int halo_engine_system_tests(void)
{
    const char *root="z:\\native-system-regression";
    const char *source="z:\\native-system-regression\\source.bin";
    const char *destination="z:\\native-system-regression\\copy.bin";
    const char *missing="z:\\native-system-regression\\missing.bin";
    byte data[10003]; struct native_file_info info;
    unsigned int i,file; unsigned long before;
    CHECK(!n3ds_file_stat(root,&info)); CHECK(n3ds_file_create(root,1));
    for(i=0;i<sizeof(data);++i) data[i]=(byte)((i*37)^(i>>7));
    CHECK(n3ds_file_create(source,0)); file=n3ds_file_open(source,3); CHECK(file);
    CHECK(n3ds_file_write(file,data,sizeof(data)));
    CHECK(!CopyFileA(source,destination,TRUE)); /* Existing handle denies sharing. */
    CHECK(n3ds_file_close(file));
    CHECK(!CopyFileA(missing,destination,FALSE) && !n3ds_file_stat(destination,&info));
    CHECK(!CopyFileA(source,source,FALSE));
    CHECK(!CopyFileA(source,"Z:/native-system-regression/SOURCE.bin",FALSE));
    CHECK(!CopyFileA(source,"d:\\native-system-must-not-write.bin",FALSE));
    CHECK(!CopyFileA(source,"z:\\native-system-regression\\..\\outside.bin",FALSE));
    CHECK(!CopyFileA(root,destination,FALSE));
    n3ds_system_write_test_arm(2);
    CHECK(!CopyFileA(source,destination,TRUE));
    CHECK(n3ds_system_write_test_complete(1) && !n3ds_file_stat(destination,&info));
    CHECK(matches(source,data,sizeof(data)));
    n3ds_system_write_test_arm(1);
    CHECK(CopyFileA(source,destination,TRUE));
    CHECK(n3ds_system_write_test_complete(0)); CHECK(matches(destination,data,sizeof(data)));
    CHECK(!CopyFileA(source,destination,TRUE)); CHECK(matches(destination,data,sizeof(data)));
    file=n3ds_file_open(destination,1); CHECK(file);
    CHECK(!CopyFileA(source,destination,FALSE)); CHECK(n3ds_file_close(file));
    file=n3ds_file_open(source,3); CHECK(file);
    CHECK(n3ds_file_resize(file,37)); CHECK(n3ds_file_close(file));
    CHECK(CopyFileA(source,destination,FALSE)); CHECK(matches(destination,data,37));
    CHECK(!CopyFileA(missing,destination,FALSE)); CHECK(matches(destination,data,37));
    file=n3ds_file_open(source,3); CHECK(file);
    CHECK(n3ds_file_resize(file,0)); CHECK(n3ds_file_close(file));
    CHECK(CopyFileA(source,destination,FALSE)); CHECK(matches(destination,data,0));
    CHECK(!CopyFileA(source,root,FALSE));
    CHECK(n3ds_file_delete(source,0) && n3ds_file_delete(destination,0) && n3ds_file_delete(root,1));
    CHECK(!n3ds_files_open_count());
    before=system_milliseconds(); Sleep(3);
    CHECK((unsigned long)(system_milliseconds()-before)>=2 && (unsigned long)(system_milliseconds()-before)<5000);
    Sleep(0); shell_idle(); CHECK(shell_get_command_line() && !*shell_get_command_line());
    OutputDebugStringA("Native diagnostic output bridge exercised"); OutputDebugStringA(NULL);
    n3ds_log("PASS: native system copies binary/empty files, recovers short writes, removes failed new copies, honors overwrite/sharing rules, rejects invalid targets and provides finite Sleep/diagnostic/shell services");
    return 0;
}
