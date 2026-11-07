/* Copied installed values, never pointers into per-draw material packets. */
static struct {
    C3D_Mtx projection;
    float delta[4],parallel[4],phase[4],options[4],pose[12];
    int sky,waves,sides,has_pose;
    unsigned int tint;
} surface_uniforms;
void n3ds_gpu_surface_batch_break(void)
{
    if(surface_state_mode)n3ds_gpu_world_state_restore();
}
void n3ds_gpu_surface_batch_begin(void)
{
    n3ds_gpu_surface_batch_break();surface_batch_active=1;
}
void n3ds_gpu_surface_batch_end(void)
{
    n3ds_gpu_surface_batch_break();surface_batch_active=0;
}
static void surface_vector(int location,const float value[4],float cached[4],int retained)
{
    if(!retained || memcmp(value,cached,4*sizeof(float))) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,location,value[0],value[1],value[2],value[3]);
        memcpy(cached,value,4*sizeof(float));
    }
}
static void surface_projection(int location,const C3D_Mtx *projection,int retained)
{
    if(!retained || memcmp(projection,&surface_uniforms.projection,sizeof(*projection))) {
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,projection);
        surface_uniforms.projection=*projection;
    }
}
static void surface_water_pass(int base,int mask)
{
    int pass=base?1:2+!!mask;
    if(surface_pass==pass)return;
    C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);
    if(base) {
        C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_MULTIPLY);
    } else {
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_PRIMARY_COLOR,GPU_TEXTURE1,GPU_CONSTANT);
        C3D_TexEnvFunc(e,C3D_Alpha,mask?GPU_MODULATE:GPU_REPLACE);
        n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
    }
    surface_pass=pass;
}
static void surface_glass_pass(int diffuse,unsigned int tint)
{
    int pass=diffuse?2:1;
    if(surface_pass!=pass) {
        C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);
        if(diffuse) {
            C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_CONSTANT);
            C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
            C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);
            C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
            C3D_AlphaTest(true,GPU_GREATER,0);
            n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ALPHA);
        } else {
            C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
            C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);
            C3D_AlphaTest(false,GPU_ALWAYS,0);
            C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_CONSTANT_COLOR,GPU_ZERO,GPU_ONE);
        }
    }
    if(!diffuse && (surface_pass!=pass || tint!=surface_uniforms.tint)) {
        C3D_BlendingColor(tint);surface_uniforms.tint=tint;
    }
    surface_pass=pass;
}
