#!/usr/bin/env python3
"""Compare production generic vertex inputs with a frozen arithmetic reference.

Host ASan/UBSan checks output bytes; it does not measure native FPS or pixels.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
workspace = tempfile.TemporaryDirectory(prefix="halo-generic-view-")
out = Path(workspace.name)
prefix=r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PIN(a,b,c) ((a)<(b)?(b):(a)>(c)?(c):(a))
typedef struct {float n[3];} real_vector3d;
struct render_fog {float atmospheric_minimum_distance,planar_maximum_depth,planar_maximum_distance;struct {real_vector3d n;float d;} plane;};
static struct {struct render_fog fog;struct {real_vector3d position,forward;} camera;} global_window_parameters;
struct generic_fog_constants {int atmospheric,planar;float range,atmospheric_density,planar_density,camera_depth;};
'''
def functions(s):
 return s[s.index('static float generic_fog_visibility('):s.index('#include "engine_hologram_model.inl"')]
a=(root/'port/n3ds/tests/host/generic_view_reference.inc').read_text().replace('generic_fog_visibility','reference_visibility').replace('generic_view','reference_view').replace('generic_position_visibility','reference_position_visibility')
b=functions((root/'port/n3ds/src/engine_generic_model.c').read_text())
main=r'''
static uint32_t seed=12345;
static float random_float(void){seed=seed*1664525u+1013904223u;return ((int)(seed>>8)-8388608)/8192.f;}
int main(void){
 for(int mode=0;mode<4;++mode) for(int test=0;test<25000;++test){
  struct generic_fog_constants f={mode&1,mode&2,1.f+fabsf(random_float()),
   (test%11)/10.f,(test%7)/6.f,(test%5)/4.f};
  global_window_parameters.fog.atmospheric_minimum_distance=random_float();
  global_window_parameters.fog.planar_maximum_depth=0.001f+fabsf(random_float());
  global_window_parameters.fog.planar_maximum_distance=0.001f+fabsf(random_float());
  global_window_parameters.fog.plane.d=random_float();
  real_vector3d normal;float position[3];
  for(int c=0;c<3;++c){normal.n[c]=random_float();position[c]=random_float();global_window_parameters.camera.position.n[c]=random_float();global_window_parameters.camera.forward.n[c]=random_float()/1024;global_window_parameters.fog.plane.n.n[c]=random_float()/1024;}
  float ref[2][4],actual[2][4],reflected[3],reflected_actual[3];
  memset(ref,0x55,sizeof(ref));memset(actual,0xaa,sizeof(actual));
  reference_view(normal,position,ref,reflected,&f);generic_view(normal,position,actual,reflected_actual,&f);
  float visibility=generic_position_visibility(position,&f);assert(!memcmp(&visibility,&ref[0][3],sizeof(visibility)));assert(!memcmp(ref,actual,sizeof(ref)));assert(!memcmp(reflected,reflected_actual,sizeof(reflected)));
 }
 puts("PASS: 100000 production generic-view comparisons, all four fog modes, bit-identical vertex/reflection outputs and positional visibility, varying fog distances/densities, poisoned buffers");
}
'''
(out/'check.c').write_text(prefix+a+b+main)
subprocess.run(['clang','-O2','-ffp-contract=off','-fsanitize=address,undefined',str(out/'check.c'),'-o',str(out/'check')],check=True,timeout=60)
r=subprocess.run([str(out/'check')],capture_output=True,text=True,timeout=60);(out/'check.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
