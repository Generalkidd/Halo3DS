/* Game-facing platform calls. ABI stays Xbox/Clang; kernel and filesystem
 * details remain behind fixed-width GCC adapters. */
#include "cseries.h"
#include <xtl.h>
#include "engine_files.h"
void n3ds_log(const char *message);
void n3ds_engine_sleep(unsigned int milliseconds);
BOOL WINAPI CopyFileA(LPCSTR source,LPCSTR destination,BOOL fail_if_exists)
{ return n3ds_file_copy(source,destination,fail_if_exists)!=0; }
void WINAPI Sleep(DWORD milliseconds) { n3ds_engine_sleep((unsigned int)milliseconds); }
void WINAPI OutputDebugStringA(LPCSTR message) { if(message && *message) n3ds_log(message); }
/* This homebrew entry point has no Xbox demo-launch command line. */
const char *shell_get_command_line(void) { return ""; }
/* The original Xbox implementation has no idle work. Native waits use Sleep;
 * application lifecycle handling remains in the native frame/main loop. */
void shell_idle(void) { }
void main_crash(const char *reason)
{
    n3ds_log(reason?reason:"Explicit engine crash requested");
    abort();
}
