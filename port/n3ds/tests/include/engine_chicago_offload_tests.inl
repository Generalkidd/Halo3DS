int n3ds_gpu_chicago_transform_tests(void)
{
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),pixels[64];
    void *textures[3]={0};int active=0,ok=0;unsigned int worst=0,edges_total=0;
    const unsigned short indices[6]={0,1,2,0,2,3};
    if(!reference || !actual) goto done;
    for(int t=0;t<3;++t) {
        for(int i=0;i<64;++i) pixels[i]=((60+(i%8)*18)<<24)|((50+(i/8)*20)<<16)|((80+t*40)<<8)|200;
        textures[t]=n3ds_gpu_texture_create(pixels,8,8);if(!textures[t]) goto done;
    }
    for(int test=0;test<16;++test) {
        int skinned=test&1;unsigned int nodes=test==15?28:skinned?2:1,second=nodes-1;
        float rows[28*12]={0},uv[24]={0};
        for(unsigned int n=0;n<nodes;++n) {
            float angle=n==second?.2f:-.1f,scale=n==second?1.05f:.94f;
            float r[12]={cosf(angle)*scale,-sinf(angle)*scale,0,.05f,sinf(angle)*scale,cosf(angle)*scale,0,-.02f,0,0,scale,0};
            memcpy(rows+n*12,r,sizeof(r));
        }
        for(int m=0;m<3;++m) {uv[m*8]=.75f+m*.17f;uv[m*8+1]=.13f;uv[m*8+3]=-.2f;uv[m*8+4]=-.11f;uv[m*8+5]=.82f;uv[m*8+7]=.15f;}
        struct native_render_vertex rigid[4];struct native_skin_vertex skin[4];struct native_chicago_vertex cpu[4];
        for(int i=0;i<4;++i) {
            rigid[i]=(struct native_render_vertex){{i==1||i==2?.8f:-.8f,i>=2?.7f:-.7f,-3},{0,0,1},{i==1||i==2?.8f:.2f,i>=2?.8f:.2f}};
            memcpy(skin+i,rigid+i,sizeof(*rigid));skin[i].bones[0]=0;skin[i].bones[1]=second*3;skin[i].bones[2]=.3f;skin[i].bones[3]=.7f;
            memset(cpu+i,0,sizeof(*cpu));
            for(int a=0;a<3;++a) for(int k=0;k<(skinned?2:1);++k) {
                const float *r=rows+(k?second:0)*12+a*4;
                cpu[i].position[a]+=(r[0]*rigid[i].position[0]+r[1]*rigid[i].position[1]+r[2]*rigid[i].position[2]+r[3])*(skinned?(k?.7f:.3f):1);
            }
            for(int m=0;m<3;++m) for(int a=0;a<2;++a) cpu[i].uv[m][a]=uv[m*8+a*4]*rigid[i].uv[0]+uv[m*8+a*4+1]*rigid[i].uv[1]+uv[m*8+a*4+3];
        }
        struct native_chicago_material m={.maps=1+test%3,.blend=test%4==2?3:test%4==3?7:test%4,.two_sided=1,.fade=.7f,.sky=test==12};
        for(int j=0;j<2;++j) {m.color_function[j]=2;m.alpha_function[j]=0;}
        for(int pass=0;pass<2;++pass) {
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
            for(int j=0;j<3;++j) if(!n3ds_gpu_texture_bind(j,textures[j])) goto done;
            if(pass) {
                unsigned int bytes=skinned?sizeof(skin):sizeof(rigid);
                void *v=n3ds_gpu_model_vertices_allocate((bytes+31)/32);if(!v) goto done;
                memcpy(v,skinned?(const void *)skin:(const void *)rigid,bytes);n3ds_gpu_geometry_flush(v,bytes);
                if(!n3ds_gpu_chicago_transform_draw(&m,v,4,indices,6,rows,nodes,skinned,uv)) goto done;
            } else {
                struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);if(!v) goto done;memcpy(v,cpu,sizeof(cpu));
                if(!n3ds_gpu_chicago_draw(&m,v,4,indices,6)) goto done;
            }
            n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pass?actual:reference,240*400)) goto done;
        }
        unsigned int covered=0,edges=0,bad=0;
        for(unsigned int i=0;i<240*400;++i) {
            int a=reference[i]!=0x102030ff,b=actual[i]!=0x102030ff;covered+=a;if(a!=b){++edges;continue;}
            for(int shift=0;shift<32;shift+=8) {unsigned int d=abs((int)((reference[i]>>shift)&255)-(int)((actual[i]>>shift)&255));if(d>worst)worst=d;if(d>3)++bad;}
        }
        edges_total+=edges;
        if(covered<1000 || edges>64 || bad>32) {char msg[180];snprintf(msg,sizeof(msg),"FAIL: Chicago GPU transform test=%d covered=%u edges=%u bad=%u worst=%u",test,covered,edges,bad,worst);n3ds_log(msg);goto done;}
    }
    {char msg[180];snprintf(msg,sizeof(msg),"PASS: Chicago GPU transforms: 16 CPU-reference comparisons, rigid/weighted/palette28, animated UVs, 1-3 maps, sky, fade and blends; max=%u edges=%u",worst,edges_total);n3ds_log(msg);}
    ok=1;
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    for(int i=0;i<3;++i) n3ds_gpu_texture_destroy(textures[i]);linearFree(reference);linearFree(actual);return !ok;
}
