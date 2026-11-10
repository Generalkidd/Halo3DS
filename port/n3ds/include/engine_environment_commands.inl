/* New3DS integration additions by Kevin Boggs. */
/* Environment geometry already has a GPU-frame lifetime: resident BSP
 * vertices, light coordinates and per-eye indices remain immutable until
 * FrameBegin's GPU wait. Copy animated material values, not those meshes. */
#ifdef HALO_N3DS_RENDER_COMMANDS
struct native_environment_command {
    const struct native_render_vertex *vertices;
    const void *coordinates;
    const unsigned short *indices;
    unsigned int count,material,on,phase;
    int kind,lightmap,detail,alpha_test;
    float scale[2];
    struct native_environment_emission emission;
};
/* Kevin Boggs: environment_command_execute */
static int environment_command_execute(const void *payload)
{
    const struct native_environment_command *p=payload;
    if(p->kind==3)return n3ds_gpu_environment_bump_draw(p->vertices,p->coordinates,p->indices,p->count,
        p->scale[0],p->scale[1],p->material,p->alpha_test);
    if(p->kind==2)return n3ds_gpu_environment_plasma_draw(p->vertices,p->coordinates,p->indices,p->count,
        p->lightmap,p->material,p->alpha_test,&p->emission,p->on,p->phase);
    if(p->kind==1)return n3ds_gpu_environment_emission_draw(p->vertices,p->coordinates,p->indices,p->count,
        p->lightmap,p->material,p->alpha_test,&p->emission);
    return n3ds_gpu_environment_draw(p->vertices,p->coordinates,p->indices,p->count,
        p->lightmap,p->detail,p->scale[0],p->scale[1],p->material,p->alpha_test);
}
/* Kevin Boggs: environment_command_record */
static int environment_command_record(int kind,const struct native_render_vertex *vertices,
    const void *coordinates,const unsigned short *indices,unsigned int count,int lightmap,int detail,
    float u,float v,unsigned int material,int alpha_test,const struct native_environment_emission *emission,
    unsigned int on,unsigned int phase)
{
    if(!n3ds_render_commands_recording())return 0;
    struct native_environment_command *p=n3ds_render_command_allocate(environment_command_execute,sizeof(*p));
    if(!p)return 0;
    *p=(struct native_environment_command){.vertices=vertices,.coordinates=coordinates,.indices=indices,
        .count=count,.material=material,.on=on,.phase=phase,.kind=kind,.lightmap=lightmap,
        .detail=detail,.alpha_test=alpha_test,.scale={u,v}};
    if(emission)p->emission=*emission;
#ifdef HALO_N3DS_RENDER_WORKER
    if(render_commands.count>=32)n3ds_render_commands_submit();
#endif
    return 1;
}
/* Kevin Boggs: environment_command_end_execute */
static int environment_command_end_execute(const void *unused)
{
    (void)unused;n3ds_gpu_world_state_restore();return 1;
}
#endif
/* Kevin Boggs: n3ds_gpu_environment_batch_end */
void n3ds_gpu_environment_batch_end(void)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    if(n3ds_render_commands_recording() && n3ds_render_command_allocate(environment_command_end_execute,0)) {
        if(!n3ds_render_commands_submit())n3ds_render_commands_flush(N3DS_RENDER_FENCE_STATE);
        return;
    }
#endif
    n3ds_gpu_world_state_restore();
}
