int n3ds_gpu_flashlight_tests(void)
{
    struct native_render_vertex *v=linearAlloc(4*sizeof(*v));
    struct native_environment_bump_vertex *b=linearAlloc(4*sizeof(*b));
    float *uv=linearAlloc(8*sizeof(float));unsigned short *ix=linearAlloc(12);
    unsigned int texels[64],*pixels=linearAlloc(240*400*4),off[6]={0};
    void *base=NULL,*map=NULL,*normal=NULL;int ok=0,active=0;
    if(!v || !b || !uv || !ix || !pixels) goto done;
    unsigned short indices[6]={0,2,1,0,3,2};memcpy(ix,indices,12);
    for(int i=0;i<64;++i) texels[i]=0x808080ff;
    base=n3ds_gpu_texture_create(texels,8,8);if(!base) goto done;
    for(int i=0;i<64;++i) texels[i]=0x202020ff;
    map=n3ds_gpu_texture_create(texels,8,8);if(!map) goto done;
    for(int i=0;i<64;++i) texels[i]=0x8080ffff;
    normal=n3ds_gpu_texture_create(texels,8,8);if(!normal) goto done;
    for(int path=0;path<6;++path) for(int phase=0;phase<6;++phase) {
        float origin[3]={0,0,0},forward[3]={0,0,phase==3?1:-1};
        if(phase==4) origin[0]=2;
        if(phase==5) origin[2]=10;
        for(int i=0;i<4;++i) {
            v[i]=(struct native_render_vertex){{i==1 || i==2?.8f:-.8f,i>=2?.8f:-.8f,-2},{.125f,.125f,.125f},{.5f,.5f}};
            uv[i*2]=uv[i*2+1]=.5f;
            b[i]=(struct native_environment_bump_vertex){{.5f,.5f},{0,0,0,1},.5f};
            if(path==4) {v[i].color[0]=v[i].color[1]=0;v[i].color[2]=1;}
        }
        n3ds_gpu_geometry_flush(v,4*sizeof(*v));n3ds_gpu_geometry_flush(b,4*sizeof(*b));
        n3ds_gpu_geometry_flush(uv,8*sizeof(float));n3ds_gpu_geometry_flush(ix,12);
        n3ds_gpu_flashlight_set(phase==0 || phase==2?0:1,origin,forward);
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x000000ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!n3ds_gpu_texture_bind(0,base) || !n3ds_gpu_texture_bind(1,map) || !n3ds_gpu_texture_bind(2,normal)) goto done;
        if(path<2) {if(!n3ds_gpu_environment_draw(v,uv,ix,6,1,path,1,1,0xffffffffu,0)) goto done;}
        if(path==2) {if(!n3ds_gpu_environment_bump_draw(v,b,ix,6,1,1,0xffffffffu,0)) goto done;}
        if(path==3) {
            struct native_environment_emission e={.uv={1,1,0,0}};
            if(!n3ds_gpu_environment_emission_draw(v,uv,ix,6,1,0xffffffffu,0,&e)) goto done;
        }
        if(path==4) {
            struct native_model_lighting l={.ambient={.125f,.125f,.125f}};
            struct native_model_material m={.lighting=&l,.base=base};
            float rows[12]={1,0,0,0,0,1,0,0,0,0,1,0},scale[2]={1,1};
            if(!n3ds_gpu_model_rigid_draw(v,ix,6,rows,scale,0,0,0,&m)) goto done;
        }
        if(path==5 && !n3ds_gpu_model_cpu_draw(v,ix,6,0,0,0,NULL)) goto done;
        n3ds_gpu_present();active=0;if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        unsigned int center=pixels[200*240+120];
        char line[160];snprintf(line,sizeof(line),"FLASHLIGHT GPU: path=%d phase=%d center=%08x",path,phase,center);n3ds_log(line);
        if(!phase) off[path]=center;
        else if(phase==1) {if((center>>24)<(off[path]>>24)+15) goto done;}
        else if(center!=off[path]) goto done;
    }
    ok=1;n3ds_log("PASS: flashlight single-pass GPU: base, detail, bump, emission, model and software pose; off/on/off, cone direction, offset and range");
done:
    n3ds_gpu_flashlight_set(0,NULL,NULL);
    if(active) n3ds_gpu_present();
    if(base)n3ds_gpu_texture_destroy(base);if(map)n3ds_gpu_texture_destroy(map);if(normal)n3ds_gpu_texture_destroy(normal);
    linearFree(v);linearFree(b);linearFree(uv);linearFree(ix);linearFree(pixels);return !ok;
}
