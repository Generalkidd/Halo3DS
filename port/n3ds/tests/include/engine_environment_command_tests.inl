/* New3DS integration additions by Kevin Boggs. */
/* Exact immediate/owned-command pixels. Invoke only between frames in a test build. */
/* Kevin Boggs: n3ds_environment_command_tests */
int n3ds_environment_command_tests(void)
{
#ifndef HALO_N3DS_RENDER_COMMANDS
    return 1;
#else
    struct native_render_vertex *v=linearAlloc(4*sizeof(*v));
    struct native_environment_bump_vertex *b=linearAlloc(4*sizeof(*b));
    float *uv=linearAlloc(8*sizeof(float));unsigned short *indices=linearAlloc(12);
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4);
    void *textures[3]={0};unsigned int texels[64];int active=0,ok=0;
    if(!v || !b || !uv || !indices || !reference || !actual)goto done;
    unsigned short quad[6]={0,2,1,0,3,2};memcpy(indices,quad,12);
    for(unsigned int i=0;i<4;++i) {
        v[i]=(struct native_render_vertex){{i==1 || i==2?.5f:-.5f,i>=2?.5f:-.5f,-3},{.7f,.8f,.9f},{i==1 || i==2?1:0,i>=2?1:0}};
        uv[2*i]=v[i].uv[0];uv[2*i+1]=v[i].uv[1];
        b[i]=(struct native_environment_bump_vertex){{uv[2*i],uv[2*i+1]},{0,0,1,0},.5f};
    }
    n3ds_gpu_geometry_flush(v,4*sizeof(*v));n3ds_gpu_geometry_flush(b,4*sizeof(*b));
    n3ds_gpu_geometry_flush(uv,8*sizeof(float));n3ds_gpu_geometry_flush(indices,12);
    for(unsigned int t=0;t<3;++t) {
        for(unsigned int i=0;i<64;++i)texels[i]=t==2?0x8080ffffu:(i&1?0x804020ffu:0x204080ffu);
        textures[t]=n3ds_gpu_texture_create(texels,8,8);if(!textures[t])goto done;
    }
    for(unsigned int scene=0;scene<16;++scene)for(unsigned int pass=0;pass<2;++pass) {
        if(!n3ds_gpu_frame_begin())goto done;active=1;
        if(!pass)n3ds_render_commands_end(); /* exact immediate reference */
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        n3ds_gpu_window_begin(&camera);
        unsigned int kind=scene/4;
        for(unsigned int repeat=0;repeat<12;++repeat) {
            for(unsigned int t=0;t<3;++t)if(!n3ds_gpu_texture_prepare_bind(t,textures[t],scene&1?15:0))goto done;
            struct native_environment_emission emission={{0xff203040u+repeat,0xff506070u,0xff102030u},{1,1,.125f,.25f}};
            int drawn;
            if(kind==0)drawn=n3ds_gpu_environment_draw(v,uv,indices,6,1,scene%4,1.5f,.75f,0xffccbbaau,scene&1);
            else if(kind==1)drawn=n3ds_gpu_environment_emission_draw(v,uv,indices,6,scene&1,0xffccbbaau,0,&emission);
            else if(kind==2)drawn=n3ds_gpu_environment_plasma_draw(v,uv,indices,6,scene&1,0xffccbbaau,0,&emission,0xff804020u,(scene*17+repeat)%256);
            else drawn=n3ds_gpu_environment_bump_draw(v,b,indices,6,1.25f,.75f,0xffccbbaau,scene&1);
            if(!drawn)goto done;
            memset(&emission,0,sizeof(emission));
        }
        n3ds_gpu_environment_batch_end();n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(pass?actual:reference,240*400))goto done;
        if(pass) {
            unsigned int coverage=0;
            for(unsigned int i=0;i<240*400;++i){if(reference[i]!=actual[i]){char s[96];snprintf(s,sizeof(s),"RESULT: FAIL environment pixels scene=%u pixel=%u",scene,i);n3ds_log(s);goto done;}coverage+=actual[i]!=actual[0];}
            if(coverage<200)goto done;
        }
    }
    ok=1;n3ds_log("RESULT: PASS environment command pixels: 16 scenes, plain/detail/emission/plasma/bump, copied animation and prepared samplers");
done:
    if(active)n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier())abort();
    for(unsigned int t=0;t<3;++t){n3ds_gpu_texture_bind(t,NULL);if(textures[t])n3ds_gpu_texture_destroy(textures[t]);}
    linearFree(v);linearFree(b);linearFree(uv);linearFree(indices);linearFree(reference);linearFree(actual);return ok;
#endif
}
