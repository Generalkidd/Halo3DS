/* New3DS integration additions by Kevin Boggs. */
/* Backend draw inputs own their pose and material values. Geometry borrows
 * only the renderer's immutable resident streams or this frame's upload arena.
 * Texture/state/view mutations drain this serial prototype before changing
 * anything a pending draw can read. No simulation objects enter the queue. */
static int model_cpu_draw(const struct native_render_vertex *,const unsigned short *,unsigned int,
    int,int,int,const struct native_model_material *,int);
#ifdef HALO_N3DS_RENDER_COMMANDS
struct native_model_command {
    const void *vertices;
    const unsigned short *indices;
    unsigned int count,nodes;
    int sky,decal,two_sided,skinned;
    float uv[2];
    struct native_model_material material;
    struct native_model_lighting lighting;
    float rows[];
};

#endif
/* Kevin Boggs: n3ds_gpu_model_geometry_owned */
int n3ds_gpu_model_geometry_owned(const void *vertices,unsigned int bytes)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    uintptr_t at=(uintptr_t)vertices;
    if(!vertices || !bytes)return 0;
    for(unsigned int i=0;i<128;++i) {
        uintptr_t base=(uintptr_t)published_geometry[i].data;
        if(base && at>=base && at-base<=published_geometry[i].bytes && bytes<=published_geometry[i].bytes-(at-base))return 1;
    }
    for(unsigned int i=0;i<MODEL_RESIDENT_SLOTS;++i) {
        uintptr_t base=(uintptr_t)model_resident[i].data;
        if(base && model_resident[i].last_frame==model_frame_serial && at>=base && at-base<=model_resident[i].bytes && bytes<=model_resident[i].bytes-(at-base))return 1;
    }
    for(unsigned int i=0;i<MODEL_BLOCKS;++i) {
        uintptr_t base=(uintptr_t)model_blocks[i].data;
        unsigned int size=model_blocks[i].used*sizeof(struct native_render_vertex);
        if(base && at>=base && at-base<=size && bytes<=size-(at-base))return 1;
    }
#else
    (void)vertices;(void)bytes;
#endif
    return 0;
}

#ifdef HALO_N3DS_RENDER_COMMANDS
/* Kevin Boggs: model_command_execute */
static int model_command_execute(const void *payload)
{
    const struct native_model_command *p=payload;
    if(p->skinned<0)return model_cpu_draw(p->vertices,p->indices,p->count,p->sky,p->decal,
        p->two_sided,&p->material,-p->skinned-1);
    return model_transform_draw(p->vertices,p->indices,p->count,p->rows,p->nodes,p->uv,
        p->sky,p->decal,p->two_sided,p->skinned,&p->material);
}
#endif

/* Kevin Boggs: model_command_draw */
static int model_command_draw(const void *vertices,const unsigned short *indices,unsigned int count,
    const float *rows,unsigned int nodes,const float uv[2],int sky,int decal,int two_sided,int skinned,
    const struct native_model_material *material)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    const struct native_model_material *m=material?material:&model_plain_material;
    int supported=frame_active && (skinned<0?!nodes:(rigid_ready && skin_ready && nodes && nodes<=NATIVE_GPU_MODEL_BONES && rows)) &&
        vertices && indices && uv && count && !(count%3);
    int detailed=skinned>=0 && !sky && m->lighting && model_light_ready && !(m->reflection && m->multipurpose) && m->detail;
    if(detailed && (m->detail_function<0 || m->detail_function>1 || m->detail_mask<0 || m->detail_mask>8 ||
        (m->detail_mask && !m->multipurpose) || !isfinite(m->detail_scale[0]) || !isfinite(m->detail_scale[1])))supported=0;
    if(supported && n3ds_render_commands_recording() && n3ds_gpu_model_geometry_owned(vertices,1)) {
        /* Preserve the existing index-arena lifetime and upload exactly once.
         * Never retain callers' stack or reused staging-index pointers. */
        uintptr_t at=(uintptr_t)indices,base=(uintptr_t)index_stream;
        if(at<base || at-base>index_used || (at&3) || count>(index_used-(at-base))/2)
            indices=n3ds_gpu_indices_upload(indices,count);
        if(!indices)return 0;
        struct native_model_command *p=n3ds_render_command_allocate(model_command_execute,sizeof(*p)+nodes*12*sizeof(float));
        if(p) {
            p->vertices=vertices;p->indices=indices;p->count=count;p->nodes=nodes;
            p->sky=sky;p->decal=decal;p->two_sided=two_sided;p->skinned=skinned;
            memcpy(p->uv,uv,sizeof(p->uv));if(nodes)memcpy(p->rows,rows,nodes*12*sizeof(float));
            p->material=*m;
            if(m->lighting) {p->lighting=*m->lighting;p->material.lighting=&p->lighting;}
#ifdef HALO_N3DS_RENDER_WORKER
            /* Publish useful spans while later parts are still being prepared,
             * rather than waiting until the end of all object preparation.
             * The other arena remains writable; state/resource fences join. */
            if(render_commands.count>=32)n3ds_render_commands_submit();
#endif
            return 1;
        }
    }
    N3DS_RENDER_FENCE(N3DS_RENDER_FENCE_IMMEDIATE);
#endif
    if(skinned<0)return model_cpu_draw(vertices,indices,count,sky,decal,two_sided,material,-skinned-1);
    return model_transform_draw(vertices,indices,count,rows,nodes,uv,sky,decal,two_sided,skinned,material);
}

/* Kevin Boggs: n3ds_model_command_tests */
int n3ds_model_command_tests(void)
{
#ifdef HALO_N3DS_RENDER_COMMANDS
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4);
    void *texture=NULL,*optional=NULL;int active=0,batch=0,ok=0;
    unsigned int texels[64];for(unsigned int i=0;i<64;++i)texels[i]=0xffffffffu;
    if(!reference || !actual)goto done;
    texture=n3ds_gpu_texture_create(texels,8,8);if(!texture)goto done;
    for(unsigned int path=0;path<4;++path)for(unsigned int pass=0;pass<2;++pass) {
        unsigned int skinned=path==1;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin())goto done;active=1;n3ds_gpu_window_begin(&camera);
        struct native_skin_vertex *vertices=path?(void *)n3ds_gpu_model_vertices_allocate(6):
            (optional=n3ds_gpu_geometry_optional_allocate(6*sizeof(struct native_render_vertex)));
        if(!vertices)goto done;
        for(unsigned int i=0;i<4;++i) {
            struct native_render_vertex v={{i==1 || i==2?.5f:-.5f,i>=2?.5f:-.5f,-3},{0,0,1},{i==1 || i==2?1:0,i>=2?1:0}};
            if(skinned) {memcpy(vertices+i,&v,sizeof(v));vertices[i].bones[0]=0;vertices[i].bones[1]=3;vertices[i].bones[2]=.4f;vertices[i].bones[3]=.6f;}
            else ((struct native_render_vertex *)vertices)[i]=v;
        }
        float rows[24]={1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,0},uv[2]={1,1};
        unsigned short indices[6]={0,2,1,0,3,2};
        struct native_model_lighting light={.ambient={.3f,.6f,.8f},.count=0};
        if(optional)n3ds_gpu_geometry_publish(optional,6*sizeof(struct native_render_vertex));
        struct native_model_material material={.base=texture,.lighting=&light};
        if(!n3ds_gpu_texture_bind(0,texture))goto done;
        n3ds_gpu_model_batch_begin();batch=1;
        int drawn=1;
        /* Cross two mid-batch handoffs, appending indices and preparing the
         * next owned payload while the worker consumes the previous span.
         * Opaque repeats preserve the image. */
        for(unsigned int repeat=0;repeat<80 && drawn;++repeat) {
        if(path>=2)drawn=pass?model_command_draw(vertices,indices,6,NULL,0,uv,0,0,1,1-(int)path,&material):
            model_cpu_draw((const void *)vertices,indices,6,0,0,1,&material,path-2);
        else drawn=pass?model_command_draw(vertices,indices,6,rows,skinned?2:1,uv,0,0,1,skinned,&material):
            model_transform_draw(vertices,indices,6,rows,skinned?2:1,uv,0,0,1,skinned,&material);
        }
        if(!drawn || (pass && !render_commands.count))goto done;
        /* Destroy all caller-owned inputs before the deferred draw executes. */
        if(pass) {memset(rows,0,sizeof(rows));memset(indices,0,sizeof(indices));memset(&light,0,sizeof(light));memset(&material,0,sizeof(material));uv[0]=uv[1]=0;}
        n3ds_gpu_model_batch_end();batch=0;n3ds_gpu_present();active=0;
        if(!n3ds_gpu_readback(pass?actual:reference,240*400))goto done;
        if(optional) {n3ds_gpu_geometry_free(optional);optional=NULL;}
        if(pass) {
            unsigned int covered=0;
            for(unsigned int i=0;i<240*400;++i) {if(reference[i]!=actual[i])goto done;covered+=reference[i]!=reference[0];}
            if(covered<200)goto done;
        }
    }
    ok=1;
done:
    if(batch)n3ds_gpu_model_batch_end();if(active)n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier())abort();
    if(optional)n3ds_gpu_geometry_free(optional);
    if(texture)n3ds_gpu_texture_destroy(texture);linearFree(reference);linearFree(actual);
    if(!ok)return 0;
    n3ds_log("RESULT: PASS owned model commands: rigid/skinned/CPU-world/CPU-relative pixels identical after caller pose, UV, material, lighting and indices overwritten");
#endif
    return 1;
}
