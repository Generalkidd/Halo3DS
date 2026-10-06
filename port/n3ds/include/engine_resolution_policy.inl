/* Time-based feedback, excluding the intentional 30 Hz pacing sleep.
 * A resolution change is a measured trial, not a promise that fill cost is
 * the bottleneck. Original models also reduce projected model detail with
 * resolution, so measured CPU rendering cost can justify a bounded trial. */
struct resolution_policy {
 int level,ready,trial,prior_level;
 float work,gpu,wait,over,headroom,cooldown,trial_time,prior_work,prior_gpu;
 float recovery,up_guard;
 float prepare,prior_prepare,prior_wait;
 unsigned int recovery_failures,drop_failures;
};
static const float resolution_pixels[]={96000.f,61440.f,38912.f,24000.f};
static void resolution_recovery_failed(struct resolution_policy *p)
{
 /* Remember rejected recovery rather than probing the same expensive view
  * every few seconds. A later quiet view still gets another bounded chance. */
 if(p->recovery_failures<2)++p->recovery_failures;
 p->recovery=6000.f*(1u<<p->recovery_failures);
 p->up_guard=0;
}
static void resolution_trial(struct resolution_policy *p,int level)
{
 p->prior_level=p->level;p->prior_work=p->work;p->prior_gpu=p->gpu;
 p->prior_prepare=p->prepare;p->prior_wait=p->wait;
 p->trial=level>p->level?1:-1;p->level=level;p->trial_time=0;
 p->over=p->headroom=0;
}
static int resolution_policy_step_profile(struct resolution_policy *p,float work,float gpu,float wait,float prepare,int original)
{
 const float limit=1000.f/28.f;
 const int minimum=original?1:0;
 if(p->level<minimum)p->level=minimum;
 if(!isfinite(work) || !isfinite(gpu) || !isfinite(wait) || !isfinite(prepare) || work<0 || gpu<0 || wait<0 || prepare<0)return p->level;
 /* Loading, checkpoints and debugger stops do not describe steady rendering. */
 if(work>250 || gpu>250){
  if(p->trial){
   if(p->trial<0)resolution_recovery_failed(p);
   p->level=p->prior_level;p->trial=0;p->cooldown=1000.f;
  }
  p->recovery=fmaxf(p->recovery,4000.f);
  p->ready=0;p->over=p->headroom=0;return p->level;
 }
 float elapsed=fmaxf(1000.f/30.f,work);
 if(!p->ready){p->work=work;p->gpu=gpu;p->wait=wait;p->prepare=prepare;p->ready=1;}
 else {float a=fminf(.4f,elapsed/220.f);p->work+=(work-p->work)*a;p->gpu+=(gpu-p->gpu)*a;p->wait+=(wait-p->wait)*a;p->prepare+=(prepare-p->prepare)*a;}
 if(p->cooldown>0)p->cooldown=fmaxf(0,p->cooldown-elapsed);
 if(p->recovery>0)p->recovery=fmaxf(0,p->recovery-elapsed);
 if(p->up_guard>0){p->up_guard=fmaxf(0,p->up_guard-elapsed);if(!p->up_guard)p->recovery_failures=0;}
 if(p->trial){
  p->trial_time+=elapsed;
  if(p->trial_time<650)return p->level;
  int accepted;
  if(p->trial>0)accepted=((!original || p->prior_wait>=2.f) && p->prior_gpu-p->gpu>=fmaxf(1.f,p->prior_gpu*.05f)) ||
      (p->prior_work-p->work>=fmaxf(1.f,p->prior_work*.03f)) ||
      (original && p->prior_prepare-p->prepare>=fmaxf(1.f,p->prior_prepare*.05f));
  else accepted=p->work<=limit ||
      (!original && p->wait<1.5f && p->gpu<(p->work-p->wait)*.8f);
  if(!accepted)p->level=p->prior_level;
  if(p->trial>0 && accepted){
   if(p->up_guard>0)resolution_recovery_failed(p);
   p->recovery=fmaxf(p->recovery,4000.f);
  }else if(p->trial<0){
   if(!accepted)resolution_recovery_failed(p);
   else {p->recovery=4000.f;p->up_guard=6000.f;}
  }
  p->cooldown=accepted?(p->trial>0?350.f:750.f):3000.f;
  if(original && p->trial>0){
   if(accepted)p->drop_failures=0;
   else {
    /* Some authored LODs share identical geometry. Avoid repeating an
     * ineffective CPU-render trial every three seconds in the same view. */
    if(p->drop_failures<2)++p->drop_failures;
    p->cooldown=6000.f*(1u<<p->drop_failures);
   }
  }
  p->trial=0;p->over=p->headroom=0;return p->level;
 }
 /* Waiting on GPU completion is direct evidence that reducing graphics work
  * can help. Queue duration alone can overlap useful CPU simulation. */
 float busy=fmaxf(0,p->work-p->wait);
 int gpu_limited=p->wait>=2.f && p->gpu>=8.f;
 /* Lower pixel count also selects cheaper authored model LODs. Try this on
  * slow Original CPUs only when rendering is a substantial part of the cost;
  * an AI/checkpoint bottleneck alone must not drive the world to minimum. */
 int cpu_render_limited=original && p->prepare>=12.f && p->prepare>=p->work*.35f;
 if(p->work>limit && (gpu_limited || cpu_render_limited)){
  p->headroom=0;p->over+=elapsed;
  float duration=p->work>50.f?200.f:750.f;
  if(!p->cooldown && p->level<3 && p->over>=duration)resolution_trial(p,p->level+1);
 }else{
  p->over=0;
  if(p->level>minimum && !p->cooldown && !p->recovery){
   float ratio=resolution_pixels[p->level-1]/resolution_pixels[p->level];
   float predicted=p->work+p->gpu*(ratio-1.f);
   if(original)predicted+=p->prepare*(sqrtf(ratio)-1.f);
   int spare=(p->work<limit-3.f && predicted<=limit-3.f) ||
       (!original && p->wait<1.f && p->gpu*ratio<busy*.65f);
   if(spare){p->headroom+=elapsed;if(p->headroom>=2000.f)resolution_trial(p,p->level-1);}
   else p->headroom=0;
  }else p->headroom=0;
 }
 return p->level;
}
/* Preserve the New-model policy and its existing regression vectors. */
static int resolution_policy_step(struct resolution_policy *p,float work,float gpu,float wait)
{return resolution_policy_step_profile(p,work,gpu,wait,0,0);}
