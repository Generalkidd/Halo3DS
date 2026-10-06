#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_gpu_hologram_tests(void)
{
    unsigned int texels[64];void *textures[3]={0};unsigned int *pixels=NULL;
    const unsigned int palette[3][4]={{0x34567814,0xabcdef33,0x789abc77,0x123456e0},
        {0x10203011,0x40102066,0x204060ab,0x795313ff},
        {0x031321ff,0x342213ff,0x194727ff,0x837359ff}};
    const float xy[4][2]={{-.5f,-.4f},{.5f,-.4f},{.5f,.4f},{-.5f,.4f}};
    const unsigned short idx[6]={0,1,2,0,2,3};
    int active=0,result=1;unsigned int maximum=0;char text[180];
#define HCHECK(e) do {if(!(e)){snprintf(text,sizeof(text),"HOLOGRAM GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(text);goto done;}}while(0)
    pixels=linearAlloc(240*400*4);HCHECK(pixels);
    for(int t=0;t<3;++t) {
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) texels[tiled_offset(x,7-y,8)]=palette[t][(x>=4)+2*(y>=4)];
        textures[t]=n3ds_gpu_texture_create(texels,8,8);HCHECK(textures[t]);
    }
    for(int test=0;test<32;++test) {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ab};
        int body=test&1,q0=(test>>1)&3,q1=(test>>3)&3,q2=(test>>2)&3;
        float noise=(test&8)?0.85f:1.f,visibility=(test&16)?.4f:1.f;
        const float tint[3]={.867f,.93f,1.f};float q=(float)(1+(test%5));
        float rows[3][4]={{0,0,0,((q0&1)?.8125f:.1875f)*q},{0,0,0,((q0&2)?.8125f:.1875f)*q},{0,0,0,q}};
        struct native_chicago_material m={.maps=body?3:2,.blend=NATIVE_BLEND_ADD,.two_sided=1,.fade=1};
        HCHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        u32 params[3];for(int t=0;t<m.maps;++t) {m.point[t]=1;params[t]=((C3D_Tex *)textures[t])->param;HCHECK(n3ds_gpu_texture_bind(t,textures[t]));}
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);HCHECK(v);
        for(int i=0;i<4;++i) {
            memset(v+i,0,sizeof(*v));v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[2]=-1;
            v[i].uv[1][0]=(q1&1)?.8125f:.1875f;v[i].uv[1][1]=(q1&2)?.8125f:.1875f;
            v[i].uv[2][0]=(q2&1)?.8125f:.1875f;v[i].uv[2][1]=(q2&2)?.8125f:.1875f;
            for(int c=0;c<4;++c) v[i].color[c]=visibility;
        }
        HCHECK(n3ds_gpu_hologram_draw(&m,v,4,idx,6,rows,tint,noise));
        for(int t=0;t<m.maps;++t) HCHECK(((C3D_Tex *)textures[t])->param==params[t]);
        n3ds_gpu_present();active=0;HCHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int expected=0xab;
        float alpha=((palette[0][q0]>>8)&255)/255.f*roundf(noise*255)/255.f;
        for(int c=0;c<3;++c) {
            float base=((palette[1][q1]>>(24-c*8))&255)/255.f*roundf(tint[c]*255)/255.f;
            float stream=body?((palette[2][q2]>>(24-c*8))&255)/255.f:0;
            float rgb=fminf(1,fminf(1,base+stream)*alpha*4)*visibility;
            expected|=(unsigned int)roundf(fminf(255,rgb*255+16*(c+1)))<<(24-c*8);
        }
        unsigned int difference=0;
        for(int y=190;y<210;++y) for(int x=110;x<130;++x) for(int s=0;s<32;s+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>s)&255)-(int)((expected>>s)&255));if(delta>difference) difference=delta;
        }
        if(difference>maximum) maximum=difference;
        if(difference>4) {snprintf(text,sizeof(text),"HOLOGRAM GPU vector=%d expected=%08x actual=%08lx difference=%u",test,expected,(unsigned long)pixels[200*240+120],difference);n3ds_log(text);goto done;}
    }
    snprintf(text,sizeof(text),"PASS: hologram 32 GPU vectors: projective division, independent body/stream UVs, gain, fog/fade, additive blending and sampler restoration; maximum error=%u",maximum);n3ds_log(text);result=0;
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    for(int t=0;t<3;++t) if(textures[t]) n3ds_gpu_texture_destroy(textures[t]);linearFree(pixels);
    return result;
#undef HCHECK
}
#endif
