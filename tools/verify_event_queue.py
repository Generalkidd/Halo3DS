#!/usr/bin/env python3
"""Check production UI queue/update behavior with native-width host types.

Requires clang with ASan/UBSan. Uses temporary files; no device is started.
This checks input semantics, not native frame time or visual equivalence.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
os.chdir(root)
workspace = tempfile.TemporaryDirectory(prefix="halo-event-queue-")
out = Path(workspace.name)
h=Path('source/interface/event_manager.h').read_text();s=Path('source/interface/event_manager.c').read_text()
h=re.sub(r'^#(?:include|pragma once).*$','',h,flags=re.M);s=re.sub(r'^#(?:include|pragma once).*$','',s,flags=re.M)
code='''#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t boolean;typedef uint8_t byte;typedef struct {int16_t x,y;} point2d;
#define MAXIMUM_GAMEPADS 4
#define NUMBER_OF_GAMEPAD_STICKS 2
#define _gamepad_stick_left 0
#define _gamepad_stick_right 1
#define FALSE 0
#define TRUE 1
#define NONE -1
#define ABS(x) ((x)<0?-(x):(x))
#define match_assert(a,b,c) assert(c)
#define csmemset memset
#define csmemmove memmove
static uint32_t now=100;
static uint32_t system_milliseconds(void){return now;}
'''
input_header = Path('source/input/input.h').read_text()
buttons = input_header[input_header.index('enum\n{\n\tFIRST_GAMEPAD_ANALOG_BUTTON'):input_header.index('enum\n{\n\t_gamepad_stick_left')]
state = input_header[input_header.index('struct gamepad_state\n{'):input_header.index('/* ---------- prototypes/INPUT_XBOX.C')]
code += buttons + state + '''
static struct gamepad_state pads[MAXIMUM_GAMEPADS];
static boolean connected[MAXIMUM_GAMEPADS];
static boolean input_has_gamepad(short i){return connected[i];}
static const struct gamepad_state *input_get_gamepad_state(short i){return &pads[i];}
''' + h + s + '''
static void post(int id){struct event_record e={0};e.type=_event_type_button;e.data.button.index=id;queue_event(&e,0);}
int main(void){struct event_record e;event_manager_initialize();
post(1);post(2);post(3);
for(int i=1;i<=3;i++){assert(get_next_event(&e,0));assert(e.data.button.index==i);}assert(!get_next_event(&e,0));
for(int i=0;i<10;i++)post(i);
for(int i=2;i<10;i++){assert(get_next_event(&e,0));assert(e.data.button.index==i);}assert(!get_next_event(&e,0));
event_manager_suppress(1);post(44);assert(!get_next_event(&e,0));event_manager_suppress(0);
post(55);event_manager_flush();assert(!get_next_event(&e,0));
/* Drive the complete update path with two connected controllers. */
connected[0]=connected[1]=TRUE;
pads[0].buttons[2]=1;pads[0].buttons[5]=7;pads[1].buttons[3]=2;
event_manager_update();
assert(get_next_event(&e,NONE));assert(e.controller_index==0 && e.data.button.index==2 && e.data.button.value==1);
assert(get_next_event(&e,NONE));assert(e.controller_index==0 && e.data.button.index==5 && e.data.button.value==7);
assert(get_next_event(&e,NONE));assert(e.controller_index==1 && e.data.button.index==3 && e.data.button.value==2);
assert(!get_next_event(&e,NONE));
memset(pads,0,sizeof(pads));
pads[0].sticks[0].x=30000;now=1000;event_manager_update();
assert(get_next_event(&e,0));assert(e.type==_event_type_left_stick && e.data.stick.x==32767);
now=1249;event_manager_update();assert(!get_next_event(&e,0));
now=1250;event_manager_update();assert(get_next_event(&e,0));assert(!get_next_event(&e,0));
event_manager_dispose();event_manager_update();assert(!get_next_event(&e,NONE));
puts("PASS: FIFO, overflow, suppression, flush, controller isolation, update ordering, stick repeat and disposal");}
'''
code=code.replace('unsigned long','uint32_t').replace('long','int32_t')
(out/'check.c').write_text(code);subprocess.run(['clang','-O2','-fsanitize=address,undefined',str(out/'check.c'),'-o',str(out/'check')],check=True,timeout=60)
r=subprocess.run([str(out/'check')],capture_output=True,text=True,timeout=60);(out/'check.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);raise SystemExit(r.returncode)
