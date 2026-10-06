/* Force the documented PICA input precision even when an emulator keeps f32.
 * Finite normal test vectors only; use libctru's actual f32tof24 conversion.
 * https://www.3dbrew.org/wiki/GPU/Shader_Instruction_Set#Floating-Point_Behavior */
static float precision_f24_input(float f)
{
 if(f==0)return 0;
 unsigned int p=f32tof24(f),bits=((p&0x800000u)<<8)|((((p>>16)&127u)+64u)<<23)|((p&65535u)<<7);
 float result;memcpy(&result,&bits,4);return result;
}
static int cpu_model_precision_tests(void)
{
 unsigned int *a=linearAlloc(240*400*4),*b=linearAlloc(240*400*4),pixels[64];
 void *texture=NULL;int active=0,ok=0;unsigned int worst=0,legacy_worst=0;
 const unsigned short ids[6]={0,1,2,0,2,3};
 for(int i=0;i<64;++i)pixels[i]=0x4070b0ff;
 texture=n3ds_gpu_texture_create(pixels,8,8);if(!a || !b || !texture){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
 for(int test=0;test<30;++test) {
  int path=test%5;float shift=(test/5%3==0?128.f:test/5%3==1?1024.f:4096.f);
  for(int pass=0;pass<2;++pass) {
   float origin=pass?shift:0;
   struct native_render_camera cam={.position={origin,-origin,origin},.forward={.125f,.0625f,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.05f,.far_clip=100,.clear_color=0x102030ff};
   float light[3]={origin,-origin,origin},direction[3]={0,0,-1};
   n3ds_gpu_flashlight_set(path==3?1:0,light,direction);
   if(!n3ds_gpu_frame_begin()){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}active=1;n3ds_gpu_window_begin(&cam);
   struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);if(!v){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
   for(int i=0;i<4;++i) v[i]=(struct native_render_vertex){
    {origin+(i==1 || i==2?.53125f:-.375f)+(test>=15?1.f/2048:0),-origin+(i>=2?.4375f:-.5625f),origin-1.5f},
    {.5f,.5f,.5f},{.5f,.5f}};
   if(path!=4)n3ds_gpu_model_rebase_vertices(v,4,0);
   for(int i=0;i<4;++i)for(int c=0;c<3;++c)v[i].position[c]=precision_f24_input(v[i].position[c]);
   n3ds_gpu_geometry_flush(v,4*sizeof(*v));
   for(int i=0;i<3;++i)if(!n3ds_gpu_texture_bind(i,texture)){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
   if(path==0 || path==3 || path==4) {
    if(!(path==4?n3ds_gpu_model_cpu_draw(v,ids,6,0,0,1,NULL):n3ds_gpu_model_cpu_relative_draw(v,ids,6,0,0,1,NULL))){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
   } else {
    if(!n3ds_gpu_model_depth_relative_draw(v,ids,6)){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
    struct native_chicago_vertex *c=n3ds_gpu_chicago_allocate(4);if(!c){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
    for(int i=0;i<4;++i) {memset(c+i,0,sizeof(*c));memcpy(c[i].position,v[i].position,12);for(int k=0;k<4;++k)c[i].color[k]=1;for(int t=0;t<3;++t)c[i].uv[t][0]=c[i].uv[t][1]=.5f;}
    struct native_chicago_material m={.maps=path==2?3:1,.blend=NATIVE_BLEND_ADD,.two_sided=1,.fade=1};
    for(int i=0;i<m.maps;++i)if(!n3ds_gpu_texture_bind(i,texture))goto done;
    if(path==2) {
     float screen[3][4]={{.125f,0,0,.5f},{0,.125f,0,.5f},{0,0,0,1}},tint[3]={.8f,.9f,1};
     if(!n3ds_gpu_hologram_draw(&m,c,4,ids,6,screen,tint,1)){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
    } else if(!n3ds_gpu_chicago_relative_draw(&m,c,4,ids,6)){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
   }
   n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pass?b:a,240*400)){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
  }
  unsigned int changed=0,covered=0;
  for(unsigned int i=0;i<240*400;++i){changed+=a[i]!=b[i];covered+=a[i]!=0x102030ff;}
  if(path==4) {if(changed>legacy_worst)legacy_worst=changed;continue;}
  if(changed>worst)worst=changed;
  if(changed>32 || covered<1000){char line[128];snprintf(line,sizeof(line),"CPU PRECISION FAIL: case=%d changed=%u covered=%u",test,changed,covered);n3ds_log(line);{char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}}
 }
 if(legacy_worst<=32){char error[96];snprintf(error,sizeof(error),"CPU PRECISION: failed at line %d",__LINE__);n3ds_log(error);goto done;}
 ok=1;{char line[220];snprintf(line,sizeof(line),"PASS: CPU model f24-input precision: 24 translated/subpixel opaque, depth/transparent, hologram and flashlight cases; changed_pixels_max=%u; legacy world-space control=%u",worst,legacy_worst);n3ds_log(line);}
done:
 n3ds_gpu_flashlight_set(0,NULL,NULL);if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();
 n3ds_gpu_texture_destroy(texture);linearFree(a);linearFree(b);return !ok;
}
int n3ds_gpu_model_precision_tests(void)
{
 unsigned int *a=linearAlloc(240*400*4),*b=linearAlloc(240*400*4),pixels[64];
 void *texture=NULL;int active=0,ok=0;unsigned int worst=0;
 const unsigned short ids[6]={0,1,2,0,2,3};float uv[2]={1,1};
 for(int i=0;i<64;++i)pixels[i]=0x40c090ff;
 texture=n3ds_gpu_texture_create(pixels,8,8);if(!a || !b || !texture)goto done;
 for(int test=0;test<12;++test){
  int skinned=test&1,lit=test&2;float shift=(test/4==0?128.f:test/4==1?1024.f:4096.f);
  for(int pass=0;pass<2;++pass){
   float origin=pass?shift:0;
   struct native_render_camera cam={.position={origin,-origin,origin},.forward={.125f,.0625f,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.05f,.far_clip=100,.clear_color=0x102030ff};
   if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&cam);
   struct native_render_vertex rigid[4]={{{-1,-1,-3},{0,0,1},{0,0}},{{1,-1,-3},{0,0,1},{1,0}},{{1,1,-3},{0,0,1},{1,1}},{{-1,1,-3},{0,0,1},{0,1}}};
   struct native_skin_vertex skin[4];
   for(int i=0;i<4;++i){memcpy(skin+i,rigid+i,sizeof(*rigid));skin[i].bones[0]=0;skin[i].bones[1]=3;skin[i].bones[2]=.75f;skin[i].bones[3]=.25f;}
   float rows[24]={1,0,0,origin,0,1,0,-origin,0,0,1,origin, 1,0,0,origin+.25f,0,1,0,-origin+.125f,0,0,1,origin};
   struct native_model_lighting light={.ambient={.25f,.3f,.35f},.distant={{{.5f,.5f,.5f},{0,0,1}}},.count=1};
   struct native_model_material material={.base=texture,.lighting=lit?&light:NULL};
   unsigned int bytes=skinned?sizeof(skin):sizeof(rigid);
   void *v=n3ds_gpu_model_vertices_allocate((bytes+31)/32);if(!v)goto done;
   memcpy(v,skinned?(void *)skin:(void *)rigid,bytes);n3ds_gpu_geometry_flush(v,bytes);n3ds_gpu_texture_bind(0,texture);
   if(!(skinned?n3ds_gpu_model_skin_draw(v,ids,6,rows,2,uv,0,0,1,&material):n3ds_gpu_model_rigid_draw(v,ids,6,rows,uv,0,0,1,&material)))goto done;
   n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pass?b:a,240*400))goto done;
  }
  unsigned int changed=0,covered=0;
  for(unsigned int i=0;i<240*400;++i){changed+=a[i]!=b[i];covered+=a[i]!=0x102030ff;}
  if(changed>worst)worst=changed;
  if(changed>32 || covered<1000){char line[128];snprintf(line,sizeof(line),"PRECISION FAIL: case=%d changed=%u covered=%u",test,changed,covered);n3ds_log(line);goto done;}
 }
 ok=1;{char line[160];snprintf(line,sizeof(line),"PASS: camera-relative model precision: 12 translated rigid/skinned/lit cases through 4096 world units; changed_pixels_max=%u",worst);n3ds_log(line);}
 done:if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();n3ds_gpu_texture_destroy(texture);linearFree(a);linearFree(b);return ok?cpu_model_precision_tests():1;
}
