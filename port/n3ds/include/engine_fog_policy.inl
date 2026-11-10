/* New3DS integration additions by Kevin Boggs.
 * Rendering budget only. Never changes simulation, activation or collision. */
#include "main/main.h"
#include "cutscene/cinematics.h"
#include "interface/ui_widget.h"
void n3ds_log(const char *);
static struct {
 unsigned int generation; int active,level,trial; float base,distance,work,prepare;
 float over,quiet,hold,trial_time,prior_work,prior_prepare;
} distance_budget;

/* Kevin Boggs: n3ds_engine_fog_busy */
int n3ds_engine_fog_busy(void)
{
 float target=distance_budget.base*(1.f-.18f*distance_budget.level);
 return distance_budget.active && (distance_budget.trial || fabsf(target-distance_budget.distance)>.75f);
}

/* Kevin Boggs: n3ds_engine_fog_feedback */
void n3ds_engine_fog_feedback(float work,float gpu,float wait,float prepare,int resolution_busy)
{
 if(!distance_budget.active || !isfinite(work) || !isfinite(prepare) || !isfinite(gpu) || !isfinite(wait) ||
    work<0 || prepare<0 || gpu<0 || wait<0 || work>200 || gpu>200)return;
 float dt=fmaxf(33.333f,work);
 distance_budget.work+=(work-distance_budget.work)*.12f;
 distance_budget.prepare+=(prepare-distance_budget.prepare)*.12f;
 distance_budget.hold=fmaxf(0,distance_budget.hold-dt);
 float target=distance_budget.base*(1.f-.18f*distance_budget.level);
 float step=distance_budget.base*(target<distance_budget.distance?.12f:.05f)*dt*.001f;
 distance_budget.distance+=fmaxf(-step,fminf(step,target-distance_budget.distance));
 if(distance_budget.trial) {
  if(fabsf(target-distance_budget.distance)>.75f)return;
  distance_budget.trial_time+=dt;if(distance_budget.trial_time<1800)return;
  int gain=distance_budget.prior_work-distance_budget.work>=fmaxf(1.5f,distance_budget.prior_work*.03f) ||
      distance_budget.prior_prepare-distance_budget.prepare>=fmaxf(1.f,distance_budget.prior_prepare*.05f);
  if(!gain)--distance_budget.level;
  {char line[160];snprintf(line,sizeof(line),"FOG BUDGET: trial %s level=%d work_ms=%.2f prior_ms=%.2f",gain?"accepted":"rejected",distance_budget.level,distance_budget.work,distance_budget.prior_work);n3ds_log(line);}
  distance_budget.trial=0;distance_budget.hold=gain?10000.f:30000.f;
  distance_budget.over=distance_budget.quiet=0;return;
 }
 if(resolution_busy || distance_budget.hold || n3ds_engine_fog_busy())return;
 int pressure=distance_budget.work>1000.f/28.f && (wait>=2.f || distance_budget.prepare>=12.f);
 if(pressure) {
  distance_budget.quiet=0;distance_budget.over+=dt;
  if(distance_budget.over>2500 && distance_budget.level<2) {
   distance_budget.prior_work=distance_budget.work;distance_budget.prior_prepare=distance_budget.prepare;
   ++distance_budget.level;distance_budget.trial=1;distance_budget.trial_time=0;distance_budget.over=0;
   {char line[128];snprintf(line,sizeof(line),"FOG BUDGET: trial level=%d range=%.1f work_ms=%.2f",distance_budget.level,distance_budget.base*(1.f-.18f*distance_budget.level),distance_budget.work);n3ds_log(line);}
  }
 } else {
  distance_budget.over=0;
  if(distance_budget.work<29.f)distance_budget.quiet+=dt;else distance_budget.quiet=0;
  if(distance_budget.quiet>6000 && distance_budget.level) {
   --distance_budget.level;distance_budget.quiet=0;distance_budget.hold=20000;
  }
 }
}

/* Kevin Boggs: n3ds_engine_fog_prepare */
void n3ds_engine_fog_prepare(struct render_camera *camera,struct render_camera *raster,
 const struct render_fog *authored,int outdoor)
{
 unsigned int generation=n3ds_engine_cache_generation();
 if(generation!=distance_budget.generation) {
  memset(&distance_budget,0,sizeof(distance_budget));distance_budget.generation=generation;
 }
 const char *map=main_get_map_name();int original=n3ds_input_platform_original_model();
 int cinematic=cinematic_in_progress();
 int demanding=map && (strstr(map,"\\a30\\") || strstr(map,"\\b30\\") || strstr(map,"\\c10\\"));
 /* Indoor authored lighting, menus, and scoped aiming retain their distance.
  * Cinematics get a longer fixed range, without feedback responding to cuts. */
 int active=outdoor && !main_menu_is_active() && camera->vertical_field_of_view>=.5f && (!cinematic || demanding);
 if(!active || cinematic) {
  if(distance_budget.trial){--distance_budget.level;distance_budget.trial=0;}
  /* Finish a pending range recovery while this view does not use it. This
   * prevents resuming that movement into a resolution trial after zooming. */
  distance_budget.distance=distance_budget.base*(1.f-.18f*distance_budget.level);
  distance_budget.hold=fmaxf(distance_budget.hold,4000);
  distance_budget.over=distance_budget.quiet=0;
 }
 if(!active) {distance_budget.active=0;n3ds_gpu_fog_config(0,0,0);return;}
 float base=original?(demanding?60.f:80.f):(demanding?110.f:160.f);
 if(!distance_budget.base || base!=distance_budget.base) {
  distance_budget.base=base;distance_budget.distance=base;distance_budget.level=distance_budget.trial=0;
  distance_budget.hold=4000;distance_budget.over=distance_budget.quiet=0;
  {char line[128];snprintf(line,sizeof(line),"FOG BUDGET: %s outdoor range=%.1f; rendering only",original?"Original":"New",base);n3ds_log(line);}
 }
 distance_budget.active=!cinematic;
 float end=cinematic?fmaxf(base,original?100.f:150.f):distance_budget.distance;
 end=floorf(end*2.f)*.5f;
 if(authored->atmospheric_maximum_density>=1.f && authored->atmospheric_maximum_distance>1.f)
  end=fminf(end,authored->atmospheric_maximum_distance);
 end=fminf(end,fminf(camera->z_far,raster->z_far));
 if(end<=camera->z_near+1.f){n3ds_gpu_fog_config(0,0,0);return;}
 unsigned int rgb=0;float sum=0;
 for(int c=0;c<3;++c)sum+=authored->atmospheric_color.n[c];
 for(int c=0;c<3;++c) {
  int authored_color=sum>.02f || authored->atmospheric_maximum_distance>0 || authored->atmospheric_maximum_density>0;
  float value=authored_color?authored->atmospheric_color.n[c]:(c==0?.38f:c==1?.44f:.52f);
  rgb|=(unsigned int)(fmaxf(0,fminf(1,value))*255.f+.5f)<<(8*c);
 }
 n3ds_gpu_fog_config(end*.55f,end,rgb);
 camera->z_far=raster->z_far=end;
}
