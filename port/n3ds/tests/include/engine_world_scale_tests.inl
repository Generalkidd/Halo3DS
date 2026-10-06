static int original_resolution_policy_tests(void)
{
 struct resolution_policy p={0};
 /* Original starts at 320x192, even when the GPU never blocks the CPU. */
 for(int i=0;i<120;++i)if(resolution_policy_step_profile(&p,25,10,0,15,1)!=1)return 1;
 memset(&p,0,sizeof(p));
 for(int i=0;i<4;++i)resolution_policy_step_profile(&p,60,18,0,30,1);
 if(p.level!=2 || p.trial!=1)return 1;
 for(int i=0;i<15;++i)resolution_policy_step_profile(&p,45,14,0,22,1);
 if(p.level!=2 || p.trial)return 1;
 /* Cheaper GPU time alone cannot justify blur when the CPU is the bottleneck. */
 memset(&p,0,sizeof(p));
 for(int i=0;i<4;++i)resolution_policy_step_profile(&p,60,18,0,30,1);
 for(int i=0;i<12;++i)resolution_policy_step_profile(&p,60,10,0,30,1);
 if(p.level!=1 || p.trial || p.cooldown<11000)return 1;
 for(int i=0;i<160;++i)if(resolution_policy_step_profile(&p,60,10,0,30,1)!=1)return 1;
 /* Simulation-heavy scenes and loading stalls do not request a reduction. */
 memset(&p,0,sizeof(p));
 for(int i=0;i<180;++i)if(resolution_policy_step_profile(&p,90,15,0,20,1)!=1)return 1;
 if(resolution_policy_step_profile(&p,4000,15,0,20,1)!=1 || p.ready)return 1;
 /* An interrupted trial restores the last accepted level. */
 memset(&p,0,sizeof(p));
 for(int i=0;i<4;++i)resolution_policy_step_profile(&p,60,18,0,30,1);
 if(resolution_policy_step_profile(&p,500,18,0,30,1)!=1 || p.trial || p.ready)return 1;
 /* Recover only with actual headroom, stopping at the Original baseline. */
 memset(&p,0,sizeof(p));p.level=3;
 for(int i=0;i<1000;++i)resolution_policy_step_profile(&p,14,5,0,8,1);
 if(p.level!=1 || p.trial)return 1;
 memset(&p,0,sizeof(p));p.level=2;
 for(int i=0;i<180;++i)if(resolution_policy_step_profile(&p,55,5,0,15,1)!=2)return 1;
 /* A failed upward trial retains the existing long recovery backoff. */
 memset(&p,0,sizeof(p));p.level=2;
 for(int i=0;i<61;++i)resolution_policy_step_profile(&p,14,5,0,8,1);
 for(int i=0;i<15;++i)resolution_policy_step_profile(&p,55,12,0,28,1);
 if(p.level!=2 || p.trial || p.recovery<11000)return 1;
 return 0;
}
int n3ds_gpu_world_scale_tests(void)
{
 unsigned int *a=linearAlloc(400*240*4),*b=linearAlloc(400*240*4);int ok=0,active=0;
 unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;
 void *white=n3ds_gpu_texture_create(pixels,8,8);if(!a || !b || !white)goto done;
 if(original_resolution_policy_tests())goto done;
 struct resolution_policy policy={0};
 /* Good 28-30 FPS, CPU-bound frames and isolated stalls retain clarity. */
 for(int i=0;i<120;++i)if(resolution_policy_step(&policy,35,26,4))goto done;
 for(int i=0;i<120;++i)if(resolution_policy_step(&policy,65,18,0))goto done;
 if(resolution_policy_step(&policy,4000,20,0))goto done;
 memset(&policy,0,sizeof(policy));
 for(int i=0;i<19;++i)if(resolution_policy_step(&policy,38,29,9))goto done;
 for(int i=0;i<4;++i)resolution_policy_step(&policy,38,29,9);
 if(policy.level!=1 || policy.trial!=1)goto done;
 struct resolution_policy interrupted=policy;
 if(resolution_policy_step(&interrupted,500,29,9) || interrupted.trial || interrupted.ready)goto done;
 /* A successful reduction settles; unchanged cost rolls the trial back. */
 for(int i=0;i<30;++i)resolution_policy_step(&policy,31,21,2);
 if(policy.level!=1 || policy.trial)goto done;
 memset(&policy,0,sizeof(policy));
 for(int i=0;i<40;++i)resolution_policy_step(&policy,38,29,9);
 if(policy.level || policy.trial || policy.cooldown<=0)goto done;
 memset(&policy,0,sizeof(policy));policy.level=3;
 for(int i=0;i<61;++i)resolution_policy_step(&policy,14,5,0);
 if(policy.level!=2 || policy.trial!=-1)goto done;
 for(int i=0;i<21;++i)resolution_policy_step(&policy,18,8,0);
 if(policy.level!=2 || policy.trial)goto done;
 /* Failed quality recovery cannot flap every frame. */
 memset(&policy,0,sizeof(policy));policy.level=1;
 for(int i=0;i<61;++i)resolution_policy_step(&policy,14,5,0);
 for(int i=0;i<20;++i)resolution_policy_step(&policy,45,38,15);
 if(policy.level!=1 || policy.trial || policy.cooldown<=0)goto done;
 if(policy.recovery<11000.f)goto done;
 for(int i=0;i<300;++i)if(resolution_policy_step(&policy,14,5,0)!=1)goto done;
 for(int i=0;i<150;++i)resolution_policy_step(&policy,14,5,0);
 if(policy.level || policy.trial)goto done;
 for(int test=0;test<4;++test){
  world_budget_enabled=0;
  if(!n3ds_gpu_frame_begin())goto done;active=1;
  world_budget_enabled=1;world_scale=test;n3ds_gpu_world_view_begin();
  struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
  n3ds_gpu_window_begin(&camera);n3ds_gpu_texture_bind(0,white);
  const unsigned short ids[6]={0,1,2,0,2,3};
  for(int tile=0;tile<4;++tile){
   float x=(tile&1)?0:-1.f,y=(tile&2)?0:-.6f;
   struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);if(!v)goto done;
   const float xy[4][2]={{0,0},{1,0},{1,.6f},{0,.6f}};
   for(int i=0;i<4;++i){v[i]=(struct native_render_vertex){{x+xy[i][0],y+xy[i][1],-2},{tile==0 || tile==3,tile==1 || tile==3,tile==2},{0,0}};}
   n3ds_gpu_geometry_flush(v,4*sizeof(*v));if(!n3ds_gpu_geometry_draw(v,ids,6))goto done;
  }
  n3ds_gpu_world_resolve();controls_quad(197,117,6,6,0xffffffffu,1);
  n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(test?b:a,400*240))goto done;
  if(test){unsigned int bad=0,covered=0,white_a=0,white_b=0;
   /* RGB565 quantization is expected on Original world targets. Edges and
    * HUD coverage still have to match; New RGBA8 comparisons stay exact. */
   unsigned int tolerance=n3ds_input_platform_original_model()?8:0;
   for(unsigned int i=0;i<400*240;++i){
    int different=0,background=1;
    for(int shift=8;shift<=24;shift+=8){
     different|=abs((int)((a[i]>>shift)&255)-(int)((b[i]>>shift)&255))>(int)tolerance;
     background&=abs((int)((b[i]>>shift)&255)-(int)((0x102030ffu>>shift)&255))<=(int)tolerance;
    }
    bad+=different;covered+=!background;white_a+=a[i]==~0u;white_b+=b[i]==~0u;
   }
   if(bad>4000 || covered<10000 || white_a!=white_b){loading_write_capture("sdmc:/world-scale-a.rgba",a,400*240*4);loading_write_capture("sdmc:/world-scale-b.rgba",b,400*240*4);char message[160];snprintf(message,sizeof(message),"WORLD SCALE FAIL: level=%d different=%u covered=%u native_overlay=%u/%u",test,bad,covered,white_a,white_b);n3ds_log(message);goto done;}
  }
 }
 /* No world reduction is needed for native night vision. The HUD is composed
  * after the filter, and turning it off releases the offscreen path. */
 for(int video=0;video<2;++video) {
  world_scale=0;struct native_screen_effect night={.enhancement=1,.desaturation=1,.tint={.1f,1,.15f},.video=video,.noise=.3f};
  n3ds_gpu_screen_effect(&night);
  if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_world_view_begin();
  if(!world_low_active)goto done;
  struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x404040ff};
  n3ds_gpu_window_begin(&camera);n3ds_gpu_world_resolve();controls_quad(197,117,6,6,0xffffffffu,1);
  n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(b,400*240))goto done;
  unsigned int green=0,white_pixels=0;
  for(unsigned int i=0;i<400*240;++i){unsigned int red=b[i]>>24,g=(b[i]>>16)&255,blue=(b[i]>>8)&255;
   if(g>90 && red<40 && blue<50)++green;if(b[i]==~0u)++white_pixels;}
  if(green<90000 || white_pixels!=36){char message[100];snprintf(message,sizeof(message),"SCREEN FILTER FAIL: video=%d green=%u white=%u pixel=%08x",video,green,white_pixels,b[10000]);n3ds_log(message);goto done;}
 }
 n3ds_gpu_screen_effect(NULL);n3ds_gpu_world_view_begin();if(world_low_active)goto done;
 ok=1;n3ds_log("PASS: native green/brightness/video resolve, full-resolution unfiltered HUD, filter-off direct rendering");
 n3ds_log("PASS: Original resolution: 320 baseline, CPU-render trials without GPU waits, measured benefit, simulation/stall protection, recovery/backoff; RGB565 resolve preserves HUD");
 n3ds_log("PASS: 28fps resolution policy: CPU-bound and transient protection, timed drop/recovery, failed-trial rollback and cooldown; three resolutions preserve quadrants and full-resolution overlay");
 done:if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();world_scale_dispose();n3ds_gpu_texture_destroy(white);linearFree(a);linearFree(b);return !ok;
}
