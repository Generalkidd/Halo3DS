#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_text.h"
#include "engine_widgets.h"
#include "engine_scene_shader.h"
#include "engine_rigid_shader.h"
#include "engine_skin_shader.h"
#include "engine_skin_depth_shader.h"
#include "engine_rigid_depth_shader.h"
#include "engine_environment_shader.h"
#include "engine_fragment_shader.h"
#include "engine_rigid_lit_shader.h"
#include "engine_skin_lit_shader.h"
#include "engine_rigid_reflect_shader.h"
#include "engine_skin_reflect_shader.h"
#include "engine_water_shader.h"
#include "engine_glass_shader.h"
#include "engine_chicago.h"
#include "engine_glass.h"
#include "engine_controls.h"
void n3ds_log(const char *message);

/* Temporary hardware diagnosis: retain the normal rendering path and flush
 * only its first three loading submissions to locate an early GPU stall. */
#ifdef HALO_N3DS_BOOT_TRACE
static unsigned int boot_trace_pulses;
static int boot_trace_active;
#define BOOT_TRACE(message) do { if(boot_trace_active) n3ds_log("STARTUP: TRACE " message); } while(0)
#else
#define BOOT_TRACE(message) ((void)0)
#endif

/* Citro3D binds an environment pointer but does not dirty its hardware
 * registers on an ordinary switch. Track the last non-null owner, retaining
 * it through unlit draws. LUT RAM has independent owners: switching through
 * an environment without that table cannot clobber it. Caller setters still
 * mark mutable tables dirty. Reset each frame to cover suspend/restore and
 * transient test environments without relying on stale pointer identities. */
static C3D_LightEnv *native_lighting_owner;
static C3D_LightEnv *native_lut_owner[6],*native_sp_owner[8],*native_da_owner[8];
static void native_light_frame_begin(void)
{
    native_lighting_owner=NULL;
    memset(native_lut_owner,0,sizeof(native_lut_owner));
    memset(native_sp_owner,0,sizeof(native_sp_owner));
    memset(native_da_owner,0,sizeof(native_da_owner));
}
static void native_light_env_bind(C3D_LightEnv *env)
{
    if(env && env!=native_lighting_owner) {
        env->flags|=C3DF_LightEnv_Dirty;
        for(int i=0;i<6;++i) if(env->luts[i] && native_lut_owner[i]!=env) {
            env->flags|=C3DF_LightEnv_LutDirty(i);native_lut_owner[i]=env;
        }
        for(int i=0;i<8;++i) if(env->lights[i]) {
            C3D_Light *light=env->lights[i];light->flags|=C3DF_Light_Dirty;
            if(light->lut_SP && native_sp_owner[i]!=env) {light->flags|=C3DF_Light_SPDirty;native_sp_owner[i]=env;}
            if(light->lut_DA && native_da_owner[i]!=env) {light->flags|=C3DF_Light_DADirty;native_da_owner[i]=env;}
        }
        native_lighting_owner=env;
    }
    C3D_LightEnvBind(env);
}

#define INDEX_BYTES (2u*1024u*1024u)
#define MODEL_VERTEX_COUNT 196608u
static C3D_RenderTarget *target,*bottom_target,*right_target;
static int bottom_selected;
static void *loading_font,*loading_art;
static void controls_dispose(void);
static int loading_art_tried;
static int loading_enabled=1;
static unsigned int loading_submissions;
static int eye_right,overlay_stereo;
static float stereo_level,eye_offset;
static int diagnostic_mode;
#include "engine_world_scale.inl"
void n3ds_gpu_stereo_level(float level) {stereo_level=isfinite(level)?fminf(1,fmaxf(0,level)):0;}
void n3ds_gpu_stereo_eye(int right,float offset) {eye_right=!!right;eye_offset=offset;}
void n3ds_gpu_stereo_overlay(int enabled) {overlay_stereo=!!enabled;}
int n3ds_gpu_overlay_other_begin(void)
{
    if(!overlay_stereo || !stereo_level || bottom_selected) return 0;
    C3D_FrameDrawOn(right_target);return 1;
}
void n3ds_gpu_overlay_other_end(void) {world_target_bind();}
int n3ds_gpu_bottom_owned(void) { return bottom_target!=NULL; }
void n3ds_gpu_hud_screen(int bottom)
{
    bottom_selected=!!bottom;
    world_target_bind();
    C3D_SetScissor(diagnostic_mode==1 && !bottom_selected?GPU_SCISSOR_NORMAL:GPU_SCISSOR_DISABLE,0,0,120,200);
}
void n3ds_gpu_hud_screen_restore(void)
{ world_target_bind(); }
static DVLB_s *shader;
static shaderProgram_s program;
static int program_ready, matrix_location, frame_active;
static const void *bound_vertex_data;
static unsigned int bound_vertex_stride,bound_vertex_attributes,bound_vertex_permutation;
static int bound_vertex_valid;
static unsigned long long vertex_buffer_requests,vertex_buffer_reuses;
int n3ds_gpu_vertex_buffer_bind(const void *data,unsigned int stride,unsigned int attributes,unsigned int permutation)
{
    if(!data) return 0;
    ++vertex_buffer_requests;
    if(frame_active && bound_vertex_valid && data==bound_vertex_data && stride==bound_vertex_stride &&
       attributes==bound_vertex_attributes && permutation==bound_vertex_permutation) {++vertex_buffer_reuses;return 1;}
    bound_vertex_valid=0;
    C3D_BufInfo *buffers=C3D_GetBufInfo();if(!buffers) return 0;
    BufInfo_Init(buffers);
    if(BufInfo_Add(buffers,data,stride,attributes,permutation)<0) return 0;
    if(frame_active) {
        bound_vertex_data=data;bound_vertex_stride=stride;
        bound_vertex_attributes=attributes;bound_vertex_permutation=permutation;bound_vertex_valid=1;
    }
    return 1;
}
static DVLB_s *rigid_shader;
static shaderProgram_s rigid_program;
static int rigid_ready,rigid_projection,rigid_bone,rigid_options;
static DVLB_s *skin_shader;
static shaderProgram_s skin_program;
static int skin_ready,skin_projection,skin_bone,skin_options;
static struct {DVLB_s *binary;shaderProgram_s program;int ready,projection,bone;} depth_programs[2];
static DVLB_s *environment_shader;
static shaderProgram_s environment_program;
static int environment_ready,environment_projection,environment_options,environment_state_valid,environment_key;
static float environment_scale[4];
static unsigned int emission_colors[4];
static unsigned int environment_material;
/* Both environment variants use one program; material changes only update
 * attributes, lighting and combiners, not vertex microcode/projection. */
static int environment_program_valid,bump_state_valid,bump_alpha,plasma_state_valid;
/* Native alpha-dependent environment illumination. TEX2 is immutable ARGB:
 * filtered authored alpha feeds signed LN, while G/B/A retain all RGB masks.
 * Eight bounded phase tables are shared by materials, BSP draws and both eyes.
 * Citro3D copies a table into commands at draw submission, before reuse. */
enum { PLASMA_LUT_SLOTS=8 };
static C3D_LightEnv plasma_env;
static C3D_Light plasma_light;
static C3D_LightLut plasma_luts[PLASMA_LUT_SLOTS];
static unsigned int plasma_phase_key[PLASMA_LUT_SLOTS],plasma_next_lut;
static int plasma_ready,plasma_bound_lut=-1,plasma_alpha,plasma_lightmap;
static unsigned int plasma_colors[5];
static float plasma_uv[4];
static const struct native_render_vertex *plasma_vertices;
static const float *plasma_light_uv;
static unsigned int plasma_draws,plasma_lut_builds,plasma_lut_hits;

static float bump_scale[2];static unsigned int bump_material;
static C3D_LightEnv bump_light_env;static C3D_Light bump_light;
static C3D_AttrInfo environment_attributes[2];
/* Immutable recipes: eight base/detail/lightmap variants and bump.
 * Alpha threshold and material color remain independent dynamic state. */
enum { ENVIRONMENT_BASE_RECIPES=8, ENVIRONMENT_BUMP_RECIPE=ENVIRONMENT_BASE_RECIPES, ENVIRONMENT_EMISSION_RECIPE=ENVIRONMENT_BASE_RECIPES+1 };
static C3D_TexEnv environment_recipes[ENVIRONMENT_BASE_RECIPES+2][6];
static void environment_recipes_initialize(void)
{
    for(int variant=0;variant<2;++variant) {
        C3D_AttrInfo *a=&environment_attributes[variant];AttrInfo_Init(a);
        AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);
        AttrInfo_AddLoader(a,2,GPU_FLOAT,2);AttrInfo_AddLoader(a,3,GPU_FLOAT,2);
        if(variant) {AttrInfo_AddLoader(a,4,GPU_FLOAT,4);AttrInfo_AddLoader(a,5,GPU_FLOAT,1);}
        else {AttrInfo_AddFixed(a,4);AttrInfo_AddFixed(a,5);}
    }
    for(int key=0;key<ENVIRONMENT_BASE_RECIPES;++key) {
        int lightmap=key&1,detail=key>>1;
        for(int i=0;i<6;++i) C3D_TexEnvInit(&environment_recipes[key][i]);
        C3D_TexEnv *env=&environment_recipes[key][0];
        /* Match Xbox pass ordering: saturate the albedo/detail combination
         * before multiplying it by the baked lighting, not afterwards. */
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
        if(detail==3) {
            /* Xbox EXPAND_NORMAL: clamp(base + 2*detail - 1), then light.
             * PICA ADD_SIGNED computes (A+B-.5); halve base first and use
             * its output scale2. The two intermediate 8-bit roundings can
             * differ by a few channel units, covered by readback tests. */
            C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);C3D_TexEnvColor(env,0x80808080u);
            env=&environment_recipes[key][1];
            C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE2,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_ADD_SIGNED);C3D_TexEnvScale(env,C3D_RGB,GPU_TEVSCALE_2);
            env=&environment_recipes[key][2];
            C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,lightmap?GPU_TEXTURE1:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
        } else {
            C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,detail?GPU_TEXTURE2:(lightmap?GPU_TEXTURE1:GPU_PRIMARY_COLOR),GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
            if(detail==2) C3D_TexEnvScale(env,C3D_RGB,GPU_TEVSCALE_2);
            if(detail) {
                env=&environment_recipes[key][1];C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,lightmap?GPU_TEXTURE1:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
            }
        }
    }
        for(int i=0;i<6;++i) C3D_TexEnvInit(&environment_recipes[ENVIRONMENT_BUMP_RECIPE][i]);
        C3D_TexEnv *e=&environment_recipes[ENVIRONMENT_BUMP_RECIPE][0];
        /* Xbox program16: weight=clamp(length(incident)^2), then
         * I=(1-weight)+weight*max(dot(normalMap,incidentDirection),0).
         * Its normalization cube is replaced by native fragment lighting. */
        C3D_TexEnvSrc(e,C3D_RGB,GPU_FRAGMENT_PRIMARY_COLOR,GPU_CONSTANT,GPU_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);C3D_TexEnvColor(e,0xffffffffu);
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        e=&environment_recipes[ENVIRONMENT_BUMP_RECIPE][1];C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        e=&environment_recipes[ENVIRONMENT_BUMP_RECIPE][2];C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        e=&environment_recipes[ENVIRONMENT_BUMP_RECIPE][3];C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        for(int i=0;i<6;++i) C3D_TexEnvInit(&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][i]);
        for(int i=0;i<3;++i) {
            C3D_TexEnv *e=&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][i];
            C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_PREVIOUS);
            C3D_TexEnvOpRgb(e,i==0?GPU_TEVOP_RGB_SRC_R:i==1?GPU_TEVOP_RGB_SRC_G:GPU_TEVOP_RGB_SRC_B,
                           GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvFunc(e,C3D_RGB,i?GPU_MULTIPLY_ADD:GPU_MODULATE);
        }
        e=&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][0];
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        e=&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][3];
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        e=&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][4];
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD);
        e=&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][5];
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);

}
static struct {DVLB_s *binary; shaderProgram_s program; int ready,projection,bone,options,camera,delta,parallel;} lit_programs[4];
static struct {DVLB_s *binary; shaderProgram_s program; int ready,projection,camera,fallback,delta,parallel,wave_phase,wave_switch;} water_program;
static struct {DVLB_s *binary; shaderProgram_s program; int ready,projection,camera,fallback,delta,parallel,options,pose;} glass_program;
static C3D_LightEnv model_light_env;
static C3D_Light model_lights[3];
static float cached_translucency;
static struct native_model_lighting cached_lighting;
static int model_light_ready,model_light_valid;
static unsigned long long model_lit_draws,model_light_updates,model_reflection_draws,model_detail_draws;
static void model_lighting_apply(const struct native_model_lighting *lighting,float translucency)
{
    int changed=!model_light_valid || memcmp(&cached_lighting,lighting,sizeof(*lighting));
    if(changed || cached_translucency!=translucency) {
      /* Translucency changes a material's back light, not the scene lights. */
      if(changed) {
        C3D_LightEnvAmbient(&model_light_env,lighting->ambient[0],lighting->ambient[1],lighting->ambient[2]);
        for(unsigned int i=0;i<2;++i) {
            const float *c=lighting->distant[i].color,*d=lighting->distant[i].direction;
            int enabled=i<lighting->count && (c[0]>0 || c[1]>0 || c[2]>0) &&
                (d[0]!=0 || d[1]!=0 || d[2]!=0);
            /* Keep a zero-intensity light for the ambient-only case: PICA's
             * light count register represents one through eight lights. */
            C3D_LightEnable(&model_lights[i],enabled || i==0);
            C3D_LightDiffuse(&model_lights[i],enabled?c[0]:0,enabled?c[1]:0,enabled?c[2]:0);
            C3D_FVec direction=FVec4_New(enabled?d[0]:0,enabled?d[1]:0,enabled?d[2]:1,0);
            C3D_LightPosition(&model_lights[i],&direction);
        }
        cached_lighting=*lighting;
      }
        /* Xbox applies translucency only to the first distant light:
         * max(N.L, -translucency*N.L, 0). An opposite PICA light reproduces
         * this without a geometry pass; keep it disabled on opaque materials. */
        int back=lighting->count && translucency>0 &&
            (lighting->distant[0].color[0]>0 || lighting->distant[0].color[1]>0 || lighting->distant[0].color[2]>0) &&
            (lighting->distant[0].direction[0]!=0 || lighting->distant[0].direction[1]!=0 || lighting->distant[0].direction[2]!=0);
        C3D_LightEnable(&model_lights[2],back);
        if(back) {
            const float *c=lighting->distant[0].color,*d=lighting->distant[0].direction;
            C3D_LightDiffuse(&model_lights[2],c[0]*translucency,c[1]*translucency,c[2]*translucency);
            C3D_FVec direction=FVec4_New(-d[0],-d[1],-d[2],0);C3D_LightPosition(&model_lights[2],&direction);
        }
        cached_translucency=translucency;model_light_valid=1;++model_light_updates;
    }
    native_light_env_bind(&model_light_env);
}
static int model_batch_active,model_state_valid,model_state_key;
static unsigned int model_cached_nodes;
static float model_cached_rows[NATIVE_GPU_MODEL_BONES*12],model_cached_options[4];
static unsigned long long model_state_reuses,model_palette_reuses,model_palette_uploads;
static unsigned long long model_material_draws,model_mask_draws,model_material_updates;
static unsigned long long model_single_sided_draws,model_unculled_draws;
static struct native_model_material model_cached_material;
static const struct native_model_material model_plain_material={NULL,0xff000000,0xffffffff,0};
static void model_reflection_material_apply(const struct native_model_material *m,int decal)
{
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufUpdate(C3D_RGB,1|8);C3D_TexEnvBufColor(0);
    C3D_TexEnv *e=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_FRAGMENT_PRIMARY_COLOR);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvColor(e,m->emission);
    C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
    e=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,GPU_CONSTANT,GPU_TEXTURE2);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_B);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);C3D_TexEnvColor(e,m->tint|0xff000000u);
    e=C3D_GetTexEnv(2);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
    e=C3D_GetTexEnv(3);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
    e=C3D_GetTexEnv(4);C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE2,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_MODULATE);
    e=C3D_GetTexEnv(5);C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(e,0xffffffffu);
    C3D_TexEnvSrc(e,C3D_Alpha,(decal || m->alpha_test)?GPU_TEXTURE1:GPU_CONSTANT,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
    C3D_AlphaTest(!decal && m->alpha_test,GPU_GREATER,127);
}
static void model_detail_material_apply(const struct native_model_material *m,int decal)
{
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    C3D_TexEnv *e=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(e,C3D_Alpha,(decal || m->alpha_test)?GPU_TEXTURE1:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
    if(!m->multipurpose) {
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,m->detail_after?GPU_FRAGMENT_PRIMARY_COLOR:GPU_TEXTURE0,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        if(!m->detail_after && !m->detail_function) C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
        e=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,m->detail_after?GPU_TEXTURE0:GPU_FRAGMENT_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        if(m->detail_after && !m->detail_function) C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
    } else {
        /* Preserve Xbox saturation/order. Mask R/G/B/A selects reflection,
         * emission, change-color or auxiliary detail weight (or its inverse).
         * Buffer lit*tint, then modulate it with the saturated detailed base;
         * detail-after instead buffers lit*tint*base before applying detail. */
        C3D_TexEnvBufUpdate(C3D_RGB,1|(m->detail_after?8:4));
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_FRAGMENT_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvColor(e,m->emission);
        e=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,GPU_CONSTANT,GPU_TEXTURE2);
        C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_B);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);C3D_TexEnvColor(e,m->tint|0xff000000u);
        e=C3D_GetTexEnv(2);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        e=C3D_GetTexEnv(m->detail_after?4:3);
        if(m->detail_mask) {
            const GPU_TEVOP_RGB channel[4]={GPU_TEVOP_RGB_SRC_R,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_B,GPU_TEVOP_RGB_SRC_ALPHA};
            const GPU_TEVOP_RGB inverse[4]={GPU_TEVOP_RGB_ONE_MINUS_SRC_R,GPU_TEVOP_RGB_ONE_MINUS_SRC_G,GPU_TEVOP_RGB_ONE_MINUS_SRC_B,GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA};
            int c=(m->detail_mask-1)/2;
            C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_TEXTURE2);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,(m->detail_mask&1)?inverse[c]:channel[c]);
            C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);C3D_TexEnvColor(e,m->detail_function?0xffffffffu:0x80808080u);
        } else {
            C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
        }
        e=C3D_GetTexEnv(m->detail_after?3:4);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        if(!m->detail_after && !m->detail_function) C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
        e=C3D_GetTexEnv(5);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        if(m->detail_after && !m->detail_function) C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
    }
    C3D_AlphaTest(!decal && m->alpha_test,GPU_GREATER,127);
}
static void model_material_apply(const struct native_model_material *m,int decal,int lit)
{
    ++model_material_updates;
    if(lit && m->reflection && m->multipurpose) {
        model_reflection_material_apply(m,decal);return;
    }
    if(lit && m->detail) {model_detail_material_apply(m,decal);return;}
    GPU_TEVSRC base=lit?GPU_TEXTURE1:GPU_TEXTURE0,mask=lit?GPU_TEXTURE2:GPU_TEXTURE1;
    GPU_TEVSRC diffuse=lit?GPU_FRAGMENT_PRIMARY_COLOR:GPU_PRIMARY_COLOR;
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Alpha,(decal || m->alpha_test)?base:GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    if(!m->multipurpose) {
        C3D_TexEnvSrc(env,C3D_RGB,base,diffuse,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    } else {
        /* Xbox stage1: saturate(vertex light + mask.G * emission).
         * Buffer that result, then form (1-mask.B)+mask.B*tint.
         * Mask.R is consumed by the separate one-pass reflection variant. */
        C3D_TexEnvSrc(env,C3D_RGB,mask,GPU_CONSTANT,diffuse);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvColor(env,m->emission);
        C3D_TexEnvBufUpdate(C3D_RGB,1);
        env=C3D_GetTexEnv(1);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT,GPU_CONSTANT,mask);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_B);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);C3D_TexEnvColor(env,m->tint|0xff000000u);
        env=C3D_GetTexEnv(2);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
        env=C3D_GetTexEnv(3);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,base,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    }
    C3D_AlphaTest(!decal && m->alpha_test,GPU_GREATER,127);
}
void n3ds_gpu_model_batch_begin(void)
{
    if(model_batch_active) svcBreak(USERBREAK_PANIC);
    model_batch_active=1;model_state_valid=0;
}
void n3ds_gpu_model_batch_flush(void)
{
    if(model_state_valid) n3ds_gpu_world_state_restore();
}
void n3ds_gpu_model_batch_end(void)
{
    n3ds_gpu_model_batch_flush();model_batch_active=0;
}
static unsigned char *index_stream;
static unsigned int index_used;
static unsigned int index_draws,index_flushes;
static unsigned int model_deferred,model_flushes;
/* Queued draws retain their vertex pointers until FrameBegin waits for the
 * GPU. Nonmoving blocks grow on demand instead of reserving 6 MiB at boot. */
enum { MODEL_BLOCKS=48, MODEL_BLOCK_VERTICES=4096 };
static struct { struct native_render_vertex *data; unsigned int capacity,used; } model_blocks[MODEL_BLOCKS];
static unsigned int model_reserved;
static unsigned int model_used;
static unsigned int model_peak;
static unsigned long long model_frame_serial;
unsigned long long n3ds_gpu_model_frame_serial(void) { return model_frame_serial; }
/* Immutable decoded model streams may stay GPU-resident across frames. Their
 * slot revision changes on every decode/reuse, never on pose or camera changes.
 * Share the existing6MiB arena allowance rather than adding another budget. */
enum { MODEL_RESIDENT_SLOTS=256, MODEL_RESIDENT_LIMIT=MODEL_VERTEX_COUNT/2 };
static struct {
    void *data;
    unsigned int units,bytes;
    unsigned long long revision,last_frame;
} model_resident[MODEL_RESIDENT_SLOTS];
static unsigned int model_resident_units,model_resident_peak;
#define MODEL_RESIDENT_HEADROOM (2u*1024u*1024u)
static int model_resident_suspended;
static unsigned long long resident_hits,resident_misses,resident_evictions,resident_saved;
static void model_resident_free(unsigned int slot)
{
    linearFree(model_resident[slot].data);model_resident_units-=model_resident[slot].units;
    memset(model_resident+slot,0,sizeof(model_resident[slot]));
}
static int model_resident_evict(void)
{
    unsigned int oldest=MODEL_RESIDENT_SLOTS;
    for(unsigned int i=0;i<MODEL_RESIDENT_SLOTS;++i)
        if(model_resident[i].data && model_resident[i].last_frame!=model_frame_serial &&
           (oldest==MODEL_RESIDENT_SLOTS || model_resident[i].last_frame<model_resident[oldest].last_frame)) oldest=i;
    if(oldest==MODEL_RESIDENT_SLOTS) return 0;
    model_resident_free(oldest);++resident_evictions;return 1;
}
static int model_resident_make_room(unsigned int units)
{
    while(units>MODEL_VERTEX_COUNT-model_reserved-model_resident_units)
        if(!model_resident_evict()) return 0;
    return 1;
}
static void model_unused_blocks_release(void)
{
    /* FrameBegin fenced all earlier frames; no draw this frame owns these. */
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) if(model_blocks[i].data && !model_blocks[i].used) {
        linearFree(model_blocks[i].data);model_reserved-=model_blocks[i].capacity;
        memset(model_blocks+i,0,sizeof(model_blocks[i]));
    }
}
static int model_resident_headroom(unsigned int bytes)
{
    /* Fragmented free bytes cannot promise a contiguous texture/readback
     * allocation. Stop optional residency for this map when the heap gets
     * tight instead of churning small cache allocations every frame. The
     * frame-boundary caller has fenced all previous draws before reclamation. */
    if(!model_resident_suspended && linearSpaceFree()<MODEL_RESIDENT_HEADROOM) {
        model_resident_suspended=1;
        model_unused_blocks_release();
    }
    if(model_resident_suspended) {
        while(model_resident_evict()) {}
        return 0;
    }
    return linearSpaceFree()>=MODEL_RESIDENT_HEADROOM+bytes;
}
void *n3ds_gpu_model_resident_upload(unsigned int slot,unsigned long long revision,const void *source,unsigned int bytes)
{
    unsigned int units=(bytes+sizeof(struct native_render_vertex)-1)/sizeof(struct native_render_vertex);
    if(!frame_active || model_resident_suspended || slot>=MODEL_RESIDENT_SLOTS || !revision || !source || !bytes ||
       bytes>MODEL_RESIDENT_LIMIT*sizeof(struct native_render_vertex)) return NULL;
    if(model_resident[slot].data && model_resident[slot].revision==revision && model_resident[slot].bytes==bytes) {
        model_resident[slot].last_frame=model_frame_serial;++resident_hits;resident_saved+=bytes;
        return model_resident[slot].data;
    }
    /* A decoded-stream slot can be reused while its old GPU draw is queued.
     * Keep the old allocation intact and use the existing frame arena instead. */
    if(model_resident[slot].data) {
        if(model_resident[slot].last_frame==model_frame_serial) return NULL;
        model_resident_free(slot);
    }
    while(units>MODEL_RESIDENT_LIMIT-model_resident_units)
        if(!model_resident_evict()) return NULL;
    if(units>MODEL_VERTEX_COUNT-model_reserved-model_resident_units) model_unused_blocks_release();
    if(!model_resident_make_room(units)) return NULL;
    if(!model_resident_headroom(units*sizeof(struct native_render_vertex))) return NULL;
    void *gpu=linearAlloc(units*sizeof(struct native_render_vertex));
    if(!gpu) {
        model_unused_blocks_release();
        while(!gpu && model_resident_evict()) gpu=linearAlloc(units*sizeof(struct native_render_vertex));
        if(!gpu) gpu=linearAlloc(units*sizeof(struct native_render_vertex));
    }
    if(!gpu) return NULL;
    memcpy(gpu,source,bytes);
    if(R_FAILED(GSPGPU_FlushDataCache(gpu,bytes))) {linearFree(gpu);return NULL;}
    model_resident[slot].data=gpu;model_resident[slot].units=units;model_resident[slot].bytes=bytes;
    model_resident[slot].revision=revision;model_resident[slot].last_frame=model_frame_serial;
    model_resident_units+=units;if(model_resident_units>model_resident_peak) model_resident_peak=model_resident_units;
    ++resident_misses;return gpu;
}

static float peak_command_usage;
static struct native_render_camera current_camera;
#include "engine_flashlight.inl"
static C3D_Mtx current_projection,current_model_projection;
static struct native_render_camera first_person_world_camera;
static int first_person_active,first_person_mask;
static long long frame_wait_ticks;
static int frame_sync=1;
static unsigned int timing_frames;
static double timing_gpu,timing_cpu,timing_wait;
static float timing_gpu_max,timing_cpu_previous;
static int timing_pending;
int n3ds_gpu_diagnostic_mode(void) {return diagnostic_mode;}
static void renderer_diagnostic_initialize(void)
{
    FILE *file=fopen("sdmc:/halo-source/renderer-profile.txt","rb");char mode[32]={0};
    if(file) {fgets(mode,sizeof(mode),file);fclose(file);}
    mode[strcspn(mode,"\r\n")]=0;
    if(!strcmp(mode,"quarter-pixels")) diagnostic_mode=1;
    else if(!strcmp(mode,"no-effects")) diagnostic_mode=2;
    char message[128];snprintf(message,sizeof(message),"GPU DIAGNOSTIC: mode=%d (0=normal 1=quarter-pixel scissor 2=no effect drawing)",diagnostic_mode);n3ds_log(message);
}

int n3ds_gpu_frame_active(void) { return frame_active; }
float n3ds_gpu_command_usage(void)
{
    u32 *buffer,size,offset;
    GPUCMD_GetBuffer(&buffer,&size,&offset);
    return buffer && size ? (float)offset/size : 0.f;
}
void n3ds_gpu_frame_sync(int enabled) { frame_sync=!!enabled; }
long long n3ds_gpu_frame_wait_ticks(void) { return frame_wait_ticks; }
void n3ds_gpu_model_stencil_apply(void)
{
    if(first_person_active) {
        C3D_StencilTest(true,GPU_ALWAYS,1,1,1);
        C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
    } else if(first_person_mask) {
        C3D_StencilTest(true,GPU_EQUAL,0,1,0);
        C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    } else C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
}
int n3ds_gpu_first_person_begin(float near_clip,float far_clip)
{
    if(!frame_active || first_person_active || !(near_clip>0) || !(far_clip>near_clip) || !isfinite(far_clip)) return 0;
    first_person_world_camera=current_camera;
    struct native_render_camera weapon=current_camera;
    weapon.near_clip=near_clip;weapon.far_clip=far_clip;weapon.suppress_clear=1;
    first_person_active=first_person_mask=1;
    n3ds_gpu_window_begin(&weapon);
    return 1;
}
void n3ds_gpu_first_person_end(void)
{
    if(!frame_active || !first_person_active) svcBreak(USERBREAK_PANIC);
    struct native_render_camera world=first_person_world_camera;
    world.suppress_clear=1;first_person_active=0;
    n3ds_gpu_window_begin(&world);
}
int n3ds_gpu_world_projection(float matrix[16])
{
    if(!frame_active || !matrix) return 0;
    memcpy(matrix,&current_projection,sizeof(current_projection));return 1;
}
int n3ds_gpu_model_projection(float matrix[16],float origin[3])
{
    if(!frame_active || !matrix || !origin)return 0;
    memcpy(matrix,&current_model_projection,sizeof(current_model_projection));
    memcpy(origin,current_camera.position,3*sizeof(float));return 1;
}
int n3ds_gpu_sky_projection(float matrix[16])
{
    C3D_Mtx projection,view,combined;
    if(!frame_active || !matrix) return 0;
    const struct native_render_camera *c=&current_camera;
    /* Original no-Z sky models are scaled by 1/1024 and use this frustum. */
    Mtx_PerspTilt(&projection,c->vertical_fov,400.f/240.f,.00390625f,1024.f,false);
    projection.r[1].z=-projection.r[1].x*eye_offset/1.5f;
    Mtx_LookAt(&view,FVec3_New(c->position[0],c->position[1],c->position[2]),
        FVec3_New(c->position[0]+c->forward[0],c->position[1]+c->forward[1],c->position[2]+c->forward[2]),
        FVec3_New(c->up[0],c->up[1],c->up[2]),false);
    Mtx_Multiply(&combined,&projection,&view);memcpy(matrix,&combined,sizeof(combined));return 1;
}
void n3ds_gpu_opaque_sky_begin(void)
{
    C3D_Mtx projection;
    if(!n3ds_gpu_sky_projection((float *)&projection)) svcBreak(USERBREAK_PANIC);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&projection);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
}
void n3ds_log(const char *message);

void *n3ds_gpu_geometry_allocate(unsigned int bytes) { return linearAlloc(bytes); }
void *n3ds_gpu_geometry_optional_allocate(unsigned int bytes)
{
    unsigned int free_bytes=linearSpaceFree();
    return free_bytes>2*1024*1024 && bytes<=free_bytes-2*1024*1024?linearAlloc(bytes):NULL;
}
void n3ds_gpu_geometry_free(void *memory) { if (memory) linearFree(memory); }
void n3ds_gpu_geometry_flush(void *memory, unsigned int bytes)
{
    uintptr_t p=(uintptr_t)memory;
    /* Only this renderer's append-only CPU vertex arena is deferred. Static
     * meshes and all external allocations retain immediate coherency. */
    if(frame_active) for(unsigned int i=0;i<MODEL_BLOCKS;++i) {
        uintptr_t start=(uintptr_t)model_blocks[i].data;
        unsigned int used_bytes=model_blocks[i].used*sizeof(struct native_render_vertex);
        if(start && p>=start && p-start<=used_bytes && bytes<=used_bytes-(p-start)) {
            ++model_deferred;return;
        }
    }
    GSPGPU_FlushDataCache(memory,bytes);
}

void n3ds_gpu_model_cache_release(void)
{
    /* Map transitions occur between frames. Retire every queued GPU reference
     * before releasing the cached blocks, including loading-overlay vertices. */
    if(frame_active || !n3ds_gpu_texture_barrier()) svcBreak(USERBREAK_PANIC);
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) linearFree(model_blocks[i].data);
    memset(model_blocks,0,sizeof(model_blocks));model_reserved=model_used=0;
    if(resident_hits || resident_misses) {
        char message[240];snprintf(message,sizeof(message),"MODEL RESIDENT CACHE: hits=%llu uploads=%llu evictions=%llu saved_bytes=%llu peak_bytes=%u shared_geometry_cap=%u pressure_suspended=%d",resident_hits,resident_misses,resident_evictions,resident_saved,model_resident_peak*(unsigned int)sizeof(struct native_render_vertex),MODEL_VERTEX_COUNT*(unsigned int)sizeof(struct native_render_vertex),model_resident_suspended);n3ds_log(message);
    }
    for(unsigned int i=0;i<MODEL_RESIDENT_SLOTS;++i) model_resident_free(i);
    resident_hits=resident_misses=resident_evictions=resident_saved=0;model_resident_peak=0;model_resident_suspended=0;
}
void n3ds_gpu_map_scratch_release(void)
{
    extern void n3ds_gpu_text_map_release(void),n3ds_gpu_chicago_map_release(void),n3ds_gpu_widgets_map_release(void);
    if(frame_active || !n3ds_gpu_texture_barrier()) svcBreak(USERBREAK_PANIC);
    for(unsigned int stage=0;stage<3;++stage) n3ds_gpu_texture_bind(stage,NULL);
    n3ds_gpu_text_map_release();n3ds_gpu_chicago_map_release();n3ds_gpu_widgets_map_release();
}

void n3ds_gpu_renderer_dispose(void)
{
    if(vertex_buffer_requests) {
        char line[160];snprintf(line,sizeof(line),"NATIVE VERTEX BINDINGS: cacheable_requests=%llu reused=%llu; world draws invalidate; same-frame descriptors only",vertex_buffer_requests,vertex_buffer_reuses);n3ds_log(line);
        vertex_buffer_requests=vertex_buffer_reuses=0;
    }
    bound_vertex_valid=0;
    if(model_reflection_draws) {char message[128];snprintf(message,sizeof(message),"MODEL NATIVE REFLECTIONS: draws=%llu; cube/base/mask, single pass",model_reflection_draws);n3ds_log(message);model_reflection_draws=0;}
    if(model_detail_draws) {char message[128];snprintf(message,sizeof(message),"MODEL NATIVE DETAIL: draws=%llu; detail/base/mask, single pass",model_detail_draws);n3ds_log(message);model_detail_draws=0;}
    if(model_lit_draws) {
        char message[160];snprintf(message,sizeof(message),"MODEL NATIVE LIGHTING: draws=%llu environment_updates=%llu; ambient and distant lights, single pass",model_lit_draws,model_light_updates);n3ds_log(message);
        model_lit_draws=model_light_updates=0;
    }
    if(model_palette_uploads) {
        char message[192];snprintf(message,sizeof(message),"MODEL SUBMISSION REUSE: state=%llu palettes=%llu uploads=%llu",model_state_reuses,model_palette_reuses,model_palette_uploads);
        n3ds_log(message);model_state_reuses=model_palette_reuses=model_palette_uploads=0;
    }
    if(model_material_draws) {
        char message[160];snprintf(message,sizeof(message),"MODEL NATIVE MATERIALS: draws=%llu masked=%llu state_updates=%llu",model_material_draws,model_mask_draws,model_material_updates);n3ds_log(message);
        snprintf(message,sizeof(message),"NATIVE MODEL CULLING: single_sided_draws=%llu unculled_draws=%llu",model_single_sided_draws,model_unculled_draws);n3ds_log(message);
        model_single_sided_draws=model_unculled_draws=0;
        model_material_draws=model_mask_draws=model_material_updates=0;
    }
    controls_dispose();
    if(loading_font) {n3ds_gpu_texture_destroy(loading_font);loading_font=NULL;
    if(loading_art) n3ds_gpu_texture_destroy(loading_art);loading_art=NULL;loading_art_tried=0;}
    /* The caller must have submitted the current frame before freeing resources. */
    if (frame_active || !n3ds_gpu_texture_barrier()) svcBreak(USERBREAK_PANIC);
    if(index_draws) {
        char message[128];snprintf(message,sizeof(message),"INDEX TRANSFERS: draws=%u frame_flushes=%u",index_draws,index_flushes);
        n3ds_log(message);index_draws=index_flushes=0;
        snprintf(message,sizeof(message),"MODEL VERTEX TRANSFERS: deferred=%u frame_flushes=%u",model_deferred,model_flushes);
        n3ds_log(message);model_deferred=model_flushes=0;
        snprintf(message,sizeof(message),"MODEL ARENA: peak_bytes=%u capacity_bytes=%u",model_peak*(unsigned int)sizeof(struct native_render_vertex),MODEL_VERTEX_COUNT*(unsigned int)sizeof(struct native_render_vertex));n3ds_log(message);model_peak=0;
    }
    n3ds_gpu_text_dispose();
    n3ds_gpu_widgets_dispose();
    n3ds_gpu_chicago_dispose();
    world_scale_dispose();
    if (target) C3D_RenderTargetDelete(target);
    if (right_target) C3D_RenderTargetDelete(right_target);right_target=NULL;
    stereo_level=eye_offset=0;eye_right=overlay_stereo=0;gfxSet3D(false);
    if (bottom_target) C3D_RenderTargetDelete(bottom_target);bottom_target=NULL;bottom_selected=0;
    native_light_env_bind(NULL);model_light_ready=model_light_valid=0;flashlight_ready=0;flashlight_power=0;
    n3ds_gpu_program_park();
    for(int i=0;i<4;++i) {
        if(lit_programs[i].ready) shaderProgramFree(&lit_programs[i].program);
        if(lit_programs[i].binary) DVLB_Free(lit_programs[i].binary);
    }
    memset(lit_programs,0,sizeof(lit_programs));
    for(int i=0;i<2;++i) {
        if(depth_programs[i].ready) shaderProgramFree(&depth_programs[i].program);
        if(depth_programs[i].binary) DVLB_Free(depth_programs[i].binary);
    }
    memset(depth_programs,0,sizeof(depth_programs));
    if(water_program.ready) shaderProgramFree(&water_program.program);
    if(water_program.binary) DVLB_Free(water_program.binary);
    memset(&water_program,0,sizeof(water_program));
    if(glass_program.ready) shaderProgramFree(&glass_program.program);
    if(glass_program.binary) DVLB_Free(glass_program.binary);
    memset(&glass_program,0,sizeof(glass_program));
    environment_program_valid=0;bump_state_valid=0;plasma_state_valid=0;plasma_ready=0;
    if(environment_ready) shaderProgramFree(&environment_program);environment_ready=0;
    if(environment_shader) DVLB_Free(environment_shader);environment_shader=NULL;
    if (program_ready) shaderProgramFree(&program);
    if (shader) DVLB_Free(shader);
    if(rigid_ready) shaderProgramFree(&rigid_program);rigid_ready=0;
    if(rigid_shader) DVLB_Free(rigid_shader);rigid_shader=NULL;
    if(skin_ready) shaderProgramFree(&skin_program);skin_ready=0;
    if(skin_shader) DVLB_Free(skin_shader);skin_shader=NULL;
    linearFree(index_stream);
    n3ds_gpu_model_cache_release();
    target = NULL; shader = NULL; index_stream = NULL; program_ready = 0;
}
int n3ds_gpu_renderer_initialize(void)
{
    if (target) return 0;
    extern void debug_dump_memory(void);
    n3ds_log("STARTUP: GPU renderer memory before targets");debug_dump_memory();
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!target) return 0;
    bottom_target=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!bottom_target) {C3D_RenderTargetDelete(target);target=NULL;return 0;}
    right_target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!right_target) {C3D_RenderTargetDelete(bottom_target);bottom_target=NULL;C3D_RenderTargetDelete(target);target=NULL;return 0;}
    n3ds_log("STARTUP: GPU render targets allocated; configuring display");
    C3D_RenderTargetSetOutput(right_target,GFX_TOP,GFX_RIGHT,
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    C3D_RenderTargetSetOutput(bottom_target,GFX_BOTTOM,GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    n3ds_log("STARTUP: GPU geometry buffers and shaders");
    index_stream = linearAlloc(INDEX_BYTES);
    shader = DVLB_ParseFile((u32 *)engine_scene_shader, sizeof(engine_scene_shader));
    if (!index_stream || !shader) { n3ds_gpu_renderer_dispose(); return 0; }
    shaderProgramInit(&program); program_ready = 1;
    if (R_FAILED(shaderProgramSetVsh(&program, &shader->DVLE[0]))) { n3ds_gpu_renderer_dispose(); return 0; }
    matrix_location = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
    if (matrix_location < 0) { n3ds_gpu_renderer_dispose(); return 0; }
    const unsigned char *depth_data[]={engine_rigid_depth_shader,engine_skin_depth_shader};
    const unsigned int depth_bytes[]={sizeof(engine_rigid_depth_shader),sizeof(engine_skin_depth_shader)};
    for(int i=0;i<2;++i) {
        depth_programs[i].binary=DVLB_ParseFile((u32 *)depth_data[i],depth_bytes[i]);
        if(!depth_programs[i].binary) {n3ds_gpu_renderer_dispose();return 0;}
        shaderProgramInit(&depth_programs[i].program);depth_programs[i].ready=1;
        if(R_FAILED(shaderProgramSetVsh(&depth_programs[i].program,&depth_programs[i].binary->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
        depth_programs[i].projection=shaderInstanceGetUniformLocation(depth_programs[i].program.vertexShader,"projection");
        depth_programs[i].bone=shaderInstanceGetUniformLocation(depth_programs[i].program.vertexShader,"bone");
        if(depth_programs[i].projection<0 || depth_programs[i].bone<0) {n3ds_gpu_renderer_dispose();return 0;}
    }
    const unsigned char *lit_data[]={engine_rigid_lit_shader,engine_skin_lit_shader,engine_rigid_reflect_shader,engine_skin_reflect_shader};
    const unsigned int lit_bytes[]={sizeof(engine_rigid_lit_shader),sizeof(engine_skin_lit_shader),sizeof(engine_rigid_reflect_shader),sizeof(engine_skin_reflect_shader)};
    for(int i=0;i<4;++i) {
        lit_programs[i].binary=DVLB_ParseFile((u32 *)lit_data[i],lit_bytes[i]);
        if(!lit_programs[i].binary) {n3ds_gpu_renderer_dispose();return 0;}
        shaderProgramInit(&lit_programs[i].program);lit_programs[i].ready=1;
        if(R_FAILED(shaderProgramSetVsh(&lit_programs[i].program,&lit_programs[i].binary->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
        shaderInstance_s *v=lit_programs[i].program.vertexShader;
        lit_programs[i].projection=shaderInstanceGetUniformLocation(v,"projection");
        lit_programs[i].bone=shaderInstanceGetUniformLocation(v,"bone");
        lit_programs[i].options=shaderInstanceGetUniformLocation(v,"options");
        lit_programs[i].camera=shaderInstanceGetUniformLocation(v,"camera");
        lit_programs[i].delta=shaderInstanceGetUniformLocation(v,"reflection_delta");
        lit_programs[i].parallel=shaderInstanceGetUniformLocation(v,"reflection_parallel");
        if(i>=2 && (lit_programs[i].delta<0 || lit_programs[i].parallel<0)) {n3ds_gpu_renderer_dispose();return 0;}
        if(lit_programs[i].projection<0 || lit_programs[i].bone<0 || lit_programs[i].options<0 || lit_programs[i].camera<0) {n3ds_gpu_renderer_dispose();return 0;}
    }
    water_program.binary=DVLB_ParseFile((u32 *)engine_water_shader,sizeof(engine_water_shader));
    if(!water_program.binary) {n3ds_gpu_renderer_dispose();return 0;}
    shaderProgramInit(&water_program.program);water_program.ready=1;
    if(R_FAILED(shaderProgramSetVsh(&water_program.program,&water_program.binary->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.projection=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"projection");
    if(water_program.projection<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.camera=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"camera");
    if(water_program.camera<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.fallback=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"fallback_eye");
    if(water_program.fallback<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.delta=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"delta");
    if(water_program.delta<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.parallel=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"parallel");
    if(water_program.parallel<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.wave_switch=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"wave_switch");
    if(water_program.wave_switch<0) {n3ds_gpu_renderer_dispose();return 0;}
    water_program.wave_phase=shaderInstanceGetUniformLocation(water_program.program.vertexShader,"wave_phase");
    if(water_program.wave_phase<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.binary=DVLB_ParseFile((u32 *)engine_glass_shader,sizeof(engine_glass_shader));
    if(!glass_program.binary) {n3ds_gpu_renderer_dispose();return 0;}
    shaderProgramInit(&glass_program.program);glass_program.ready=1;
    if(R_FAILED(shaderProgramSetVsh(&glass_program.program,&glass_program.binary->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.projection=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"projection");
    if(glass_program.projection<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.camera=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"camera");
    if(glass_program.camera<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.fallback=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"fallback_eye");
    if(glass_program.fallback<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.delta=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"delta");
    if(glass_program.delta<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.parallel=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"parallel");
    if(glass_program.parallel<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.options=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"options");
    if(glass_program.options<0) {n3ds_gpu_renderer_dispose();return 0;}
    glass_program.pose=shaderInstanceGetUniformLocation(glass_program.program.vertexShader,"pose");
    if(glass_program.pose<0) {n3ds_gpu_renderer_dispose();return 0;}
    C3D_LightEnvInit(&model_light_env);
    C3D_Material diffuse_material={.ambient={1,1,1},.diffuse={1,1,1}};
    C3D_LightEnvMaterial(&model_light_env,&diffuse_material);
    for(int i=0;i<3;++i) if(C3D_LightInit(&model_lights[i],&model_light_env)<0) {n3ds_gpu_renderer_dispose();return 0;}
    model_light_ready=1;model_light_valid=0;
    rigid_shader=DVLB_ParseFile((u32 *)engine_rigid_shader,sizeof(engine_rigid_shader));
    if(!rigid_shader) {n3ds_gpu_renderer_dispose();return 0;}
    shaderProgramInit(&rigid_program);rigid_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&rigid_program,&rigid_shader->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
    rigid_projection=shaderInstanceGetUniformLocation(rigid_program.vertexShader,"projection");
    rigid_bone=shaderInstanceGetUniformLocation(rigid_program.vertexShader,"bone");
    rigid_options=shaderInstanceGetUniformLocation(rigid_program.vertexShader,"options");
    if(rigid_projection<0 || rigid_bone<0 || rigid_options<0) {n3ds_gpu_renderer_dispose();return 0;}
    skin_shader=DVLB_ParseFile((u32 *)engine_skin_shader,sizeof(engine_skin_shader));
    if(!skin_shader) {n3ds_gpu_renderer_dispose();return 0;}
    shaderProgramInit(&skin_program);skin_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&skin_program,&skin_shader->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
    skin_projection=shaderInstanceGetUniformLocation(skin_program.vertexShader,"projection");
    skin_bone=shaderInstanceGetUniformLocation(skin_program.vertexShader,"bone");
    skin_options=shaderInstanceGetUniformLocation(skin_program.vertexShader,"options");
    if(skin_projection<0 || skin_bone<0 || skin_options<0) {n3ds_gpu_renderer_dispose();return 0;}
    C3D_LightEnvInit(&bump_light_env);
    C3D_Material bump_mtl={.diffuse={1,1,1},.specular0={1,1,1}};C3D_LightEnvMaterial(&bump_light_env,&bump_mtl);
    C3D_LightEnvBumpMode(&bump_light_env,GPU_BUMP_AS_BUMP);C3D_LightEnvBumpSel(&bump_light_env,2);C3D_LightEnvBumpNormalZ(&bump_light_env,true);
    if(C3D_LightInit(&bump_light,&bump_light_env)<0) {n3ds_gpu_renderer_dispose();return 0;}
    C3D_LightSpecular0(&bump_light,0,0,0);C3D_LightSpecular1(&bump_light,0,0,0);
    if(!flashlight_initialize()) {n3ds_gpu_renderer_dispose();return 0;}
    C3D_LightDiffuse(&bump_light,1,1,1);C3D_FVec bump_direction=FVec4_New(0,0,1,0);C3D_LightPosition(&bump_light,&bump_direction);
    environment_shader=DVLB_ParseFile((u32 *)engine_environment_shader,sizeof(engine_environment_shader));
    if(!environment_shader) {n3ds_gpu_renderer_dispose();return 0;}
    shaderProgramInit(&environment_program);environment_ready=1;
    if(R_FAILED(shaderProgramSetVsh(&environment_program,&environment_shader->DVLE[0]))) {n3ds_gpu_renderer_dispose();return 0;}
    environment_projection=shaderInstanceGetUniformLocation(environment_program.vertexShader,"projection");
    environment_options=shaderInstanceGetUniformLocation(environment_program.vertexShader,"options");
    if(environment_projection<0 || environment_options<0) {n3ds_gpu_renderer_dispose();return 0;}
    environment_recipes_initialize();
    n3ds_log("STARTUP: GPU text initialize");
    if (!n3ds_gpu_text_initialize()) { n3ds_gpu_renderer_dispose(); return 0; }
    n3ds_log("STARTUP: GPU widgets initialize");
    if (!n3ds_gpu_widgets_initialize()) { n3ds_gpu_renderer_dispose(); return 0; }
    n3ds_log("STARTUP: GPU Chicago initialize");
    if (!n3ds_gpu_chicago_initialize()) { n3ds_gpu_renderer_dispose(); return 0; }
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    renderer_diagnostic_initialize();
    n3ds_log("STARTUP: GPU renderer ready");debug_dump_memory();
    return 1;
}
int n3ds_gpu_frame_begin(void)
{
    frame_wait_ticks=0;
    if (!target || frame_active) return 0;
    if(!aptMainLoop()) {
        /* Close can arrive at this inner frame boundary as well as the loop. */
        n3ds_log("EXIT: HOME requested close at GPU frame boundary");exit(0);
    }
    long long before=svcGetSystemTick();
    /* Flag zero still blocks on the previous GPU queue. SYNCDRAW adds a
     * separate VBlank wait; the live mission loop already paces to30Hz. */
    BOOT_TRACE("frame begin: waiting for GPU queue");
    /* A stuck GPU must leave a durable report instead of an unexplained hang.
     * NONBLOCK only polls queue completion; buffers stay owned until it ends. */
    while(!C3D_FrameBegin(C3D_FRAME_NONBLOCK | (frame_sync?C3D_FRAME_SYNCDRAW:0))) {
        if(svcGetSystemTick()-(u64)before>5ULL*SYSCLOCK_ARM11) {
            u32 busy=0;Result status=GSPGPU_ReadHWRegs(0x400034,&busy,sizeof(busy));
            char message[192];snprintf(message,sizeof(message),
                "GPU STALL FATAL: queue wait exceeded 5s busy=%08lx status=%08lx command_peak=%.4f models=%u indices=%u stereo=%d",
                (unsigned long)busy,(unsigned long)status,peak_command_usage,model_used,index_used,stereo_level>0);
            n3ds_log(message);
            /* Never recycle/free resources still referenced by stalled work. */
            for(;;) svcSleepThread(100000000LL);
        }
        svcSleepThread(250000LL);
    }
    BOOT_TRACE("frame begin: GPU queue ready");
    /* Queue has completed: read the previous submission, not an in-flight
     * timer. Queue time includes transfers; it is not a GPU busy percentage.
     * Read CPU time saved at previous FrameEnd because FrameBegin resets it. */
    if(timing_pending) {
        float gpu=C3D_GetDrawingTime();world_scale_feedback(gpu,timing_cpu_previous);timing_gpu+=gpu;timing_cpu+=timing_cpu_previous;
        if(gpu>timing_gpu_max) timing_gpu_max=gpu;
        timing_wait+=(svcGetSystemTick()-before)*(1000.0/268123480.0);
        if(++timing_frames==120) {
            char message[256];snprintf(message,sizeof(message),"GPU QUEUE PROFILE: frames=120 cpu_prepare_ms=%.3f queue_ms=%.3f queue_max_ms=%.3f begin_wait_ms=%.3f vblank_sync=%d diagnostic=%d stereo=%d",
                timing_cpu/120,timing_gpu/120,timing_gpu_max,timing_wait/120,frame_sync,diagnostic_mode,stereo_level>0);n3ds_log(message);
            timing_frames=0;timing_cpu=timing_gpu=timing_wait=0;timing_gpu_max=0;
        }
    }
    native_light_frame_begin();
    /* Apply scanout changes only after the previous frame has completed. */
    gfxSet3D(stereo_level>0);overlay_stereo=0;
    frame_wait_ticks=svcGetSystemTick()-before;
    /* FrameBegin waits for the previous command queue, so its indices can be reused. */
    bound_vertex_valid=0;
    frame_active = 1; index_used = 0; model_used = 0;bottom_selected=0;
    ++model_frame_serial;
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) model_blocks[i].used=0;
    model_resident_headroom(0);
    C3D_RenderTargetClear(bottom_target,C3D_CLEAR_ALL,0x050c16ff,0);
    C3D_FrameDrawOn(bottom_target);
    n3ds_gpu_text_frame_begin();
    n3ds_gpu_widgets_frame_begin();
    n3ds_gpu_chicago_frame_begin();
    return 1;
}
struct native_render_vertex *n3ds_gpu_model_vertices_allocate(unsigned int count)
{
    /* CPU skinning writes here; this arena survives every view and queued draw
     * until the next FrameBegin has waited for the GPU. Never alias staging. */
    if (!frame_active || !count || count>MODEL_VERTEX_COUNT-model_used) {
        char message[160];snprintf(message,sizeof(message),"MODEL ARENA REJECT: requested=%u used=%u capacity=%u active=%d",count,model_used,MODEL_VERTEX_COUNT,frame_active);n3ds_log(message);return NULL;
    }
    struct native_render_vertex *result=NULL;
    unsigned int best=MODEL_BLOCKS,space=~0u;
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) {
        unsigned int available=model_blocks[i].capacity-model_blocks[i].used;
        if(available>=count && available<space) {best=i;space=available;}
    }
    if(best==MODEL_BLOCKS) {
        /* A new frame may need a larger block than a prior frame's layout.
         * Unused blocks have no references in this frame; FrameBegin already
         * waited for their previous GPU users. Reclaim them before rejecting
         * an otherwise valid allocation because cached capacities fill the cap. */
        unsigned int free_slot=0;
        for(unsigned int i=0;i<MODEL_BLOCKS;++i) free_slot+=!model_blocks[i].data;
        if(!free_slot || count>MODEL_VERTEX_COUNT-model_reserved-model_resident_units) {
            for(unsigned int i=0;i<MODEL_BLOCKS;++i) if(model_blocks[i].data && !model_blocks[i].used) {
                linearFree(model_blocks[i].data);model_reserved-=model_blocks[i].capacity;
                memset(&model_blocks[i],0,sizeof(model_blocks[i]));
            }
        }
        for(unsigned int i=0;i<MODEL_BLOCKS;++i) if(!model_blocks[i].data) {best=i;break;}
        if(best==MODEL_BLOCKS || !model_resident_make_room(count)) return NULL;
        unsigned int capacity=(count+MODEL_BLOCK_VERTICES-1)&~(MODEL_BLOCK_VERTICES-1);
        if(capacity>MODEL_VERTEX_COUNT-model_reserved-model_resident_units) capacity=MODEL_VERTEX_COUNT-model_reserved-model_resident_units;
        model_blocks[best].data=linearAlloc(capacity*sizeof(*result));
        if(!model_blocks[best].data) {
            /* Physical linear memory can run out before our arena limit.
             * Only reclaim blocks with no queued draws in this frame. */
            for(unsigned int i=0;i<MODEL_BLOCKS;++i) if(model_blocks[i].data && !model_blocks[i].used) {
                linearFree(model_blocks[i].data);model_reserved-=model_blocks[i].capacity;
                memset(&model_blocks[i],0,sizeof(model_blocks[i]));
            }
            while(model_resident_evict()) {}
            model_blocks[best].data=linearAlloc(capacity*sizeof(*result));
            if(!model_blocks[best].data && capacity!=count) {
                /* Do not require 128 KiB of spare memory for a tiny mesh. */
                capacity=count;
                model_blocks[best].data=linearAlloc(capacity*sizeof(*result));
            }
        }
        if(!model_blocks[best].data) {
            char message[192];snprintf(message,sizeof(message),"MODEL ARENA MEMORY: requested=%u used=%u reserved=%u linear_free=%u",count,model_used,model_reserved,(unsigned int)linearSpaceFree());n3ds_log(message);return NULL;
        }
        model_blocks[best].capacity=capacity;model_reserved+=capacity;
    }
    result=model_blocks[best].data+model_blocks[best].used;
    model_blocks[best].used+=count;
    model_used+=count;
    if(model_used>model_peak) model_peak=model_used;
    return result;
}
int n3ds_gpu_model_storage_tests(void)
{
    if(frame_active) return 0;
    n3ds_gpu_model_cache_release();
    if(!n3ds_gpu_frame_begin()) return 0;
    struct native_render_vertex *a=n3ds_gpu_model_vertices_allocate(4096);
    struct native_render_vertex *b=n3ds_gpu_model_vertices_allocate(4096);
    struct native_render_vertex *c=n3ds_gpu_model_vertices_allocate(MODEL_VERTEX_COUNT-8192);
    int ok=a && b && c && a!=b && b!=c && a!=c;
    if(ok) {
        a[0].position[0]=13; a[4095].position[0]=17;
        b[0].position[0]=19; b[4095].position[0]=23;
        c[0].position[0]=29;c[MODEL_VERTEX_COUNT-8193].position[0]=31;
        ok=a[0].position[0]==13 && a[4095].position[0]==17 &&
           b[0].position[0]==19 && b[4095].position[0]==23 &&
           !n3ds_gpu_model_vertices_allocate(1);
    }
    n3ds_gpu_present();
    if(!n3ds_gpu_frame_begin()) return 0;
    struct native_render_vertex *reuse=n3ds_gpu_model_vertices_allocate(4096);
    ok=ok && reuse==a && reuse[0].position[0]==13;
    n3ds_gpu_present();
    if(!n3ds_gpu_frame_begin()) return 0;
    /* A large next frame must not be blocked by the preceding cached layout. */
    ok=ok && n3ds_gpu_model_vertices_allocate(MODEL_VERTEX_COUNT)!=NULL;
    n3ds_gpu_present();n3ds_gpu_model_cache_release();
    ok=ok && !model_reserved && !model_used;
    if(ok) n3ds_log("PASS: model storage grows in stable blocks, retains full vertex limit, reuses after GPU wait and releases between maps");
    return ok;
}
static void window_state_begin(const struct native_render_camera *camera)
{
    n3ds_gpu_effect_state_invalidate();
    native_light_env_bind(NULL);
    model_state_valid=0;
    environment_state_valid=0;bump_state_valid=0;environment_program_valid=0;plasma_state_valid=0;
    if(!camera->suppress_clear) first_person_active=first_person_mask=0;
    current_camera=*camera;flashlight_model_camera_update();
    C3D_BindProgram(&program);
    C3D_AttrInfo *attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 2);
    for (int i=0; i<6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    n3ds_gpu_model_stencil_apply();
    n3ds_gpu_texture_bind(1, NULL); n3ds_gpu_texture_bind(2, NULL);
    if (!camera->suppress_clear) C3D_RenderTargetClear(world_target(), C3D_CLEAR_ALL, world_clear_color(camera->clear_color), 0);
    world_target_bind();
    C3D_SetScissor(diagnostic_mode==1 && !bottom_selected?GPU_SCISSOR_NORMAL:GPU_SCISSOR_DISABLE,0,0,120,200);
}
void n3ds_gpu_window_begin(const struct native_render_camera *camera)
{
    C3D_Mtx projection, view, combined;
    window_state_begin(camera);
    Mtx_PerspTilt(&projection, camera->vertical_fov, 400.f/240.f, camera->near_clip, camera->far_clip, false);
    /* The camera already carries its eye translation; only shift the frustum
     * here, keeping a point 1.5 world units away at zero disparity. */
    projection.r[1].z=-projection.r[1].x*eye_offset/1.5f;
    Mtx_LookAt(&view, FVec3_New(0,0,0),
        FVec3_New(camera->forward[0],camera->forward[1],camera->forward[2]),
        FVec3_New(camera->up[0],camera->up[1],camera->up[2]), false);
    Mtx_Multiply(&current_model_projection,&projection,&view);
    Mtx_Translate(&view,-camera->position[0],-camera->position[1],-camera->position[2],true);
    Mtx_Multiply(&combined, &projection, &view);
    current_projection=combined;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, matrix_location, &combined);
}
void n3ds_gpu_world_state_restore(void)
{
    struct native_render_camera restore=current_camera;
    if(!frame_active) svcBreak(USERBREAK_PANIC);
    restore.suppress_clear=1;
    /* Material changes do not change the camera. Reinstall its exact matrix;
     * camera/weapon-clip transitions still rebuild it in window_begin. */
    window_state_begin(&restore);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&current_projection);
}
int n3ds_gpu_geometry_draw(const struct native_render_vertex *vertices, const unsigned short *indices, unsigned int count)
{ return n3ds_gpu_indexed_draw(vertices,indices,count,sizeof(*vertices),3,0x210); }
static int model_transform_draw(const void *vertices,const unsigned short *indices,unsigned int count,
    const float *rows,unsigned int nodes,const float uv[2],int sky,int decal,int two_sided,int skinned,
    const struct native_model_material *material)
{
    if(!frame_active || !rigid_ready || !skin_ready || !nodes || nodes>NATIVE_GPU_MODEL_BONES) return 0;
    const struct native_model_material *m=material?material:&model_plain_material;
    int lit=!sky && m->lighting && model_light_ready;
    int reflective=lit && m->reflection && m->multipurpose;
    int detailed=lit && !reflective && m->detail;
    if(detailed && (m->detail_function<0 || m->detail_function>1 || m->detail_mask<0 || m->detail_mask>8 ||
       (m->detail_mask && !m->multipurpose) || !isfinite(m->detail_scale[0]) || !isfinite(m->detail_scale[1]))) return 0;
    int pi=skinned+2;
    int key=!!sky | (!!decal<<1) | (!!two_sided<<2) | (!!skinned<<3) | (lit<<4) | (reflective<<5) | (detailed<<6);
    int projection_location=lit?lit_programs[pi].projection:(skinned?skin_projection:rigid_projection);
    int bone_location=lit?lit_programs[pi].bone:(skinned?skin_bone:rigid_bone);
    int options_location=lit?lit_programs[pi].options:(skinned?skin_options:rigid_options);
    ++model_material_draws;if(m->multipurpose) ++model_mask_draws;
    if(sky || two_sided) ++model_unculled_draws;else ++model_single_sided_draws;
    int retained=model_batch_active && model_state_valid;
    /* Reflection changes TEV/options, not depth, attributes, projection or
     * the shared lit program. Retain that pipeline across material parts. */
    int reuse=retained && !((model_state_key^key)&31);
    int same_program=retained && !((model_state_key^key)&24);
    float options[4]={uv[0],uv[1],sky?1.f:0.f,reflective?1.f:0.f};
    if(!reuse) {
      /* A model-program change need not erase texture combiners/samplers.
       * Reinstall only pipeline state; explicit batch flushes still restore
       * the complete world state before any external material path. */
      if(!retained) n3ds_gpu_world_state_restore();
      C3D_Mtx projection=current_model_projection;
      if(sky) {if(!n3ds_gpu_sky_projection((float *)&projection)) return 0;C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);}
      else C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
      if(decal) {
        C3D_DepthTest(!sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
        C3D_CullFace(sky || two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
      } else {C3D_CullFace(sky || two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);}
      n3ds_gpu_model_stencil_apply();
      if(!same_program) C3D_BindProgram(lit?&lit_programs[pi].program:(skinned?&skin_program:&rigid_program));
      C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection_location,&projection);
      if(lit) C3D_FVUnifSet(GPU_VERTEX_SHADER,lit_programs[pi].camera,0,0,0,0);
      C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
      AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
      if(skinned) AttrInfo_AddLoader(a,3,GPU_FLOAT,4);
    } else ++model_state_reuses;
    if(!retained || ((model_state_key^key)&114) || !!m->multipurpose!=!!model_cached_material.multipurpose ||
       m->alpha_test!=model_cached_material.alpha_test || (detailed &&
       (m->detail_function!=model_cached_material.detail_function || m->detail_mask!=model_cached_material.detail_mask || m->detail_after!=model_cached_material.detail_after)))
        model_material_apply(m,decal,lit);
    else if(m->multipurpose) {
        if(m->emission!=model_cached_material.emission) C3D_TexEnvColor(C3D_GetTexEnv(0),m->emission);
        if(m->tint!=model_cached_material.tint) C3D_TexEnvColor(C3D_GetTexEnv(1),m->tint|0xff000000u);
    }
    if(lit) {
        if(!n3ds_gpu_texture_bind(1,m->base) || (m->multipurpose && !n3ds_gpu_texture_bind(2,m->multipurpose))) return 0;
    } else if(m->multipurpose && !n3ds_gpu_texture_bind(1,m->multipurpose)) return 0;
    if(reflective) {
        if(!n3ds_gpu_texture_bind(0,m->reflection)) return 0;
        if(!same_program || !model_cached_material.reflection || memcmp(model_cached_material.reflection_delta,m->reflection_delta,sizeof(m->reflection_delta)))
            C3D_FVUnifSet(GPU_VERTEX_SHADER,lit_programs[pi].delta,m->reflection_delta[0],m->reflection_delta[1],m->reflection_delta[2],m->reflection_delta[3]);
        if(!same_program || memcmp(model_cached_material.reflection_parallel,m->reflection_parallel,sizeof(m->reflection_parallel)))
            C3D_FVUnifSet(GPU_VERTEX_SHADER,lit_programs[pi].parallel,m->reflection_parallel[0],m->reflection_parallel[1],m->reflection_parallel[2],m->reflection_parallel[3]);
        ++model_reflection_draws;
    }
    if(detailed) {
        if(!n3ds_gpu_texture_bind(0,m->detail)) return 0;
        if(!same_program || !model_cached_material.detail || model_cached_material.reflection ||
           memcmp(model_cached_material.detail_scale,m->detail_scale,sizeof(m->detail_scale)))
            C3D_FVUnifSet(GPU_VERTEX_SHADER,lit_programs[pi].delta,m->detail_scale[0],m->detail_scale[1],0,0);
        ++model_detail_draws;
    }
    if(lit) {model_lighting_apply(m->lighting,m->translucency);++model_lit_draws;}
    else native_light_env_bind(NULL);
    model_cached_material=*m;
    /* Uniform registers are shared between programs. Compare values only while
     * the exact model pipeline is retained; a view/material change invalidates
     * all cached registers. Copy values, never use temporary row addresses. */
    /* Sky palettes keep their original origin; ordinary models subtract the
     * camera. Re-upload even identical rows when crossing that boundary. */
    unsigned int retained_nodes=same_program && !((model_state_key^key)&1)?(model_cached_nodes<nodes?model_cached_nodes:nodes):0;

    if(retained_nodes && memcmp(model_cached_rows,rows,retained_nodes*12*sizeof(float)))retained_nodes=0;

    if(retained_nodes<nodes) {

        for(unsigned int i=retained_nodes*3;i<nodes*3;++i) C3D_FVUnifSet(GPU_VERTEX_SHADER,bone_location+i,rows[i*4],rows[i*4+1],rows[i*4+2],rows[i*4+3]-(sky?0:current_camera.position[i%3]));

        memcpy(model_cached_rows+retained_nodes*12,rows+retained_nodes*12,(nodes-retained_nodes)*12*sizeof(float));

        ++model_palette_uploads;

    } else ++model_palette_reuses;

    model_cached_nodes=same_program?(model_cached_nodes>nodes?model_cached_nodes:nodes):nodes;

    if(!same_program || memcmp(model_cached_options,options,sizeof(options))) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,options_location,options[0],options[1],options[2],options[3]);
        memcpy(model_cached_options,options,sizeof(options));
    }
    model_state_key=key;model_state_valid=1;
    int result=n3ds_gpu_indexed_draw(vertices,indices,count,skinned?sizeof(struct native_skin_vertex):sizeof(struct native_render_vertex),skinned?4:3,skinned?0x3210:0x210);
    if(!model_batch_active) n3ds_gpu_world_state_restore();
    return result;
}
int n3ds_gpu_model_rigid_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int count,
    const float rows[12],const float uv[2],int sky,int decal,int two_sided,const struct native_model_material *m)
{return model_transform_draw(v,i,count,rows,1,uv,sky,decal,two_sided,0,m);}
int n3ds_gpu_model_skin_draw(const struct native_skin_vertex *v,const unsigned short *i,unsigned int count,
    const float *rows,unsigned int nodes,const float uv[2],int sky,int decal,int two_sided,const struct native_model_material *m)
{return model_transform_draw(v,i,count,rows,nodes,uv,sky,decal,two_sided,1,m);}
void n3ds_gpu_camera_origin(float origin[3])
{memcpy(origin,current_camera.position,3*sizeof(float));}
void n3ds_gpu_model_rebase_vertices(struct native_render_vertex *v,unsigned int count,int sky)
{
    if(!sky) for(unsigned int i=0;i<count;++i) for(int c=0;c<3;++c)
        v[i].position[c]-=current_camera.position[c];
}
static int model_cpu_draw(const struct native_render_vertex *v,const unsigned short *indices,unsigned int count,
    int sky,int decal,int two_sided,const struct native_model_material *material,int relative)
{
    const struct native_model_material *m=material?material:&model_plain_material;
    ++model_material_draws;if(m->multipurpose) ++model_mask_draws;
    if(sky || two_sided) ++model_unculled_draws;else ++model_single_sided_draws;
    n3ds_gpu_world_state_restore();
    if(sky) n3ds_gpu_opaque_sky_begin();
    else if(relative) C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&current_model_projection);
    C3D_CullFace(sky || two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    if(decal) {
        C3D_DepthTest(!sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
        C3D_CullFace(sky || two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
    }
    model_material_apply(m,decal,0);
    if(m->multipurpose && !n3ds_gpu_texture_bind(1,m->multipurpose)) return 0;
    if(flashlight_power && !sky) {
        /* Software-posed models already contain vertex lighting. Reuse the
         * world-position program and add the same GPU cone to that result. */
        C3D_BindProgram(&environment_program);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,relative?&current_model_projection:&current_projection);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,1,1,0,0);
        C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
        AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
        AttrInfo_AddFixed(a,3);AttrInfo_AddFixed(a,4);AttrInfo_AddFixed(a,5);
        C3D_FixedAttribSet(3,0,0,0,0);C3D_FixedAttribSet(4,0,0,0,1);C3D_FixedAttribSet(5,1,0,0,0);
        native_light_env_bind(&flashlight_env);flashlight_environment_combine(0);
        if(relative) {
            C3D_FVec p=FVec4_New(flashlight_origin[0]-current_camera.position[0],
                flashlight_origin[1]-current_camera.position[1],flashlight_origin[2]-current_camera.position[2],1);
            C3D_LightPosition(&flashlight_plain,&p);
        }
    }
    int result=n3ds_gpu_geometry_draw(v,indices,count);
    if(flashlight_power && !sky && relative) {
        C3D_FVec p=FVec4_New(flashlight_origin[0],flashlight_origin[1],flashlight_origin[2],1);
        C3D_LightPosition(&flashlight_plain,&p);
    }
    n3ds_gpu_world_state_restore();return result;
}
int n3ds_gpu_model_cpu_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n,
    int sky,int decal,int two_sided,const struct native_model_material *m)
{return model_cpu_draw(v,i,n,sky,decal,two_sided,m,0);}
int n3ds_gpu_model_cpu_relative_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n,
    int sky,int decal,int two_sided,const struct native_model_material *m)
{return model_cpu_draw(v,i,n,sky,decal,two_sided,m,1);}
int n3ds_gpu_fragment_tests(void)
{
    static const float normals[][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {1,1,1},{-1,1,1},{1,-1,1},{1,1,-1},{-1,-1,-1},{.3f,.4f,.8660254f},
        {-.3f,-.4f,-.8660254f},{2,4,6},{.001f,0,-1},{0,.001f,-1}};
    const float ambient[3]={.2f,.3f,.4f},diffuse[3]={.6f,.5f,.4f},direction[3]={.3f,.4f,.8660254f};
    const unsigned short indices[6]={0,1,2,0,2,3};
    struct fragment_vertex {float position[3],normal[3],uv[3];} *vertices=linearAlloc(4*sizeof(*vertices));
    unsigned int *pixels=linearAlloc(240*400*4),maximum=0;
    DVLB_s *binary=NULL;shaderProgram_s probe;int ready=0,active=0,ok=0;
    C3D_LightEnv env;C3D_Light light;
    if(!vertices || !pixels) goto done;
    binary=DVLB_ParseFile((u32 *)engine_fragment_shader,sizeof(engine_fragment_shader));if(!binary) goto done;
    shaderProgramInit(&probe);ready=1;
    if(R_FAILED(shaderProgramSetVsh(&probe,&binary->DVLE[0]))) goto done;
    int location=shaderInstanceGetUniformLocation(probe.vertexShader,"projection");if(location<0) goto done;
    C3D_LightEnvInit(&env);
    /* Citro3D material arrays use BGR, unlike its RGB light setter arguments. */
    C3D_Material mtl={.ambient={.4f,.3f,.2f},.diffuse={.4f,.5f,.6f}};
    C3D_LightEnvMaterial(&env,&mtl);C3D_LightEnvAmbient(&env,1,1,1);
    if(C3D_LightInit(&light,&env)<0) goto done;
    C3D_LightDiffuse(&light,1,1,1);
    C3D_FVec light_direction=FVec4_New(direction[0],direction[1],direction[2],0);
    C3D_LightPosition(&light,&light_direction);
    for(unsigned int side=0;side<2;++side) for(unsigned int n=0;n<sizeof(normals)/sizeof(*normals);++n) {
        float length=sqrtf(normals[n][0]*normals[n][0]+normals[n][1]*normals[n][1]+normals[n][2]*normals[n][2]);
        float dot=0;for(int c=0;c<3;++c) dot+=direction[c]*normals[n][c]/length;
        float term=side?fabsf(dot):fmaxf(0,dot);unsigned int expected=255;
        for(int c=0;c<3;++c) {
            /* Citro3D converts material coefficients to integer color fields. */
            float value=(int)(ambient[c]*255)+(int)(diffuse[c]*255)*term;
            expected|=(unsigned int)(fminf(255,value)+.5f)<<(24-8*c);
        }
        for(int i=0;i<4;++i) {
            vertices[i]=(struct fragment_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{0,0,0},{.25f,.75f,1}};
            memcpy(vertices[i].normal,normals[n],sizeof(normals[n]));
        }
        n3ds_gpu_geometry_flush(vertices,4*sizeof(*vertices));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        C3D_BindProgram(&probe);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&current_projection);
        C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
        for(int i=0;i<3;++i) AttrInfo_AddLoader(a,i,GPU_FLOAT,3);
        C3D_LightTwoSideDiffuse(&light,side!=0);native_light_env_bind(&env);
        for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv *tev=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(tev,C3D_RGB,GPU_FRAGMENT_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
        if(!n3ds_gpu_indexed_draw(vertices,indices,6,sizeof(*vertices),3,0x210)) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(unsigned int shift=8;shift<32;shift+=8) {
            unsigned int d=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(d>maximum) maximum=d;
            if(d>3) {char message[180];snprintf(message,sizeof(message),"FAIL: fragment lighting side=%u normal=%u expected=%08x actual=%08x difference=%u",side,n,expected,pixels[y*240+x],d);n3ds_log(message);goto done;}
        }
        native_light_env_bind(NULL);
    }
    ok=1;{char message[160];snprintf(message,sizeof(message),"PASS: native fragment diffuse: 32 axis/slope/south-pole/unnormalized/two-sided GPU vectors, max difference=%u",maximum);n3ds_log(message);}
done:
    native_light_env_bind(NULL);
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    C3D_BindProgram(&program);
    if(ready) shaderProgramFree(&probe);if(binary) DVLB_Free(binary);
    linearFree(vertices);linearFree(pixels);return !ok;
}
int n3ds_gpu_cube_tests(void)
{
    unsigned char rgba[6*8*8*4];unsigned int *readback=linearAlloc(240*400*4);
    struct cube_vertex {float position[3],color[3],direction[3];} *vertices=linearAlloc(4*sizeof(*vertices));
    const unsigned short indices[6]={0,1,2,0,2,3};void *cube=NULL;int active=0,ok=0;
    if(!readback || !vertices) goto done;
    for(unsigned int face=0;face<6;++face) for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
        unsigned char *p=rgba+((face*8+y)*8+x)*4;p[0]=32+x*24;p[1]=16+y*28;p[2]=24+face*36;p[3]=255;
    }
    cube=n3ds_gpu_texture_create_cube(rgba,8);if(!cube) goto done;
    if(n3ds_gpu_texture_bind(1,cube) || n3ds_gpu_texture_bind(2,cube)) goto done;
    for(unsigned int face=0;face<6;++face) for(unsigned int point=0;point<5;++point) {
        float u=point==4?0:point&1?.5f:-.5f,v=point==4?0:point&2?.5f:-.5f;
        float directions[6][3]={{1,-v,-u},{-1,-v,u},{u,1,v},{u,-1,-v},{u,-v,1},{-u,-v,-1}};
        for(unsigned int i=0;i<4;++i) {
            vertices[i]=(struct cube_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{1,1,1},{0,0,0}};
            memcpy(vertices[i].direction,directions[face],3*sizeof(float));
        }
        n3ds_gpu_geometry_flush(vertices,4*sizeof(*vertices));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!n3ds_gpu_texture_bind(0,cube)) goto done;
        C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
        for(int i=0;i<3;++i) AttrInfo_AddLoader(a,i,GPU_FLOAT,3);
        if(!n3ds_gpu_indexed_draw(vertices,indices,6,sizeof(*vertices),3,0x210)) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(readback,240*400)) goto done;
        unsigned int expected=((unsigned int)(32+(u*.5f+.5f)*8*24-12)<<24) |
            ((unsigned int)(16+(v*.5f+.5f)*8*28-14)<<16) | ((24+face*36)<<8) | 255;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) {
            unsigned int actual=readback[y*240+x];
            for(unsigned int shift=8;shift<32;shift+=8) if(abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255))>2) {
                char message[160];snprintf(message,sizeof(message),"FAIL: native cube face=%u point=%u expected=%08x actual=%08x",face,point,expected,actual);n3ds_log(message);goto done;
            }
        }
    }
    ok=1;n3ds_log("PASS: native PICA cube sampler: six Xbox faces, 30 axis/off-axis gradient vectors and unit restrictions");
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(cube);linearFree(vertices);linearFree(readback);return !ok;
}
/* Compare retained-state draws with independent two-sided reference images.
 * Clockwise Xbox fronts are tested separately from reversed and mirrored faces. */
static int model_culling_tests(void)
{
    struct native_render_vertex *rigid=linearAlloc(4*sizeof(*rigid));
    struct native_skin_vertex *skin=linearAlloc(4*sizeof(*skin));
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),texels[64];
    const unsigned short indices[2][6]={{0,2,1,0,3,2},{0,1,2,0,2,3}};
    const float uv[2]={1,1};void *texture=NULL;int active=0,ok=0;
    if(!rigid || !skin || !reference || !actual) goto done;
    for(int i=0;i<64;++i) texels[i]=0x80a0c080;
    texture=n3ds_gpu_texture_create(texels,8,8);if(!texture) goto done;
    for(unsigned int test=0;test<96;++test) {
        unsigned int path=test%3,bits=test/3;
        int reverse=bits&1,two_sided=(bits>>1)&1,decal=(bits>>2)&1,sky=(bits>>3)&1,mirror=(bits>>4)&1;
        float rows[24]={1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,0};
        rows[0]=rows[12]=mirror?-1:1;
        for(int i=0;i<4;++i) {
            float x=i==1 || i==2?.8f:-.8f,y=i>=2?.8f:-.8f;
            rigid[i]=(struct native_render_vertex){{path==2?x*rows[0]:x,y,-2},{path==2?1:0,path==2?1:0,1},{.5f,.5f}};
            memcpy(skin+i,rigid+i,sizeof(*rigid));
            skin[i].bones[0]=0;skin[i].bones[1]=3;skin[i].bones[2]=(i&1)?1.f:.35f;skin[i].bones[3]=(i&1)?0.f:.65f;
        }
        n3ds_gpu_geometry_flush(rigid,4*sizeof(*rigid));n3ds_gpu_geometry_flush(skin,4*sizeof(*skin));
        for(int pass=0;pass<2;++pass) {
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
            if(!n3ds_gpu_texture_bind(0,texture)) goto done;
            if(pass && path!=2) {
                float offscreen[24];memcpy(offscreen,rows,sizeof(rows));offscreen[3]=offscreen[15]=100;
                n3ds_gpu_model_batch_begin();
                int prior=path==1?n3ds_gpu_model_skin_draw(skin,indices[reverse],6,offscreen,2,uv,sky,decal,!two_sided,NULL):
                    n3ds_gpu_model_rigid_draw(rigid,indices[reverse],6,offscreen,uv,sky,decal,!two_sided,NULL);
                if(!prior) goto done;
            }
            int sides=pass?two_sided:1;
            int drawn=path==2?n3ds_gpu_model_cpu_draw(rigid,indices[reverse],6,sky,decal,sides,NULL):
                path==1?n3ds_gpu_model_skin_draw(skin,indices[reverse],6,rows,2,uv,sky,decal,sides,NULL):
                n3ds_gpu_model_rigid_draw(rigid,indices[reverse],6,rows,uv,sky,decal,sides,NULL);
            if(model_batch_active) n3ds_gpu_model_batch_end();
            if(!drawn) goto done;
            n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pass?actual:reference,240*400)) goto done;
        }
        unsigned int covered=0,changed=0;
        int visible=sky || two_sided || !(reverse^mirror);
        for(unsigned int i=0;i<240*400;++i) {
            covered+=(reference[i]&0xffffff00u)!=0x10203000u;
            unsigned int expected=visible?reference[i]:0x102030ff;
            changed+=(actual[i]&0xffffff00u)!=(expected&0xffffff00u);
        }
        if(covered<1000 || changed) {
            char message[192];snprintf(message,sizeof(message),"FAIL: model culling test=%u path=%u visible=%d covered=%u changed=%u",test,path,visible,covered,changed);n3ds_log(message);goto done;
        }
    }
    n3ds_log("PASS: model culling:96 GPU references,clockwise/reversed/mirrored,rigid/weighted/CPU,two-sided/sky/decal,retained state transitions");ok=1;
done:
    if(model_batch_active) n3ds_gpu_model_batch_end();
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(texture);linearFree(rigid);linearFree(skin);linearFree(reference);linearFree(actual);return !ok;
}
int n3ds_gpu_model_material_tests(void)
{
    unsigned int *readback=linearAlloc(240*400*4),pixels[64];
    struct native_skin_vertex *vertices=linearAlloc(4*sizeof(*vertices));
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float rows[12]={1,0,0,0,0,1,0,0,0,0,1,0},scale[2]={1,1};
    void *base=NULL,*mask=NULL;int active=0,ok=0;unsigned int worst=0;
    if(!readback || !vertices) goto done;
    for(unsigned int i=0;i<64;++i) pixels[i]=0x804020ff;
    base=n3ds_gpu_texture_create(pixels,8,8);mask=n3ds_gpu_texture_create(pixels,8,8);
    if(!base || !mask) goto done;
    for(unsigned int test=0;test<13;++test) {
        /* Distinct channel vectors make PC/Xbox mask swaps observable. */
        static const unsigned int vectors[8]={0xff0000ff,0x00ff00ff,0x0000ffff,0x004080ff,0x000000ff,0xffffffff,0x20a060ff,0x000000ff};
        unsigned int sample=vectors[test%8];
        unsigned int base_pixel=test==10?0x80402040:test==11?0x80402080:0x804020ff;
        struct native_model_material m={test==7 || test==12?NULL:mask,0xff604020,0xff2080c0,test==10 || test==12};
        if(test==5) m.emission=0xffffffff;
        for(unsigned int i=0;i<64;++i) pixels[i]=base_pixel;
        C3D_TexLoadImage(base,pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(base);
        for(unsigned int i=0;i<64;++i) pixels[i]=sample;
        C3D_TexLoadImage(mask,pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(mask);
        float light[3]={.25f,.5f,.75f};
        if(test==8 || test==9) {
            float value=.3f+.7f*.8660254f;light[0]=value*.72f;light[1]=value*.82f;light[2]=value*.9f;
        }
        for(unsigned int i=0;i<4;++i) {
            float x=i==1 || i==2?.8f:-.8f,y=i>=2?.8f:-.8f;
            if(test==9) vertices[i]=(struct native_skin_vertex){{x,y,-2},{0,0,1},{.3125f,.6875f},{0,0,1,0}};
            else ((struct native_render_vertex *)vertices)[i]=(struct native_render_vertex){{x,y,-2},
                {test==8?0:light[0],test==8?0:light[1],test==8?1:light[2]},{.3125f,.6875f}};
        }
        n3ds_gpu_geometry_flush(vertices,4*sizeof(*vertices));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!n3ds_gpu_texture_bind(0,base)) goto done;
        int drawn=test==9?n3ds_gpu_model_skin_draw(vertices,indices,6,rows,1,scale,0,0,1,&m):
            test==8?n3ds_gpu_model_rigid_draw((const void *)vertices,indices,6,rows,scale,0,0,1,&m):
            n3ds_gpu_model_cpu_draw((const void *)vertices,indices,6,0,test==11,1,&m);
        if(!drawn) goto done;n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        unsigned int expected=0xff;
        for(unsigned int channel=0;channel<3;++channel) {
            float value=light[channel];
            if(m.multipurpose) {
                float emission=((m.emission>>(channel*8))&255)/255.f;
                float tint=((m.tint>>(channel*8))&255)/255.f;
                float green=((sample>>16)&255)/255.f,blue=((sample>>8)&255)/255.f;
                value=fminf(1.f,value+green*emission)*(1-blue+blue*tint);
            }
            value*=((base_pixel>>(24-channel*8))&255)/255.f;
            if(test==11) value=value*(128/255.f)+((0x102030ff>>(24-channel*8))&255)/255.f*(127/255.f);
            expected|=(unsigned int)(value*255.f+.5f)<<(24-channel*8);
        }
        if(test==10) expected=0x102030ff;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) {
            unsigned int actual=readback[y*240+x];
            for(unsigned int shift=8;shift<32;shift+=8) {
                unsigned int delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
                if(delta>worst) worst=delta;
                if(delta>3) {char message[160];snprintf(message,sizeof(message),"FAIL: model material GPU test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;}
            }
        }
    }
    ok=1;{char message[192];snprintf(message,sizeof(message),"PASS: model material GPU: 13 scalar-reference vectors, Xbox mask channels, saturation, tint/emission, CPU/rigid/skinned, alpha/decal; max difference=%u",worst);n3ds_log(message);}
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(base);n3ds_gpu_texture_destroy(mask);linearFree(readback);linearFree(vertices);return ok?model_culling_tests():1;
}
int n3ds_gpu_model_transform_tests(void)
{
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),texels[64];
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-.8f,-.7f},{.8f,-.7f},{.8f,.7f},{-.8f,.7f}};
    void *texture=NULL;int active=0,result=1;unsigned int worst=0,edge_total=0;char message[192];
#define TRANSFORM_CHECK(e) do {if(!(e)){snprintf(message,sizeof(message),"MODEL TRANSFORM GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    TRANSFORM_CHECK(reference && actual);
    for(unsigned int i=0;i<64;++i) texels[i]=((96+(i%8)*16)<<24)|((96+(i/8)*16)<<16)|0xb080;
    texture=n3ds_gpu_texture_create(texels,8,8);TRANSFORM_CHECK(texture);
    for(unsigned int iteration=0;iteration<48;++iteration) {
        unsigned int test=iteration%16,weight_case=iteration/16;
        int skinned=test>=8?(test&1):test>=2,sky=test==5,decal=test==6;
        int depth=test>=8 && test<12,cloak=test>=12;
        unsigned int second=test==3?NATIVE_GPU_MODEL_BONES-1:1,nodes=skinned?second+1:1;
        float rows[NATIVE_GPU_MODEL_BONES*12]={0},uv[2]={.75f,1.25f};
        for(unsigned int n=0;n<nodes;++n) {
            float angle=test?(n==second?-.23f:.37f):0,scale=test?(n==second?1.2f:.8f):1;
            float c=cosf(angle)*scale,s=sinf(angle)*scale;
            float m[12]={c,-s,0,n==second?.12f:-.08f,s,c,0,.06f,0,0,scale,0};memcpy(rows+n*12,m,sizeof(m));
        }
        struct native_render_vertex cpu[4],rigid[4];struct native_skin_vertex skin[4];
        for(int i=0;i<4;++i) {
            float p[3]={xy[i][0],xy[i][1],-3},normal[3]={.3f+i*.1f,.4f,.8f},world[3]={0},normal_world[3]={0};
            float weight[2]={skinned?(weight_case==1?1.f:weight_case==2?0.f:test==4?0.f:.35f):1.f,skinned?(weight_case==1?0.f:weight_case==2?1.f:test==4?1.f:.65f):0.f};
            rigid[i]=(struct native_render_vertex){{p[0],p[1],p[2]},{normal[0],normal[1],normal[2]},{i==1 || i==2?.8f:.2f,i>=2?.8f:.2f}};
            memcpy(&skin[i],&rigid[i],sizeof(rigid[i]));skin[i].bones[0]=0;skin[i].bones[1]=second*3;skin[i].bones[2]=weight[0];skin[i].bones[3]=weight[1];
            for(int k=0;k<2;++k) if(weight[k]) for(int axis=0;axis<3;++axis) {
                const float *r=rows+(k?second:0)*12+axis*4;
                world[axis]+=(r[0]*p[0]+r[1]*p[1]+r[2]*p[2]+r[3])*weight[k];
                normal_world[axis]+=(r[0]*normal[0]+r[1]*normal[1]+r[2]*normal[2])*weight[k];
            }
            float length=sqrtf(normal_world[0]*normal_world[0]+normal_world[1]*normal_world[1]+normal_world[2]*normal_world[2]);
            float light=.3f+.7f*fabsf((normal_world[0]*.3f+normal_world[1]*.4f+normal_world[2]*.8660254f)/length);
            cpu[i]=(struct native_render_vertex){{world[0],world[1],world[2]},{sky?1:light*.72f,sky?1:light*.82f,sky?1:light*.9f},{rigid[i].uv[0]*uv[0],rigid[i].uv[1]*uv[1]}};
        }
        for(int pass=0;pass<2;++pass) {
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            TRANSFORM_CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);TRANSFORM_CHECK(n3ds_gpu_texture_bind(0,texture));
            unsigned int bytes=pass && skinned?sizeof(skin):sizeof(cpu);
            void *v=n3ds_gpu_model_vertices_allocate((bytes+31)/32);TRANSFORM_CHECK(v);
            memcpy(v,pass?(skinned?(const void *)skin:(const void *)rigid):(const void *)cpu,bytes);n3ds_gpu_geometry_flush(v,bytes);
            if(depth) {
                if(pass) TRANSFORM_CHECK(n3ds_gpu_model_depth_transform_draw(v,indices,6,rows,nodes,skinned));
                else TRANSFORM_CHECK(n3ds_gpu_model_depth_draw(v,indices,6));
                struct native_render_vertex *back=n3ds_gpu_model_vertices_allocate(4);TRANSFORM_CHECK(back);
                for(int k=0;k<4;++k) back[k]=(struct native_render_vertex){{xy[k][0]*4,xy[k][1]*4,-5},{1,1,1},{.5f,.5f}};
                TRANSFORM_CHECK(n3ds_gpu_geometry_draw(back,indices,6));
            } else if(cloak) {
                int textured=test<14;float fade=.27f;
                if(pass) TRANSFORM_CHECK(n3ds_gpu_model_cloak_transform_draw(v,indices,6,rows,nodes,skinned,uv,fade,textured,1));
                else {
                    struct native_chicago_vertex *out=n3ds_gpu_chicago_allocate(4);TRANSFORM_CHECK(out);
                    for(int k=0;k<4;++k) {memset(out+k,0,sizeof(*out));memcpy(out[k].position,cpu[k].position,12);memcpy(out[k].uv[0],cpu[k].uv,8);memcpy(out[k].color,cpu[k].color,12);out[k].color[3]=1;}
                    struct native_chicago_material m={.maps=1,.blend=0,.two_sided=1,.fade=fade,.vertex_color=!textured};
                    TRANSFORM_CHECK(n3ds_gpu_chicago_draw(&m,out,4,indices,6));
                }
            } else if(pass) {
                if(skinned) TRANSFORM_CHECK(n3ds_gpu_model_skin_draw(v,indices,6,rows,nodes,uv,sky,decal,1,NULL));
                else TRANSFORM_CHECK(n3ds_gpu_model_rigid_draw(v,indices,6,rows,uv,sky,decal,1,NULL));
            } else if(decal) TRANSFORM_CHECK(n3ds_gpu_model_decal_draw(v,indices,6,sky,1));
            else {if(sky) n3ds_gpu_opaque_sky_begin();TRANSFORM_CHECK(n3ds_gpu_geometry_draw(v,indices,6));n3ds_gpu_world_state_restore();}
            n3ds_gpu_present();active=0;TRANSFORM_CHECK(n3ds_gpu_readback(pass?actual:reference,240*400));
        }
        unsigned int covered=0,edges=0,bad=0,max_delta=0;
        for(unsigned int i=0;i<240*400;++i) {
            int a=reference[i]!=0x102030ff,b=actual[i]!=0x102030ff;if(a) ++covered;
            if(a!=b) {++edges;continue;}
            for(unsigned int shift=0;shift<32;shift+=8) {
                unsigned int delta=abs((int)((reference[i]>>shift)&255)-(int)((actual[i]>>shift)&255));
                if(delta>max_delta) max_delta=delta;if(delta>2) ++bad;
            }
        }
        if(max_delta>worst) worst=max_delta;edge_total+=edges;
        if(covered<1000 || edges>64 || bad>32) {snprintf(message,sizeof(message),"MODEL TRANSFORM GPU vector=%u covered=%u edges=%u bad=%u difference=%u",test,covered,edges,bad,max_delta);n3ds_log(message);goto done;}
    }
    snprintf(message,sizeof(message),"PASS: model GPU transforms: 48 CPU-reference pixel comparisons, rigid, weighted bones, palette28, zero weight, UV, sky, decal, depth and cloak; max difference=%u edge pixels=%u",worst,edge_total);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(reference);linearFree(actual);return result;
#undef TRANSFORM_CHECK
}
int n3ds_gpu_model_batch_tests(void)
{
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),texels[64];
    void *texture=NULL,*mask=NULL,*cube=NULL;int active=0,ok=0;
    const unsigned short indices[6]={0,2,1,0,3,2};
    float usage[2]={0,0};
    unsigned long long old_state=model_state_reuses,old_palette=model_palette_reuses;
    if(!reference || !actual) goto done;
    for(unsigned int i=0;i<64;++i) texels[i]=0x6090c080u+i*0x02010000u;
    texture=n3ds_gpu_texture_create(texels,8,8);if(!texture) goto done;
    for(unsigned int i=0;i<64;++i) texels[i]=0x4080c0ff;
    mask=n3ds_gpu_texture_create(texels,8,8);if(!mask) goto done;
    unsigned char faces[6*8*8*4];
    for(unsigned int i=0;i<sizeof(faces);++i) faces[i]=(i*17+i/256*23)&255;
    cube=n3ds_gpu_texture_create_cube(faces,8);if(!cube) goto done;
    for(int pass=0;pass<2;++pass) {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;
        active=1;n3ds_gpu_window_begin(&camera);
        struct native_render_vertex *rigid=n3ds_gpu_model_vertices_allocate(4);
        struct native_skin_vertex *skin=(void *)n3ds_gpu_model_vertices_allocate(6);
        if(!rigid || !skin) goto done;
        for(unsigned int i=0;i<4;++i) {
            rigid[i]=(struct native_render_vertex){{i==1 || i==2?.25f:-.25f,i>=2?.25f:-.25f,-3},{0,0,1},{i==1 || i==2?1:0,i>=2?1:0}};
            memcpy(skin+i,rigid+i,sizeof(*rigid));
            skin[i].bones[0]=0;skin[i].bones[1]=3;skin[i].bones[2]=.4f;skin[i].bones[3]=.6f;
        }
        const unsigned short *draw_indices=pass?n3ds_gpu_indices_upload(indices,6):indices;
        if(!draw_indices) goto done;
        if(pass) n3ds_gpu_model_batch_begin();
        for(unsigned int draw=0;draw<32;++draw) {
            unsigned int group=draw/4;
            int skinned=group&1,sky=group==2,decal=group==3 || group==7;
            float rows[24]={1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,0};
            float uv[2]={draw%4==3?.7f:1.f,1.f};
            rows[3]=rows[15]=((int)(group%4)-1.5f)*.55f;
            rows[7]=rows[19]=group>=4?.4f:-.4f;
            if(draw%4==2) rows[3]+=.08f; /* same-address palette mutation */
            if(draw==16) {camera.suppress_clear=1;camera.position[0]=.07f;n3ds_gpu_window_begin(&camera);}
            if(draw==20 && !n3ds_gpu_first_person_begin(.05f,50)) goto done;
            if(draw==24) n3ds_gpu_first_person_end();
            if(!n3ds_gpu_texture_bind(0,texture)) goto done;
            struct native_model_material m={draw%4?mask:NULL,0xff204080,0xff804020u+draw*0x010101u,(int)(draw&1)};
            struct native_model_lighting lighting={.ambient={.2f,.3f,.4f},.distant={
                {.color={.6f,.5f,.4f},.direction={.3f,.4f,.8660254f}},
                {.color={.4f,.1f,.3f},.direction={-.6f,.8f,0}}},.count=draw%3};
            /* Include live lighting, fallback, sky, changing light counts and
             * same-address packet mutation in the retained-state reference. */
            if(draw%4!=3) m.lighting=&lighting;
            m.base=texture;m.translucency=(draw%3)*.4f;
            if(draw%4==1 || (group>=4 && draw%4==2)) {
                m.reflection=cube;m.base=texture;
                for(int c=0;c<4;++c) {m.reflection_delta[c]=.25f;m.reflection_parallel[c]=.2f+draw*.01f;}
            }
            int result=skinned?n3ds_gpu_model_skin_draw(skin,draw_indices,6,rows,2,uv,sky,decal,!!(draw&2),&m):
                n3ds_gpu_model_rigid_draw(rigid,draw_indices,6,rows,uv,sky,decal,!!(draw&2),&m);
            if(!result) goto done;
            if(draw==10) {n3ds_gpu_model_batch_flush();n3ds_gpu_world_state_restore();}
        }
        if(pass) n3ds_gpu_model_batch_end();
        usage[pass]=n3ds_gpu_command_usage();
        n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(pass?actual:reference,240*400)) goto done;
    }
    unsigned int changed=0,covered=0;
    for(unsigned int i=0;i<240*400;++i) {changed+=reference[i]!=actual[i];covered+=reference[i]!=0x102030ff;}
    ok=!changed && covered>1000 && usage[1]<usage[0] && model_state_reuses>old_state && model_palette_reuses>old_palette;
    char message[256];snprintf(message,sizeof(message),"%s: model batch pixel comparison: changed=%u covered=%u command_ratio=%.3f state_reuses=%llu palette_reuses=%llu; rigid/skinned/reflection/sky/decal/view/weapon/mutated rows",ok?"PASS":"FAIL",changed,covered,usage[1]/usage[0],model_state_reuses-old_state,model_palette_reuses-old_palette);n3ds_log(message);
done:
    if(model_batch_active) n3ds_gpu_model_batch_end();
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(cube);n3ds_gpu_texture_destroy(texture);n3ds_gpu_texture_destroy(mask);linearFree(reference);linearFree(actual);return !ok;
}
/* Depth and cloak use the same immutable palettes as opaque models. No posed
 * vertex copy, CPU normal transform or second staging allocation is needed. */
static int model_special_transform(const void *v,const unsigned short *indices,unsigned int count,
    const float *rows,unsigned int nodes,int skinned,const float *uv,int depth,float fade,int textured,int two_sided)
{
    if(!frame_active || !v || !rows || !nodes || nodes>NATIVE_GPU_MODEL_BONES ||
       (!depth && (!uv || !isfinite(fade) || fade<0 || fade>1))) return 0;
    n3ds_gpu_world_state_restore();
    C3D_BindProgram(depth?&depth_programs[skinned].program:skinned?&skin_program:&rigid_program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,depth?depth_programs[skinned].projection:skinned?skin_projection:rigid_projection,&current_model_projection);
    int loc=depth?depth_programs[skinned].bone:skinned?skin_bone:rigid_bone;
    for(unsigned int i=0;i<nodes*3;++i) C3D_FVUnifSet(GPU_VERTEX_SHADER,loc+i,rows[i*4],rows[i*4+1],rows[i*4+2],rows[i*4+3]-current_camera.position[i%3]);
    if(!depth) C3D_FVUnifSet(GPU_VERTEX_SHADER,skinned?skin_options:rigid_options,uv?uv[0]:1,uv?uv[1]:1,0,0);
    C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
    AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
    if(skinned) AttrInfo_AddLoader(a,3,GPU_FLOAT,4);
    C3D_CullFace(depth || two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(true,GPU_GEQUAL,depth?GPU_WRITE_DEPTH:GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    if(depth) {
        C3D_TexEnv *env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        C3D_StencilTest(true,GPU_EQUAL,0,1,0);
        C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    } else {
        n3ds_gpu_model_stencil_apply();n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
        C3D_TexEnv *env=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env,C3D_Both,textured?GPU_TEXTURE0:GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        env=C3D_GetTexEnv(1);C3D_TexEnvColor(env,((unsigned int)(fade*255.f+.5f))*0x01010101u);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);
    }
    int result=n3ds_gpu_indexed_draw(v,indices,count,skinned?sizeof(struct native_skin_vertex):sizeof(struct native_render_vertex),skinned?4:3,skinned?0x3210:0x210);
    n3ds_gpu_world_state_restore();return result;
}
int n3ds_gpu_model_depth_transform_draw(const void *v,const unsigned short *indices,unsigned int count,
    const float *rows,unsigned int nodes,int skinned)
{return model_special_transform(v,indices,count,rows,nodes,skinned,NULL,1,1,0,1);}
int n3ds_gpu_model_cloak_transform_draw(const void *v,const unsigned short *indices,unsigned int count,
    const float *rows,unsigned int nodes,int skinned,const float *uv,float fade,int textured,int two_sided)
{return model_special_transform(v,indices,count,rows,nodes,skinned,uv,0,fade,textured,two_sided);}
static int model_depth_draw(const struct native_render_vertex *vertices,const unsigned short *indices,unsigned int count,int relative)
{
    if(!frame_active) return 0;
    n3ds_gpu_world_state_restore();
    if(relative) C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&current_model_projection);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_DEPTH);
    C3D_StencilTest(true,GPU_EQUAL,0,1,0);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    int result=n3ds_gpu_geometry_draw(vertices,indices,count);
    n3ds_gpu_world_state_restore();return result;
}
int n3ds_gpu_model_depth_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n)
{return model_depth_draw(v,i,n,0);}
int n3ds_gpu_model_depth_relative_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n)
{return model_depth_draw(v,i,n,1);}
int n3ds_gpu_model_decal_draw(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count,int sky,int two_sided)
{
    if(!frame_active || !n3ds_gpu_texture_bound(0)) return 0;
    /* Keep the bounded native model lighting in RGB, while the base-map alpha
     * controls original SRCALPHA/INVSRCALPHA blending. Decals never write Z or A.
     * Exact Xbox Z bias and reflection/detail passes remain fidelity limits. */
    if(sky) n3ds_gpu_opaque_sky_begin();
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    C3D_DepthTest(!sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    C3D_CullFace(two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
    int result=n3ds_gpu_geometry_draw(vertices,indices,count);
    n3ds_gpu_world_state_restore();return result;
}
int n3ds_gpu_glass_sky_draw(const struct native_render_vertex *vertices,const unsigned short *indices,
    unsigned int count,int pass,int two_sided,int sky)
{
    if(!frame_active || pass<0 || pass>2) return 0;
    n3ds_gpu_world_state_restore();
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    if(pass==2) {
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    }
    if(sky)n3ds_gpu_opaque_sky_begin();
    C3D_DepthTest(!sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    C3D_CullFace(two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    C3D_AlphaTest(pass==2,GPU_GREATER,0);
    n3ds_gpu_model_stencil_apply();
    n3ds_gpu_framebuffer_blend(pass==0?NATIVE_BLEND_MULTIPLY:pass==1?NATIVE_BLEND_ADD:NATIVE_BLEND_ALPHA);
    int result=n3ds_gpu_geometry_draw(vertices,indices,count);n3ds_gpu_world_state_restore();return result;
}
int n3ds_gpu_glass_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n,int pass,int sides)
{return n3ds_gpu_glass_sky_draw(v,i,n,pass,sides,0);}
static int indexed_draw_resident(const void *,const unsigned short *,unsigned int,unsigned int,unsigned int,unsigned int);
int n3ds_gpu_environment_bump_draw(const struct native_render_vertex *vertices,
    const struct native_environment_bump_vertex *bump,const unsigned short *indices,unsigned int count,
    float u_scale,float v_scale,unsigned int material,int alpha_test)
{
    if(!frame_active || !environment_ready || !vertices || !bump || !indices || ((uintptr_t)indices&3) ||
       !count || count%3 || !isfinite(u_scale) || !isfinite(v_scale)) return 0;
    if(!n3ds_gpu_texture_wrap(1,1,1)) return 0;
    if(plasma_state_valid) {plasma_state_valid=0;bump_state_valid=0;}
    if(!bump_state_valid) {
        environment_state_valid=0;
        if(!environment_program_valid) {
            C3D_BindProgram(&environment_program);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,&current_projection);
        /* Xbox BSP triangles use clockwise fronts after this projection. */
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);C3D_CullFace(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            environment_program_valid=1;
        }
        C3D_SetAttrInfo(&environment_attributes[1]);
        n3ds_gpu_model_stencil_apply();native_light_env_bind(&bump_light_env);
        for(int i=0;i<6;++i) C3D_SetTexEnv(i,&environment_recipes[ENVIRONMENT_BUMP_RECIPE][i]);
        flashlight_environment_combine(0);
        C3D_TexEnvBufUpdate(C3D_RGB,1);C3D_TexEnvBufColor(0);
    }
    if(!bump_state_valid || material!=bump_material) {C3D_TexEnvColor(C3D_GetTexEnv(3),material);bump_material=material;}
    if(!bump_state_valid || !!alpha_test!=bump_alpha) {C3D_AlphaTest(!!alpha_test,GPU_GREATER,127);bump_alpha=!!alpha_test;}
    if(!bump_state_valid || bump_scale[0]!=u_scale || bump_scale[1]!=v_scale) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,u_scale,v_scale,0,0);bump_scale[0]=u_scale;bump_scale[1]=v_scale;
    }
    bump_state_valid=1;
    bound_vertex_valid=0;
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    if(BufInfo_Add(buffers,vertices,sizeof(*vertices),3,0x210)<0 || BufInfo_Add(buffers,bump,sizeof(*bump),3,0x543)<0) return 0;
    for(unsigned int i=0;i<count;i+=30000) C3D_DrawElements(GPU_TRIANGLES,count-i>30000?30000:count-i,C3D_UNSIGNED_SHORT,indices+i);
    return 1;
}
int n3ds_gpu_environment_bump_tests(void)
{
    struct native_render_vertex *v=linearAlloc(4*sizeof(*v));
    struct native_environment_bump_vertex *b=linearAlloc(4*sizeof(*b));
    float *plain_uv=linearAlloc(8*sizeof(float));
    unsigned short *indices=linearAlloc(12);unsigned int *pixels=linearAlloc(240*400*4),texels[64],worst=0;
    void *textures[3]={0};int active=0,ok=0;
    /* Match original Xbox BSP clockwise-front index order. */
    const unsigned short quad[6]={0,2,1,0,3,2};
    const float incident[8][3]={{0,0,1},{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},{0,0,-1},{.6f,0,.8f},{.3f,.4f,.8660254f}};
    const unsigned int normals[8]={0x8080ffff,0xff8080ff,0x80ff80ff,0x008080ff,0x800080ff,0x808000ff,0xcc80e6ff,0xa6b3eeff};
    if(!v || !b || !plain_uv || !indices || !pixels) goto done;
    for(int i=0;i<8;++i) plain_uv[i]=.5f;
    n3ds_gpu_geometry_flush(plain_uv,8*sizeof(float));
    memcpy(indices,quad,12);n3ds_gpu_geometry_flush(indices,12);
    for(int t=0;t<3;++t) {
        for(int i=0;i<64;++i) texels[i]=t==0?0x804020ff:t==1?0xc08040ff:0x8080ffff;
        textures[t]=n3ds_gpu_texture_create(texels,8,8);if(!textures[t]) goto done;
    }
    for(unsigned int test=0;test<200;++test) {
        int cutout=test>=192,li=test%8,ni=(test/8)%8;
        float weight=cutout?1:(test/64)*.5f;
        unsigned int normal=normals[ni];float q[4]={incident[li][0],incident[li][1],1+incident[li][2],0};
        float len=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
        if(len>1e-8f) for(int a=0;a<3;++a) q[a]/=len;else {q[0]=1;q[1]=q[2]=0;}
        float dot=0;for(int a=0;a<3;++a) dot+=incident[li][a]*(2.f*((normal>>(24-8*a))&255)/255.f-1);
        float intensity=1-weight+weight*fminf(1,fmaxf(0,dot));unsigned int expected=255;
        unsigned int mat=0xffccb399;
        for(int a=0;a<3;++a) {
            float value=((0x804020ffu>>(24-8*a))&255)/255.f*((0xc08040ffu>>(24-8*a))&255)/255.f;
            value*=intensity*((mat>>(8*a))&255)/255.f;
            expected|=(unsigned int)(value*255+.5f)<<(24-8*a);
        }
        if(cutout) expected=0x102030ff;
        for(int i=0;i<64;++i) texels[i]=normal;
        C3D_TexLoadImage(textures[2],texels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[2]);
        if(test==192) {for(int i=0;i<64;++i) texels[i]=0x80402040;C3D_TexLoadImage(textures[0],texels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[0]);}
        for(int i=0;i<4;++i) {
            v[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{1,1,1},{.25f,.75f}};
            b[i]=(struct native_environment_bump_vertex){{.75f,.25f},{0},weight};memcpy(b[i].quaternion,q,sizeof(q));
        }
        n3ds_gpu_geometry_flush(v,4*sizeof(*v));n3ds_gpu_geometry_flush(b,4*sizeof(*b));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        /* Exercise shared-program attribute and uniform changes within one
         * frame. The final equal-depth draw must replace the earlier color. */
        if(test%8==0 && !n3ds_gpu_environment_bump_draw(v,b,indices,6,3,2,mat^0x00ffffffu,cutout)) goto done;
        if(test%16==0 && !n3ds_gpu_environment_draw(v,plain_uv,indices,6,1,test%32?2:3,2,3,0xff375f9bu,cutout)) goto done;
        if(test%32==0) {
            struct native_environment_emission emission={.color={0xff402010u,0xff103020u,0xff201040u},.uv={2,3,.25f,.5f}};
            if(!n3ds_gpu_environment_emission_draw(v,plain_uv,indices,6,1,mat,cutout,&emission)) goto done;
        }
        if(!n3ds_gpu_environment_bump_draw(v,b,indices,6,1,1,mat,cutout)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int s=0;s<32;s+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>s)&255)-(int)((expected>>s)&255));if(delta>worst) worst=delta;
            if(delta>3) {char line[192];snprintf(line,sizeof(line),"FAIL: environment bump test=%u expected=%08x actual=%08x delta=%u",test,expected,pixels[y*240+x],delta);n3ds_log(line);goto done;}
        }
    }
    ok=1;{char line[256];snprintf(line,sizeof(line),"PASS: environment bump:200 GPU references,incident/bump directions,radiosity weights,material color and alpha; same-frame plain/bump/material transitions; max difference=%u",worst);n3ds_log(line);}
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    for(int i=0;i<3;++i) n3ds_gpu_texture_destroy(textures[i]);
    linearFree(v);linearFree(b);linearFree(plain_uv);linearFree(indices);linearFree(pixels);return !ok;
}

int n3ds_gpu_environment_draw(const struct native_render_vertex *vertices,const float *light_uv,
    const unsigned short *indices,unsigned int count,int lightmap,int detail,float detail_u_scale,float detail_v_scale,unsigned int material,int alpha_test)
{
    if(!frame_active || !environment_ready || !vertices || !light_uv || !indices || !count || count%3 ||
       ((uintptr_t)indices&3) || detail<0 || detail>3 || !isfinite(detail_u_scale) || !isfinite(detail_v_scale)) return 0;
    if(bump_state_valid || plasma_state_valid) {native_light_env_bind(NULL);bump_state_valid=plasma_state_valid=0;environment_state_valid=0;}
    int key=!!lightmap | (detail<<1) | (!!alpha_test<<3);
    if(lightmap && !n3ds_gpu_texture_wrap(1,1,1)) return 0;
    if(!environment_state_valid) {
        if(!environment_program_valid) {
            C3D_BindProgram(&environment_program);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,&current_projection);
        /* Xbox BSP triangles use clockwise fronts after this projection. */
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);C3D_CullFace(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            environment_program_valid=1;
        }
        C3D_SetAttrInfo(&environment_attributes[0]);
        C3D_FixedAttribSet(4,0,0,0,1);C3D_FixedAttribSet(5,1,0,0,0);
        n3ds_gpu_model_stencil_apply();
    }
    if(!environment_state_valid || ((key^environment_key)&23)) {
        for(int i=0;i<6;++i) C3D_SetTexEnv(i,&environment_recipes[key&7][i]);
        native_light_env_bind(flashlight_power?&flashlight_env:NULL);flashlight_environment_combine(0);
        C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    }
    /* The Xbox lightmap pass applies diffuse.material_color to lighting.
     * Keep the native albedo/detail clamp before lighting; this final product
     * differs only by intermediate framebuffer/TEV quantization. Alpha stays
     * authored base alpha. White materials retain the existing pass-through.
     * Stage3 is unused by all four plain/detail recipes; no extra draw/sampler. */
    material|=0xff000000u;
    if(!environment_state_valid || ((key^environment_key)&23) || material!=environment_material) {
        C3D_TexEnv *tint=C3D_GetTexEnv(3);C3D_TexEnvInit(tint);
        if(material!=0xffffffffu) {
            C3D_TexEnvSrc(tint,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
            C3D_TexEnvFunc(tint,C3D_RGB,GPU_MODULATE);C3D_TexEnvColor(tint,material);
        }
        environment_material=material;
    }
    if(!environment_state_valid || ((key^environment_key)&8)) C3D_AlphaTest(alpha_test,GPU_GREATER,127);
    environment_key=key;
    if(!environment_state_valid || environment_scale[0]!=detail_u_scale || environment_scale[1]!=detail_v_scale || environment_scale[2]!=0 || environment_scale[3]!=0) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,detail_u_scale,detail_v_scale,0,0);
        environment_scale[0]=detail_u_scale;environment_scale[1]=detail_v_scale;environment_scale[2]=environment_scale[3]=0;
    }
    environment_state_valid=1;
    bound_vertex_valid=0;
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    if(BufInfo_Add(buffers,vertices,sizeof(*vertices),3,0x210)<0 || BufInfo_Add(buffers,light_uv,2*sizeof(float),1,3)<0) return 0;
    for(unsigned int i=0;i<count;i+=30000) C3D_DrawElements(GPU_TRIANGLES,count-i>30000?30000:count-i,C3D_UNSIGNED_SHORT,indices+i);
    return 1;
}
int n3ds_gpu_environment_emission_draw(const struct native_render_vertex *vertices,const float *light_uv,
    const unsigned short *indices,unsigned int count,int lightmap,unsigned int material,int alpha_test,
    const struct native_environment_emission *emission)
{
    if(!frame_active || !environment_ready || !vertices || !light_uv || !indices || !count || count%3 ||
       ((uintptr_t)indices&3) || !emission) return 0;
    for(int i=0;i<4;++i) if(!isfinite(emission->uv[i])) return 0;
    if(bump_state_valid || plasma_state_valid) {native_light_env_bind(NULL);bump_state_valid=plasma_state_valid=0;environment_state_valid=0;}
    int key=16 | !!lightmap | (!!alpha_test<<3),changed=!environment_state_valid || ((key^environment_key)&23);
    if(lightmap && !n3ds_gpu_texture_wrap(1,1,1)) return 0;
    if(!environment_state_valid) {
        if(!environment_program_valid) {
            C3D_BindProgram(&environment_program);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,&current_projection);
        /* Xbox BSP triangles use clockwise fronts after this projection. */
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);C3D_CullFace(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            environment_program_valid=1;
        }
        C3D_SetAttrInfo(&environment_attributes[0]);
        C3D_FixedAttribSet(4,0,0,0,1);C3D_FixedAttribSet(5,1,0,0,0);
        n3ds_gpu_model_stencil_apply();
    }
    if(changed) {
        for(int i=0;i<6;++i) C3D_SetTexEnv(i,&environment_recipes[ENVIRONMENT_EMISSION_RECIPE][i]);
        native_light_env_bind(flashlight_power?&flashlight_env:NULL);flashlight_environment_combine(1);
        if(!lightmap) C3D_TexEnvSrc(C3D_GetTexEnv(3),C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PRIMARY_COLOR);
        /* Stage2 emission becomes available to stage4 after one intervening
         * stage. The original light+emission sum saturates before base multiply. */
        C3D_TexEnvBufUpdate(C3D_RGB,1<<2);C3D_TexEnvBufColor(0);
    }
    for(int i=0;i<4;++i) {
        unsigned int color=(i<3?emission->color[i]:material)|0xff000000u;
        if(changed || color!=emission_colors[i]) {C3D_TexEnvColor(C3D_GetTexEnv(i),color);emission_colors[i]=color;}
    }
    if(!environment_state_valid || ((key^environment_key)&8)) C3D_AlphaTest(alpha_test,GPU_GREATER,127);
    if(!environment_state_valid || memcmp(environment_scale,emission->uv,sizeof(environment_scale))) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,emission->uv[0],emission->uv[1],emission->uv[2],emission->uv[3]);
        memcpy(environment_scale,emission->uv,sizeof(environment_scale));
    }
    environment_key=key;environment_state_valid=1;
    bound_vertex_valid=0;
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    if(BufInfo_Add(buffers,vertices,sizeof(*vertices),3,0x210)<0 || BufInfo_Add(buffers,light_uv,2*sizeof(float),1,3)<0) return 0;
    for(unsigned int i=0;i<count;i+=30000) C3D_DrawElements(GPU_TRIANGLES,count-i>30000?30000:count-i,C3D_UNSIGNED_SHORT,indices+i);
    return 1;
}
static float plasma_pulse(float phase,float alpha)
{
    float x=1-2*fabsf(phase-alpha);x*=x;x*=x;x=2*x-1;
    return x>0?x*x:0;
}
static void plasma_lut_create(C3D_LightLut *lut,unsigned int phase)
{
    float p=phase/255.f;int center=(int)(phase*256/255);
    memset(lut,0,sizeof(*lut));
    /* A pulse is nonzero only within~20.37 samples of its circular center.
     * Include22 on either side for the floor and adjacent slope sample.
     * Evaluate wrapped endpoints at their original0..256 coordinates: using
     * phase-wrap identities can change floating-point rounding at the seam. */
    for(int sample=center-22;sample<=center+22;++sample) {
        unsigned int i=(unsigned int)sample&255;
        float first=plasma_pulse(p,i/256.f),next=plasma_pulse(p,(i+1)/256.f),in=first,diff=next-first;
        /* Same12-bit value and signed11-bit slope encoding as Citro3D's
         * LightLut_FromArray, fused with sampling to avoid a512-float scratch
         * array and second pass. Signed-LN indexes wrap at the half table. */
        u32 value=0,slope=0;
        if(in>0) {in*=4096;value=in<4096?(u32)in:4095;}
        if(diff!=0) {
            if(diff<0) {diff=-diff;slope=0x800;}
            diff*=2048;slope|=diff<2048?(u32)diff:2047;
        }
        lut->data[(i+128)&255]=value|(slope<<12);
    }
}
int n3ds_gpu_plasma_lut_tests(void)
{
    C3D_LightLut old_table,new_table;float values[512];u64 before,old_ticks=0,new_ticks=0;
    volatile unsigned int checksum=0;
    for(unsigned int repeat=0;repeat<2;++repeat) for(unsigned int phase=0;phase<256;++phase) {
        before=svcGetSystemTick();float first=plasma_pulse(phase/255.f,0);
        for(int i=-128;i<128;++i) {
            float next=plasma_pulse(phase/255.f,(i+129)/256.f);
            values[i&255]=first;values[256+(i&255)]=next-first;first=next;
        }
        LightLut_FromArray(&old_table,values);old_ticks+=svcGetSystemTick()-before;
        before=svcGetSystemTick();plasma_lut_create(&new_table,phase);new_ticks+=svcGetSystemTick()-before;
        if(memcmp(&old_table,&new_table,sizeof(old_table))) {
            char line[128];snprintf(line,sizeof(line),"FAIL: plasma LUT exact encoding phase=%u",phase);n3ds_log(line);return 1;
        }
        checksum^=old_table.data[phase]^new_table.data[(phase+1)&255];
    }
    char line[200];snprintf(line,sizeof(line),"PASS: plasma LUT exact encoding:256 phases,65536 GPU words,2 repeats; old_us=%.3f fused_us=%.3f checksum=%08x",old_ticks*1e6/SYSCLOCK_ARM11/512,new_ticks*1e6/SYSCLOCK_ARM11/512,checksum);n3ds_log(line);
    return 0;
}

static int plasma_prepare(unsigned int phase,unsigned int on)
{
    if(!plasma_ready) {
        C3D_LightEnvInit(&plasma_env);C3D_Material material={.specular0={1,1,1}};
        C3D_LightEnvMaterial(&plasma_env,&material);
        C3D_LightEnvBumpMode(&plasma_env,GPU_BUMP_AS_BUMP);C3D_LightEnvBumpSel(&plasma_env,2);
        C3D_LightEnvBumpNormalZ(&plasma_env,true);C3D_LightEnvClampHighlights(&plasma_env,false);
        if(C3D_LightInit(&plasma_light,&plasma_env)<0) return 0;
        C3D_FVec direction=FVec4_New(1,0,0,0);C3D_LightPosition(&plasma_light,&direction);
        C3D_LightGeoFactor(&plasma_light,0,false);C3D_LightGeoFactor(&plasma_light,1,false);
        for(unsigned int i=0;i<PLASMA_LUT_SLOTS;++i) plasma_phase_key[i]=~0u;
        plasma_bound_lut=-1;plasma_ready=1;
    }
    unsigned int slot=plasma_bound_lut>=0 && plasma_phase_key[plasma_bound_lut]==phase?(unsigned int)plasma_bound_lut:0;
    for(;slot<PLASMA_LUT_SLOTS;++slot) if(plasma_phase_key[slot]==phase) break;
    if(slot==PLASMA_LUT_SLOTS) {
        slot=plasma_next_lut++%PLASMA_LUT_SLOTS;
        plasma_lut_create(plasma_luts+slot,phase);plasma_phase_key[slot]=phase;++plasma_lut_builds;
        if(plasma_bound_lut==(int)slot) plasma_env.flags|=C3DF_LightEnv_LutDirty(0);
    } else ++plasma_lut_hits;
    if(plasma_bound_lut!=(int)slot) {
        C3D_LightEnvLut(&plasma_env,GPU_LUT_D0,GPU_LUTINPUT_LN,true,plasma_luts+slot);
        plasma_bound_lut=slot;
    }
    if(!plasma_state_valid || on!=plasma_colors[4]) {
        C3D_LightSpecular0(&plasma_light,(on&255)/255.f,((on>>8)&255)/255.f,((on>>16)&255)/255.f);
        plasma_colors[4]=on;
    }
    native_light_env_bind(&plasma_env);return 1;
}
int n3ds_gpu_environment_plasma_draw(const struct native_render_vertex *vertices,const float *light_uv,
    const unsigned short *indices,unsigned int count,int lightmap,unsigned int material,int alpha_test,
    const struct native_environment_emission *emission,unsigned int on,unsigned int phase)
{
    if(!frame_active || !environment_ready || !vertices || !light_uv || !indices || !count || count%3 ||
       ((uintptr_t)indices&3) || !emission || phase>255) return 0;
    for(int i=0;i<4;++i) if(!isfinite(emission->uv[i])) return 0;
    int changed=!plasma_state_valid || plasma_lightmap!=!!lightmap;
    int buffer_changed=!plasma_state_valid || plasma_vertices!=vertices || plasma_light_uv!=light_uv;
    if(lightmap && !n3ds_gpu_texture_wrap(1,1,1)) return 0;
    if(!plasma_state_valid) {
        environment_state_valid=bump_state_valid=0;
        C3D_BindProgram(&environment_program);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,environment_projection,&current_projection);
        environment_program_valid=1;
        C3D_SetAttrInfo(&environment_attributes[0]);C3D_FixedAttribSet(4,0,0,0,1);C3D_FixedAttribSet(5,1,0,0,0);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);C3D_CullFace(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        n3ds_gpu_model_stencil_apply();
    }
    if(!plasma_prepare(phase,on)) return 0;
    if(changed) {
        for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnvBufUpdate(C3D_RGB,1<<1);C3D_TexEnvBufColor(0);
        C3D_TexEnv *stage=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(stage,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvOpRgb(stage,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_MODULATE);
        C3D_TexEnvSrc(stage,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(stage,C3D_Alpha,GPU_REPLACE);
        stage=C3D_GetTexEnv(1);C3D_TexEnvSrc(stage,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_PREVIOUS);
        C3D_TexEnvOpRgb(stage,GPU_TEVOP_RGB_SRC_B,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_MULTIPLY_ADD);
        stage=C3D_GetTexEnv(2);C3D_TexEnvSrc(stage,C3D_RGB,GPU_FRAGMENT_SECONDARY_COLOR,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_ADD);
        stage=C3D_GetTexEnv(3);C3D_TexEnvSrc(stage,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE2,GPU_PREVIOUS_BUFFER);
        C3D_TexEnvOpRgb(stage,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_MULTIPLY_ADD);
        stage=C3D_GetTexEnv(4);C3D_TexEnvSrc(stage,C3D_RGB,lightmap?GPU_TEXTURE1:GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PREVIOUS);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_MULTIPLY_ADD);
        stage=C3D_GetTexEnv(5);C3D_TexEnvSrc(stage,C3D_RGB,GPU_TEXTURE0,GPU_PREVIOUS,GPU_CONSTANT);
        C3D_TexEnvFunc(stage,C3D_RGB,GPU_MODULATE);
    }
    for(int i=0;i<4;++i) {
        unsigned int color=(i<3?emission->color[i]:material)|0xff000000u;
        if(changed || plasma_colors[i]!=color) {C3D_TexEnvColor(C3D_GetTexEnv(i<3?i:4),color);plasma_colors[i]=color;}
    }
    if(!plasma_state_valid || plasma_alpha!=!!alpha_test) C3D_AlphaTest(!!alpha_test,GPU_GREATER,127);
    if(!plasma_state_valid || memcmp(plasma_uv,emission->uv,sizeof(plasma_uv))) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,environment_options,emission->uv[0],emission->uv[1],emission->uv[2],emission->uv[3]);
        memcpy(plasma_uv,emission->uv,sizeof(plasma_uv));
    }
    plasma_alpha=!!alpha_test;plasma_lightmap=!!lightmap;plasma_state_valid=1;bound_vertex_valid=0;
    if(buffer_changed) {
        C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
        if(BufInfo_Add(buffers,vertices,sizeof(*vertices),3,0x210)<0 || BufInfo_Add(buffers,light_uv,2*sizeof(float),1,3)<0) return 0;
        plasma_vertices=vertices;plasma_light_uv=light_uv;
    }
    for(unsigned int i=0;i<count;i+=30000) C3D_DrawElements(GPU_TRIANGLES,count-i>30000?30000:count-i,C3D_UNSIGNED_SHORT,indices+i);
    ++plasma_draws;return 1;
}
void n3ds_gpu_plasma_stats(unsigned int values[3])
{values[0]=plasma_draws;values[1]=plasma_lut_builds;values[2]=plasma_lut_hits;}

int n3ds_gpu_environment_tests(void)
{
    unsigned int pixels[64],*readback=linearAlloc(240*400*4);
    struct native_render_vertex *vertices=linearAlloc(4*sizeof(*vertices));
    float *uv=linearAlloc(8*sizeof(float));unsigned short *indices=linearAlloc(12);
    void *textures[3]={NULL,NULL,NULL};int active=0,ok=0;unsigned int cases=0;
    if(!readback || !vertices || !uv || !indices) goto done;
    /* Match original Xbox BSP clockwise-front index order. */
    const unsigned short quad[6]={0,2,1,0,3,2};memcpy(indices,quad,sizeof(quad));
    for(int texture=0;texture<3;++texture) {
        for(unsigned int i=0;i<64;++i) {
            unsigned int x=(i&1)|((i>>1)&2)|((i>>2)&4);
            pixels[i]=texture==0?0x804020ff:texture==1?(x>=4?0x804020ff:0x204080ff):0x808080ff;
        }
        textures[texture]=n3ds_gpu_texture_create(pixels,8,8);if(!textures[texture]) goto done;
    }
    for(unsigned int i=0;i<4;++i) {
        vertices[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{.25f,.5f,1},{.1875f,.1875f}};
        uv[i*2]=.8125f;uv[i*2+1]=.1875f;
    }
    n3ds_gpu_geometry_flush(vertices,4*sizeof(*vertices));n3ds_gpu_geometry_flush(uv,8*sizeof(float));n3ds_gpu_geometry_flush(indices,12);
    for(unsigned int test=0;test<16;++test) {
        int lightmap=test!=6,detail=test>=12 || test==9 || test==10?1:test==8?2:test<6?test%3:0,alpha=test==7;
        /* Second UV set must sample the right half, independently of base UV.
         * The later pair proves runtime fallback and original alpha rejection. */
        if(test==7) {for(unsigned int i=0;i<64;++i) pixels[i]=0x80402040;C3D_TexLoadImage(textures[0],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[0]);}
        if(test==8) {
            for(unsigned int i=0;i<64;++i) pixels[i]=0xc0c0c0ff;C3D_TexLoadImage(textures[0],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[0]);
            for(unsigned int i=0;i<64;++i) pixels[i]=0xffffffff;C3D_TexLoadImage(textures[2],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[2]);
        }
        if(test==9) {
            for(unsigned int i=0;i<64;++i) pixels[i]=0x804020ff;C3D_TexLoadImage(textures[0],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[0]);
            for(unsigned int i=0;i<64;++i) {
                unsigned int x=(i&1)|((i>>1)&2)|((i>>2)&4);
                pixels[i]=x>=4?0xc0c0c0ff:0x404040ff;
            }
            C3D_TexLoadImage(textures[2],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[2]);
        }
        if(test==11) {
            /* Clamp must retain the right edge rather than wrap to the left. */
            for(unsigned int i=0;i<4;++i) uv[i*2]=1.1875f;
            n3ds_gpu_geometry_flush(uv,8*sizeof(float));
        }
        float u_scale=test<3 || test==9?1.f:4.f,v_scale=u_scale;
        if(test>=12) {
            const unsigned int colors[4]={0x4080c0ff,0xc04080ff,0x80c040ff,0xc0c0c0ff};
            for(unsigned int i=0;i<64;++i) {
                unsigned int x=(i&1)|((i>>1)&2)|((i>>2)&4);
                /* PICA tiled rows run opposite to authored texture V. */
                unsigned int y=7-(((i>>1)&1)|((i>>2)&2)|((i>>3)&4));
                pixels[i]=colors[(x>=4)|((y>=4)<<1)];
            }
            if(test==12) {C3D_TexLoadImage(textures[2],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[2]);}
            u_scale=(test&1)?4.f:1.f;v_scale=(test&2)?4.f:1.f;
        }
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        if(test>=12 && !n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,detail,5.f-u_scale,5.f-v_scale,0xffffffffu,alpha)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,detail,u_scale,v_scale,0xffffffffu,alpha)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        unsigned int expected=test==10?0x300c03ff:test==9?0x100401ff:test==8?0x804020ff:test==7?0x102030ff:test==6?0x202020ff:detail==1?0x200802ff:0x401004ff;
        if(test>=12) {const unsigned int refs[4]={0x100803ff,0x300402ff,0x200c01ff,0x300c03ff};expected=refs[test-12];}
        /* Readback retains the native rotated 240-wide, 400-high layout. */
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) {
            unsigned int actual=readback[y*240+x];
            for(unsigned int shift=0;shift<32;shift+=8) if(abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255))>2) {
                char message[160];snprintf(message,sizeof(message),"FAIL: environment GPU test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;
            }
        }
        ++cases;
    }
    n3ds_log("PASS: native environment GPU: 16 lightmap/detail/fallback/alpha/saturation tests; independent U/V detail rescaling, UV streams and lightmap clamp");
    unsigned int add_worst=0;
    for(unsigned int test=0;test<96;++test) {
        const unsigned int base_values[6]={0,1,63,128,192,255};
        const unsigned int detail_values[8]={0,31,63,96,127,128,192,255};
        int lightmap=test>=48,alpha_mode=(test/16)%3;
        unsigned int base=alpha_mode?64:255,detail=255,expected=alpha_mode?64:255;
        for(int c=0;c<3;++c) {
            unsigned int b=base_values[(test/8+c*2)%6],d=detail_values[(test+c*3)%8];
            base|=b<<(24-c*8);detail|=d<<(24-c*8);
            /* Original Xbox EXPAND_NORMAL, independent of the PICA recipe. */
            float value=fminf(1,fmaxf(0,b/255.f+2*d/255.f-1));
            float light=lightmap?((0x804020ffu>>(24-c*8))&255)/255.f:vertices[0].color[c];
            expected|=(unsigned int)(value*light*255+.5f)<<(24-c*8);
        }
        if(alpha_mode==2) expected=0x102030ff;
        for(int texture=0;texture<3;++texture) {
            for(int i=0;i<64;++i) pixels[i]=texture==0?base:texture==1?0x804020ff:detail;
            C3D_TexLoadImage(textures[texture],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[texture]);
        }
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        /* The prior draw is alpha-rejected in cutout cases. Final equal-depth
         * replacement exercises recipes and alpha state without another clear. */
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,test%3,1,1,0xffffffffu,alpha_mode==2)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,3,3,2,0xffffffffu,alpha_mode==2)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int actual=readback[y*240+x],delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>add_worst) add_worst=delta;
            if(delta>3) {char message[192];snprintf(message,sizeof(message),"FAIL: environment biased add test=%u expected=%08x actual=%08x difference=%u",test,expected,actual,delta);n3ds_log(message);goto done;}
        }
    }
    {char message[256];snprintf(message,sizeof(message),"PASS: environment biased add:96 GPU references,black/white/neutral/clamp boundaries,lightmap/vertex light,alpha/cutout and same-frame recipe transitions; max difference=%u",add_worst);n3ds_log(message);}
    unsigned int color_worst=0;
    for(unsigned int test=0;test<144;++test) {
        const unsigned int colors[6]={0xffffffffu,0xff000000u,0xff0000ffu,0xff6c6c6cu,0xff8f8f8fu,0xffda9137u};
        int detail=test%4,lightmap=(test/4)%2,alpha_mode=(test/48)%3;
        unsigned int tint=colors[(test/8)%6],alpha=alpha_mode?64:255;
        unsigned int base=0xc08040ffu,detail_pixel=0xf0a050ffu,light_pixel=0x6080c0ffu;
        base=(base&0xffffff00u)|alpha;
        unsigned int expected=alpha;
        for(int c=0;c<3;++c) {
            float b=((base>>(24-c*8))&255)/255.f;
            float d=((detail_pixel>>(24-c*8))&255)/255.f;
            float light=lightmap?((light_pixel>>(24-c*8))&255)/255.f:vertices[0].color[c];
            /* Xbox diffuse clamp precedes the lightmap/material-color product.
             * Independent ideal expression allows bounded 8-bit TEV rounding. */
            float albedo=detail==3?fminf(1,fmaxf(0,b+2*d-1)):detail?fminf(1,b*d*(detail==2?2:1)):b;
            float color=((tint>>(c*8))&255)/255.f;
            expected|=(unsigned int)(albedo*light*color*255+.5f)<<(24-c*8);
        }
        if(alpha_mode==2) expected=0x102030ff;
        for(int texture=0;texture<3;++texture) {
            for(int i=0;i<64;++i) pixels[i]=texture==0?base:texture==1?light_pixel:detail_pixel;
            C3D_TexLoadImage(textures[texture],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[texture]);
        }
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        /* Restore from the opposite white/tinted state, then change a recipe
         * while retaining the same color. Both must reconfigure stage3. */
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,detail,1,1,tint==0xffffffffu?0xff0000ffu:0xffffffffu,alpha_mode==2)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,(detail+1)%4,1,1,tint,alpha_mode==2)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,detail,1,1,tint,alpha_mode==2)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int actual=readback[y*240+x],delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>color_worst) color_worst=delta;
            if(delta>3) {char message[192];snprintf(message,sizeof(message),"FAIL: environment color test=%u expected=%08x actual=%08x difference=%u",test,expected,actual,delta);n3ds_log(message);goto done;}
        }
    }
    {char message[256];snprintf(message,sizeof(message),"PASS: environment material color:144 GPU references,white/red/gray/black/asymmetric tints,four detail modes,lightmap/vertex light,alpha/cutout and same-frame tint/recipe transitions; max difference=%u",color_worst);n3ds_log(message);}
    unsigned int emission_worst=0;
    for(unsigned int test=0;test<96;++test) {
        const unsigned int masks[8]={0x000000ff,0xff000000,0x00ff0080,0x0000ffff,0x4080c0ff,0xffffffff,0x90603000,0x030507ff};
        unsigned int sample=test%8,alpha_mode=(test/16)%3,phase=test/48;
        int lightmap=(test/8)%2;unsigned int alpha=alpha_mode?64:255;
        unsigned int base=0x7090b000u|alpha,light=0x302010ff,material=phase?0xff8055bbu:0xffffffffu;
        struct native_environment_emission emission={.color={phase?0xffe06020u:0xff103070u,phase?0xff105040u:0xff603010u,0xff182820u}};
        float scale=test%3+1;
        emission.uv[0]=scale;emission.uv[1]=2;
        emission.uv[2]=(sample+.5f)/8.f-vertices[0].uv[0]*scale;
        emission.uv[3]=.3125f-vertices[0].uv[1]*2;
        unsigned int expected=alpha;
        for(int c=0;c<3;++c) {
            float illumination=0;
            for(int term=0;term<3;++term) illumination+=((masks[sample]>>(24-term*8))&255)/255.f*((emission.color[term]>>(c*8))&255)/255.f;
            float baked=lightmap?((light>>(24-c*8))&255)/255.f:vertices[0].color[c];
            baked*=((material>>(c*8))&255)/255.f;
            /* Xbox RGB stages2/3 extract R/G and weight by animated constants;
             * with plasma-on zero, stage4 is off-color and stage5 weights by B.
             * Final combiner adds light and saturates before diffuse texture. */
            expected|=(unsigned int)(fminf(1,baked+fminf(1,illumination))*((base>>(24-c*8))&255)+.5f)<<(24-c*8);
        }
        if(alpha_mode==2) expected=0x102030ff;
        for(int texture=0;texture<3;++texture) {
            for(unsigned int i=0;i<64;++i) {
                unsigned int x=(i&1)|((i>>1)&2)|((i>>2)&4);
                pixels[i]=texture==0?base:texture==1?light:masks[x];
            }
            C3D_TexLoadImage(textures[texture],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[texture]);
        }
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        if(!n3ds_gpu_environment_emission_draw(vertices,uv,indices,6,lightmap,material,alpha_mode==2,&emission)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,test%4,3,2,material,alpha_mode==2)) goto done;
        struct native_environment_emission previous=emission;previous.color[0]^=0xffffffu;previous.uv[2]+=.25f;
        if(!n3ds_gpu_environment_emission_draw(vertices,uv,indices,6,lightmap,material^0xffffffu,alpha_mode==2,&previous)) goto done;
        if(!n3ds_gpu_environment_emission_draw(vertices,uv,indices,6,lightmap,material,alpha_mode==2,&emission)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int actual=readback[y*240+x],delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>emission_worst) emission_worst=delta;
            if(delta>3) {char message[192];snprintf(message,sizeof(message),"FAIL: environment emission test=%u expected=%08x actual=%08x difference=%u",test,expected,actual,delta);n3ds_log(message);goto done;}
        }
    }
    {char message[256];snprintf(message,sizeof(message),"PASS: environment emission:96 GPU references,RGB masks,animated constants,lightmap/vertex light,material color,alpha/cutout,UV scale/offset and same-frame emission/plain transitions; max difference=%u",emission_worst);n3ds_log(message);}
    /* Texture contents still match the final emission vector: base7090b040,
     * light302010ff, emission stripe1=red. Plain detail must reset the scrolling
     * offset and every combiner/buffer constant left by an emissive draw. */
    for(unsigned int test=0;test<16;++test) {
        int detail=test%4,lightmap=(test/4)%2;
        unsigned int material=test/8?0xff985432u:0xffffffffu,expected=64;
        for(int c=0;c<3;++c) {
            float base=((0x7090b040u>>(24-c*8))&255)/255.f,d=c==0?1:0;
            float albedo=detail==3?fminf(1,fmaxf(0,base+2*d-1)):detail?fminf(1,base*d*(detail==2?2:1)):base;
            float light=lightmap?((0x302010ffu>>(24-c*8))&255)/255.f:vertices[0].color[c];
            expected|=(unsigned int)(albedo*light*((material>>(8*c))&255)+.5f)<<(24-c*8);
        }
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        struct native_environment_emission emission={.color={0xffc09070u,0xff4090c0u,0xff9070c0u},.uv={3,2,.25f,.5f}};
        if(!n3ds_gpu_environment_emission_draw(vertices,uv,indices,6,!lightmap,0xff802010u,0,&emission)) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,lightmap,detail,1,1,material,0)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int actual=readback[y*240+x];
            if(abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255))>3) {
                char message[160];snprintf(message,sizeof(message),"FAIL: emission restoration test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;
            }
        }
    }
    n3ds_log("PASS: emission restoration:16 GPU references,plain/detail/tint/lightmap state and zero UV offset after emissive draw");
    for(int texture=0;texture<3;++texture) {
        for(int i=0;i<64;++i) pixels[i]=texture==2?0x808080ff:0x804020ff;
        C3D_TexLoadImage(textures[texture],pixels,GPU_TEXFACE_2D,0);C3D_TexFlush(textures[texture]);
    }
    for(unsigned int test=0;test<8;++test) {
        const unsigned short reverse[6]={0,1,2,0,2,3};
        int back=test>=4,detail=test%4;
        memcpy(indices,back?reverse:quad,sizeof(quad));n3ds_gpu_geometry_flush(indices,sizeof(quad));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        for(int i=0;i<3;++i) if(!n3ds_gpu_texture_bind(i,textures[i])) goto done;
        if(!n3ds_gpu_environment_draw(vertices,uv,indices,6,1,detail,1,1,0xffffffffu,0)) goto done;
        n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(readback,240*400)) goto done;
        unsigned int expected=back?0x102030ff:detail==1?0x200802ff:detail==3?0x411004ff:0x401004ff;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int actual=readback[y*240+x];
            if(abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255))>3) {
                char message[160];snprintf(message,sizeof(message),"FAIL: environment culling test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;
            }
        }
    }
    ok=1;n3ds_log("PASS: environment culling:8 GPU references,Xbox clockwise fronts retained and reversed faces rejected across four material recipes");
done:
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    for(int i=0;i<3;++i) n3ds_gpu_texture_destroy(textures[i]);
    linearFree(readback);linearFree(vertices);linearFree(uv);linearFree(indices);return !ok;
}
int n3ds_gpu_geometry_draw_resident(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count)
{ return indexed_draw_resident(vertices,indices,count,sizeof(*vertices),3,0x210); }
const unsigned short *n3ds_gpu_indices_upload(const unsigned short *indices,unsigned int count)
{
    if (!frame_active || !indices || !count || count%3 || count > (INDEX_BYTES-index_used)/2) return NULL;
    unsigned short *gpu_indices = (unsigned short *)(index_stream+index_used);
    memcpy(gpu_indices, indices, count*2);
    ++index_draws;
    index_used = (index_used+count*2+3)&~3u;
    return gpu_indices;
}
int n3ds_gpu_indexed_draw(const void *vertices,const unsigned short *indices,unsigned int count,
    unsigned int stride,unsigned int attributes,unsigned int permutation)
{
    if(!frame_active || !vertices || !indices || !count || count%3) return 0;
    uintptr_t at=(uintptr_t)indices,base=(uintptr_t)index_stream;
    const unsigned short *gpu_indices=indices;
    /* Only our immutable, already-written prefix can bypass a staging copy.
     * Ordinary caller buffers remain copied, including reused stack buffers. */
    if(at<base || at-base>index_used || (at&3) || count>(index_used-(at-base))/2)
        gpu_indices=n3ds_gpu_indices_upload(indices,count);
    if(!gpu_indices) return 0;
    return indexed_draw_resident(vertices,gpu_indices,count,stride,attributes,permutation);
}
static int indexed_draw_resident(const void *vertices,const unsigned short *gpu_indices,unsigned int count,
    unsigned int stride,unsigned int attributes,unsigned int permutation)
{
    if(!frame_active || !vertices || !gpu_indices || !count || count%3 || ((uintptr_t)gpu_indices&3)) return 0;
    /* Models/effects rarely repeat a stream: avoid cache lookup/copy cost.
     * Invalidating here keeps text/widget reuse safe after every world draw. */
    bound_vertex_valid=0;
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    if(BufInfo_Add(buffers,vertices,stride,attributes,permutation)<0) return 0;
    for (unsigned int i=0; i<count; i+=30000) {
        unsigned int chunk = count-i;
        if (chunk>30000) chunk=30000;
        C3D_DrawElements(GPU_TRIANGLES, chunk, C3D_UNSIGNED_SHORT, gpu_indices+i);
    }
    return 1;
}
void n3ds_gpu_present(void)
{
    if (!frame_active) svcBreak(USERBREAK_PANIC);
    /* Dynamic indexed draws append to this arena; immutable BSP indices are
     * flushed once by their cache owner. Citro3D executes the
     * queued commands at FrameEnd, so flush the written prefix once here.
     * FrameBegin waits for that queue before reusing the arena. */
    if(index_used) { GSPGPU_FlushDataCache(index_stream,index_used);++index_flushes; }
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) if(model_blocks[i].used)
        GSPGPU_FlushDataCache(model_blocks[i].data,model_blocks[i].used*sizeof(struct native_render_vertex));
    if(model_used) ++model_flushes;
    n3ds_gpu_chicago_flush();
    n3ds_gpu_text_flush();
    n3ds_gpu_widgets_flush();
    /* Other vertices and textures are flushed when written. Preserve GPU-written
     * display buffers by requesting only command-memory flushing below. */
    C3D_FrameEnd(GX_CMDLIST_FLUSH);timing_cpu_previous=C3D_GetProcessingTime();timing_pending=1; frame_active = 0;bound_vertex_valid=0;
    float usage=C3D_GetCmdBufUsage();
    if(usage>peak_command_usage) peak_command_usage=usage;
}

/* The retail artwork is supplied by setup from the user's own Xbox executable.
 * Fixed dimensions, checksum and bounded RLE expansion reject interrupted copies. */
static void loading_art_prepare(void)
{
    if(loading_art_tried) return;
    loading_art_tried=1;
    FILE *file=fopen("sdmc:/halo-source/ntsc2276-loading.bin","rb");
    if(!file) {n3ds_log("LOADING ART: optional retail artwork absent; using text screen");return;}
    unsigned char *data=malloc(26604);
    int valid=data && fread(data,1,26604,file)==26604 && fgetc(file)==EOF;
    fclose(file);
    unsigned int hash=2166136261u;
    if(valid) for(unsigned int i=0;i<26604;++i) hash=(hash^data[i])*16777619u;
    valid=valid && hash==0xe578b2edu;
    unsigned short *pixels=valid?calloc(512*256,sizeof(*pixels)):NULL;
    if(pixels) {
        unsigned int at=0;
        for(unsigned int i=8;i<26604 && valid;++i) {
            unsigned int count=data[i]>>4,intensity=data[i]&15;
            if(count>320*240-at) {valid=0;break;}
            for(unsigned int j=0;j<count;++j,++at) {
                unsigned int x=at%320,y=255-at/320;
                unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
                pixels[((y>>3)*64+(x>>3))*64+morton]=((intensity/4)<<12)|((intensity/2)<<8)|(intensity<<4)|15;
            }
        }
        if(valid && at==320*240) loading_art=n3ds_gpu_texture_create_rgba4(pixels,512,256);
    }
    free(pixels);free(data);
    n3ds_log(loading_art?"LOADING ART: verified original retail image, 320x240; native animated glow":
        "LOADING ART: invalid/unavailable optional artwork; using text screen");
}
static void loading_art_draw(float progress)
{
    if(!loading_art) return;
    void *saved=n3ds_gpu_texture_bound(0);
    extern void n3ds_gpu_hud_layout(int);n3ds_gpu_hud_layout(0);
    n3ds_gpu_texture_bind(0,loading_art);n3ds_gpu_text_begin(400,240,0xffffffffu,0,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    /* Keep the original 4:3 picture centred. A moving, feathered light sweep
     * replaces the Xbox feedback-blur pass without extra render targets. */
    float phase=(float)fmod((double)svcGetSystemTick()/SYSCLOCK_ARM11*.18,1.);
    float center=phase*448-64;
    for(unsigned int strip=0;strip<32;++strip) {
        float x0=strip*10.f,x1=x0+10;
        struct native_text_vertex q[4]={
            {{40+x0,0},{1,1,1,1},{x0/512.f,0}},{{40+x1,0},{1,1,1,1},{x1/512.f,0}},
            {{40+x1,240},{1,1,1,1},{x1/512.f,240.f/256}},{{40+x0,240},{1,1,1,1},{x0/512.f,240.f/256}}};
        for(int i=0;i<4;++i) {
            float x=(i==1 || i==2)?x1:x0;
            float glow=fmaxf(0,1-fabsf(x-center)/64);
            float light=.65f+.35f*glow*glow;
            for(int c=0;c<3;++c) q[i].color[c]=light;
        }
        n3ds_gpu_text_draw(q);
    }
    (void)progress;
    n3ds_gpu_texture_bind(0,saved);
}
static void status_text(const char *const *lines,unsigned int count,int menu)
{
    if(!loading_font) {
        unsigned int pixels[128*64]={0};
        const ConsoleFont *font=&consoleGetDefault()->font;
        static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
        for(unsigned int c=32;c<127;++c) for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
            unsigned int px=(c%16)*8+x,py=63-((c/16)*8+y);
            unsigned int at=((py/8)*16+px/8)*64+spread[px&7]+2*spread[py&7];
            pixels[at]=(font->gfx[c*8+y]&(0x80u>>x))?0xffffffffu:0;
        }
        loading_font=n3ds_gpu_texture_create(pixels,128,64);
    }
    if(!loading_font) return;

    void *saved=n3ds_gpu_texture_bound(0);
    extern void n3ds_gpu_hud_layout(int);n3ds_gpu_hud_layout(0);
    n3ds_gpu_texture_bind(0,loading_font);
    n3ds_gpu_text_begin(320,240,0xffffffffu,1,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    for(unsigned int line=0;line<count;++line) {
        float size=menu?8:line?8:16,x=(320-strlen(lines[line])*size)*.5f,y=menu?184+line*20:line?80+line*24:40;
        for(const char *p=lines[line];*p;++p,x+=size) {
            unsigned int c=(unsigned char)*p;
            float u=(c%16)/16.f,v=(c/16)/8.f;
            struct native_text_vertex q[4]={
                {{x,y},{1,1,1,1},{u,v}},{{x+size,y},{1,1,1,1},{u+1.f/16,v}},
                {{x+size,y+size},{1,1,1,1},{u+1.f/16,v+1.f/8}},{{x,y+size},{1,1,1,1},{u,v+1.f/8}}};
            n3ds_gpu_text_draw(q);
        }
    }
    n3ds_gpu_texture_bind(0,saved);
}

extern void n3ds_gpu_hud_layout(int);
#include "engine_controls_draw.inl"
static void controls_dispose(void){if(controls_white)n3ds_gpu_texture_destroy(controls_white);controls_white=NULL;}

static void loading_text(void)
{
    static const char *lines[]={"Halo3DS", "Loading game data...", "Initial loading may take a while."};
    status_text(lines,3,0);
}
void n3ds_gpu_campaign_help(void)
{
    struct native_control_settings s;n3ds_controls_get(&s);
    const char *lines[]={"At difficulty selection:",s.xbox_buttons?"B: Start mission   R + B: Resume save":"A: Start mission   R + A: Resume save"};
    n3ds_gpu_hud_screen(1);status_text(lines,2,1);
    n3ds_gpu_hud_screen(0);n3ds_gpu_world_state_restore();
}

int n3ds_gpu_loading_ready(void) { return target && program_ready; }
void n3ds_gpu_loading_enable(int enabled)
{
    if(loading_enabled && !enabled) {
        char message[100];snprintf(message,sizeof(message),"LOADING DISPLAY: end, total submissions=%u",loading_submissions);n3ds_log(message);
    }
    loading_enabled=enabled;
}
unsigned int n3ds_gpu_loading_submissions(void) { return loading_submissions; }
void n3ds_gpu_loading_pulse(float progress,int force)
{
    static u64 last;
    if(!loading_enabled) return;
    u64 now=svcGetSystemTick();
    /* Map I/O runs between engine frames. Never disturb an active frame or
     * begin a nested engine window. Limit feedback to ten submissions/sec. */
    if(frame_active || !n3ds_gpu_loading_ready() || (!force && now-last<SYSCLOCK_ARM11/10)) return;
    last=now;
    struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},
        .vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x000000ff};
    loading_art_prepare();
#ifdef HALO_N3DS_BOOT_TRACE
    boot_trace_active=boot_trace_pulses++<3;
#endif
    BOOT_TRACE("loading pulse begin");
    if(!n3ds_gpu_frame_begin()) return;
    BOOT_TRACE("loading frame initialized");
    float saved_offset=eye_offset;int saved_eye=eye_right;
    for(int eye=0;eye<(stereo_level>0?2:1);++eye) {
        n3ds_gpu_stereo_eye(eye,0);n3ds_gpu_window_begin(&camera);
        BOOT_TRACE("loading window initialized");
        n3ds_gpu_loading_draw(progress);
    }
    BOOT_TRACE("loading frame submit begin");
    n3ds_gpu_present();n3ds_gpu_stereo_eye(saved_eye,saved_offset);
    BOOT_TRACE("loading frame submitted");
#ifdef HALO_N3DS_BOOT_TRACE
    boot_trace_active=0;
#endif
    ++loading_submissions;
}
static int loading_bar(float progress)
{
    if(!frame_active || !n3ds_gpu_loading_ready() || !isfinite(progress) || progress<0 || progress>1 ||
       n3ds_gpu_command_usage()>.95f) return 0;
    struct native_render_vertex *vertices=n3ds_gpu_model_vertices_allocate(12);
    if(!vertices) return 0;
    static const unsigned int corners[6]={0,1,2,0,2,3};
    for(unsigned int rectangle=0;rectangle<2;++rectangle) {
        float left=rectangle?52:48,top=rectangle?200:196;
        float right=rectangle?52+296*progress:352,bottom=rectangle?212:216;
        float xy[4][2]={{left,top},{right,top},{right,bottom},{left,bottom}};
        for(unsigned int i=0;i<6;++i) {
            struct native_render_vertex *v=vertices+rectangle*6+i;
            memset(v,0,sizeof(*v)); v->position[0]=xy[corners[i]][0]; v->position[1]=xy[corners[i]][1];
            v->color[0]=rectangle?.2f:.15f; v->color[1]=rectangle?.8f:.2f; v->color[2]=rectangle?1.f:.25f;
        }
    }
    C3D_Mtx projection;
    Mtx_OrthoTilt(&projection,0,400,240,0,0,1,true);
    C3D_BindProgram(&program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,matrix_location,&projection);
    C3D_AttrInfo *attributes=C3D_GetAttrInfo(); AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3); AttrInfo_AddLoader(attributes,1,GPU_FLOAT,3); AttrInfo_AddLoader(attributes,2,GPU_FLOAT,2);
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE); C3D_AlphaTest(false,GPU_ALWAYS,0); C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    GSPGPU_FlushDataCache(vertices,12*sizeof(*vertices));
    int result=n3ds_gpu_vertex_buffer_bind(vertices,sizeof(*vertices),3,0x210);
    if(result) C3D_DrawArrays(GPU_TRIANGLES,0,12);
    n3ds_gpu_world_state_restore();
    return result;
}

int n3ds_gpu_loading_draw(float progress)
{
    if(!loading_enabled) return 1;
    if(!frame_active || !isfinite(progress) || progress<0 || progress>1) return 0;
    int saved_overlay=overlay_stereo;overlay_stereo=0;
    BOOT_TRACE("loading artwork draw begin");
    loading_art_draw(progress);
    BOOT_TRACE("loading artwork draw complete");
    if(!eye_right) {
        n3ds_gpu_hud_screen(1);loading_bar(progress);
        BOOT_TRACE("loading bottom bar complete");
        loading_text();
        BOOT_TRACE("loading bottom text complete");
        n3ds_gpu_hud_screen(0);
    }
    overlay_stereo=saved_overlay;n3ds_gpu_world_state_restore();
    return 1;
}

/* Focused display evidence: recorded-screen verification also checks these
 * independent target/front paths. This never changes display contents. */
static int loading_write_capture(const char *path,const void *data,size_t size)
{
    /* FS IPC consumes ordinary CPU memory. Stage GPU-visible buffers through a
     * bounded CPU chunk, and check disk readback against the original bytes. */
    unsigned char chunk[4096];
    FILE *file=fopen(path,"wb");
    if(!file) return 1;
    setvbuf(file,NULL,_IONBF,0);
    int result=0;
    for(size_t offset=0;offset<size;offset+=sizeof(chunk)) {
        size_t count=size-offset; if(count>sizeof(chunk)) count=sizeof(chunk);
        memcpy(chunk,(const unsigned char *)data+offset,count);
        if(fwrite(chunk,1,count,file)!=count) { result=1;break; }
    }
    if(fclose(file)) result=1;
    if(result) return result;
    file=fopen(path,"rb"); if(!file) return 1;
    for(size_t offset=0;offset<size;offset+=sizeof(chunk)) {
        size_t count=size-offset; if(count>sizeof(chunk)) count=sizeof(chunk);
        if(fread(chunk,1,count,file)!=count || memcmp(chunk,(const unsigned char *)data+offset,count)) { result=1;break; }
    }
    fclose(file);
    return result;
}
static int capture_stage(const char *name,unsigned int stage)
{
    if(frame_active || !target || !n3ds_gpu_texture_barrier()) return 1;
    u32 *pixels=linearAlloc(240*400*4);
    if(!pixels) return 1;
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32 *)pixels,GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(pixels,240*400*4);
    char path[96]; snprintf(path,sizeof(path),"sdmc:/%s-%u-target.rgba",name,stage);
    int result=loading_write_capture(path,pixels,240*400*4);
    linearFree(pixels);
    gfxSetDoubleBuffering(GFX_TOP,false);
    u8 *front=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
    gfxSetDoubleBuffering(GFX_TOP,true);
    GSPGPU_InvalidateDataCache(front,240*400*3);
    snprintf(path,sizeof(path),"sdmc:/%s-%u-front.rgb",name,stage);
    result|=loading_write_capture(path,front,240*400*3);
    gfxSetDoubleBuffering(GFX_BOTTOM,false);
    front=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    GSPGPU_InvalidateDataCache(front,240*320*3);
    snprintf(path,sizeof(path),"sdmc:/%s-%u-bottom.rgb",name,stage);
    result|=loading_write_capture(path,front,240*320*3);
    return result;
}
int n3ds_gpu_loading_capture(unsigned int stage)
{ return stage>4?1:capture_stage("halo-loading",stage); }
int n3ds_gpu_screen_quad_capture(void) { return capture_stage("halo-screen",0); }

int n3ds_gpu_readback(unsigned int *pixels,unsigned int count)
{
    static unsigned int calls;
    int trace=++calls<=2;
    if(frame_active || !target || !pixels || count!=240*400) return 0;
    if(trace) n3ds_log("STARTUP: readback waiting for submitted GPU work");
    if(trace) {
        /* Keep early hardware stalls observable without reusing live resources.
         * The normal barrier blocks forever when a GPU draw never completes. */
        u64 deadline=svcGetSystemTick()+5ULL*SYSCLOCK_ARM11;
        while(!C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
            if(svcGetSystemTick()>=deadline) {
                char message[160];u32 busy=0;
                n3ds_log("STARTUP FAIL: submitted GPU work did not complete within five seconds");
                Result status=GSPGPU_ReadHWRegs(0x400034,&busy,sizeof(busy));
                snprintf(message,sizeof(message),"STARTUP FAIL: GPU busy register status=%08lx value=%08lx P3D=%u PPF=%u",
                    (unsigned long)status,(unsigned long)busy,(unsigned int)(busy>>31),(unsigned int)((busy>>30)&1));
                n3ds_log(message);
                /* Do not free or resubmit buffers that the GPU may still use.
                 * Leave the diagnostic log intact until the user powers off. */
                for(;;) svcSleepThread(100000000LL);
            }
            svcSleepThread(1000000LL);
        }
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    } else if(!n3ds_gpu_texture_barrier()) return 0;
    if(trace) n3ds_log("STARTUP: readback GPU work complete; transferring pixels");
    /* The CPU read this buffer during the preceding probe. Clean/invalidate it
     * before DMA reuses it, and check cache-service failures instead of treating
     * stale cached pixels as a renderer failure. */
    Result cache_status=GSPGPU_FlushDataCache(pixels,count*4);
    if(R_FAILED(cache_status)) {
        char message[128];snprintf(message,sizeof(message),"READBACK FAIL: pre-transfer cache flush status=%08lx",(unsigned long)cache_status);n3ds_log(message);return 0;
    }
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32 *)pixels,GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(trace) n3ds_log("STARTUP: readback pixel transfer complete");
    cache_status=GSPGPU_InvalidateDataCache(pixels,count*4);
    if(R_FAILED(cache_status)) {
        Result fallback=svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)pixels,count*4);
        char message[160];snprintf(message,sizeof(message),"STARTUP: readback cache service=%08lx direct fallback=%08lx",(unsigned long)cache_status,(unsigned long)fallback);n3ds_log(message);
        if(R_FAILED(fallback)) return 0;
    }
    return 1;
}

int n3ds_gpu_screen_test(void)
{
    char capture_memory[160];
    snprintf(capture_memory,sizeof(capture_memory),"SCREEN CAPTURE: linear_free=%u geometry_resident=%u geometry_arena=%u frame_active=%d",(unsigned int)linearSpaceFree(),model_resident_units*(unsigned int)sizeof(struct native_render_vertex),model_reserved*(unsigned int)sizeof(struct native_render_vertex),frame_active);
    n3ds_log(capture_memory);
    if(stereo_level>0) {
        if(!n3ds_gpu_texture_barrier()) return 1;
        u32 *pixels=linearAlloc(240*400*4);if(!pixels) return 1;
        C3D_SyncDisplayTransfer(right_target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),pixels,GX_BUFFER_DIM(240,400),
            GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        GSPGPU_InvalidateDataCache(pixels,240*400*4);
        int failed=loading_write_capture("sdmc:/halo-native-right-target.rgba",pixels,240*400*4);linearFree(pixels);
        gfxSetDoubleBuffering(GFX_TOP,false);u8 *front=gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,NULL,NULL);gfxSetDoubleBuffering(GFX_TOP,true);
        GSPGPU_InvalidateDataCache(front,240*400*3);
        failed|=loading_write_capture("sdmc:/halo-native-right-front.rgb",front,240*400*3);
        if(failed) return 1;
    }
    if (frame_active || !target || !n3ds_gpu_texture_barrier()) {n3ds_log("SCREEN CAPTURE: target/frame/barrier unavailable");return 1;}
    u32 *bottom_pixels=linearAlloc(240*320*4);
    if(!bottom_pixels) {n3ds_log("SCREEN CAPTURE: bottom staging allocation failed");return 1;}
    C3D_SyncDisplayTransfer(bottom_target->frameBuf.colorBuf,GX_BUFFER_DIM(240,320),bottom_pixels,GX_BUFFER_DIM(240,320),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(bottom_pixels,240*320*4);
    int bottom_result=loading_write_capture("sdmc:/halo-native-bottom-target.rgba",bottom_pixels,240*320*4);
    if(bottom_result) n3ds_log("SCREEN CAPTURE: bottom target file verification failed");
    linearFree(bottom_pixels);
    gfxSetDoubleBuffering(GFX_BOTTOM,false);
    u8 *bottom_front=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    GSPGPU_InvalidateDataCache(bottom_front,240*320*3);
    bottom_result|=loading_write_capture("sdmc:/halo-native-bottom-front.rgb",bottom_front,240*320*3);
    if(bottom_result) {n3ds_log("SCREEN CAPTURE: bottom capture failed");return 1;}
    char capacity_message[96];
    snprintf(capacity_message,sizeof(capacity_message),"GPU command buffer peak: %.6f",peak_command_usage);
    n3ds_log(capacity_message);
    u32 *pixels=linearAlloc(240*400*4);
    if (!pixels) return 1;
    C3D_SyncDisplayTransfer(target->frameBuf.colorBuf, GX_BUFFER_DIM(240,400), pixels, GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(pixels,240*400*4);
    unsigned int colored=0;
    for (unsigned int i=0; i<240*400; ++i) if (pixels[i]!=0x101822ff && pixels[i]!=0 && pixels[i]!=0x000000ff) ++colored;
    char message[160];
    snprintf(message,sizeof(message),"GPU world target: %u non-clear pixels, center %08lx",colored,(unsigned long)pixels[120*400+200]);
    n3ds_log(message);
    FILE *file=fopen("sdmc:/halo-native-target.rgba","wb");
    if (file) { fwrite(pixels,4,240*400,file); fclose(file); }
    u16 width,height;
    u8 *screen=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&width,&height);
    GSPGPU_InvalidateDataCache(screen,width*height*3);
    file=fopen("sdmc:/halo-native-screen.rgb","wb");
    if (file) { fwrite(screen,3,width*height,file); fclose(file); }
    unsigned int nonzero=0;
    for (unsigned int i=0; i<width*height*3; ++i) if (screen[i]) ++nonzero;
    snprintf(message,sizeof(message),"Screen buffer: %ux%u, %u nonzero bytes, pending %d",width,height,nonzero,gspIsPresentPending(GFX_TOP));
    n3ds_log(message);
    /* gfxGetFramebuffer normally returns the back buffer. Inspect the selected
     * front buffer too, without swapping or changing its displayed address. */
    gfxSetDoubleBuffering(GFX_TOP,false);
    u8 *front=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
    gfxSetDoubleBuffering(GFX_TOP,true);
    GSPGPU_InvalidateDataCache(front,width*height*3);
    unsigned int front_nonzero=0;
    for (unsigned int i=0; i<width*height*3; ++i) if (front[i]) ++front_nonzero;
    snprintf(message,sizeof(message),"Front buffer: physical %08lx, %u nonzero bytes; back physical %08lx",
        (unsigned long)osConvertVirtToPhys(front),front_nonzero,(unsigned long)osConvertVirtToPhys(screen));
    n3ds_log(message);
    file=fopen("sdmc:/halo-native-front.rgb","wb");
    if (file) { fwrite(front,3,width*height,file); fclose(file); }
    u32 registers[16]={0};
    Result status=GSPGPU_ReadHWRegs(0x40045c,registers,sizeof(registers));
    snprintf(message,sizeof(message),"Screen controller: status %08lx geometry %08lx A %08lx B %08lx selected %lu format %08lx",
        (unsigned long)status,(unsigned long)registers[0],(unsigned long)registers[3],(unsigned long)registers[4],
        (unsigned long)registers[7],(unsigned long)registers[5]);
    n3ds_log(message);
    u32 lcd_fill=0;
    status=GSPGPU_ReadHWRegs(0x202204,&lcd_fill,sizeof(lcd_fill));
    snprintf(message,sizeof(message),"LCD top fill: status %08lx value %08lx",(unsigned long)status,(unsigned long)lcd_fill);
    n3ds_log(message);
    status=GSPGPU_ReadHWRegs(0x40055c,registers,sizeof(registers));
    snprintf(message,sizeof(message),"BOTTOM controller: status %08lx geometry %08lx A %08lx B %08lx selected %lu format %08lx physical-front %08lx pending %d",
        (unsigned long)status,(unsigned long)registers[0],(unsigned long)registers[3],(unsigned long)registers[4],
        (unsigned long)registers[7],(unsigned long)registers[5],(unsigned long)osConvertVirtToPhys(bottom_front),gspIsPresentPending(GFX_BOTTOM));n3ds_log(message);
    status=GSPGPU_ReadHWRegs(0x202a04,&lcd_fill,sizeof(lcd_fill));
    snprintf(message,sizeof(message),"LCD bottom fill: status %08lx value %08lx",(unsigned long)status,(unsigned long)lcd_fill);n3ds_log(message);
    linearFree(pixels);
    return colored<1000 || nonzero<1000 || front_nonzero<1000;
}
/* Flat-normal pixel references exercise the actual rigid and weighted programs,
 * both directional lights, ambient-only, masks, sky bypass, and state changes. */
int n3ds_gpu_model_lighting_tests(void)
{
    const float normals[][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,2,3},{-1,-2,-3}};
    const unsigned short indices[6]={0,2,1,0,3,2};
    const float uv[2]={1,1};
    struct native_skin_vertex *skin=linearAlloc(4*sizeof(*skin));
    struct native_render_vertex *rigid=linearAlloc(4*sizeof(*rigid));
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],worst=0;
    void *base=NULL,*mask=NULL;int active=0,ok=0;
    if(!skin || !rigid || !pixels) goto done;
    for(int i=0;i<64;++i) texels[i]=0xc08040ff;
    base=n3ds_gpu_texture_create(texels,8,8);if(!base) goto done;
    for(int i=0;i<64;++i) texels[i]=0x4080c0ff;
    mask=n3ds_gpu_texture_create(texels,8,8);if(!mask) goto done;
    for(unsigned int test=0;test<72;++test) {
        unsigned int n=test%8,variant=test/8;
        struct native_model_lighting lighting={.ambient={.2f,.3f,.4f},.distant={
            {.color={.6f,.3f,.2f},.direction={.3f,.4f,.8660254f}},
            {.color={.1f,.4f,.5f},.direction={-.6f,.8f,0}}},.count=variant%3};
        struct native_model_material m={mask,0xff604020,0xff2080c0,0,&lighting};m.base=base;
        if(variant<3) m.multipurpose=NULL;
        if(variant>=6) {m.translucency=.65f;lighting.count=2;}
        if(variant==8) memset(lighting.distant,0,sizeof(lighting.distant));
        float angle=variant&1?.37f:-.23f,c=cosf(angle),s=sinf(angle);
        float rows[24]={c,-s,0,0,s,c,0,0,0,0,1,0,c,-s,0,0,s,c,0,0,0,0,1,0};
        float nw[3]={c*normals[n][0]-s*normals[n][1],s*normals[n][0]+c*normals[n][1],normals[n][2]};
        float length=sqrtf(nw[0]*nw[0]+nw[1]*nw[1]+nw[2]*nw[2]);
        for(int axis=0;axis<3;++axis) nw[axis]/=length;
        unsigned int expected=255;
        for(int channel=0;channel<3;++channel) {
            float light=(int)(lighting.ambient[channel]*255);
            for(unsigned int i=0;i<lighting.count;++i) {
                float dot=0;for(int axis=0;axis<3;++axis) dot+=nw[axis]*lighting.distant[i].direction[axis];
                light+=(int)(lighting.distant[i].color[channel]*255)*fmaxf(0,dot);
                if(i==0 && m.translucency>0) light+=(int)(lighting.distant[i].color[channel]*m.translucency*255)*fmaxf(0,-dot);
            }
            light=fminf(255,light);
            if(m.multipurpose) {
                light=fminf(255,light+128.f*((m.emission>>(channel*8))&255)/255.f);
                light*=((255-192)+192.f*((m.tint>>(channel*8))&255)/255.f)/255.f;
            }
            light*=(192-channel*64)/255.f;
            expected|=(unsigned int)(light+.5f)<<(24-channel*8);
        }
        for(int i=0;i<4;++i) {
            rigid[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{0,0,0},{.5f,.5f}};
            memcpy(rigid[i].color,normals[n],12);memcpy(skin+i,rigid+i,sizeof(*rigid));
            skin[i].bones[0]=0;skin[i].bones[1]=3;skin[i].bones[2]=(i&1)?1.f:.35f;skin[i].bones[3]=(i&1)?0.f:.65f;
        }
        n3ds_gpu_geometry_flush(skin,4*sizeof(*skin));n3ds_gpu_geometry_flush(rigid,4*sizeof(*rigid));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!n3ds_gpu_texture_bind(0,base)) goto done;
        int success=variant&1?n3ds_gpu_model_skin_draw(skin,indices,6,rows,2,uv,0,0,0,&m):
            n3ds_gpu_model_rigid_draw(rigid,indices,6,rows,uv,0,0,0,&m);
        if(!success) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        for(unsigned int y=190;y<210;++y) for(unsigned int x=110;x<130;++x) {
            unsigned int actual=pixels[y*240+x];
            for(unsigned int shift=8;shift<32;shift+=8) {
                unsigned int delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
                if(delta>worst) worst=delta;
                if(delta>3) {char message[192];snprintf(message,sizeof(message),"FAIL: model native lighting test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;}
            }
        }
    }
    ok=1;{char message[192];snprintf(message,sizeof(message),"PASS: model native lighting: 72 rotated rigid/weighted pixel references, ambient/distant/translucency/zero lights and masks; max difference=%u",worst);n3ds_log(message);}
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(base);n3ds_gpu_texture_destroy(mask);linearFree(skin);linearFree(rigid);linearFree(pixels);return !ok;
}

int n3ds_gpu_model_detail_tests(void)
{
    const unsigned int base_colors[4]={0x30507040,0xc0b0a040,0x90a0b040,0xe0c09040};
    const unsigned int detail_colors[4]={0x50607000,0x90a0b040,0xd0e0f080,0x204080ff};
    const unsigned short indices[6]={0,1,2,0,2,3};const float uv[2]={13.f/3,1};
    unsigned int texels[64],*pixels=linearAlloc(240*400*4),worst=0;
    void *base=NULL,*detail=NULL,*mask=NULL,*cube=NULL;int active=0,ok=0;
    struct native_render_vertex *rigid=linearAlloc(4*sizeof(*rigid));
    struct native_skin_vertex *skin=linearAlloc(4*sizeof(*skin));
    if(!pixels || !rigid || !skin) goto done;
    for(int texture=0;texture<2;++texture) {
        for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
            unsigned int ty=7-y,morton=(x&1)|((ty&1)<<1)|((x&2)<<1)|((ty&2)<<2)|((x&4)<<2)|((ty&4)<<3);
            texels[morton]=(texture?detail_colors:base_colors)[(x>=4)+2*(y>=4)];
        }
        if(texture) detail=n3ds_gpu_texture_create(texels,8,8);else base=n3ds_gpu_texture_create(texels,8,8);
    }
    for(int i=0;i<64;++i) texels[i]=0xc0806040;
    mask=n3ds_gpu_texture_create(texels,8,8);
    unsigned char faces[6*8*8*4];memset(faces,128,sizeof(faces));cube=n3ds_gpu_texture_create_cube(faces,8);
    if(!base || !detail || !mask || !cube) goto done;
    for(unsigned int test=0;test<152;++test) {
        int masked=test<144,mode=masked?test/36:0;
        int weighted=masked?mode&1:(test-144)/4;
        int mask_mode=masked?test%9:0,function=masked?(test/9)%2:(test-144)%2,after=masked?(test/18)%2:((test-144)/2)%2;
        unsigned int quadrant=test%4;
        struct native_model_lighting lighting={.ambient={.2f,.3f,.4f}};
        struct native_model_material m={.multipurpose=masked?mask:NULL,.emission=0xff302010u+(test/9%7)*0x00030201u,.tint=0xff80b0d0u-(test/9%7)*0x00030102u,
            .lighting=&lighting,.base=base,.alpha_test=mode==2,.detail=detail,.detail_function=function,
            .detail_mask=mask_mode,.detail_after=after,.detail_scale={quadrant&1?13.f/3:1,quadrant&2?13.f/3:1}};
        float weight=1;
        if(mask_mode) {const float channels[4]={192.f/255,128.f/255,96.f/255,64.f/255};weight=channels[(mask_mode-1)/2];if(mask_mode&1) weight=1-weight;}
        unsigned int expected=255;
        for(int c=0;c<3;++c) {
            float light=(int)(lighting.ambient[c]*255)/255.f;
            if(masked) {
                light=fminf(1,light+128.f/255*((m.emission>>(8*c))&255)/255.f);
                light*=(1-96.f/255)+96.f/255*((m.tint>>(8*c))&255)/255.f;
            }
            float value=((base_colors[1]>>(24-c*8))&255)/255.f;
            float d=((detail_colors[quadrant]>>(24-c*8))&255)/255.f;
            d=d*weight+(function?1:.5f)*(1-weight);
            if(after) value*=light;
            value=fminf(1,value*d*(function?1:2));
            if(!after) value*=light;
            float clear=((0x102030ffu>>(24-c*8))&255)/255.f;
            if(mode==2) value=clear;
            if(mode==3) value=value*64.f/255+clear*191.f/255;
            expected|=(unsigned int)(value*255+.5f)<<(24-c*8);
        }
        for(int i=0;i<4;++i) {
            rigid[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{0,0,1},{.1875f,.1875f}};
            memcpy(skin+i,rigid+i,sizeof(*rigid));skin[i].bones[0]=0;skin[i].bones[1]=84;skin[i].bones[2]=(i&1)?1.f:.35f;skin[i].bones[3]=(i&1)?0.f:.65f;
        }
        n3ds_gpu_geometry_flush(rigid,4*sizeof(*rigid));n3ds_gpu_geometry_flush(skin,4*sizeof(*skin));
        float rows[NATIVE_GPU_MODEL_BONES*12]={0},offscreen[NATIVE_GPU_MODEL_BONES*12]={0};
        for(int i=0;i<NATIVE_GPU_MODEL_BONES;++i) {rows[i*12]=rows[i*12+5]=rows[i*12+10]=1;}
        memcpy(offscreen,rows,sizeof(rows));for(int i=0;i<NATIVE_GPU_MODEL_BONES;++i) offscreen[i*12+3]=1000;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        struct native_model_material prior=m;
        if(test%3==0) {prior.detail=NULL;prior.reflection=cube;prior.multipurpose=mask;}
        else if(test%3==1) prior.detail=NULL;
        else {prior.detail_function=1-function;prior.detail_mask=(mask_mode+1)%9;prior.multipurpose=mask;prior.detail_after=!after;prior.detail_scale[0]=.5f;prior.detail_scale[1]=2;}
        n3ds_gpu_model_batch_begin();
        int result=weighted?n3ds_gpu_model_skin_draw(skin,indices,6,offscreen,NATIVE_GPU_MODEL_BONES,uv,0,mode==3,1,&prior):n3ds_gpu_model_rigid_draw(rigid,indices,6,offscreen,uv,0,mode==3,1,&prior);
        if(result) result=weighted?n3ds_gpu_model_skin_draw(skin,indices,6,rows,NATIVE_GPU_MODEL_BONES,uv,0,mode==3,1,&m):n3ds_gpu_model_rigid_draw(rigid,indices,6,rows,uv,0,mode==3,1,&m);
        n3ds_gpu_model_batch_end();if(!result) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        for(unsigned int y=198;y<202;++y) for(unsigned int x=118;x<122;++x) for(unsigned int shift=8;shift<32;shift+=8) {
            unsigned int actual=pixels[y*240+x],delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>worst) worst=delta;
            if(delta>4) {char message[192];snprintf(message,sizeof(message),"FAIL: model detail test=%u expected=%08x actual=%08x difference=%u",test,expected,actual,delta);n3ds_log(message);goto done;}
        }
    }
    ok=1;{char message[240];snprintf(message,sizeof(message),"PASS: model native detail:152 GPU references,9 masks,2 multiply modes,before/after light,independent UV scales,rigid/29-bone,cutout/decal,animated constants and retained material transitions; max difference=%u",worst);n3ds_log(message);}
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(base);n3ds_gpu_texture_destroy(detail);n3ds_gpu_texture_destroy(mask);n3ds_gpu_texture_destroy(cube);
    linearFree(skin);linearFree(rigid);linearFree(pixels);return !ok;
}
int n3ds_gpu_model_reflection_tests(void)
{
    const float h=.70710678f,normals[6][3]={{h,0,h},{-h,0,h},{0,h,h},{0,-h,h},{0,0,1},{1,0,0}};
    const unsigned int face_colors[6]={0xc03050ff,0x30b070ff,0x5080d0ff,0xd0a020ff,0x9060b0ff,0x60c0a0ff};
    const unsigned short indices[6]={0,1,2,0,2,3};const float uv[2]={1,1};
    unsigned int texels[64],*pixels=linearAlloc(240*400*4),worst=0;
    unsigned char faces[6*8*8*4];void *cube=NULL,*base=NULL,*translucent=NULL,*mask=NULL;int active=0,ok=0;
    struct native_render_vertex *rigid=linearAlloc(4*sizeof(*rigid));
    struct native_skin_vertex *skin=linearAlloc(4*sizeof(*skin));
    if(!pixels || !rigid || !skin) goto done;
    for(int face=0;face<6;++face) for(int i=0;i<64;++i) for(int c=0;c<4;++c)
        faces[(face*64+i)*4+c]=face_colors[face]>>(24-c*8);
    cube=n3ds_gpu_texture_create_cube(faces,8);if(!cube) goto done;
    for(int i=0;i<64;++i) texels[i]=0x8060a0ff;
    base=n3ds_gpu_texture_create(texels,8,8);if(!base) goto done;
    for(int i=0;i<64;++i) texels[i]=0x8060a040;
    translucent=n3ds_gpu_texture_create(texels,8,8);if(!translucent) goto done;
    for(int i=0;i<64;++i) texels[i]=0xc08060ff;
    mask=n3ds_gpu_texture_create(texels,8,8);if(!mask) goto done;
    for(unsigned int test=0;test<96;++test) {
        unsigned int face=test%6,mode=test/24;int weighted=(test/12)&1;float sign=(test/6)&1?-1.f:1.f;
        float normal[3];for(int c=0;c<3;++c) normal[c]=sign*normals[face][c];
        struct native_model_lighting lighting={.ambient={.2f,.3f,.4f},
            .distant={{{.2f,.3f,.1f},{.3f,.4f,.8660254f}}},.count=1};
        struct native_model_material m={.multipurpose=mask,.emission=0xff302010,.tint=0xff80b0d0,.lighting=&lighting,
            .reflection=cube,.base=mode?translucent:base,.alpha_test=mode==2,.translucency=.4f,.reflection_delta={.3f,.1f,-.2f,.25f},.reflection_parallel={.4f,.6f,.7f,.4f}};
        /* All four corners are equidistant from the eye; the centre's
         * interpolated signed N.V is Nz * 2/sqrt(4+2*.8*.8). */
        float cosine=normal[2]*2/sqrtf(5.28f);unsigned int expected=255;
        float dot=normal[0]*.3f+normal[1]*.4f+normal[2]*.8660254f;
        for(int c=0;c<3;++c) {
            float light=(int)(lighting.ambient[c]*255)+(int)(lighting.distant[0].color[c]*255)*fmaxf(0,dot)
                +(int)(lighting.distant[0].color[c]*m.translucency*255)*fmaxf(0,-dot);
            float diffuse=fminf(255,light+128.f*((m.emission>>(c*8))&255)/255.f);
            diffuse*=((255-96)+96.f*((m.tint>>(c*8))&255)/255.f)/255.f;
            diffuse*=((0x8060a0ffu>>(24-c*8))&255)/255.f;
            float tint=m.reflection_parallel[c]+m.reflection_delta[c]*cosine;
            float brightness=m.reflection_parallel[3]+m.reflection_delta[3]*cosine;
            float reflection=((face_colors[face]>>(24-c*8))&255)*tint*brightness*(192.f/255);
            float color=fminf(255,diffuse+reflection),clear=(0x102030ffu>>(24-c*8))&255;
            if(mode==2) color=clear;
            if(mode==3) color=color*64.f/255+clear*191.f/255;
            expected|=(unsigned int)(color+.5f)<<(24-c*8);
        }
        for(int i=0;i<4;++i) {
            rigid[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{0,0,0},{.5f,.5f}};
            memcpy(rigid[i].color,normal,12);memcpy(skin+i,rigid+i,sizeof(*rigid));
            skin[i].bones[0]=0;skin[i].bones[1]=(NATIVE_GPU_MODEL_BONES-1)*3;skin[i].bones[2]=(i&1)?1.f:.35f;skin[i].bones[3]=(i&1)?0.f:.65f;
        }
        n3ds_gpu_geometry_flush(rigid,4*sizeof(*rigid));n3ds_gpu_geometry_flush(skin,4*sizeof(*skin));
        float rows[NATIVE_GPU_MODEL_BONES*12]={0};
        for(int i=0;i<NATIVE_GPU_MODEL_BONES;++i) rows[i*12]=rows[i*12+5]=rows[i*12+10]=1;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        int result=weighted?n3ds_gpu_model_skin_draw(skin,indices,6,rows,NATIVE_GPU_MODEL_BONES,uv,0,mode==3,1,&m):
            n3ds_gpu_model_rigid_draw(rigid,indices,6,rows,uv,0,mode==3,1,&m);
        if(!result) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        for(unsigned int y=198;y<202;++y) for(unsigned int x=118;x<122;++x) {
            unsigned int actual=pixels[y*240+x];
            for(unsigned int shift=8;shift<32;shift+=8) {
                unsigned int delta=abs((int)((actual>>shift)&255)-(int)((expected>>shift)&255));
                if(delta>worst) worst=delta;
                if(delta>3) {char message[192];snprintf(message,sizeof(message),"FAIL: model reflection test=%u expected=%08x actual=%08x",test,expected,actual);n3ds_log(message);goto done;}
            }
        }
    }
    ok=1;{char message[192];snprintf(message,sizeof(message),"PASS: model native reflections:96 cube/base/mask/light pixel references,6 faces,signed normals,rigid/29-bone,opaque/cutout/decal; max difference=%u",worst);n3ds_log(message);}
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(translucent);n3ds_gpu_texture_destroy(cube);n3ds_gpu_texture_destroy(base);n3ds_gpu_texture_destroy(mask);
    linearFree(rigid);linearFree(skin);linearFree(pixels);return !ok;
}


int n3ds_gpu_model_resident_tests(void)
{
    const unsigned short indices[6]={0,2,1,0,3,2};const float uv[2]={1,1};
    const float rows[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    struct native_render_vertex vertices[4];
    unsigned int *reference=linearAlloc(240*400*4),*pixels=linearAlloc(240*400*4);
    void *first=NULL,*pressure[128]={0};unsigned int pressure_count=0;int active=0,ok=0;
    if(!reference || !pixels) goto done;
    n3ds_gpu_model_cache_release();
    for(int i=0;i<4;++i) vertices[i]=(struct native_render_vertex){
        {i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{0,0,1},{.5f,.5f}};
    for(int pass=0;pass<2;++pass) {
        if(!n3ds_gpu_frame_begin()) goto done;active=1;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        n3ds_gpu_window_begin(&camera);n3ds_gpu_texture_bind(0,NULL);
        void *v=n3ds_gpu_model_resident_upload(0,1,vertices,sizeof(vertices));
        if(!v || (pass && v!=first)) goto done;
        if(!pass) first=v;
        if(memcmp(v,vertices,sizeof(vertices)) || n3ds_gpu_model_resident_upload(0,1,vertices,sizeof(vertices))!=v) goto done;
        if(!n3ds_gpu_model_rigid_draw(v,indices,6,rows,uv,0,0,0,NULL)) goto done;
        /* Reusing a cache slot must not overwrite the already queued draw. */
        struct native_render_vertex changed[4];memcpy(changed,vertices,sizeof(changed));changed[0].position[0]=4;
        if(n3ds_gpu_model_resident_upload(0,2,changed,sizeof(changed)) || memcmp(v,vertices,sizeof(vertices))) goto done;
        if(n3ds_gpu_model_resident_upload(256,1,vertices,sizeof(vertices)) ||
           n3ds_gpu_model_resident_upload(1,0,vertices,sizeof(vertices)) ||
           n3ds_gpu_model_resident_upload(1,1,NULL,sizeof(vertices)) ||
           n3ds_gpu_model_resident_upload(1,1,vertices,0) ||
           n3ds_gpu_model_resident_upload(1,1,vertices,MODEL_RESIDENT_LIMIT*sizeof(*vertices)+1)) goto done;
        n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(pass?pixels:reference,240*400)) goto done;
    }
    unsigned int covered=0;
    for(unsigned int i=0;i<240*400;++i) {
        if(reference[i]!=pixels[i]) goto done;
        covered+=pixels[i]!=0x102030ff;
    }
    if(covered<1000 || resident_misses!=1) goto done;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    vertices[0].position[0]=-.5f;
    void *revised=n3ds_gpu_model_resident_upload(0,2,vertices,sizeof(vertices));
    if(!revised || memcmp(revised,vertices,sizeof(vertices)) || resident_misses!=2) goto done;
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    /* Dynamic geometry may reclaim an inactive resident working set. */
    struct native_render_vertex *arena=n3ds_gpu_model_vertices_allocate(MODEL_VERTEX_COUNT);
    if(!arena || model_resident_units || model_reserved!=MODEL_VERTEX_COUNT) goto done;
    arena[0].position[0]=37;
    if(n3ds_gpu_model_resident_upload(1,3,vertices,sizeof(vertices)) || arena[0].position[0]!=37) goto done;
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    /* And new residents can reclaim unused blocks after the next fence. */
    if(!n3ds_gpu_model_resident_upload(1,3,vertices,sizeof(vertices)) || model_reserved ||
       model_resident_units+model_reserved>MODEL_VERTEX_COUNT) goto done;
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    void *protected=n3ds_gpu_model_resident_upload(1,3,vertices,sizeof(vertices));
    if(!protected) goto done;
    /* Exhaust real linear space, while retaining a current-frame GPU owner. */
    while(linearSpaceFree()>MODEL_RESIDENT_HEADROOM-256*1024) {
        unsigned int size=linearSpaceFree()-(MODEL_RESIDENT_HEADROOM-256*1024);
        if(size>1024*1024) size=1024*1024;
        if(pressure_count==128) goto done;
        void *allocation=NULL;
        while(size>=128 && !(allocation=linearAlloc(size))) size/=2;
        if(!allocation) goto done;
        pressure[pressure_count++]=allocation;
    }
    if(n3ds_gpu_model_resident_upload(2,4,vertices,sizeof(vertices)) || !model_resident_suspended ||
       memcmp(protected,vertices,sizeof(vertices))) goto done;
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    if(model_resident_units || !model_resident_suspended) goto done;
    while(pressure_count) {linearFree(pressure[--pressure_count]);pressure[pressure_count]=NULL;}
    if(n3ds_gpu_model_resident_upload(2,4,vertices,sizeof(vertices))) goto done;
    struct native_render_vertex *fallback=n3ds_gpu_model_vertices_allocate(4);
    if(!fallback) goto done;
    unsigned int fallback_capacity=model_reserved;
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_frame_begin()) goto done;active=1;
    if(model_reserved!=fallback_capacity || !model_resident_suspended) goto done;
    n3ds_gpu_present();active=0;
    n3ds_gpu_model_cache_release();
    if(model_resident_suspended || model_resident_units || model_reserved) goto done;
    n3ds_log("PASS: resident pressure fallback: queued data protected, fenced release, map-latched suspension, transient arena retained and reset on unload");
    ok=1;
done:
    if(active) n3ds_gpu_present();n3ds_gpu_model_cache_release();
    while(pressure_count) linearFree(pressure[--pressure_count]);
    linearFree(reference);linearFree(pixels);
    n3ds_log(ok?"PASS: resident model vertices: cross-frame identical GPU pixels, revision replacement, queued-draw protection, shared6MiB pressure/fallback and map release":"FAIL: resident model vertex lifetime or shared budget");
    return !ok;
}


int n3ds_gpu_water_draw(const struct native_render_vertex *vertices,const unsigned short *indices,
    unsigned int count,const struct native_water_material *m)
{
    if(!water_program.ready || !vertices || !indices || !count || !m || (m->flags&~15u)) return 0;
    C3D_Mtx projection;
    if(!n3ds_gpu_world_projection((float *)&projection) ||
       (m->sky && !n3ds_gpu_sky_projection((float *)&projection))) return 0;
    n3ds_gpu_world_state_restore();
    /* Two-dimensional mask and cube are independently filtered in one draw.
     * Null maps bind the neutral white texture, preserving authored absence. */
    if(!n3ds_gpu_texture_bind(0,m->cube) || !n3ds_gpu_texture_bind_sampler(1,m->base,
        NATIVE_SAMPLER_CLAMP_U|NATIVE_SAMPLER_CLAMP_V)) return 0;
    C3D_BindProgram(&water_program.program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,water_program.projection,&projection);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.camera,current_camera.position[0],current_camera.position[1],current_camera.position[2],0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.fallback,-current_camera.forward[0],-current_camera.forward[1],-current_camera.forward[2],0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.delta,m->perpendicular[0]-m->parallel[0],m->perpendicular[1]-m->parallel[1],m->perpendicular[2]-m->parallel[2],m->perpendicular[3]-m->parallel[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.parallel,m->parallel[0],m->parallel[1],m->parallel[2],m->parallel[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.wave_switch,m->wave_phase?1.f:0.f,0,0,0);
    if(m->wave_phase) C3D_FVUnifSet(GPU_VERTEX_SHADER,water_program.wave_phase,m->wave_phase[0],m->wave_phase[1],m->wave_phase[2],m->wave_phase[3]);
    unsigned int stride=m->wave_phase?sizeof(struct native_water_wave_vertex):sizeof(*vertices);
    unsigned int attributes=m->wave_phase?4:3,permutation=m->wave_phase?0x3210:0x210;
    C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
    AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
    if(m->wave_phase) AttrInfo_AddLoader(a,3,GPU_FLOAT,4);
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    native_light_env_bind(NULL);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(!m->sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    n3ds_gpu_model_stencil_apply();
    C3D_TexEnv *e=C3D_GetTexEnv(0);
    if(m->flags&2) {
        /* Preserve dst *= base before the reflection blend. This cannot be
         * folded into ordinary source-alpha blending without a scene read. */
        C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_MULTIPLY);
        if(!n3ds_gpu_indexed_draw(vertices,indices,count,stride,attributes,permutation)) {n3ds_gpu_world_state_restore();return 0;}
    }
    e=C3D_GetTexEnv(0); /* A preceding draw consumed the dirty flag. */
    C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvSrc(e,C3D_Alpha,GPU_PRIMARY_COLOR,GPU_TEXTURE1,GPU_CONSTANT);
    C3D_TexEnvFunc(e,C3D_Alpha,m->flags&1?GPU_MODULATE:GPU_REPLACE);
    n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
    int result=n3ds_gpu_indexed_draw(vertices,indices,count,stride,attributes,permutation);
    n3ds_gpu_texture_bind(1,m->base);n3ds_gpu_world_state_restore();return result;
}

int n3ds_gpu_glass_native_sky_draw(const struct native_render_vertex *vertices,
    const unsigned short *indices,unsigned int count,const struct native_glass_material *m,int sky)
{
    if(!glass_program.ready || !vertices || !indices || !count || !m) return 0;
    C3D_Mtx projection;if(!(sky?n3ds_gpu_sky_projection((float *)&projection):n3ds_gpu_world_projection((float *)&projection))) return 0;
    n3ds_gpu_world_state_restore();
    if(!n3ds_gpu_texture_bind(0,m->cube) || !n3ds_gpu_texture_bind(1,m->diffuse)) return 0;
    C3D_BindProgram(&glass_program.program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,glass_program.projection,&projection);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.camera,current_camera.position[0],current_camera.position[1],current_camera.position[2],0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.fallback,-current_camera.forward[0],-current_camera.forward[1],-current_camera.forward[2],0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.delta,m->perpendicular[0]-m->parallel[0],m->perpendicular[1]-m->parallel[1],m->perpendicular[2]-m->parallel[2],m->perpendicular[3]-m->parallel[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.parallel,m->parallel[0],m->parallel[1],m->parallel[2],m->parallel[3]);
    if(m->pose) for(int i=0;i<3;++i)
        C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.pose+i,m->pose[i*4],m->pose[i*4+1],m->pose[i*4+2],m->pose[i*4+3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.options,0,!!m->pose,m->uv_scale[0],m->uv_scale[1]);
    C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
    AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    native_light_env_bind(NULL);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_CullFace(m->two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    C3D_DepthTest(!sky,GPU_GEQUAL,GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
    n3ds_gpu_model_stencil_apply();
    int ok=1;
    if(m->cube || (m->tint&0xffffffu)!=0xffffffu) {
        C3D_TexEnv *e=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);
        /* Constant destination tint combines the old multiply and additive
         * reflection passes. Keep diffuse separate: their intermediate
         * framebuffer saturation cannot in general be algebraically fused. */
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_CONSTANT_COLOR,GPU_ZERO,GPU_ONE);
        C3D_BlendingColor(m->tint);
        ok=n3ds_gpu_indexed_draw(vertices,indices,count,sizeof(*vertices),3,0x210);
    }
    if(ok && m->diffuse) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,glass_program.options,1,!!m->pose,m->uv_scale[0],m->uv_scale[1]);
        C3D_TexEnv *e=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        C3D_AlphaTest(true,GPU_GREATER,0);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
        ok=n3ds_gpu_indexed_draw(vertices,indices,count,sizeof(*vertices),3,0x210);
    }
    n3ds_gpu_world_state_restore();return ok;
}
int n3ds_gpu_glass_native_draw(const struct native_render_vertex *v,const unsigned short *i,unsigned int n,const struct native_glass_material *m)
{return n3ds_gpu_glass_native_sky_draw(v,i,n,m,0);}
#include "engine_flashlight_tests.inl"

#ifdef HALO_N3DS_RENDERER_TESTS
#include "engine_model_precision_tests.inl"
#include "engine_world_scale_tests.inl"
#endif
