/* Two native samplers for hair; three for body. Only the animated stream
 * mixture uses the bounded CPU bake. Body detail and projected noise are
 * evaluated per fragment, with no intermediate framebuffer or extra pass. */
int n3ds_gpu_hologram_draw(const struct native_chicago_material *m,
    struct native_chicago_vertex *v,unsigned int n,const unsigned short *indices,
    unsigned int count,const float screen[3][4],const float tint[3],float noise)
{
    C3D_Mtx projection;
    float origin[3];
    if(!hologram_ready || !m || (m->maps!=2 && m->maps!=3) || m->blend!=NATIVE_BLEND_ADD ||
       !v || !n || !indices || !count || (uintptr_t)v<(uintptr_t)stream ||
       (uintptr_t)v>(uintptr_t)(stream+used) || n>(unsigned int)(stream+used-v) ||
       !n3ds_gpu_model_projection((float *)&projection,origin)) return 0;
    if(m->sky && !n3ds_gpu_sky_projection((float *)&projection))return 0;
    for(unsigned int i=0;i<count;++i) if(indices[i]>=n) return 0;
    void *textures[3];for(int i=0;i<m->maps;++i) {textures[i]=n3ds_gpu_texture_bound(i);if(!textures[i]) return 0;}
    n3ds_gpu_world_state_restore();
    C3D_BindProgram(&hologram_program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,hologram_projection,&projection);
    for(int i=0;i<3;++i) C3D_FVUnifSet(GPU_VERTEX_SHADER,hologram_screen+i,screen[i][0],screen[i][1],screen[i][2],screen[i][3]);
    C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);AttrInfo_AddLoader(a,0,GPU_FLOAT,3);
    for(int i=1;i<4;++i) AttrInfo_AddLoader(a,i,GPU_FLOAT,2);
    AttrInfo_AddLoader(a,4,GPU_FLOAT,4);
    for(int i=0;i<6;++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both,0);C3D_TexEnvBufColor(0);
    unsigned int constant=((unsigned int)(fminf(fmaxf(noise,0),1)*255.f+.5f))<<24;
    for(int c=0;c<3;++c) constant|=(unsigned int)(fminf(fmaxf(tint[c],0),1)*255.f+.5f)<<(c*8);
    C3D_TexEnv *env=C3D_GetTexEnv(0);C3D_TexEnvColor(env,constant);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);
    /* Generic alpha source15 is texture0 BLUE (cross-component enum). */
    C3D_TexEnvOpAlpha(env,GPU_TEVOP_A_SRC_B,GPU_TEVOP_A_SRC_ALPHA,0);
    env=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE2,GPU_CONSTANT);
    C3D_TexEnvFunc(env,C3D_RGB,m->maps==3?GPU_ADD:GPU_REPLACE);
    env=C3D_GetTexEnv(2);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS,GPU_CONSTANT);
    C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,0);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(env,C3D_RGB,GPU_TEVSCALE_4);
    env=C3D_GetTexEnv(3);C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_CullFace(m->two_sided?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
    C3D_DepthTest(!m->sky,GPU_GEQUAL,GPU_WRITE_COLOR&~GPU_WRITE_ALPHA);
    C3D_AlphaTest(false,GPU_ALWAYS,0);n3ds_gpu_model_stencil_apply();n3ds_gpu_framebuffer_blend(NATIVE_BLEND_ADD);
    for(int i=0;i<m->maps;++i) {
        unsigned int flags=(i==0?NATIVE_SAMPLER_PROJECTIVE:0)|
            (m->point[i]?(NATIVE_SAMPLER_MIN_POINT|NATIVE_SAMPLER_MAG_POINT):0)|
            (m->clamp_u[i]?NATIVE_SAMPLER_CLAMP_U:0)|(m->clamp_v[i]?NATIVE_SAMPLER_CLAMP_V:0);
        if(!n3ds_gpu_texture_bind_sampler(i,textures[i],flags)) return 0;
    }
    int result=n3ds_gpu_indexed_draw(v,indices,count,sizeof(*v),5,0x43210);
    for(int i=0;i<m->maps;++i) n3ds_gpu_texture_bind(i,textures[i]);
    n3ds_gpu_world_state_restore();return result;
}
