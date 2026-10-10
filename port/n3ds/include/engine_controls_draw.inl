/* New3DS integration additions by Kevin Boggs. */
/* Native controller UI uses a tiny shared font and one white texel, keeping
 * text at the LCD's resolution independently of the world rendering scale. */
static void *controls_white;
static struct native_text_vertex controls_panel_vertices[4*128];
static unsigned int controls_panel_quads;
/* Kevin Boggs: controls_panel_trapezoid - New3DS integration. */
static void controls_panel_trapezoid(float left_top,float right_top,float top,
 float left_bottom,float right_bottom,float bottom,unsigned int color)
{
 if(controls_panel_quads>=128)return;
 struct native_text_vertex *v=controls_panel_vertices+4*controls_panel_quads++;
 static const float pos[4][2]={{0,0},{1,0},{1,1},{0,1}};
 for(int i=0;i<4;++i){
  v[i].position[0]=pos[i][1]?(pos[i][0]?right_bottom:left_bottom):(pos[i][0]?right_top:left_top);
  v[i].position[1]=pos[i][1]?bottom:top;
  v[i].uv[0]=pos[i][0];v[i].uv[1]=pos[i][1];
  for(int c=0;c<4;++c)v[i].color[c]=((color>>(8*c))&255)/255.f;
 }
}
/* Kevin Boggs: controls_panel_rect - New3DS integration. */
static void controls_panel_rect(float x,float y,float w,float h,unsigned int color)
{controls_panel_trapezoid(x,x+w,y,x,x+w,y+h,color);}
/* Kevin Boggs: controls_panel_flush - New3DS integration. */
static void controls_panel_flush(void)
{
 if(!controls_panel_quads)return;
 if(!controls_white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;controls_white=n3ds_gpu_texture_create(pixels,8,8);}
 if(controls_white){
  n3ds_gpu_texture_bind(0,controls_white);n3ds_gpu_text_begin(320,240,0xffffffff,1,0);
  C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
  n3ds_gpu_text_draw_many(controls_panel_vertices,controls_panel_quads);
 }
 controls_panel_quads=0;
}
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
/* Kevin Boggs: controls_radar_edge - New3DS integration. */
static float controls_radar_edge(float y)
{float d=y-116.f;return 160.f-sqrtf(80.f*80.f-d*d);}
/* Kevin Boggs: controls_halo_touch_panel - New3DS integration. */
static void controls_halo_touch_panel(int right,int lower,int pressed,int blank)
{
 const unsigned int rim=pressed?0xffd7ec7e:0xffffb971;
 const unsigned int fill=pressed?0xff785038:0xff3e2812;
 const unsigned int ink=pressed?0xffecffd7:0xffe9cf9a;
 float top=lower?122.f:64.f,bottom=lower?164.f:114.f;
 /* Twelve connected trapezoids follow a circle outside the radar rim. The
  * inset layer leaves one continuous three-pixel border at the corners. */
 for(int inset=0;inset<=3;inset+=3){
  float a=top+inset,b=bottom-inset;
  for(int segment=0;segment<12;++segment){
   float ya=a+(b-a)*segment/12.f,yb=a+(b-a)*(segment+1)/12.f;
   float ea=controls_radar_edge(ya),eb=controls_radar_edge(yb);
   if(right)controls_panel_trapezoid(320.f-ea+inset,312.f-inset,ya,
       320.f-eb+inset,312.f-inset,yb,inset?fill:rim);
   else controls_panel_trapezoid(8.f+inset,ea-inset,ya,
       8.f+inset,eb-inset,yb,inset?fill:rim);
  }
 }
 float cy=(top+bottom)*.5f,edge=controls_radar_edge(cy);
 float cx=right?(320.f-edge+312.f)*.5f:(8.f+edge)*.5f;
 controls_panel_rect(right?320.f-controls_radar_edge(top+5)+12.f:20.f,
     top+5.f,right?312.f-(320.f-controls_radar_edge(top+5)+24.f):controls_radar_edge(top+5)-32.f,
     2.f,rim);
 if(blank){
  controls_panel_rect(cx-12,cy-1,8,2,ink);controls_panel_rect(cx+4,cy-1,8,2,ink);
  controls_panel_rect(cx-1,cy-11,2,7,ink);controls_panel_rect(cx-1,cy+4,2,7,ink);
  if(pressed)controls_panel_rect(cx-2,cy-2,4,4,ink);
 }
}
/* Kevin Boggs: n3ds_gpu_match_touch_overlay - New3DS integration. */
void n3ds_gpu_match_touch_overlay(int fps,int show_fps,int show_score,int pressed,
 const struct native_touch_meter *meters,int meter_count)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 if(meter_count>0){
  controls_panel_rect(8,27,104,28,0xff281808);
  controls_panel_rect(11,29,98,1,0xffe5be63);
  for(int i=0;i<meter_count && i<3;++i){
   float y=meter_count==1?39.f:meter_count==2?35.f+10.f*i:33.f+8.f*i;
   float fill=fmaxf(0.f,fminf(1.f,meters[i].fill));
   unsigned int color=meters[i].state==2?0xff329bfa:meters[i].state>=4?0xffe47db4:0xfff4da64;
   controls_panel_rect(12,y,96,5,0xff4d381d);
   if(fill>0)controls_panel_rect(12,y,96.f*fill,5,color);
  }
 }
 controls_halo_touch_panel(0,0,pressed==1,1);
 controls_halo_touch_panel(1,0,pressed==2,1);
 controls_halo_touch_panel(0,1,pressed==3,1);
 controls_halo_touch_panel(1,1,pressed==4,show_score?0:1);
 controls_panel_flush();
 if(show_score){controls_text(253,132,"SCORE",0xfffff5e9,0);controls_text(257,146,"HOLD",0xffe9cf9a,0);}
 if(show_fps){char label[20];snprintf(label,sizeof(label),"FPS %02d",fps);
  controls_text(232,47,label,fps>=28?0xff7eecd7:fps>=20?0xffaad6e9:0xff7070ff,0);
 }
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
/* Kevin Boggs: n3ds_gpu_match_scoreboard - New3DS integration. */
void n3ds_gpu_match_scoreboard(const struct native_score_row *rows,int count)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(0);n3ds_gpu_hud_layout(0);
 controls_quad(24,24,352,192,0xffb97535,1);controls_quad(26,26,348,188,0xff221a12,1);
 controls_text(40,34,"MULTIPLAYER SCORE",0xfff6dcaa,1);
 controls_text(40,49,"PLAYER            SCORE   K   D",0xffa8d8ec,1);
 for(int i=0;i<count && i<16;++i){
  char line[48];snprintf(line,sizeof(line),"%-15.15s  %5ld  %2d  %2d",
   rows[i].name,rows[i].score,rows[i].kills,rows[i].deaths);
  controls_text(40,63+i*9,line,rows[i].local?0xff6de5ff:0xffe8e4d5,1);
 }
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
/* Kevin Boggs: n3ds_gpu_match_postgame_lobby - New3DS integration. */
void n3ds_gpu_match_postgame_lobby(void)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_panel_rect(8,8,304,52,0xffb97535);
 controls_panel_rect(11,11,298,46,0xff221a12);
 controls_panel_flush();
 controls_text(88,17,"MATCH COMPLETE",0xfff6dcaa,0);
 controls_text(80,34,"RETURN TO LOBBY",0xff7eecd7,0);
 controls_text(120,47,"TAP OR B",0xffe9cf9a,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
void n3ds_gpu_controls_entry(void)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_quad(24,202,272,30,0xff53391a,0);
 controls_text(32,214,"SELECT: Game settings",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
void n3ds_gpu_controls_draw(const struct native_control_settings *s,int selected,int original,int save_error)
{
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
 static unsigned int settings_draw_trace;
 if(settings_draw_trace++<3) n3ds_log("ARSENAL SETTINGS: draw called");
#endif
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(0);
 controls_quad(0,0,400,240,0xff24150c,1);
 controls_text(128,70,"GAME SETTINGS",0xffffffff,1);
 controls_text(64,104,"Use the touch screen or D-pad.",0xffe9d6aa,1);
 controls_text(96,128,"Press START to save and return.",0xffe9d6aa,1);
 controls_text(48,168,s->xbox_buttons?"Xbox layout selected":"Nintendo layout selected",0xffffffff,1);
 controls_text(64,188,original?"L: fire  R: grenade  Up: zoom":s->swap_shoulders?"R: fire  L: grenade  ZR: zoom":"ZR: fire  ZL: grenade  R: zoom",0xffe9d6aa,1);
 if(!original)controls_text(64,208,"PC high textures apply next map load.",0xffe9d6aa,1);
 n3ds_gpu_hud_screen(1);controls_quad(0,0,320,240,0xff24150c,0);
 controls_text(108,16,"GAME SETTINGS",0xffffffff,0);
 const char *labels[]={"C-stick aim","Touch aim","ABXY layout","Swap shoulders","PC textures","Swap X/Y","Swap A/B","Show FPS","Decals"};
 for(int row=0;row<(original?7:9);++row){int field=original && row>=3?row+2:row;
  unsigned int color=0xffffffff;float y=24+row*18;
  controls_quad(12,y,296,17,selected==row?0xff674826:0xff35251a,0);
  controls_text(20,y+5,labels[field],color,0);char value[32];
  if(field<2)snprintf(value,sizeof(value),"%u%%",field?s->touch:s->cstick);
  else snprintf(value,sizeof(value),"%s",field==2?(s->xbox_buttons?"Xbox":"Nintendo"):
      field==3?(s->swap_shoulders?"On":"Off"):
      field==4?(s->high_textures?"128+mip":"64"):
      field==5?(s->swap_xy?"On":"Off"):
      field==6?(s->swap_ab?"On":"Off"):
      field==7?(s->show_fps?"On":"Off"):(s->decals?"On":"Off"));
  controls_text(164,y+5,value,color,0);
  controls_quad(242,y+1,28,15,0xff735333,0);controls_quad(274,y+1,28,15,0xff735333,0);
  controls_text(252,y+5,"-",color,0);controls_text(284,y+5,"+",color,0);
 }
 controls_text(16,188,save_error?"Save failed. START retry / B cancel":"Up/down: select  Left/right: change",save_error?0xff70a0ff:0xffe9d6aa,0);
 controls_quad(16,202,134,30,0xff53391a,0);controls_quad(170,202,134,30,0xff674826,0);
 controls_text(35,214,"Reset defaults",0xffffffff,0);controls_text(190,214,"Save + back",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
#ifdef HALO_N3DS_PC_LOCAL_MENU
/* Kevin Boggs: n3ds_gpu_pc_local_menu - New3DS integration. */
void n3ds_gpu_pc_local_menu(int map,int mode,int bots,int difficulty,int weapon_set,int vehicle_choice,int page,int field,int error,int high_textures,int original)
{
 static const char *const maps[]={"Beaver Creek","Blood Gulch","Boarding Action","Derelict","Chill Out","Damnation","Danger Canyon","Death Island","Gephyrophobia","Hang 'Em High","Ice Fields","Infinity","Longest","Prisoner","Chiron TL-34","Rat Race","Sidewinder","Timberland","Wizard"};
 static const char *const modes[]={"Slayer","CTF","Oddball","King","Race"};
 static const char *const difficulties[]={"Easy","Normal","Hard"};
 static const char *const weapons[]={"Normal (PC map)","Pistols","Rifles","Plasma","Snipers","No sniping","Rockets","Shotguns","Short range","Human","None","Flamethrowers","Fuel Rods"};
 static const char *const vehicles[]={"Mode default","All PC vehicles","None","Warthog + Rocket","Ghost","Scorpion"};
 char line[48];void *saved=n3ds_gpu_texture_bound(0);
 n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(1);
 controls_quad(0,0,400,240,0xff24150c,1);
 controls_text(104,48,page?"MATCH EQUIPMENT":"LOCAL MULTIPLAYER",0xffffffff,1);
 controls_text(48,90,page?"Pick map weapon and vehicle rules.":"Choose a PC retail map and original game mode.",0xffe9d6aa,1);
 controls_text(80,120,page?"Normal keeps PC map weapon pickups.":"The match runs on this 3DS with bots.",0xffe9d6aa,1);
 controls_text(96,170,page?"X: match setup  B: back":"X: equipment  B: back",0xffffffff,1);
 n3ds_gpu_hud_screen(0);controls_quad(0,0,320,240,0xff24150c,0);
 controls_text(72,10,page?"MATCH EQUIPMENT":"LOCAL MULTIPLAYER",0xffffffff,0);
 if(page){
  for(int row=0;row<2;++row){
   float y=40+row*42;controls_quad(12,y,296,36,field==row?0xff674826:0xff35251a,0);
   snprintf(line,sizeof(line),"%s: %s",row?"Vehicles":"Weapons",row?vehicles[vehicle_choice]:weapons[weapon_set]);
   controls_text(20,y+13,line,0xffffffff,0);
  }
  controls_text(18,143,"Normal uses original PC placements.",0xffe9d6aa,0);
  controls_text(18,160,"All adds Banshees to 3 PC maps",0xffe9d6aa,0);
  controls_text(18,174,"and keeps original vehicles.",0xffe9d6aa,0);
 }else{
  for(int row=0;row<(original?4:5);++row){
   float y=27+row*33;controls_quad(12,y,296,30,field==row?0xff674826:0xff35251a,0);
   if(row==0)snprintf(line,sizeof(line),"Map %02d/19: %s",map+1,maps[map]);
   else if(row==1)snprintf(line,sizeof(line),"Mode: %s",modes[mode]);
   else if(row==2)snprintf(line,sizeof(line),"Bots: %d",bots);
   else if(row==3)snprintf(line,sizeof(line),"Difficulty: %s",difficulties[difficulty]);
   else snprintf(line,sizeof(line),"PC textures: %s",high_textures?"128 + mip":"64 standard");
   controls_text(20,y+10,line,0xffffffff,0);
  }
 }
 controls_text(16,191,error?"Save or launch failed":"Tap choice or use left/right",error?0xff70a0ff:0xffe9d6aa,0);
 controls_quad(16,202,136,30,0xff674826,0);controls_quad(168,202,136,30,0xff53391a,0);
 controls_text(55,214,page?"A: DONE":"A: START",0xffffffff,0);controls_text(206,214,page?"X: BACK":"X: GEAR",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
#endif
