/* New3DS integration additions by Kevin Boggs. */
/* Kevin Boggs: n3ds_chicago_command_tests */
int n3ds_chicago_command_tests(void)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),pixels[64];
    void *textures[3]={0};int active=0,batch=0,ok=0;unsigned int cases=0;
    if(!reference || !actual)goto done;
    for(int t=0;t<3;++t) {
        for(int i=0;i<64;++i)pixels[i]=((60+(i%8)*18)<<24)|((50+(i/8)*20)<<16)|((80+t*40)<<8)|200;
        textures[t]=n3ds_gpu_texture_create(pixels,8,8);if(!textures[t])goto done;
    }
    for(unsigned int path=0;path<4;++path)for(unsigned int variant=0;variant<(path<2?12:6);++variant) {
        for(unsigned int pass=0;pass<2;++pass) {
            struct native_render_camera camera={.position={1,2,3},.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&camera);
            C3D_Tex descriptors[3];
            for(int i=0;i<3;++i) {descriptors[i]=*(C3D_Tex *)textures[i];if(!n3ds_gpu_texture_bind(i,descriptors+i))goto done;}
            float rows[24]={1,0,0,1,0,1,0,2,0,0,1,3,1,0,0,1.1f,0,1,0,2,0,0,1,3},uv[24]={0};
            for(int i=0;i<3;++i){uv[i*8]=.8f;uv[i*8+3]=.1f;uv[i*8+5]=.8f;uv[i*8+7]=.1f;}
            unsigned short indices[6]={0,1,2,0,2,3};
            struct native_chicago_material m={.maps=1+variant%3,.blend=variant%4,.two_sided=1,.fade=.7f,.sky=variant==5};
            if(m.sky){rows[3]=rows[7]=rows[11]=rows[15]=rows[19]=rows[23]=0;}
            for(int i=0;i<2;++i){m.color_function[i]=2;m.alpha_function[i]=0;}
            for(int i=0;i<3;++i){m.point[i]=variant&1;m.clamp_u[i]=variant&2;m.clamp_v[i]=variant&4;}
            if(variant==6 || variant==7){m.maps=1;m.vertex_fade=variant-5;}
            if(variant==8){m.maps=1;m.meter=1;m.fade=1;m.meter_tint=0x705030ff;}
            if(variant==9){m.maps=1;m.vertex_color=1;}
            if(variant==10){m.maps=2;m.blend=0;m.generic_lit_lerp=1;}
            if(variant==11){m.maps=2;m.shield_mask=1;m.shield_tint=0x908050ff;}
            struct native_chicago_vertex *cpu=NULL;void *gpu=NULL;
            if(path<2)cpu=n3ds_gpu_chicago_allocate(4);
            else gpu=n3ds_gpu_model_vertices_allocate(path==3?6:4);
            if(!cpu && !gpu)goto done;
            for(int i=0;i<4;++i) {
                float x=(i==1 || i==2)?.8f:-.8f,y=i>=2?.7f:-.7f;
                if(cpu) {
                    memset(cpu+i,0,sizeof(*cpu));
                    cpu[i].position[0]=x+(path==0 && !m.sky?1:0);cpu[i].position[1]=y+(path==0 && !m.sky?2:0);cpu[i].position[2]=-3+(path==0 && !m.sky?3:0);
                    for(int t=0;t<3;++t){cpu[i].uv[t][0]=(i==1||i==2)?.8f:.2f;cpu[i].uv[t][1]=i>=2?.8f:.2f;}
                    for(int c=0;c<4;++c)cpu[i].color[c]=.4f+c*.15f;
                } else {
                    struct native_render_vertex v={{x,y,-3},{0,0,1},{(i==1||i==2)?.8f:.2f,i>=2?.8f:.2f}};
                    if(path==3){struct native_skin_vertex *s=gpu;memcpy(s+i,&v,sizeof(v));s[i].bones[0]=0;s[i].bones[1]=3;s[i].bones[2]=.4f;s[i].bones[3]=.6f;}
                    else ((struct native_render_vertex *)gpu)[i]=v;
                }
            }
            if(gpu)n3ds_gpu_geometry_flush(gpu,4*(path==3?sizeof(struct native_skin_vertex):sizeof(struct native_render_vertex)));
            unsigned int before=chicago_command_recorded;
            unsigned int repeats=path==2 && variant==0?80:1;
            if(repeats>1) {
                /* The first draw dirties the ordinary model shader cache.
                 * Switching to transparent material must restore state in
                 * sequence even when the two commands replay on a worker. */
                struct native_model_material material={.base=textures[0],.tint=0xffffffffu,.emission=0xff000000u};
                const float scale[2]={1,1};
                n3ds_gpu_model_batch_begin();batch=1;
                if(!n3ds_gpu_model_rigid_draw(gpu,indices,6,rows,scale,0,0,1,&material))goto done;
                if(pass)n3ds_gpu_model_prepare_boundary();else n3ds_gpu_model_batch_flush();
            }
            if(pass) {
                for(unsigned int repeat=0;repeat<repeats;++repeat)
                    if(!chicago_draw(&m,cpu,4,indices,6,gpu,rows,path==3?2:1,path==3,uv,path==1))goto done;
                if(chicago_command_recorded!=before+repeats)goto done;
                /* All CPU inputs can be overwritten before replay. Published
                 * immutable GPU geometry deliberately retains frame lifetime. */
                memset(&m,0,sizeof(m));memset(indices,0,sizeof(indices));memset(rows,0,sizeof(rows));memset(uv,0,sizeof(uv));
                memset(descriptors,0,sizeof(descriptors));if(cpu)memset(cpu,0,4*sizeof(*cpu));
                n3ds_render_commands_submit();
            } else {
                struct native_chicago_command p;
                if(!chicago_command_prepare(&p,&m,cpu,4,indices,6,gpu,rows,path==3?2:1,path==3,uv,path==1))goto done;
                if(cpu)chicago_vertex_colors(&m,cpu,4);
                for(unsigned int repeat=0;repeat<repeats;++repeat)
                    if(!chicago_command_execute_draw(&p,rows,uv))goto done;
            }
            if(batch){n3ds_gpu_model_batch_end();batch=0;}
            n3ds_gpu_present();active=0;
            if(!n3ds_gpu_readback(pass?actual:reference,240*400))goto done;
            if(pass) {
                unsigned int covered=0;
                for(unsigned int i=0;i<240*400;++i){if(actual[i]!=reference[i])goto mismatch;covered+=reference[i]!=reference[0];}
                if(covered<100)goto mismatch;
                ++cases;
            }
            continue;
mismatch:
            {char message[140];snprintf(message,sizeof(message),"RESULT: FAIL Chicago ownership pixels path=%u variant=%u",path,variant);n3ds_log(message);}goto done;
        }
    }
    {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100};
        if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!n3ds_gpu_texture_bind(0,textures[0]))goto done;
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);if(!v)goto done;
        memset(v,0,4*sizeof(*v));
        for(int i=0;i<4;++i){v[i].position[0]=(i==1||i==2)?.8f:-.8f;v[i].position[1]=i>=2?.7f:-.7f;v[i].position[2]=-3;}
        struct native_chicago_material material={.maps=1,.two_sided=1,.fade=1};
        const unsigned short indices[6]={0,1,2,0,2,3};
        if(!chicago_draw(&material,v,4,indices,6,NULL,NULL,0,0,NULL,0) ||
            !n3ds_gpu_chicago_allocate(VERTICES-4) || used!=VERTICES)goto done;
        /* A full optional pool must drain and draw synchronously, without
         * losing a primitive or reducing the original arena's capacity. */
        unsigned int capacity=command_vertices_capacity,before=chicago_command_fallback;
        command_vertices_capacity=command_vertices_used;
        int drawn=chicago_draw(&material,v,4,indices,6,NULL,NULL,0,0,NULL,0);
        command_vertices_capacity=capacity;
        if(!drawn || chicago_command_fallback!=before+1)goto done;
        n3ds_gpu_present();active=0;
    }
    {char message[200];snprintf(message,sizeof(message),"RESULT: PASS owned Chicago commands: %u exact pixel comparisons, CPU/world/relative/rigid/skinned, fades, shield, meter, material/pose/UV/sampler overwrite",cases);n3ds_log(message);}
    n3ds_log("RESULT: PASS Chicago copy-pool exhaustion fallback preserves full original vertex capacity");
    ok=1;
done:
    if(batch)n3ds_gpu_model_batch_end();if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();
    for(int i=0;i<3;++i)n3ds_gpu_texture_bind(i,NULL);
    for(int i=0;i<3;++i)if(textures[i])n3ds_gpu_texture_destroy(textures[i]);linearFree(reference);linearFree(actual);
    if(!ok)return 0;
#endif
    return 1;
}
