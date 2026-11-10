# New3DS integration additions by Kevin Boggs.
"""Exercise skipped-time accounting and conservative per-eye box rejection.
Usage: python test_physics_schedule.py -- clang -O2
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]

# Kevin Boggs: main
def main():
    compiler = sys.argv[sys.argv.index('--') + 1:]
    render = (ROOT/'port/n3ds/src/engine_mission_render.c').read_text()
    culling = render[render.index('static int batch_plane_rejects('):render.index('/* Reuse the largest BSP')]
    source = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/n3ds/include/engine_physics_schedule.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define NONE (-1)
typedef float real;
typedef struct {float i,j,k;} real_vector3d;
typedef struct {real_vector3d n;float d;} real_plane3d;
struct render_frustum {real_plane3d world_planes[6];};
struct mission_batch {real_vector3d center,extent;unsigned char reject_plane[2];};
static float dot_product3d(const real_vector3d*a,const real_vector3d*b){return a->i*b->i+a->j*b->j+a->k*b->k;}
''' + culling + r'''
static uint32_t seed=17;
static float random_float(void){seed=seed*1664525u+1013904223u;return ((int32_t)(seed>>8)-8388608)*0.0001f;}
int main(void){
    for(int identity=1;identity<=16;identity++) {
        struct n3ds_physics_schedule state={0};
        int elapsed=0,skipped=0;
        for(int tick=1;tick<10000;tick++) {
            int step=n3ds_physics_schedule_step(&state,identity,tick,1);
            elapsed+=step;skipped+=step==0;
            CHECK(elapsed==tick || elapsed==tick-1);
            CHECK((step==0)==state.skipped);
        }
        CHECK(skipped>=4998);
        CHECK(n3ds_physics_schedule_step(&state,identity,0,1)==1); /* rewind */
        CHECK(n3ds_physics_schedule_step(&state,identity,100,1)==1); /* inactive gap */
        CHECK(n3ds_physics_schedule_step(&state,identity+256,101,1)==1); /* slot collision */
        for(int tick=102;tick<1000;tick++)CHECK(n3ds_physics_schedule_step(&state,identity+256,tick,0)==1);
        state.valid=0;CHECK(n3ds_physics_schedule_step(&state,identity+256,1000,1)==1);
        /* Disabling the experiment after a skip must never double a player,
         * airborne or networked step. */
        state.skipped=1;CHECK(n3ds_physics_schedule_step(&state,identity+256,1001,0)==1);
    }
    for(unsigned trial=0;trial<1000000;trial++) {
        struct mission_batch batch;struct render_frustum frustum;real_vector3d absolute[6];
        batch.center=(real_vector3d){random_float(),random_float(),random_float()};
        batch.extent=(real_vector3d){fabsf(random_float()),fabsf(random_float()),fabsf(random_float())};
        batch.reject_plane[0]=trial%6;batch.reject_plane[1]=(trial+3)%6;
        int reference=-1;
        for(unsigned p=0;p<6;p++) {
            real_plane3d *plane=frustum.world_planes+p;
            plane->n=(real_vector3d){random_float(),random_float(),random_float()};plane->d=random_float();
            absolute[p]=(real_vector3d){fabsf(plane->n.i),fabsf(plane->n.j),fabsf(plane->n.k)};
            float distance=dot_product3d(&plane->n,&batch.center)-plane->d;
            float radius=fabsf(plane->n.i)*batch.extent.i+fabsf(plane->n.j)*batch.extent.j+fabsf(plane->n.k)*batch.extent.k;
            if(p!=4 && distance-radius>.01f)reference=(int)p;
        }
        for(unsigned eye=0;eye<2;eye++) {
            int result=batch_outside_plane(&batch,&frustum,absolute,eye);
            CHECK((result<0)==(reference<0));
            if(result>=0)CHECK(batch_plane_rejects(&batch,frustum.world_planes+result,absolute+result));
        }
    }
    puts("PASS: timestep conservation, slot reuse, rewind, gaps and full-rate transition; 2 million per-eye box decisions match baseline");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
        folder = Path(folder)
        c = folder/'physics-test.c'; exe = folder/'physics-test.exe'
        c.write_text(source)
        subprocess.run([*compiler, '-I'+str(ROOT), str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
