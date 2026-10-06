/* World-only PICA postprocess, folded into the existing upscale when active.
 * HUD and bottom LCD are drawn afterwards. No framebuffer readback, CPU pixel
 * conversion or iterative Xbox convolution passes. All six TEV stages are
 * bounded; optional video grain uses a tiny immutable, tiled texture. */
static struct native_screen_effect screen_effect;
static int screen_effect_active;
static void *screen_video_texture,*screen_saved_texture1;
static unsigned int screen_video_phase;
static float screen_clamp(float x){return isfinite(x)?fminf(1,fmaxf(0,x)):0;}
static unsigned int screen_color(float r,float g,float b,float a)
{return (unsigned int)(screen_clamp(r)*255+.5f)|((unsigned int)(screen_clamp(g)*255+.5f)<<8)|((unsigned int)(screen_clamp(b)*255+.5f)<<16)|((unsigned int)(screen_clamp(a)*255+.5f)<<24);}
void n3ds_gpu_screen_effect(const struct native_screen_effect *effect)
{
 ++screen_video_phase;
 if(effect)screen_effect=*effect;else memset(&screen_effect,0,sizeof(screen_effect));
 screen_effect.enhancement=screen_clamp(screen_effect.enhancement);
 screen_effect.desaturation=screen_clamp(screen_effect.desaturation);
 screen_effect.noise=screen_clamp(screen_effect.noise);
 for(int i=0;i<3;++i)screen_effect.tint[i]=screen_clamp(screen_effect.tint[i]);
 screen_effect_active=screen_effect.enhancement>.001f || screen_effect.desaturation>.001f || screen_effect.video;
}
static void screen_effect_combiners(void)
{
 if(!screen_effect_active)return;
 for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
 C3D_TexEnvBufUpdate(C3D_Both,0);
 /* DOT3 first needs unsigned color shifted into [.5,1]. Its coefficients
  * include the PICA factor four, yielding luminance across the full range. */
 C3D_TexEnv *e=C3D_GetTexEnv(0);
 C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);
 C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvColor(e,screen_color(.5f,.5f,.5f,1));
 e=C3D_GetTexEnv(1);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,0);
 C3D_TexEnvFunc(e,C3D_RGB,GPU_DOT3_RGB);C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_4);
 C3D_TexEnvColor(e,screen_color(.5f+.299f/8,.5f+.587f/8,.5f+.114f/8,1));
 e=C3D_GetTexEnv(2);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,0);
 C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
 float *t=screen_effect.tint;
 C3D_TexEnvColor(e,screen_color(t[0],t[1],t[2],1));
 e=C3D_GetTexEnv(3);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0,GPU_CONSTANT);
 C3D_TexEnvFunc(e,C3D_RGB,screen_effect.additive?GPU_MULTIPLY_ADD:GPU_INTERPOLATE);
 if(screen_effect.additive) {
  C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_TEXTURE0);
  C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
 } else C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
 C3D_TexEnvColor(e,screen_color(1,1,1,screen_effect.desaturation));
 /* Up to 2x brightness, smoothly tied to the original flashlight fade. */
 e=C3D_GetTexEnv(4);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,0);
 C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
 float gain=.5f+.5f*screen_effect.enhancement;
 if(screen_effect.video && screen_effect.video_overbright>0){
  gain=1.f;if(screen_effect.video_overbright>=2)C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_4);
 }
 if(screen_effect.video) gain*=1.f-screen_effect.noise*((screen_video_phase&1)?.015f:0.f);
 C3D_TexEnvColor(e,screen_color(gain,gain,gain,1));
 if(screen_effect.video) {
  if(!screen_video_texture) {
   unsigned int pixels[128*8],seed=1234;
   for(int y=0;y<8;++y)for(int x=0;x<128;++x){seed=seed*1664525u+1013904223u;
    unsigned int value=(x&1)?218:246;value-=seed>>28;
    unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
    pixels[(x>>3)*64+morton]=value*0x01010100u|255;}
   screen_video_texture=n3ds_gpu_texture_create(pixels,128,8);
  }
  if(screen_video_texture){screen_saved_texture1=n3ds_gpu_texture_bound(1);n3ds_gpu_texture_bind(1,screen_video_texture);
   C3D_TexSetFilter(screen_video_texture,GPU_NEAREST,GPU_NEAREST);
   e=C3D_GetTexEnv(5);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,0);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
   C3D_TexSetWrap(screen_video_texture,GPU_REPEAT,GPU_REPEAT);
  }
 }
}
static void screen_effect_unbind(void)
{if(screen_effect.video && screen_video_texture)n3ds_gpu_texture_bind(1,screen_saved_texture1);}
static void screen_effect_dispose(void)
{n3ds_gpu_texture_destroy(screen_video_texture);screen_video_texture=NULL;screen_effect_active=0;memset(&screen_effect,0,sizeof(screen_effect));}
#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_gpu_screen_effect_test_state(int video)
{return screen_effect_active && !!screen_effect.video==!!video;}
#endif
