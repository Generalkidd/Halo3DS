/* New3DS integration additions by Kevin Boggs. */
/* Kevin Boggs: n3ds_gpu_fog_tests */
int n3ds_gpu_fog_tests(void)
{
 struct native_render_vertex *v=linearAlloc(4*sizeof(*v));
 unsigned int *pixels=linearAlloc(240*400*4);int active=0,ok=0;
 const unsigned short indices[6]={0,1,2,0,2,3};
 const float depths[]={5,20,30,40,50,59};
 if(!v || !pixels)goto done;
 for(int test=0;test<9;++test) {
  float d=depths[test<6?test:4];
  for(int i=0;i<4;++i)v[i]=(struct native_render_vertex){{(i==1 || i==2?.2f:-.2f)*d,(i>=2?.2f:-.2f)*d,-d},{1,1,1},{0,0}};
  n3ds_gpu_geometry_flush(v,4*sizeof(*v));
  n3ds_gpu_fog_config(20,60,0xff0000);
  struct native_render_camera c={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.01f,.far_clip=60,.clear_color=0x102030ff};
  if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&c);
  if(test==6)n3ds_gpu_fog_sky(1);
  if(test==7)n3ds_gpu_fog_blend(NATIVE_BLEND_ADD);
  if(test==8)n3ds_gpu_fog_overlay();
  for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
  C3D_TexEnv *tev=C3D_GetTexEnv(0);
  C3D_TexEnvSrc(tev,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);
  C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);C3D_TexEnvColor(tev,0xff0000ff);
  if(!n3ds_gpu_geometry_draw(v,indices,6))goto done;
  n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400))goto done;
  float visibility=test==6 || test==8?1:fmaxf(0,fminf(1,(60-d)/40));
  unsigned int expected=((unsigned int)(visibility*255+.5f)<<24)|((unsigned int)((test==6 || test==7 || test==8?0:1-visibility)*255+.5f)<<8)|255;
  unsigned int actual=pixels[200*240+120];
  for(int shift=8;shift<32;shift+=8)if(abs((int)((expected>>shift)&255)-(int)((actual>>shift)&255))>4) {
   char line[180];snprintf(line,sizeof(line),"FAIL: distance fog test=%d depth=%.1f expected=%08x actual=%08x",test,d,expected,actual);n3ds_log(line);goto done;
  }
 }
 for(int test=0;test<3;++test) {
  n3ds_gpu_fog_config(20,60,0xff0000);
  struct native_render_camera c={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.01f,.far_clip=60,.clear_color=0x102030ff};
  if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&c);
  for(int pass=0;pass<2;++pass) {
   int red=test==1?pass==1:pass==0;
   float d=test==2?(pass?1:2):(red?10:40);
   if(test==2 && !pass && !n3ds_gpu_first_person_begin(.01f,10))goto done;
   /* Separate storage is required for two draws in the same GPU submission. */
   struct native_render_vertex *q=n3ds_gpu_model_vertices_allocate(4);if(!q)goto done;
   for(int i=0;i<4;++i)q[i]=(struct native_render_vertex){{(i==1 || i==2?.2f:-.2f)*d,(i>=2?.2f:-.2f)*d,-d},{1,1,1},{0,0}};
   for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
   C3D_TexEnv *tev=C3D_GetTexEnv(0);
   C3D_TexEnvSrc(tev,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);
   C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);C3D_TexEnvColor(tev,red?0xff0000ff:0xff00ff00);
   if(!n3ds_gpu_geometry_draw(q,indices,6))goto done;
   if(test==2 && !pass)n3ds_gpu_first_person_end();
  }
  n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400))goto done;
  if((pixels[200*240+120]&0xffffff00u)!=0xff000000u) {
   char line[128];snprintf(line,sizeof(line),"FAIL: distance fog depth order/weapon test=%d actual=%08x",test,pixels[200*240+120]);n3ds_log(line);goto done;
  }
 }
 for(int test=0;test<3;++test) {
  n3ds_gpu_fog_config(20,60,0xff0000);
  struct native_render_camera c={.forward={0,1,0},.up={0,0,1},.vertical_fov=1,.near_clip=.01f,.far_clip=60,.clear_color=0x102030ff};
  if(test){c.forward[1]=0;c.forward[2]=test==1?-1:1;c.up[1]=1;c.up[2]=0;}
  if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&c);n3ds_gpu_fog_backdrop();
  n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400))goto done;
  for(int point=0;point<2;++point) {
   unsigned int expected=test==1 || (test==0 && point==0)?0x0000ff00:0x10203000;
   unsigned int actual=pixels[200*240+(point?220:20)]&0xffffff00u;
   if(actual!=expected){char line[160];snprintf(line,sizeof(line),"FAIL: fog horizon test=%d point=%d expected=%08x actual=%08x",test,point,expected,actual);n3ds_log(line);goto done;}
  }
 }
 n3ds_log("PASS: distance fog linear depth samples, sky/overlay exclusion, additive neutral color, depth ordering, first-person stencil and horizon background");ok=1;
done:
 if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();
 n3ds_gpu_fog_config(0,0,0);n3ds_gpu_fog_overlay();linearFree(v);linearFree(pixels);return !ok;
}
