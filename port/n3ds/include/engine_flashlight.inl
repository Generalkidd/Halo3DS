/* One spot light in the existing PICA material pass. No projected mesh,
 * texture upload, CPU vertex lighting or second scene submission. The BSP
 * secondary channel avoids multiplying the lightmap twice. */
static C3D_LightEnv flashlight_env;
static C3D_Light flashlight_plain,flashlight_bump,flashlight_model;
static C3D_LightLut flashlight_spot,flashlight_constant;
static C3D_LightLutDA flashlight_distance;
static float flashlight_power,flashlight_origin[3],flashlight_camera[3];
static int flashlight_ready,flashlight_camera_valid;
static float flashlight_cone(float cosine,float unused)
{
    (void)unused;float t=fminf(1,fmaxf(0,(cosine-.9063078f)/(.9612617f-.9063078f)));
    return t*t*(3-2*t);
}
static float flashlight_falloff(float distance,float unused,float unused2)
{
    (void)unused;(void)unused2;
    float t=fmaxf(0,1-distance/8.f);return t*t;
}
static float flashlight_one(float x,float unused) {(void)x;(void)unused;return 1;}
static int flashlight_initialize(void)
{
    flashlight_power=0;flashlight_camera_valid=0;
    LightLut_FromFunc(&flashlight_spot,flashlight_cone,0,true);
    LightLut_FromFunc(&flashlight_constant,flashlight_one,0,false);
    LightLutDA_Create(&flashlight_distance,flashlight_falloff,0,8,0,0);
    C3D_LightEnvInit(&flashlight_env);
    C3D_Material material={.specular0={1,1,1}};
    C3D_LightEnvMaterial(&flashlight_env,&material);
    C3D_LightEnv *envs[3]={&flashlight_env,&bump_light_env,&model_light_env};
    C3D_Light *lights[3]={&flashlight_plain,&flashlight_bump,&flashlight_model};
    for(int i=0;i<3;++i) {
        if(C3D_LightInit(lights[i],envs[i])<0) return 0;
        C3D_LightDiffuse(lights[i],0,0,0);
        C3D_LightSpecular0(lights[i],0,0,0);C3D_LightSpecular1(lights[i],0,0,0);
        C3D_LightSpotLut(lights[i],&flashlight_spot);
        C3D_LightDistAttn(lights[i],&flashlight_distance);
        C3D_LightEnable(lights[i],false);
        C3D_LightSpotEnable(lights[i],false);C3D_LightDistAttnEnable(lights[i],false);
        /* Citro3D uploads dirty LUTs even on a disabled light. Detach it when
         * off so ordinary material switches retain their original cost. */
        if(i>0) {envs[i]->lights[lights[i]->id]=NULL;envs[i]->flags|=C3DF_LightEnv_LCDirty;}
        if(i==0) {
            C3D_LightEnvLut(envs[i],GPU_LUT_D0,GPU_LUTINPUT_NH,false,&flashlight_constant);
            C3D_LightEnvClampHighlights(envs[i],false);
        }
    }
    flashlight_ready=1;return 1;
}
void n3ds_gpu_flashlight_set(float power,const float origin[3],const float forward[3])
{
    if(!flashlight_ready) return;
    power=isfinite(power)?fminf(1,fmaxf(0,power)):0;
    if(!origin || !forward) power=0;
    if(!power && !flashlight_power) return;
    environment_state_valid=bump_state_valid=0;
    if(!!power!=!!flashlight_power) {
        bump_light_env.lights[flashlight_bump.id]=power?&flashlight_bump:NULL;
        model_light_env.lights[flashlight_model.id]=power?&flashlight_model:NULL;
        bump_light_env.flags|=C3DF_LightEnv_LCDirty;model_light_env.flags|=C3DF_LightEnv_LCDirty;
        C3D_LightEnvLut(&bump_light_env,GPU_LUT_D0,GPU_LUTINPUT_NH,false,power?&flashlight_constant:NULL);
        /* A light can toggle before any bump material consumed the pending
         * LUT upload. Citro3D does not clear that dirty flag on a NULL LUT. */
        if(!power) bump_light_env.flags&=~C3DF_LightEnv_LutDirty(0);
        C3D_LightEnvClampHighlights(&bump_light_env,!power);
    }
    flashlight_power=power;flashlight_camera_valid=0;
    C3D_Light *lights[3]={&flashlight_plain,&flashlight_bump,&flashlight_model};
    for(int i=0;i<3;++i) {
        C3D_LightEnable(lights[i],power>0);
        C3D_LightSpotEnable(lights[i],power>0);C3D_LightDistAttnEnable(lights[i],power>0);
        if(!power) continue;
        C3D_LightSpotDir(lights[i],forward[0],forward[1],forward[2]);
        if(i<2) {
            C3D_FVec position=FVec4_New(origin[0],origin[1],origin[2],1);
            C3D_LightPosition(lights[i],&position);
            C3D_LightSpecular0(lights[i],power*.85f,power*.82f,power*.75f);
        } else {
            C3D_LightAmbient(lights[i],power*.17f,power*.164f,power*.15f);
            C3D_LightDiffuse(lights[i],power*.68f,power*.656f,power*.60f);
        }
    }
    if(power) memcpy(flashlight_origin,origin,sizeof(flashlight_origin));
}
static void flashlight_model_camera_update(void)
{
    if(!flashlight_power || (flashlight_camera_valid && !memcmp(flashlight_camera,current_camera.position,sizeof(flashlight_camera)))) return;
    C3D_FVec relative=FVec4_New(flashlight_origin[0]-current_camera.position[0],
        flashlight_origin[1]-current_camera.position[1],flashlight_origin[2]-current_camera.position[2],1);
    C3D_LightPosition(&flashlight_model,&relative);
    memcpy(flashlight_camera,current_camera.position,sizeof(flashlight_camera));flashlight_camera_valid=1;
}
static void flashlight_environment_combine(int emission)
{
    if(!flashlight_power) return;
    C3D_TexEnv *e=C3D_GetTexEnv(5);
    if(emission) {
        C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_FRAGMENT_SECONDARY_COLOR,GPU_TEXTURE0);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD_MULTIPLY);
    } else {
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_FRAGMENT_SECONDARY_COLOR,GPU_PREVIOUS);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
    }
}
