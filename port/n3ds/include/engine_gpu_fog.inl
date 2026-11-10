/* New3DS integration additions by Kevin Boggs.
 * Linear W-depth gives the 128-entry fog LUT useful outdoor precision without
 * moving the near plane. The projection and GEQUAL depth ordering are retained. */
static C3D_FogLut distance_fog_lut;
static float distance_fog_start,distance_fog_end,distance_fog_lut_far,distance_fog_lut_start,distance_fog_lut_end;
static unsigned int distance_fog_color;
static int distance_fog_world,distance_fog_sky,distance_fog_mode=-99;
static void *distance_fog_white;
static float distance_fog_depth_scale;
static int distance_fog_depth_w=-1;
/* Kevin Boggs: distance_fog_depth */
static void distance_fog_depth(int world)
{
 float far=current_camera.far_clip,near=current_camera.near_clip;
 float scale=world?-(far-near)/(far*near):-1.f;
 if(world!=distance_fog_depth_w || scale!=distance_fog_depth_scale) {
  C3D_DepthMap(!world,scale,0);distance_fog_depth_w=world;distance_fog_depth_scale=scale;
 }
}
/* Kevin Boggs: distance_fog_clip */
static int distance_fog_clip(float out[8][3],float in[8][3],int count,float limit,int below)
{
 int used=0;
 for(int i=0;i<count;++i) {
  float *a=in[i],*b=in[(i+1)%count];
  int ai=below?a[2]<=limit:a[2]>=limit,bi=below?b[2]<=limit:b[2]>=limit;
  if(ai)memcpy(out[used++],a,3*sizeof(float));
  if(ai!=bi) {
   float t=(limit-a[2])/(b[2]-a[2]);
   out[used][0]=a[0]+t*(b[0]-a[0]);out[used][1]=a[1]+t*(b[1]-a[1]);out[used++][2]=limit;
  }
 }
 return used;
}
/* Kevin Boggs: n3ds_gpu_fog_backdrop */
void n3ds_gpu_fog_backdrop(void)
{
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_STATE);
 if(!distance_fog_end || bottom_selected || first_person_active)return;
 /* A culled sea/terrain must reveal fog rather than the sky model's black
  * underside. One small sky-background draw covers the horizon and below;
  * foreground terrain subsequently overwrites it. Camera pitch/roll apply. */
 float tan_half=tanf(current_camera.vertical_fov*.5f);
 float right_z=current_camera.forward[0]*current_camera.up[1]-current_camera.forward[1]*current_camera.up[0];
 float quad[8][3]={{0,0,0},{400,0,0},{400,240,0},{0,240,0}},a[8][3],b[8][3];
 struct native_text_vertex vertices[32];unsigned int quads=0;
 for(int i=0;i<4;++i)quad[i][2]=current_camera.forward[2]+tan_half*(current_camera.up[2]*(1-quad[i][1]/120.f)+right_z*(quad[i][0]/200.f-1)*400.f/240.f);
 for(int region=0;region<2;++region) {
  int count=distance_fog_clip(a,quad,4,region?.18f:0,1);
  if(region){count=distance_fog_clip(b,a,count,0,0);memcpy(a,b,count*sizeof(*a));}
  for(int i=1;i<count-1;++i) {
   int order[4]={0,i,i+1,i+1};
   for(int j=0;j<4;++j) {
    float *p=a[order[j]];
    vertices[quads*4+j]=(struct native_text_vertex){{p[0],p[1]},{1,1,1,fmaxf(0,fminf(1,1-p[2]/.18f))},{.5f,.5f}};
   }
   ++quads;
  }
 }
 if(!quads)return;
 if(!distance_fog_white) {
  unsigned int white[64];memset(white,255,sizeof(white));distance_fog_white=n3ds_gpu_texture_create(white,8,8);
  if(!distance_fog_white)return;
 }
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_texture_bind(0,distance_fog_white);
 extern void n3ds_gpu_hud_layout(int);n3ds_gpu_hud_layout(0);
 n3ds_gpu_text_begin(400,240,distance_fog_color|0xff000000u,0,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
 n3ds_gpu_text_draw_many(vertices,quads);
 n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
/* Kevin Boggs: n3ds_gpu_fog_config */
void n3ds_gpu_fog_config(float start,float end,unsigned int rgb)
{
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_STATE);
 if(distance_fog_color!=rgb)distance_fog_mode=-99;
 distance_fog_start=start;distance_fog_end=end;distance_fog_color=rgb;
}
/* Kevin Boggs: n3ds_gpu_fog_blend */
void n3ds_gpu_fog_blend(int blend)
{
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_STATE);
 int enabled=distance_fog_world && !distance_fog_sky && !first_person_active && !bottom_selected;
 /* Premultiplied shaders already carry authored fog/fade in their alpha.
  * Adding an unscaled fog color there creates luminous rectangular particles. */
 if(blend==NATIVE_BLEND_PREMULTIPLIED_ALPHA)enabled=0;
 int key=enabled?blend+10:-1;if(key==distance_fog_mode)return;distance_fog_mode=key;
 C3D_FogGasMode(enabled?GPU_FOG:GPU_NO_FOG,GPU_PLAIN_DENSITY,false);
 if(enabled) {
  unsigned int color=distance_fog_color;
  if(blend==NATIVE_BLEND_ADD || blend==NATIVE_BLEND_SUBTRACT || blend==NATIVE_BLEND_MAX)color=0;
  if(blend==NATIVE_BLEND_MULTIPLY || blend==NATIVE_BLEND_MIN)color=0xffffff;
  if(blend==NATIVE_BLEND_DOUBLE_MULTIPLY)color=0x808080;
  C3D_FogColor(color);
 }
}
/* Kevin Boggs: n3ds_gpu_fog_sky */
void n3ds_gpu_fog_sky(int sky)
{
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_STATE);
 distance_fog_world=distance_fog_end>0 && !bottom_selected && !first_person_active;
 distance_fog_sky=!!sky;n3ds_gpu_fog_blend(-1);
 distance_fog_depth(distance_fog_world && !sky);
}
/* Kevin Boggs: n3ds_gpu_fog_overlay */
void n3ds_gpu_fog_overlay(void)
{
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_STATE);
 distance_fog_world=0;n3ds_gpu_fog_blend(-1);distance_fog_depth(0);
}
/* Kevin Boggs: distance_fog_window */
static void distance_fog_window(void)
{
 distance_fog_world=distance_fog_end>0 && !bottom_selected && !first_person_active;
 distance_fog_sky=0;distance_fog_depth(distance_fog_world);
 if(distance_fog_world) {
  float far=current_camera.far_clip;
  if(far!=distance_fog_lut_far || distance_fog_start!=distance_fog_lut_start || distance_fog_end!=distance_fog_lut_end) {
   float data[256],prior=0;
   for(int i=0;i<=128;++i) {
    float depth=(1-i/128.f)*far;
    float v=fmaxf(0,fminf(1,(distance_fog_end-depth)/(distance_fog_end-distance_fog_start)));
    if(i<128)data[i]=v;if(i)data[i+127]=v-prior;prior=v;
   }
   FogLut_FromArray(&distance_fog_lut,data);
   distance_fog_lut_far=far;distance_fog_lut_start=distance_fog_start;distance_fog_lut_end=distance_fog_end;
   C3D_FogLutBind(&distance_fog_lut);
  }
 }
 n3ds_gpu_fog_blend(-1);
}
