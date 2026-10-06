/* Focused executable only. Exercise actual copy recovery without filling SD. */
#include <unistd.h>
#include <errno.h>
ssize_t __real_write(int file,const void *data,size_t size);
static int mode,short_writes,failures;
void n3ds_system_write_test_arm(int value) { mode=value; short_writes=failures=0; }
int n3ds_system_write_test_complete(int expect_failure)
{ return !mode && short_writes==1 && failures==expect_failure; }
ssize_t __wrap_write(int file,const void *data,size_t size)
{
    if(mode==3) { mode=0; ++failures; errno=EIO; return -1; }
    if(mode && size>17) {
        int previous=mode;
        ssize_t result=__real_write(file,data,17);
        if(result==17) { ++short_writes; mode=previous==2?3:0; }
        return result;
    }
    return __real_write(file,data,size);
}
