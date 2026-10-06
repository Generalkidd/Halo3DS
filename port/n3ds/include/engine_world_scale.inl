/* Lower world pixel/model cost using measured rendering pressure.
 * Menus, HUD and touch controls always use the physical LCD resolution.
 * No texture copies/readbacks and no CPU wait are needed for the upscale. */
static C3D_Tex scaled_texture[2];
static C3D_RenderTarget *scaled_target[2];
static int world_budget_enabled,world_scale,world_low_active,world_scale_failed[2];
static unsigned int scale_frames;
#include "engine_screen_effect.inl"
#include "engine_resolution_policy.inl"
static struct resolution_policy scale_policy;
static float scale_work_previous,scale_wait_previous;
static int scale_sample_ready;
static const unsigned short scale_widths[]={400,320,256,200};
static const unsigned short scale_heights[]={240,192,152,120};
void n3ds_gpu_world_budget(int enabled)
{
 if(world_budget_enabled!=!!enabled){
  world_budget_enabled=!!enabled;memset(&scale_policy,0,sizeof(scale_policy));
  world_scale=enabled && n3ds_input_platform_original_model()?1:0;
  scale_policy.level=world_scale;scale_sample_ready=0;
  if(world_scale)n3ds_log("WORLD PIXEL BUDGET: Original baseline 320x192; adaptive 256x152/200x120, HUD/menu full resolution");
 }
}
void n3ds_gpu_frame_work(float milliseconds)
{scale_work_previous=milliseconds;scale_wait_previous=n3ds_gpu_frame_wait_ticks()*(1000.f/268123480.f);scale_sample_ready=1;}
int n3ds_gpu_world_pressure(void){return world_budget_enabled?world_scale:0;}
float n3ds_gpu_world_pixel_scale(void){return world_budget_enabled && !world_scale_failed[eye_right]?scale_widths[world_scale]/400.f:1.f;}
int n3ds_gpu_stereo_view(void){return eye_right;}
static void world_scale_feedback(float gpu,float prepare)
{
 if(!world_budget_enabled || diagnostic_mode)return;
 if(!scale_sample_ready)return;
 scale_sample_ready=0;
 int next=resolution_policy_step_profile(&scale_policy,scale_work_previous,gpu,scale_wait_previous,prepare,n3ds_input_platform_original_model());
#ifdef HALO_N3DS_WORLD_SCALE_TEST
 next=HALO_N3DS_WORLD_SCALE_TEST;
#endif
 if(next!=world_scale){world_scale=next;char message[256];snprintf(message,sizeof(message),"WORLD PIXEL BUDGET: %ux%u work_ms=%.2f gpu_ms=%.2f wait_ms=%.2f prepare_ms=%.2f trial=%d recovery_ms=%.0f failures=%u; 28fps threshold, HUD/menu full resolution",scale_widths[next],scale_heights[next],scale_policy.work,scale_policy.gpu,scale_policy.wait,scale_policy.prepare,scale_policy.trial,scale_policy.recovery,scale_policy.recovery_failures);n3ds_log(message);}
}
static void world_scale_allocation_failed(const char *stage)
{
 char message[192];snprintf(message,sizeof(message),"WORLD PIXEL BUDGET: offscreen %s allocation failed, eye=%d VRAM_free=%lu; using native resolution",stage,eye_right,(unsigned long)vramSpaceFree());n3ds_log(message);
 world_scale_failed[eye_right]=1;
}
static C3D_RenderTarget *display_target(void){return eye_right?right_target:target;}
static C3D_RenderTarget *world_target(void){return world_low_active?scaled_target[eye_right]:display_target();}
static unsigned int world_clear_color(unsigned int rgba)
{
 if(!world_low_active || scaled_texture[eye_right].fmt!=GPU_RGB565)return rgba;
 /* GX memory fill consumes packed framebuffer pixels, not RGBA colors. */
 unsigned int rgb=((rgba>>16)&0xf800u)|((rgba>>13)&0x7e0u)|((rgba>>11)&0x1fu);
 return rgb|(rgb<<16);
}
static void world_target_bind(void)
{
 C3D_FrameDrawOn(bottom_selected?bottom_target:world_target());
 if(world_low_active && !bottom_selected)C3D_SetViewport(0,0,scale_heights[world_scale],scale_widths[world_scale]);
}
void n3ds_gpu_world_view_begin(void)
{
 world_low_active=0;
 if((!world_budget_enabled || !world_scale) && !screen_effect_active || diagnostic_mode || world_scale_failed[eye_right])return;
 if(!scaled_target[eye_right]){
  C3D_Tex *t=&scaled_texture[eye_right];
  /* The resolved world is opaque. RGB565 halves Original-model color storage
   * and bandwidth; the HUD still renders into the full-resolution RGBA8 LCD.
   * New models retain their existing color precision. */
  GPU_TEXCOLOR format=n3ds_input_platform_original_model()?GPU_RGB565:GPU_RGBA8;
  if(!C3D_TexInitVRAM(t,256,512,format)){world_scale_allocation_failed("color");return;}
  scaled_target[eye_right]=C3D_RenderTargetCreateFromTex(t,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
  if(!scaled_target[eye_right]){C3D_TexDelete(t);memset(t,0,sizeof(*t));world_scale_allocation_failed("depth/target");return;}
  C3D_TexSetFilter(t,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
  char message[128];snprintf(message,sizeof(message),"WORLD PIXEL TARGET: eye=%d color=%s bytes=%lu",eye_right,format==GPU_RGB565?"RGB565":"RGBA8",(unsigned long)t->size);n3ds_log(message);
 }
 world_low_active=1;++scale_frames;
}
void n3ds_gpu_world_resolve(void)
{
 if(!world_low_active)return;
 /* Commit the world segment with cache flush flags before sampling its color
  * attachment. FrameEnd starts the ordered queue; this never waits on it. */
 C3D_FrameSplit(GX_CMDLIST_FLUSH);
 world_low_active=0;C3D_FrameDrawOn(display_target());native_light_env_bind(NULL);
 void *saved=n3ds_gpu_texture_bound(0);C3D_Tex *t=&scaled_texture[eye_right];
 n3ds_gpu_texture_bind(0,t);extern void n3ds_gpu_hud_layout(int);n3ds_gpu_hud_layout(0);
 n3ds_gpu_text_begin(400,240,0xffffffffu,0,0);
 screen_effect_combiners();
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
 /* Tilted world projection maps screen X to framebuffer Y, and screen Y to
  * decreasing framebuffer X. Sample texel centres to exclude unused padding. */
 float u0=.5f/256,u1=(scale_heights[world_scale]-.5f)/256;
 float v0=.5f/512,v1=(scale_widths[world_scale]-.5f)/512;
 struct native_text_vertex q[4]={{{0,0},{1,1,1,1},{u1,v1}},{{400,0},{1,1,1,1},{u1,v0}},{{400,240},{1,1,1,1},{u0,v0}},{{0,240},{1,1,1,1},{u0,v1}}};
 n3ds_gpu_text_draw(q);n3ds_gpu_texture_bind(0,saved);
 screen_effect_unbind();
}
static void world_scale_dispose(void)
{
 for(int i=0;i<2;++i){if(scaled_target[i])C3D_RenderTargetDelete(scaled_target[i]);scaled_target[i]=NULL;if(scaled_texture[i].data)C3D_TexDelete(&scaled_texture[i]);memset(&scaled_texture[i],0,sizeof(scaled_texture[i]));world_scale_failed[i]=0;}
 screen_effect_dispose();
 world_low_active=world_scale=world_budget_enabled=0;memset(&scale_policy,0,sizeof(scale_policy));scale_sample_ready=0;
}
