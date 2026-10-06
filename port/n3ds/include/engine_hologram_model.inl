static float hologram_visibility(const float position[3],int sky)
{
    const struct render_fog *f=&global_window_parameters.fog;
    float depth=0,plane=0,visible=1;
    if(sky) return 1;
    for(int c=0;c<3;++c) {
        depth+=(position[c]-global_window_parameters.camera.position.n[c])*global_window_parameters.camera.forward.n[c];
        plane+=f->plane.n.n[c]*position[c];
    }
    if(f->atmospheric_maximum_density>0) {
        float range=f->atmospheric_maximum_distance-f->atmospheric_minimum_distance;assert(range>0);
        visible*=1-PIN((depth-f->atmospheric_minimum_distance)/range,0.f,1.f)*PIN(f->atmospheric_maximum_density,0.f,1.f);
    }
    if(f->planar_mode && f->planar_maximum_density>0) {
        assert(f->planar_maximum_distance>0 && f->planar_maximum_depth>0);
        float x=PIN(1+(plane-f->plane.d)/f->planar_maximum_depth,0.f,1.f);
        float y=PIN(1-depth/f->planar_maximum_distance,0.f,1.f);
        float cp=plane3d_distance_to_point(&f->plane,&global_window_parameters.camera.position);
        float q=1-MIN(1.f,x*x+y*y),r=1-y*y;
        visible*=1-(q*q+(r*r-q*q)*PIN(-cp/f->planar_maximum_depth,0.f,1.f))*PIN(f->planar_maximum_density,0.f,1.f);
    }
    return visible;
}
static void hologram_screen_rows(const struct native_generic_material *m,float rows[3][4],const float origin[3])
{
    float basis[4][3];const real (*p)[4]=global_window_parameters.frustum.projection_matrix;
    for(int i=0;i<4;++i) {
        real_point3d world,v;memcpy(world.n,origin,sizeof(world));if(i<3)world.n[i]+=1;
        matrix4x3_transform_point(&global_window_parameters.frustum.world_to_view,&world,&v);
        float x=v.x*p[0][0]+v.y*p[1][0]+v.z*p[2][0]+p[3][0];
        float y=v.x*p[0][1]+v.y*p[1][1]+v.z*p[2][1]+p[3][1];
        float q=v.x*p[0][3]+v.y*p[1][3]+v.z*p[2][3]+p[3][3];
        for(int axis=0;axis<2;++axis) {
            const float *t=m->transform[0][axis];basis[i][axis]=t[0]*x+t[1]*y+t[3]*q;
        }
        basis[i][2]=q;
    }
    for(int r=0;r<3;++r) for(int c=0;c<4;++c) rows[r][c]=c==3?basis[3][r]:basis[c][r]-basis[3][r];
}
static int hologram_model_draw(int kind,const struct native_generic_material *bake,
    struct native_chicago_material native,struct native_chicago_vertex *vertices,
    const struct native_render_vertex *posed,unsigned int count,const unsigned short *indices,unsigned int index_count)
{
    if(!kind || !posed) return 0;
    float bounds[4]={1e30f,1e30f,-1e30f,-1e30f};
    for(unsigned int i=0;i<count;++i) for(int a=0;a<2;++a) {
        bounds[a]=MIN(bounds[a],posed[i].uv[a]);bounds[a+2]=MAX(bounds[a+2],posed[i].uv[a]);
    }
    for(int a=0;a<2;++a) if(bounds[a+2]-bounds[a]<.0001f) bounds[a+2]=bounds[a]+.0001f;
    void *mixture=NULL;
    if(kind==2) {
        /* Bake only lerp(streamA,streamB,body.a). Keep the full-resolution
         * body colour and noise out of this 8x8 animation approximation. */
        struct native_generic_material streams={0};streams.maps=3;streams.stages=1;
        static const int op[26]={15,0,6,0,15,1,7,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,1,0,0,0};
        memcpy(streams.stage[0].op,op,sizeof(op));
        for(int i=0;i<3;++i) {
            streams.textures[i]=bake->textures[i+1];streams.point[i]=bake->point[i+1];
            streams.clamp_u[i]=bake->clamp_u[i+1];streams.clamp_v[i]=bake->clamp_v[i+1];
            memcpy(streams.transform[i],bake->transform[i+1],sizeof(streams.transform[i]));
        }
        mixture=n3ds_gpu_hologram_stream_bake(&streams,bounds);if(!mixture) return 0;
    }
    float rows[3][4],tint[3]={1,1,1},relative_origin[3]={0};
    if(!native.sky)n3ds_gpu_camera_origin(relative_origin);
    hologram_screen_rows(bake,rows,relative_origin);
    if(kind==2) memcpy(tint,bake->stage[1].constant[0],sizeof(tint));
    for(unsigned int i=0;i<count;++i) {
        struct native_chicago_vertex *v=vertices+i;memset(v,0,sizeof(*v));
        memcpy(v->position,posed[i].position,sizeof(v->position));
        for(int a=0;a<2;++a) {
            const float *r=bake->transform[1][a];v->uv[1][a]=r[0]*posed[i].uv[0]+r[1]*posed[i].uv[1]+r[3];
            v->uv[2][a]=(posed[i].uv[a]-bounds[a])/(bounds[a+2]-bounds[a]);
        }
        float visibility=hologram_visibility(v->position,native.sky)*native.fade;
        for(int c=0;c<3;++c)v->position[c]-=relative_origin[c];
        for(int c=0;c<4;++c) v->color[c]=visibility;
    }
    native.maps=kind==2?3:2;
    for(int i=0;i<2;++i) {
        native.point[i]=bake->point[i];native.clamp_u[i]=bake->clamp_u[i];native.clamp_v[i]=bake->clamp_v[i];
        assert(n3ds_gpu_texture_bind(i,bake->textures[i]));
    }
    native.point[2]=0;native.clamp_u[2]=native.clamp_v[2]=1;
    if(mixture) assert(n3ds_gpu_texture_bind(2,mixture));
    assert(n3ds_gpu_hologram_draw(&native,vertices,count,indices,index_count,rows,tint,bake->stage[0].constant[0][3]));
    static unsigned int reported;
    if(!(reported&(1u<<kind))) {reported|=1u<<kind;n3ds_log(kind==2?
        "NATIVE HOLOGRAM: body/noise per fragment; 8x8 animated stream mixture; three samplers, one pass":
        "NATIVE HOLOGRAM: hair/noise per fragment; two samplers, one pass");}
    return 1;
}
