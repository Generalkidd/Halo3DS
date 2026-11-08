/* Exercise the production paused comparison with fixed queue observations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HALO_N3DS_GPU_PROBE 1
#define HALO_N3DS_TEXTURE_VRAM_MIRRORS 1
#define HALO_N3DS_FIXED_WORLD_WIDTH 400
#define HALO_N3DS_MISSION_TICKS 0
#include "engine_renderer.h"
typedef uint32_t u32;
typedef uint64_t u64;
enum { SYSCLOCK_ARM11=268123480 };
static int diagnostic_mode;
static u32 issued;
static u64 now;
static unsigned int phases,complete,aborts,mirror_reports;
static unsigned int resident=4,bytes=1792,uploads=4,failures;
static u64 svcGetSystemTick(void) {return now;}
static void n3ds_log(const char *text)
{
    phases+=strstr(text,"GPU PROBE PHASE:")!=NULL;
    complete+=strstr(text,"GPU PROBE COMPLETE:")!=NULL;
    aborts+=strstr(text,"GPU PROBE ABORT:")!=NULL;
    mirror_reports+=strstr(text,"GPU PROBE VRAM:")!=NULL;
}
void n3ds_gpu_texture_mirror_stats(unsigned int *r,unsigned int *b,unsigned int *u,unsigned int *f)
{*r=resident;*b=bytes;*u=uploads;*f=failures;}
u32 n3ds_c3d_gpu_probe_submission(void) {return issued;}
int n3ds_c3d_gpu_probe_sample(u32 submission,u32 *ticks)
{assert(submission==issued);*ticks=SYSCLOCK_ARM11/10;return 1;}
#include "engine_gpu_probe.inl"
static void frame(const struct native_gpu_probe_view *view,int chord)
{
    n3ds_gpu_probe_view(view,chord);
    now+=SYSCLOCK_ARM11/10;
    gpu_probe_begin(0,SYSCLOCK_ARM11/20);
    gpu_probe_count(30);
    if(gpu_probe.recording) {u32 prior=issued++;gpu_probe_submit(prior);}
}
static void reset(void)
{
    memset(&gpu_probe,0,sizeof(gpu_probe));
    now=issued=phases=complete=aborts=mirror_reports=0;
    resident=uploads=4;bytes=1792;failures=0;
}
int main(void)
{
    struct native_gpu_probe_view view={.generation=1,.tick=100,.bsp=0};
    frame(NULL,1);frame(&view,1);assert(!gpu_probe.active);
    frame(&view,0);frame(&view,1);assert(gpu_probe.active);
    for(unsigned int i=0;i<5*(GPU_PROBE_SAMPLES+GPU_PROBE_WARMUP);++i)frame(&view,1);
    assert(phases==5 && mirror_reports==5 && complete==1 && !aborts && !gpu_probe.active);
    reset();resident=0;frame(&view,1);
    assert(aborts==1 && !gpu_probe.active && !phases && !gpu_probe.pending_submission);
    for(unsigned int change=0;change<4;++change) {
        reset();frame(&view,1);assert(gpu_probe.active);
        unsigned int *fields[]={&resident,&bytes,&uploads,&failures};
        ++*fields[change];frame(&view,1);
        assert(aborts==1 && !gpu_probe.active && !phases && !gpu_probe.pending_submission);
    }
    reset();frame(&view,1);++view.tick;frame(&view,1);
    assert(aborts==1 && !gpu_probe.active);
    reset();frame(&view,1);now+=31ULL*SYSCLOCK_ARM11;frame(&view,1);
    assert(aborts==1 && !gpu_probe.active);
    puts("PASS: VRAM paired probe: five stable phases, no-mirror rejection, all residency mutations, view change and deadline cancellation");
}
