# New3DS integration additions by Kevin Boggs.
"""Check release assertion side effects and diagnostic/fatal error separation.
Run: python port/n3ds/tests/test_release.py -- clang -O2
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]

# Kevin Boggs: main
def main():
    if '--' not in sys.argv:
        raise SystemExit('Usage: test_release.py -- <host C compiler> [flags]')
    compiler = sys.argv[sys.argv.index('--') + 1:]
    macros = (ROOT/'source/cseries/cseries.h').read_text()
    macros = macros[macros.index('#ifdef HALO_RELEASE'):macros.index('#define ABS(')]
    debug = (ROOT/'source/ai/ai_debug.h').read_text()
    debug = debug[debug.index('#if defined(HALO_N3DS_STRIP_DIAGNOSTICS)'):debug.index('/* ---------- constants */')]
    profile = (ROOT/'source/ai/ai_profile.h').read_text()
    profile = profile[profile.index('#ifdef HALO_N3DS_STRIP_DIAGNOSTICS'):profile.index('/* ---------- constants */')]
    math = (ROOT/'source/math/real_math.h').read_text()
    math = math[math.index('#ifdef HALO_N3DS_STRIP_DIAGNOSTICS'):math.index('#else',math.index('#ifdef HALO_N3DS_STRIP_DIAGNOSTICS'))] + '#endif\n'
    collision = (ROOT/'source/physics/collision_usage.h').read_text()
    collision = collision[collision.index('#if defined(HALO_N3DS_STRIP_DIAGNOSTICS)'):collision.index('/* ---------- globals */')]
    errors = (ROOT/'source/cseries/errors.c').read_text()
    errors = errors[errors.index('#undef error\nvoid error('):errors.index('/* ---------- private code */')]
    source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define HALO_RELEASE 1
#define HALO_N3DS 1
#define HALO_N3DS_STRIP_DIAGNOSTICS 1
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
''' + macros + debug + profile + math + collision + r'''
#include "source/cseries/errors.h"
static int calls, status, writes, terminal, formatted;
static int required_action(void){++calls;return 1;}
static const char *message_argument(void){++formatted;return "test";}
static void display_assert(void*a,void*b,long c,int d){abort();}
static void system_exit(int code){status=code;}
static long system_milliseconds(void){return 1;}
static void terminal_printf(const void *color,const char *format,...){++terminal;}
void write_to_error_file(char *string,boolean date){++writes;}
static const void *global_real_argb_white;
struct error_global_data error_globals;
static struct {long last_error_time,error_count;} bss_0031df2c;
#define csstrcat strcat
#define csstrlen strlen
#define csstrcpy strcpy
#define csstrncpy strncpy
#define csmemmove memmove
static int exercise(void){
    match_assert("test",1,required_action());
    match_vassert("test",2,required_action(),"test");
    match_warn("test",3,required_action());
    match_vwarn("test",4,required_action(),"test");
    assert(required_action());
    CHECK(calls==5);
    AI_DEBUG_ONLY(required_action()); AI_PROFILE_ONLY(required_action());
    match_assert_valid_real_matrix4x3("test",1,NULL);
    assert_valid_real_vector3d(NULL);
    CHECK(calls==5 && !AI_DIAGNOSTICS_ENABLED);
    CHECK(AI_DEBUG_RECORD(required_action())==NULL && calls==5);
    collision_log_start_time(required_action());
    collision_log_end_time(required_action(),required_action());
    collision_log_usage(required_action());
    CHECK(calls==5);
    error(_error_silent,"%s",message_argument());
    error(_error_log,"%s",message_argument());
    CHECK(!formatted && !writes && !terminal && !status && !errors_handle());
    int priority=-1;
    error(++priority,"%s",message_argument());
    CHECK(priority==0 && formatted==1 && writes==1 && status==-4998);
    status=0;errors_clear();
    error(_error_delayed,"%s",message_argument());
    CHECK(formatted==2 && writes==2 && status==0 && errors_handle());
    CHECK(!errors_handle());
    puts("PASS: release keeps assertion actions, removes debug expressions, preserves delayed/fatal errors");
    return 0;
}
''' + errors + '\nint main(void){return exercise();}\n'
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
        path=Path(directory); c=path/'release.c'; exe=path/'release-tests.exe'
        c.write_text(source)
        subprocess.run(compiler+['-I'+str(ROOT),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__ == '__main__':
    main()
