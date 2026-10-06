/* Included in rasterizer_lights.c after its original tag/packet definitions.
 * Core coronas only: depth-tested textured quads, no occlusion readback,
 * screen-space ghosts, bloom target, or additional scene render pass. */
#include "engine_widgets.h"
#include "engine_renderer.h"
#include "engine_input.h"
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
void n3ds_log(const char *);
boolean rasterizer_set_texture_non_blocking(short,short,short,long,short);
static void n3ds_lens_flares_draw(void)
{
    static unsigned int views,submitted,omitted;
    static long long cost;
    long long started=n3ds_engine_ticks();
    int original=n3ds_input_platform_original_model();
    unsigned int budget=original?16:32,drawn=0;
    float area_left=original?24000.f:48000.f;
    real_vector3d right,up;
    const struct render_camera *camera=&render.camera;
    cross_product3d(&camera->forward,&camera->up,&right);
    if(normalize3d(&right)<=0) return;
    cross_product3d(&right,&camera->forward,&up);normalize3d(&up);
    float focal=120.f/tanf(camera->vertical_field_of_view*.5f);
    n3ds_gpu_world_state_restore();
    /* Keep weapon feedback ahead of decorative map coronas. */
    for(int pass=0;pass<2;++pass) for(int i=0;i<local_lens_flare_count;++i) {
        struct rasterizer_lens_flare_submit_parameters *p=lens_flare_parameters_get(i);
        int weapon=(p->compressed_window_index&_lens_flare_first_person_weapon_flag)!=0;
        if(weapon!=(pass==0)) continue;
        struct lens_flare_definition *d=p->definition;
        if(!d || d->primary_map.index==NONE || !d->reflections.count || !d->reflections.address) continue;
        struct lens_flare_reflection *r=d->reflections.address;
        real_vector3d offset;vector_from_points3d(&camera->position,&p->position,&offset);
        float depth=dot_product3d(&camera->forward,&offset);
        if(!isfinite(depth) || depth<=.02f) continue;
        float energy=(p->compressed_light_color>>24)/255.f;
        if(d->far_fade_distance>0) {
            float range=d->far_fade_distance-d->near_fade_distance;
            energy*=range>0?PIN((d->far_fade_distance-depth)/range,0.f,1.f):(depth<d->far_fade_distance);
        }
        float functions[4]={1,1,1,1};
        real_vector3d direction=uncompress_int32_to_real_vector3d(p->compressed_direction);
        real_vector3d unit=offset;normalize3d(&unit);
        float range=d->runtime_cosine_falloff_angle-d->runtime_cosine_cutoff_angle;
        if(fabsf(range)>1e-6f) {
            functions[1]=PIN((-dot_product3d(&camera->forward,&direction)-d->runtime_cosine_cutoff_angle)/range,0.f,1.f);
            functions[2]=PIN((-dot_product3d(&direction,&unit)-d->runtime_cosine_cutoff_angle)/range,0.f,1.f);
            functions[3]=PIN((dot_product3d(&camera->forward,&unit)-d->runtime_cosine_cutoff_angle)/range,0.f,1.f);
        }
        if(r->brightness_scale_function<0 || r->brightness_scale_function>3 ||
           r->radius_scale_function<0 || r->radius_scale_function>3) continue;
        float scale=p->compressed_light_scale/255.f;
        /* Xbox's core-reflection path interpolates the radius directly. */
        float radius=r->radius_lower_bounds+(r->radius_upper_bounds-r->radius_lower_bounds)*scale;
        energy*= (r->brightness_lower_bounds+(r->brightness_upper_bounds-r->brightness_lower_bounds)*scale)*functions[r->brightness_scale_function];
        if(r->flags&FLAG(_lens_flare_reflection_radius_not_scaled_by_distance_bit)) radius*=depth;
        float sx=d->corona_radius_scale.i,sy=d->corona_radius_scale.j;
        if(!isfinite(radius) || radius<=0 || !isfinite(energy) || energy<=0 || !isfinite(sx) || !isfinite(sy) || sx<=0 || sy<=0) continue;
        float px=focal*radius*sx/depth,py=focal*radius*sy/depth;
        float cx=dot_product3d(&right,&offset)*focal/depth,cy=dot_product3d(&up,&offset)*focal/depth;
        if(fabsf(cx)>200+px || fabsf(cy)>120+py) continue;
        float area=4*px*py;
        if(!isfinite(area) || area<=0 || !isfinite(cx) || !isfinite(cy)) continue;
        if(drawn>=budget || area_left<16 || n3ds_gpu_command_usage()>.80f) {++omitted;continue;}
        /* Cap huge decorative halos without hiding their bright core. */
        float max_pixels=weapon?48.f:32.f;
        float factor=fminf(1.f,max_pixels/fmaxf(px,py));
        if(area*factor*factor>area_left) factor=fminf(factor,sqrtf(area_left/area));
        radius*=factor;area_left-=area*factor*factor;
        if(rasterizer_set_texture_non_blocking(0,0,1,d->primary_map.index,r->bitmap_index)) continue;
        float rgb[3]={((p->compressed_light_color>>16)&255)/255.f,((p->compressed_light_color>>8)&255)/255.f,(p->compressed_light_color&255)/255.f};
        float tint=1;
        if(r->tint_color.alpha || r->tint_color.red || r->tint_color.green || r->tint_color.blue) {
            rgb[0]=r->tint_color.red;rgb[1]=r->tint_color.green;rgb[2]=r->tint_color.blue;tint=r->tint_color.alpha;
            if(r->animation_function>_periodic_function_zero && r->animation_function<12 &&
               isfinite(r->animation_period) && r->animation_period!=0 && isfinite(r->animation_phase)) {
                real_argb_color animated;
                float phase=periodic_function_evaluate(r->animation_function,
                    (global_frame_parameters.game_time_sec+r->animation_phase)/r->animation_period);
                rgb_colors_interpolate(&animated.rgb,r->animation_flags&3,
                    &r->animation_color_lower_bound.rgb,&r->animation_color_upper_bound.rgb,phase);
                animated.alpha=r->animation_color_lower_bound.alpha*(1-phase)+r->animation_color_upper_bound.alpha*phase;
                for(int k=0;k<3;++k) rgb[k]*=animated.rgb.n[k];
                energy*=animated.alpha;
            }
        }
        real_point3d center=p->position;
        /* Slightly lift a surface-mounted corona toward the viewer. */
        float lift=fminf(fmaxf(0,d->occlusion_radius),depth*.05f);
        for(int k=0;k<3;++k) center.n[k]-=unit.n[k]*lift;
        struct native_widget_vertex v[4];
        static const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
        for(int j=0;j<4;++j) {
            for(int k=0;k<3;++k) {v[j].position[k]=center.n[k]+right.n[k]*(uv[j][0]*2-1)*radius*sx+up.n[k]*(1-uv[j][1]*2)*radius*sy;v[j].color[k]=PIN(rgb[k],0.f,1.f);}
            v[j].color[3]=PIN(energy,0.f,1.f);v[j].uv[0]=uv[j][0];v[j].uv[1]=uv[j][1];
        }
        if(n3ds_gpu_widget_draw(v,1,400,240,tint,1)) {
            ++drawn;++submitted;
#ifdef HALO_N3DS_RENDERER_TESTS
            static unsigned long observed[64];static unsigned int observed_count;
            unsigned long key=(unsigned long)d->primary_map.index ^ (weapon?0x80000000u:0);
            unsigned int n;for(n=0;n<observed_count && observed[n]!=key;++n) {}
            if(n==observed_count && observed_count<64) {
                char m[180];observed[observed_count++]=key;
                snprintf(m,sizeof(m),"CORONA TEST: bitmap=%08lx first_person=%d energy=%.3f radius=%.3f",d->primary_map.index,weapon,energy,radius);n3ds_log(m);
            }
#endif
        }
    }
    n3ds_gpu_world_state_restore();
    cost+=n3ds_engine_ticks()-started;
    if(++views%120==0) {
        char message[180];snprintf(message,sizeof(message),"NATIVE CORONAS: views=120 drawn=%u omitted=%u cpu_ms=%.3f model=%s",submitted,omitted,(double)cost*1000/(120*n3ds_engine_tick_frequency()),original?"original":"New");n3ds_log(message);
        submitted=omitted=0;cost=0;
    }
}
