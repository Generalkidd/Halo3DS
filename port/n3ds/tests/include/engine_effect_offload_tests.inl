/* Pixel comparisons against the retained float/CPU effect path. */
#if HALO_N3DS_RENDERER_TESTS
int n3ds_gpu_glow_precision_tests(void)
{
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),pixels[64];
    void *texture=NULL;int active=0,ok=0;unsigned int worst=0,legacy=0;
    const unsigned int order[6]={0,1,2,0,2,3};
    const unsigned short indices[6]={0,1,2,0,2,5};
    for(int i=0;i<64;++i) pixels[i]=0x4070b0ff;
    texture=n3ds_gpu_texture_create(pixels,8,8);
    if(!reference || !actual || !texture) goto done;
    glow_test_quantize=1;
    for(int test=0;test<48;++test) {
        int path=test%8,affine=path==4 || path==5;
        float shift=test/8%3==0?128.f:test/8%3==1?1024.f:4096.f;
        glow_test_legacy=path==7;
        for(int pass=0;pass<2;++pass) {
            float offset=pass?shift:0,motion=test>=24?1.f/2048:0;
            struct native_render_camera camera={.position={offset,-offset,offset},
                .forward={.125f,.0625f,-1},.up={0,1,0},.vertical_fov=1,
                .near_clip=.05f,.far_clip=100,.clear_color=0x102030ff};
            struct native_widget_vertex quad[4],cpu[6];struct native_packed_effect_vertex packed[6];
            float rows[12]={.875f,-.125f,0,offset+.125f,.125f,.875f,0,-offset-.0625f,0,0,1,offset};
            for(int i=0;i<4;++i) {
                quad[i]=(struct native_widget_vertex){
                    {offset+(i==1 || i==2?.53125f:-.375f)+motion,-offset+(i>=2?.4375f:-.5625f),offset-1.5f},
                    {.5f,.75f,1,.5f},{.5f,.5f}};
                if(path==6) {quad[i].position[0]=i==1 || i==2?300:100;quad[i].position[1]=i>=2?180:60;quad[i].position[2]=0;}
            }
            for(int i=0;i<6;++i) {
                cpu[i]=quad[order[i]];
                memcpy(packed[i].position,cpu[i].position,12);
                if(affine) {
                    packed[i].position[0]=(order[i]==1 || order[i]==2?.53125f:-.375f)+motion;
                    packed[i].position[1]=order[i]>=2?.4375f:-.5625f;packed[i].position[2]=-1.5f;
                }
                packed[i].uv[0]=packed[i].uv[1]=.5f;packed[i].argb=0x8080bfffu;
            }
            struct native_widget_vertex saved_quad[4],saved_cpu[6];struct native_packed_effect_vertex saved_packed[6];
            memcpy(saved_quad,quad,sizeof(quad));memcpy(saved_cpu,cpu,sizeof(cpu));memcpy(saved_packed,packed,sizeof(packed));
            if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
            if(!n3ds_gpu_texture_bind(0,texture)) goto done;
            /* Submit twice to exercise retained state and prove source arrays
             * are not rebased in place (also required for a second stereo eye). */
            for(int repeat=0;repeat<2;++repeat) {
                int result;
                if(path==0 || path==6 || path==7) result=n3ds_gpu_widget_draw(quad,path!=6,400,240,.3f,0);
                else if(path==1) result=n3ds_gpu_effect_draw(cpu,6,NATIVE_BLEND_ADD,0,0,0);
                else if(path==3 || path==5) result=n3ds_gpu_effect_packed_indexed_draw(packed,6,indices,6,NATIVE_BLEND_ADD,0,0,0,affine?rows:NULL);
                else result=n3ds_gpu_effect_packed_draw(packed,6,NATIVE_BLEND_ADD,0,0,0,affine?rows:NULL);
                if(!result || memcmp(saved_quad,quad,sizeof(quad)) || memcmp(saved_cpu,cpu,sizeof(cpu)) || memcmp(saved_packed,packed,sizeof(packed))) goto done;
            }
            n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
            if(!n3ds_gpu_readback(pass?actual:reference,240*400)) goto done;
        }
        unsigned int changed=0,covered=0;
        for(unsigned int i=0;i<240*400;++i) {changed+=reference[i]!=actual[i];covered+=reference[i]!=0x102030ff;}
        if(path==7) {if(changed>legacy)legacy=changed;continue;}
        if(changed>worst)worst=changed;
        if(changed>32 || covered<1000) {
            char message[160];snprintf(message,sizeof(message),"FAIL: glow precision case=%d changed=%u covered=%u",test,changed,covered);n3ds_log(message);goto done;
        }
    }
    if(legacy<=32) {n3ds_log("FAIL: glow precision legacy control did not expose world-space rounding");goto done;}
    {char message[256];snprintf(message,sizeof(message),"PASS: glow f24-input precision: 42 translated/subpixel corona, float/packed/indexed particle, affine and screen-space cases; unchanged caller arrays; changed_pixels_max=%u legacy_world_control=%u",worst,legacy);n3ds_log(message);}
    ok=1;
done:
    glow_test_quantize=glow_test_legacy=0;
    if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();
    n3ds_gpu_texture_destroy(texture);linearFree(reference);linearFree(actual);return !ok;
}
#endif
int n3ds_gpu_effect_packed_tests(void)
{
    unsigned int *reference=linearAlloc(240*400*4),*actual=linearAlloc(240*400*4),pixels[64];
    void *texture=NULL;int active=0,ok=0;unsigned int cases=0,worst=0;
    if(!reference || !actual) goto done;
    for(int i=0;i<64;++i) pixels[i]=0x8040b0b0;
    texture=n3ds_gpu_texture_create(pixels,8,8);if(!texture) goto done;
    const unsigned int order[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-.8f,-.7f},{.8f,-.7f},{.8f,.7f},{-.8f,.7f}};
    for(int test=0;test<24;++test) {
        int blend=test%8;if(blend==NATIVE_BLEND_MIN || blend==NATIVE_BLEND_MAX) continue;
        float rows[12]={.92f,-.13f,0,.12f,.13f,.92f,0,-.06f,0,0,1.1f,-.2f};
        int transformed=test>=8;unsigned int flags=test>=16?2:0;
        struct native_packed_effect_vertex packed[6];struct native_widget_vertex cpu[6];
        for(int i=0;i<6;++i) {
            unsigned int c=order[i];packed[i]=(struct native_packed_effect_vertex){{xy[c][0],xy[c][1],-2},{.5f,.5f},0x974080cfu+c*0x110d0701u};
            for(int a=0;a<3;++a) cpu[i].position[a]=transformed?
                rows[a*4]*packed[i].position[0]+rows[a*4+1]*packed[i].position[1]+rows[a*4+2]*packed[i].position[2]+rows[a*4+3]:packed[i].position[a];
            for(int a=0;a<4;++a) cpu[i].color[a]=((packed[i].argb>>(a==3?24:16-a*8))&255)/255.f;
            memcpy(cpu[i].uv,packed[i].uv,sizeof(cpu[i].uv));
        }
        for(int pass=0;pass<2;++pass) {
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
            if(!n3ds_gpu_texture_bind(0,texture)) goto done;
            C3D_Tex *tex=texture;u32 sampler=tex->param,border=tex->border;
            for(int layer=0;layer<(test>=16?3:1);++layer) {
            unsigned int tint_flags=layer==2?0:flags;
            if(!pass) n3ds_gpu_effect_state_invalidate();
            if(pass) {
                const unsigned short index[6]={0,1,2,0,2,5};
                if(test&1) {if(!n3ds_gpu_effect_packed_indexed_draw(packed,6,index,6,blend,tint_flags,0,0,transformed?rows:NULL)) goto done;}
                else if(!n3ds_gpu_effect_packed_draw(packed,6,blend,tint_flags,0,0,transformed?rows:NULL)) goto done;
            }
            else if(!n3ds_gpu_effect_draw(cpu,6,blend,tint_flags,0,0)) goto done;
            }
            if(tex->param!=sampler || tex->border!=border) goto done;
            n3ds_gpu_world_state_restore();n3ds_gpu_present();active=0;
            if(!n3ds_gpu_readback(pass?actual:reference,240*400)) goto done;
        }
        unsigned int covered=0,edges=0,bad=0;
        for(unsigned int i=0;i<240*400;++i) {
            int a=reference[i]!=0x102030ff,b=actual[i]!=0x102030ff;covered+=a;
            if(a!=b) {++edges;continue;}
            for(int shift=0;shift<32;shift+=8) {
                unsigned int delta=abs((int)((reference[i]>>shift)&255)-(int)((actual[i]>>shift)&255));
                if(delta>worst) worst=delta;if(delta>3) ++bad;
            }
        }
        if(covered<1000 || edges>128 || bad>32) {
            char msg[160];snprintf(msg,sizeof(msg),"FAIL: packed effect test=%d covered=%u edges=%u bad=%u max=%u",test,covered,edges,bad,worst);n3ds_log(msg);goto done;
        }
        ++cases;
    }
    {char msg[160];snprintf(msg,sizeof(msg),"PASS: packed effect GPU: %u CPU-reference pixel comparisons, ARGB colors, affine transforms, blends, tint and sampler restore; max=%u",cases,worst);n3ds_log(msg);}
    ok=1;
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_texture_destroy(texture);linearFree(reference);linearFree(actual);return !ok;
}
