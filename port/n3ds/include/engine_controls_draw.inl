/* Native controller UI uses a tiny shared font and one white texel, keeping
 * text at the LCD's resolution independently of the world rendering scale. */
static void *controls_white;
static void controls_quad(float x,float y,float w,float h,unsigned int color,int top)
{
 if(!controls_white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;controls_white=n3ds_gpu_texture_create(pixels,8,8);}
 if(!controls_white)return;
 n3ds_gpu_texture_bind(0,controls_white);n3ds_gpu_text_begin(top?400:320,240,color,1,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
 struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{0,0}},{{x+w,y},{1,1,1,1},{1,0}},{{x+w,y+h},{1,1,1,1},{1,1}},{{x,y+h},{1,1,1,1},{0,1}}};
 n3ds_gpu_text_draw(q);
}
static void controls_text(float x,float y,const char *text,unsigned int color,int top)
{
 status_text(NULL,0,1);if(!loading_font)return;
 n3ds_gpu_texture_bind(0,loading_font);n3ds_gpu_text_begin(top?400:320,240,color,1,0);
 for(const char *p=text;*p;++p,x+=8){unsigned int c=(unsigned char)*p;float u=(c%16)/16.f,v=(c/16)/8.f;
  struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{u,v}},{{x+8,y},{1,1,1,1},{u+1.f/16,v}},{{x+8,y+8},{1,1,1,1},{u+1.f/16,v+1.f/8}},{{x,y+8},{1,1,1,1},{u,v+1.f/8}}};n3ds_gpu_text_draw(q);
 }
}
void n3ds_gpu_controls_entry(void)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_quad(24,202,272,30,0xff53391a,0);
 controls_text(32,214,"SELECT: Controller settings",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
void n3ds_gpu_controls_draw(const struct native_control_settings *s,int selected,int original,int save_error)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(0);
 controls_quad(0,0,400,240,0xff24150c,1);
 controls_text(128,70,"CONTROLLER SETTINGS",0xffffffff,1);
 controls_text(64,104,"Use the touch screen or D-pad.",0xffe9d6aa,1);
 controls_text(96,128,"Press START to save and return.",0xffe9d6aa,1);
 controls_text(48,168,s->xbox_buttons?"Xbox: B = jump, Y = reload / interact":"Nintendo: A = jump, X = reload / interact",0xffffffff,1);
 controls_text(64,188,original?"L: fire  R: grenade  Up: zoom":s->swap_shoulders?"R: fire  L: grenade  ZR: zoom":"ZR: fire  ZL: grenade  R: zoom",0xffe9d6aa,1);
 n3ds_gpu_hud_screen(1);controls_quad(0,0,320,240,0xff24150c,0);
 controls_text(84,16,"CONTROLLER SETTINGS",0xffffffff,0);
 const char *labels[]={"C-stick aim","Touch aim","ABXY layout","Swap L/ZL + R/ZR"};
 for(int row=0;row<4;++row){int enabled=row!=3 || !original;unsigned int color=enabled?0xffffffff:0xff7e7066;
  float y=42+row*32;controls_quad(12,y,296,30,selected==row?0xff674826:0xff35251a,0);
  controls_text(20,y+4,labels[row],color,0);char value[40];
  if(row<2)snprintf(value,sizeof(value),"%u%%",row?s->touch:s->cstick);
  else snprintf(value,sizeof(value),"%s",row==2?(s->xbox_buttons?"Xbox positions":"Nintendo letters"):original?"New models only":s->swap_shoulders?"On":"Off");
  controls_text(20,y+17,value,color,0);
  if(enabled){controls_quad(218,y+3,38,24,0xff735333,0);controls_quad(264,y+3,38,24,0xff735333,0);controls_text(233,y+11,"-",color,0);controls_text(279,y+11,"+",color,0);}
 }
 controls_text(16,184,save_error?"Save failed. START retry / B cancel":"Up/down: select  Left/right: change",save_error?0xff70a0ff:0xffe9d6aa,0);
 controls_quad(16,202,134,30,0xff53391a,0);controls_quad(170,202,134,30,0xff674826,0);
 controls_text(35,214,"Reset defaults",0xffffffff,0);controls_text(190,214,"Save + back",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
