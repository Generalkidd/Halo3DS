/* Compare actual PICA pixels with independent Chicago arithmetic. Distinct
 * quadrant textures/UVs catch accidental reuse of another map's coordinates. */
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine_chicago.h"
#include "engine_generic.h"
#include "engine_glass.h"
#include "engine_widgets.h"
#include "engine_renderer.h"
#include "engine_textures.h"
void n3ds_log(const char *text);
void n3ds_gpu_chicago_map_release(void);
static int generic_gpu_tests(void);
static int generic_bake_cache_tests(void);
static int generic_lit_lerp_gpu_tests(void);
static int shield_mask_gpu_tests(void);
static int shield_scalar_bake_tests(void);
static int meter_gpu_tests(void);
static int effect_gpu_tests(void);
static int effect_batch_gpu_tests(void);
static int effect_layer_gpu_tests(void);
static float operation(float current,float next,float current_alpha,float next_alpha,int function)
{
    switch(function) {
    case 0:return current; case 1:return next; case 2:return current*next;
    case 3:return fminf(1,2*current*next); case 4:return fminf(1,current+next);
    case 7:return fmaxf(0,next-current);case 8:return fmaxf(0,current-next);
    case 9:return next*current_alpha+current*(1-current_alpha);
    case 10:return current*current_alpha+next*(1-current_alpha);
    case 11:return next*next_alpha+current*(1-next_alpha);
    case 12:return current*next_alpha+next*(1-next_alpha);
    default:abort();
    }
}
static unsigned int reference(const struct native_chicago_material *m,const unsigned int samples[3],unsigned int dest)
{
    float color[4]; unsigned int result=dest&255;
    for(int c=0;c<4;++c) color[c]=((samples[0]>>(24-8*c))&255)/255.f;
    for(int i=1;i<m->maps;++i) {
      float ca=color[3],na=(samples[i]&255)/255.f;
      for(int c=0;c<4;++c) {
        int shift=(c<3 && m->alpha_replicate[i-1])?0:24-8*c;
        color[c]=operation(color[c],((samples[i]>>shift)&255)/255.f,ca,na,c==3?m->alpha_function[i-1]:m->color_function[i-1]);
      }
    }
    if(m->blend==0 || m->blend==7) color[3]*=m->fade;
    for(int c=0;c<3;++c) {
        float s=color[c],d=((dest>>(24-8*c))&255)/255.f,a=color[3],out;
        if(m->blend!=0) s*=m->fade;
        if(m->blend==1 || m->blend==5) s+=1-m->fade;
        if(m->blend==2) s+=(1-m->fade)*.5f;
        switch(m->blend) {
        case 0:out=s*a+d*(1-a);break;case 1:out=s*d;break;case 2:out=2*s*d;break;
        case 3:out=s+d;break;case 4:out=d-s;break;case 5:out=fminf(s,d);break;
        case 6:out=fmaxf(s,d);break;case 7:out=s+d*(1-a);break;default:abort();
        }
        if(m->alpha_test && color[3]<=127.f/255.f) out=d;
        result|=(unsigned int)floorf(fminf(1,fmaxf(0,out))*255+.5f)<<(24-8*c);
    }
    return result;
}
static int opaque_quad(float z)
{
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    const unsigned short indices[6]={0,1,2,0,2,3};
    struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);
    if(!v || !n3ds_gpu_texture_bind(0,NULL)) return 0;
    for(int i=0;i<4;++i) v[i]=(struct native_render_vertex){{xy[i][0],xy[i][1],z},{64/255.f,128/255.f,192/255.f},{0,0}};
    n3ds_gpu_geometry_flush(v,4*sizeof(*v));return n3ds_gpu_geometry_draw(v,indices,6);
}
int n3ds_gpu_chicago_tests(void)
{
    const unsigned int palette[3][4]={{0x2b597965,0xcd1b57d3,0x29cb419b,0x89a3c5dd},
        {0xab352d33,0x436b9581,0x314fa7cf,0x7bd921e1},{0x71553bb7,0x573193c7,0x7d294ba3,0xc9d35be9}};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    enum { CASES=79+11*11*2 };
    const int functions[11]={0,1,2,3,4,7,8,9,10,11,12};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[CASES][4],maximum=0;
    void *textures[3]={0}; u32 original_params[3]={0}; int result=1,active=0;
    FILE *proof=NULL; char message[192];
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"CHICAGO GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    CHECK(n3ds_generic_tests());
    CHECK(n3ds_gpu_texture_sampler_tests());
    for(unsigned int t=0;t<3;++t) {
        for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
            unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
            /* Native cache conversion reverses rows for PICA's V origin. */
            texels[morton]=palette[t][(x>=4)+2*(y<4)];
        }
        textures[t]=n3ds_gpu_texture_create(texels,8,8); CHECK(textures[t]);
        original_params[t]=((C3D_Tex *)textures[t])->param;
    }
    for(unsigned int test=0;test<CASES;++test) {
        unsigned int samples[3]={palette[0][0],palette[1][1],palette[2][2]};
        struct native_chicago_material m={.maps=3,.blend=0,.two_sided=1,.fade=1};
        m.color_function[0]=test%5; m.color_function[1]=(test/5)%5;
        m.alpha_function[0]=(test/5)%5; m.alpha_function[1]=test%5;
        m.alpha_replicate[0]=test%2; m.alpha_replicate[1]=(test/2)%2;
        if(test>=25) {
            m.maps=1+(test%3); m.blend=(test-25)%8;
            m.fade=test<33?1:.37f;
        }
        if(test==41 || test==42) {m.maps=1;m.blend=0;m.fade=1;m.alpha_test=1;}
        if(test==42) samples[0]=palette[0][1];
        if(test==45) {samples[1]=palette[0][1];samples[2]=palette[0][3];m.maps=3;m.color_function[0]=4;m.color_function[1]=2;}
        float angle=1;
        if(test>=46) {m.maps=1+(test-46)%3;m.blend=3;m.vertex_fade=2;m.fade=.37f;angle=(test-46)/3*.5f;}
        if(test>=55) {m.maps=1+(test-55)%3;m.blend=(test-55)/3;m.vertex_fade=2;m.fade=.61f;angle=.43f;}
        if(test>=79) {
            unsigned int n=test-79,f=n%11,a=(n/11)%11;
            m=(struct native_chicago_material){.maps=3,.blend=0,.two_sided=1,.fade=1};
            m.color_function[0]=functions[f];m.color_function[1]=functions[a];
            m.alpha_function[0]=functions[a];m.alpha_function[1]=functions[f];
            m.alpha_replicate[0]=n/121;m.alpha_replicate[1]=1-n/121;
        }
        unsigned int dest=0x7391adb7,expected=reference(&m,samples,dest);
        if(test>=46 && test<79) {struct native_chicago_material ref=m;ref.fade*=angle;expected=reference(&ref,samples,dest);}
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1.f,.near_clip=.1f,.far_clip=100,.clear_color=dest};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        if(test==44) {CHECK(opaque_quad(-.5f));expected=0x4080c0ff;}
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(unsigned int i=0;i<4;++i) {
            v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-1},{{.1875f,.1875f},{.8125f,.1875f},{.1875f,.8125f}}};
        }
        for(int t=0;t<3;++t) {CHECK(n3ds_gpu_texture_bind(t,textures[t]));m.point[t]=1;m.clamp_u[t]=1;m.clamp_v[t]=1;}
        if(test==42) for(int i=0;i<4;++i) v[i].uv[0][0]=.8125f;
        if(test==45) {
            /* The same texture needs different sampler descriptors per unit. */
            m.clamp_u[0]=m.clamp_v[0]=0;
            for(int t=0;t<3;++t) CHECK(n3ds_gpu_texture_bind(t,textures[0]));
            for(int i=0;i<4;++i) {v[i].uv[0][0]=1.1875f;v[i].uv[1][0]=1.8125f;v[i].uv[2][0]=1.1875f;}
        }
        const unsigned short invalid[3]={0,1,4};
        if(test>=46) for(int i=0;i<4;++i) for(int c=0;c<4;++c) v[i].color[c]=angle;
        CHECK(!n3ds_gpu_chicago_draw(&m,v,4,invalid,3));
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        for(int t=0;t<3;++t) CHECK(((C3D_Tex *)textures[t])->param==original_params[t]);
        if(test==43) {CHECK(opaque_quad(-2.f));expected=0x4080c0ff;}
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int difference=0;
        for(unsigned int y=184;y<216;++y) for(unsigned int x=104;x<136;++x) for(unsigned int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>4) {snprintf(message,sizeof(message),"CHICAGO GPU vector %u expected %08x got %08lx max difference %u",test,expected,(unsigned long)rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-chicago.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    CHECK(!generic_gpu_tests());
    CHECK(!generic_bake_cache_tests());
    CHECK(!generic_lit_lerp_gpu_tests());
    CHECK(!shield_scalar_bake_tests());
    CHECK(!shield_mask_gpu_tests());
    CHECK(!n3ds_gpu_glass_tests());
    CHECK(!meter_gpu_tests());
    CHECK(!effect_gpu_tests());
    CHECK(!effect_batch_gpu_tests());
    CHECK(!effect_layer_gpu_tests());
    snprintf(message,sizeof(message),"PASS: Chicago UVs/combiners/blends/fade/alpha test/depth/shared samplers: %u GPU vectors, %u pixels, max difference %u",CASES,CASES*1024,maximum);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();
    if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();
    for(unsigned int t=0;t<3;++t) n3ds_gpu_texture_destroy(textures[t]);
    linearFree(pixels);return result;
}

static int effect_layer_gpu_tests(void)
{
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[40][4];
    void *textures[2]={0};int result=1,active=0;char message[192];
    const unsigned int palette[4]={0x4080c080,0xc0307050,0x20c060a0,0x907020e0};
    const int order[6]={0,1,2,0,2,3};const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"EFFECT LAYER GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(int i=0;i<64;++i) texels[i]=0x20406080;
    textures[0]=n3ds_gpu_texture_create(texels,8,8);CHECK(textures[0]);
    for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
        unsigned int t=7-y,morton=(x&1)|((t&1)<<1)|((x&2)<<1)|((t&2)<<2)|((x&4)<<2)|((t&4)<<3);
        texels[morton]=palette[(x>=4)+2*(y>=4)];
    }
    textures[1]=n3ds_gpu_texture_create(texels,8,8);CHECK(textures[1]);
    for(int test=0;test<40;++test) {
        int sample_case=test%10,blend=test<10?NATIVE_BLEND_ADD:test<20?NATIVE_BLEND_SUBTRACT:NATIVE_BLEND_MAX;
        unsigned int count=test>=30?18:6;float tint=test>=30?.75f:.5f;
        /* MAX must keep destination red while visibly replacing green/blue. */
        unsigned int sample=palette[sample_case%4],dest=test<20?0x101820ff:0x600408ff,expected=255;
        float u=sample_case%2?.8125f:.1875f,v=(sample_case%4)/2?.8125f:.1875f;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=dest};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        if(sample_case>=8) {
            const unsigned short depth_indices[6]={0,1,2,0,2,3};
            struct native_render_vertex *depth=n3ds_gpu_model_vertices_allocate(4);CHECK(depth);
            for(int i=0;i<4;++i) depth[i]=(struct native_render_vertex){{xy[i][0],xy[i][1],sample_case==8?-.5f:-20.f},{1,0,1},{0,0}};
            n3ds_gpu_geometry_flush(depth,4*sizeof(*depth));
            CHECK(n3ds_gpu_model_depth_draw(depth,depth_indices,6));
        }
        for(int i=0;i<2;++i) CHECK(n3ds_gpu_texture_bind(i,textures[i]));
        struct native_chicago_vertex *vertices=n3ds_gpu_chicago_allocate(count);CHECK(vertices);
        for(unsigned int i=0;i<count;++i) {
            float vertex_tint=count>6?(i/6+1)*.25f:tint;
            int q=order[i%6];float depth=sample_case<4?4.f:(q==1 || q==2?8.f:1.f);
            vertices[i]=(struct native_chicago_vertex){{xy[q][0],xy[q][1],-depth},{{u*depth,v*depth},{.5f,.5f},{depth,0}},{vertex_tint,vertex_tint,vertex_tint,.75f}};
        }
        CHECK(n3ds_gpu_effect_layer_draw(vertices,count,blend,1,1,(sample_case>=4?4:0)|(test>=20?32:0)));
        for(int i=0;i<2;++i) CHECK(n3ds_gpu_texture_bound(i)==textures[i] && C3D_TexGetType(textures[i])==GPU_TEX_2D);
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        for(int c=0;c<3;++c) {
            int shift=24-8*c;
            float color=((0x20406080u>>shift)&255)/255.f*tint*((sample>>shift)&255)/255.f*(sample&255)/255.f;
            if(sample_case<4) color*=128.f/255.f;
            float destination=((dest>>shift)&255),source=color*255.f;
            float combined=blend==NATIVE_BLEND_ADD?fminf(255.f,source+destination):blend==NATIVE_BLEND_SUBTRACT?fmaxf(0.f,destination-source):fmaxf(destination,source);
            expected|=(unsigned int)floorf(combined+.5f)<<shift;
        }
        if(sample_case==8) expected=dest; /* Near prepass hides the effect and writes no RGB. */
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int d=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(d>difference) difference=d;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>3) {snprintf(message,sizeof(message),"EFFECT LAYER PIXELS: test=%d expected=%08x got=%08x difference=%u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    FILE *proof=fopen("sdmc:/halo-native-effect-layer.bin","wb");CHECK(proof);
    int written=fwrite(rows,sizeof(rows),1,proof)==1,closed=fclose(proof)==0;CHECK(written && closed);
    n3ds_log("PASS: effect secondary layer: 40 add/subtract/max/projective/depth/overlapping-particle GPU vectors, 40960 pixels");result=0;
done:
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    for(int i=0;i<2;++i) n3ds_gpu_texture_destroy(textures[i]);
    linearFree(pixels);return result;
}
static int generic_bake_cache_tests(void)
{
    unsigned int texels[64],saved[64],*pixels=linearAlloc(240*400*4);
    void *texture=NULL,*mutable=NULL;int active=0,result=1;char message[192];
    const float bounds[4]={0,0,1,1};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    struct native_generic_material g={.maps=1,.stages=1,.point={1}};
    g.transform[0][0][0]=g.transform[0][1][1]=1;
    g.stage[0].op[0]=g.stage[0].op[14]=5;
    g.stage[0].op[2]=g.stage[0].op[16]=1;
    g.stage[0].op[12]=g.stage[0].op[24]=1;
    struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x000000ff};
    struct native_chicago_material m={.maps=1,.blend=3,.two_sided=1,.fade=1};
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"GENERIC CACHE FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels && n3ds_gpu_texture_barrier());n3ds_gpu_chicago_map_release();
    for(int i=0;i<64;++i) texels[i]=0x285078a0;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    unsigned int identity=n3ds_gpu_texture_publish_immutable(texture);CHECK(identity);
    g.textures[0]=texture;C3D_Tex *original=NULL;
    /* Both first-use and cross-frame reuse must produce the same actual GPU
     * image. Multiple matches in one frame represent draw/stereo reuse. */
    for(int frame=0;frame<2;++frame) {
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        C3D_Tex *t=n3ds_gpu_generic_bake(&g,bounds);CHECK(t);
        if(!frame) {original=t;memcpy(saved,t->data,sizeof(saved));}
        CHECK(t==original && !memcmp(saved,t->data,sizeof(saved)));
        CHECK(n3ds_gpu_generic_bake(&g,bounds)==t);
        CHECK(n3ds_gpu_texture_bind(0,t));
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-1},{{.5f,.5f}}};
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        if(!frame) for(int change=0;change<9;++change) {
            struct native_generic_material other=g;float b[4]={0,0,1,1};
            if(change==0) other.stage[0].constant[0][0]=.5f;
            if(change==1) other.stage[0].op[13]=1;
            if(change==2) other.transform[0][0][3]=.25f;
            if(change==3) b[2]=2;
            if(change==4) other.point[0]=0;
            if(change==5) other.clamp_u[0]=1;
            if(change==6) other.clamp_v[0]=1;
            if(change==7) other.stage[0].flags=4;
            C3D_Tex *different=change==8?n3ds_gpu_meter_bake(&other,b):n3ds_gpu_generic_bake(&other,b);
            CHECK(different && different!=t && !memcmp(saved,t->data,sizeof(saved)));
        }
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        for(int y=184;y<216;++y) for(int x=104;x<136;++x)
            CHECK(pixels[y*240+x]==0x285078ff);
    }
    CHECK(n3ds_gpu_texture_barrier());
    n3ds_gpu_texture_destroy(texture);texture=NULL;
    for(int i=0;i<64;++i) texels[i]=0x5078a0c0;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    CHECK(n3ds_gpu_texture_publish_immutable(texture)!=identity);g.textures[0]=texture;
    mutable=n3ds_gpu_texture_create(texels,8,8);CHECK(mutable && !n3ds_gpu_texture_content_id(mutable));
    CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
    C3D_Tex *replacement=n3ds_gpu_generic_bake(&g,bounds);CHECK(replacement);
    CHECK(((unsigned int *)replacement->data)[0]==0x5078a0c0);
    g.textures[0]=mutable;
    C3D_Tex *dynamic=n3ds_gpu_generic_bake(&g,bounds);CHECK(dynamic);
    for(int i=0;i<64;++i) ((unsigned int *)((C3D_Tex *)mutable)->data)[i]=0xa05028ff;
    C3D_TexFlush(mutable);
    C3D_Tex *updated=n3ds_gpu_generic_bake(&g,bounds);CHECK(updated && updated!=dynamic);
    CHECK(((unsigned int *)updated->data)[0]==0xa05028ff && ((unsigned int *)dynamic->data)[0]==0x5078a0c0);
    n3ds_gpu_present();active=0;CHECK(n3ds_gpu_texture_barrier());
    n3ds_gpu_chicago_map_release();g.textures[0]=texture;
    CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
    C3D_Tex *slots[32];
    for(int i=0;i<32;++i) {
        g.stage[0].constant[0][0]=(float)i;
        slots[i]=n3ds_gpu_generic_bake(&g,bounds);CHECK(slots[i]);
        for(int j=0;j<i;++j) CHECK(slots[j]!=slots[i]);
    }
    g.stage[0].constant[0][0]=32;CHECK(!n3ds_gpu_generic_bake(&g,bounds));
    for(int i=0;i<32;++i) {
        g.stage[0].constant[0][0]=(float)i;
        CHECK(n3ds_gpu_generic_bake(&g,bounds)==slots[i]);
        CHECK(((unsigned int *)slots[i]->data)[0]==0x5078a0c0);
    }
    n3ds_gpu_present();active=0;
    CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
    g.stage[0].constant[0][0]=32;CHECK(n3ds_gpu_generic_bake(&g,bounds));
    n3ds_gpu_present();active=0;
    n3ds_log("PASS: generic bake cache: exact GPU pixels across frames, stereo/draw reuse, 9 packet mutations, source replacement, mutable bypass, 32-slot in-flight protection, full-cache hits and fenced reclamation");result=0;
done:
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    n3ds_gpu_chicago_map_release();
    n3ds_gpu_texture_destroy(texture);n3ds_gpu_texture_destroy(mutable);linearFree(pixels);return result;
}
static int generic_gpu_tests(void)
{
    /* Four independently transformed map samples, including a fourth map
     * unavailable as a simultaneous PICA sampler. Expected bytes below use
     * direct arithmetic, not the production combiner interpreter. */
    const unsigned int colors[4]={0x28406080,0x50709040,0x90b0d0c0,0xb08050a0};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    unsigned int texels[64],rows[8][4],*pixels=linearAlloc(240*400*4),maximum=0;
    void *texture=NULL,*ramp=NULL;FILE *proof=NULL;int result=1,active=0;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"GENERIC GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
        unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
        texels[morton]=colors[(x>=4)+2*(y<4)];
    }
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
        unsigned int ty=7-y;
        unsigned int morton=(x&1)|((ty&1)<<1)|((x&2)<<1)|((ty&2)<<2)|((x&4)<<2)|((ty&4)<<3);
        texels[morton]=((x*31+7)<<24)|((y*29+11)<<16)|((x+y)*13<<8)|255;
    }
    ramp=n3ds_gpu_texture_create(texels,8,8);CHECK(ramp);
    for(int test=0;test<8;++test) {
        struct native_generic_material g={.maps=4,.stages=1};
        struct native_chicago_material m={.maps=1,.blend=3,.two_sided=1,.fade=1,.vertex_fade=test==7};
        float bounds[4]={-2,-3,2,3};
        int *p=g.stage[0].op;
        p[0]=5;p[2]=6;p[4]=7;p[6]=8;p[12]=1;
        p[14]=5;p[16]=6;p[18]=7;p[20]=8;p[24]=1;
        if(test==1 || test==2) g.stage[0].flags=3;
        if(test==3) {p[1]=2;p[3]=6;p[5]=4;p[7]=6;} /* signed intermediates */
        if(test==4) {p[12]=0;p[8]=1;p[9]=1;} /* dot product */
        if(test==5) p[13]=1;
        if(test==6) p[13]=2;
        for(int i=0;i<4;++i) {
            g.textures[i]=texture;g.point[i]=1;
            g.transform[i][0][3]=(i&1)?.8125f:.1875f;
            g.transform[i][1][3]=(i&2)?.8125f:.1875f;
        }
        if(test==1) {g.transform[0][0][3]=.8125f;} /* alpha < .5, select AB */
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x000000ff};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        void *baked=n3ds_gpu_generic_bake(&g,bounds);CHECK(baked);
        /* A second bake must neither alias nor overwrite the first in flight. */
        struct native_generic_material changed=g;changed.stage[0].op[13]=4;
        void *other=n3ds_gpu_generic_bake(&changed,bounds);CHECK(other && other!=baked);
        if(test==0) {
            struct native_generic_material coarse={.maps=1,.stages=1,.point={1},.textures={ramp}};
            coarse.transform[0][0][0]=coarse.transform[0][1][1]=1;
            coarse.stage[0].op[0]=coarse.stage[0].op[14]=5;
            coarse.stage[0].op[2]=coarse.stage[0].op[16]=1;
            coarse.stage[0].op[12]=coarse.stage[0].op[24]=1;
            const float unit[4]={0,0,1,1};
            C3D_Tex *grid=n3ds_gpu_generic_bake(&coarse,unit);CHECK(grid);
            for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
                unsigned int sx=(x/2)*2+1,sy=(y/2)*2+1,ty=7-y;
                unsigned int morton=(x&1)|((ty&1)<<1)|((x&2)<<1)|((ty&2)<<2)|((x&4)<<2)|((ty&4)<<3);
                unsigned int expected=((sx*31+7)<<24)|((sy*29+11)<<16)|((sx+sy)*13<<8)|255;
                CHECK(((const unsigned int *)grid->data)[morton]==expected);
            }
            n3ds_log("PASS: generic 4x4 shading grid: 64 expanded texels match independent gradient centers");
        }
        CHECK(n3ds_gpu_texture_bind(0,baked));
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-1},{{.5f,.5f},{0,0},{.5f,0}}};
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        float color[3],dot=0;unsigned int expected=255;
        for(int c=0;c<3;++c) {
            int shift=24-c*8;
            float a=((colors[test==1?1:0]>>shift)&255)/255.f,b=((colors[1]>>shift)&255)/255.f;
            float cc=((colors[2]>>shift)&255)/255.f,d=((colors[3]>>shift)&255)/255.f;
            float value=a*b+cc*d;
            if(test==1) value=a*b;if(test==2) value=cc*d;
            if(test==3) value=(2*a-1)*b+(cc-.5f)*d;
            dot+=a*b;
            if(test==5) value*=.5f;if(test==6) value*=2;
            color[c]=value;
        }
        for(int c=0;c<3;++c) {
            float value=fminf(1,fmaxf(0,test==4?dot:color[c]));
            unsigned int quantized=(unsigned int)floorf(value*255+.5f);
            if(test==7) quantized=(quantized+1)/2;
            expected|=quantized<<(24-c*8);
        }
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>2) {snprintf(message,sizeof(message),"GENERIC GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    /* New view-dependent path must preserve vertex alpha and all three
     * framebuffer operations used by cryo glass/tint. Texture is unbound. */
    for(int mode=0;mode<3;++mode) {
        const int blends[3]={1,3,7};
        struct native_chicago_material m={.maps=1,.blend=blends[mode],.two_sided=1,.fade=1,.vertex_color=1};
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x404040ff};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        CHECK(n3ds_gpu_texture_bind(0,NULL));
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-1},{{0}}, {128/255.f,64/255.f,32/255.f,128/255.f}};
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        const int rgb[3]={128,64,32};
        for(int c=0;c<3;++c) {
            int expected=mode==0?(rgb[c]*64+127)/255:mode==1?rgb[c]+64:rgb[c]+(64*127+127)/255;
            CHECK(abs((int)((pixels[200*240+120]>>(24-c*8))&255)-expected)<=2);
        }
    }
    n3ds_log("PASS: generic vertex RGBA multiply/add/premultiplied GPU blending: 3 vectors; cryo/tint algebra: 256 CPU vectors");
    proof=fopen("sdmc:/halo-native-generic.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: generic four-map UV bake, signed/mux/dot/scale, draw lifetime, vertex fade: 8 GPU vectors, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);n3ds_gpu_texture_destroy(ramp);linearFree(pixels);return result;
}

int n3ds_gpu_model_decal_tests(void)
{
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[10][4],maximum=0;
    const unsigned short ccw[6]={0,1,2,0,2,3},cw[6]={0,2,1,0,3,2};
    const unsigned int alphas[4]={0,64,128,255};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    void *texture=NULL;FILE *proof=NULL;int active=0,result=1;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"MODEL DECAL GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(unsigned int test=0;test<10;++test) {
        unsigned int alpha=test<4?alphas[test]:128,destination=0x20406099,expected=destination&255;
        if(test==5 || test==8) destination=0x4080c0ff;
        expected=destination&255;
        const unsigned int source[3]={60,60,200}; /* base texture RGB * vertex lighting */
        for(unsigned int channel=0;channel<3;++channel) {
            unsigned int shift=24-channel*8,d=(destination>>shift)&255;
            expected|=((source[channel]*alpha+d*(255-alpha)+127)/255)<<shift;
        }
        if(test==4 || test==5 || test==9) expected=0x4080c0ff;
        if(test==6) expected=destination;
        for(unsigned int i=0;i<64;++i) texels[i]=0x7850c800|alpha;
        texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x20406099};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        if(test==5 || test==8) CHECK(opaque_quad(-.5f));
        struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);CHECK(v);
        for(unsigned int i=0;i<4;++i) v[i]=(struct native_render_vertex){{xy[i][0],xy[i][1],-1},{.5f,.75f,1},{.5f,.5f}};
        n3ds_gpu_geometry_flush(v,4*sizeof(*v));CHECK(n3ds_gpu_texture_bind(0,texture));
        CHECK(n3ds_gpu_model_decal_draw(v,test==7?cw:ccw,6,test>=8,test!=6 && test!=7));
        if(test==4 || test==9) CHECK(opaque_quad(-2));
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int difference=0;
        for(unsigned int y=184;y<216;++y) for(unsigned int x=104;x<136;++x) for(unsigned int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>2) {snprintf(message,sizeof(message),"MODEL DECAL GPU vector %u expected %08x got %08lx max difference %u",test,expected,(unsigned long)rows[test][2],difference);n3ds_log(message);goto done;}
        n3ds_gpu_texture_destroy(texture);texture=NULL;
    }
    proof=fopen("sdmc:/halo-native-model-decals.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: model decals base alpha, vertex lighting, framebuffer alpha, depth, culling and sky state: 10 GPU vectors, 10240 pixels, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}

int n3ds_gpu_first_person_tests(void)
{
    unsigned int *pixels=linearAlloc(240*400*4),rows[6][4],maximum=0,texels[64];
    int active=0,weapon=0,result=1;void *texture=NULL;FILE *proof=NULL;char message[192];
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    CHECK(pixels);
    for(unsigned int i=0;i<64;++i) texels[i]=0xff0000ff;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    for(unsigned int test=0;test<6;++test) {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ff};
        unsigned int expected=(test==0 || test==4)?0x204060ff:0x4080c0ff;
        float before[16],after[16];
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        CHECK(n3ds_gpu_world_projection(before));
        CHECK(!n3ds_gpu_first_person_begin(0,3));
        if(test>=1 && test<=4) {
            CHECK(n3ds_gpu_first_person_begin(.01f,3));weapon=1;
            CHECK(!n3ds_gpu_first_person_begin(.01f,3));
            if(test!=4) CHECK(opaque_quad(test==1?-.02f:-.5f));
            n3ds_gpu_first_person_end();weapon=0;
            CHECK(n3ds_gpu_world_projection(after) && !memcmp(before,after,sizeof(before)));
        }
        if(test==0 || test==4) CHECK(opaque_quad(-.02f));
        if(test==5) CHECK(opaque_quad(-.5f));
        if(test==2) {
            n3ds_gpu_world_state_restore();
            struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);CHECK(v);
            for(int i=0;i<4;++i) v[i]=(struct native_render_vertex){{xy[i][0],xy[i][1],-.2f},{1,0,0},{0,0}};
            n3ds_gpu_geometry_flush(v,4*sizeof(*v));CHECK(n3ds_gpu_texture_bind(0,NULL));
            CHECK(n3ds_gpu_geometry_draw(v,indices,6));
        }
        if(test==3) {
            struct native_chicago_material m={.maps=1,.blend=0,.two_sided=1,.fade=1};
            struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
            for(int i=0;i<4;++i) v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-.2f},{{.5f,.5f},{0,0},{0,0}}};
            CHECK(n3ds_gpu_texture_bind(0,texture));CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        }
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int difference=0;
        for(unsigned int y=184;y<216;++y) for(unsigned int x=104;x<136;++x) for(unsigned int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>1) {snprintf(message,sizeof(message),"FIRST PERSON GPU vector %u expected %08x got %08lx max difference %u",test,expected,(unsigned long)rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-first-person.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: first-person clip planes, stencil protects against opaque/Chicago world draws, projection restore: 6 GPU vectors, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}

int n3ds_gpu_glass_tests(void)
{
    const float directions[12][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {2,1,1},{-2,1,1},{1,2,1},{1,-2,1},{1,1,2},{1,1,-2}};
    const float coordinates[12][2]={{.5,.5},{.5,.5},{.5,.5},{.5,.5},{.5,.5},{.5,.5},
        {.25,.25},{.75,.25},{.75,.75},{.75,.25},{.75,.25},{.25,.25}};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[10][4],maximum=0;
    int active=0,weapon=0,result=1;void *texture=NULL;FILE *proof=NULL;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"GLASS GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(int i=0;i<12;++i) {
        int face=-1;float uv[2];CHECK(n3ds_cube_coordinates(directions[i],&face,uv));
        CHECK(face==i%6 && fabsf(uv[0]-coordinates[i][0])<1e-6f && fabsf(uv[1]-coordinates[i][1])<1e-6f);
    }
    const float zero[3]={0,0,0};int face;float uv[2];CHECK(!n3ds_cube_coordinates(zero,&face,uv));
    for(int i=0;i<64;++i) texels[i]=0x80604080;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    for(int test=0;test<10;++test) {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060a5};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        unsigned int expected=0xa5;
        float dst[3]={32,64,96};
        if(test==4) {CHECK(opaque_quad(-.5f));expected=0x4080c0ff;}
        if(test==7) {
            CHECK(n3ds_gpu_first_person_begin(.02f,100));weapon=1;
            CHECK(opaque_quad(-.05f));n3ds_gpu_first_person_end();weapon=0;expected=0x4080c0ff;
        }
        int first=test<3?test:0,last=test<3?test:2;
        if(test>=8){first=last=2;if(test==9){CHECK(opaque_quad(-.5f));dst[0]=64;dst[1]=128;dst[2]=192;expected=0xff;}}
        for(int pass=first;pass<=last;++pass) {
            struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(4);CHECK(v);
            for(int i=0;i<4;++i) v[i]=(struct native_render_vertex){{xy[i][0],xy[i][1],-1},{.5f,.75f,1},{.5f,.5f}};
            n3ds_gpu_geometry_flush(v,4*sizeof(*v));
            CHECK(n3ds_gpu_texture_bind(0,pass==1?NULL:texture));
            CHECK(n3ds_gpu_glass_sky_draw(v,indices,6,pass,test!=6,test>=8));
            for(int c=0;c<3;++c) {
                float light=.5f+c*.25f,source=pass==1?light*255:(128-c*32)*light;
                if(pass==0) dst[c]=dst[c]*source/255;
                if(pass==1) dst[c]=fminf(255,dst[c]+source);
                if(pass==2) dst[c]=(source*128+dst[c]*127)/255;
                dst[c]=floorf(dst[c]+.5f);
            }
        }
        if(test==5) {CHECK(opaque_quad(-2));expected=0x4080c0ff;}
        else if(test==6) expected=0x204060a5;
        else if(test!=4 && test!=7) for(int c=0;c<3;++c) expected|=(unsigned int)dst[c]<<(24-c*8);
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>2) {snprintf(message,sizeof(message),"GLASS GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-glass.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: glass tint/reflection/diffuse, depth, culling, weapon stencil: 10 GPU vectors including sky behind existing depth; 12 cube directions; max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}

static int meter_gpu_tests(void)
{
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[12][4],maximum=0;
    int active=0,weapon=0,result=1;void *texture=NULL;FILE *proof=NULL;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"METER GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(int test=0;test<12;++test) {
        float constants[5][4]={{0,.75f,0,1},{.8f,0,.1f,.125f},{.4f,.2f,0,.6f},{.1f,.1f,.2f,0},{.7f,1,.8f,.6f}};
        constants[0][3]=test<5?test*.25f:1;
        if(test==8) constants[4][3]=0;
        for(int i=0;i<5;++i) for(int c=0;c<4;++c) constants[i][c]=floorf(constants[i][c]*255+.5f)/255.f;
        unsigned int blue=test>=5?230:96,alpha=test>=10?0:test==9?32:128;
        for(int i=0;i<64;++i) texels[i]=0x60a00000|(blue<<8)|alpha;
        texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
        struct native_generic_material g={0};n3ds_meter_program(&g,constants,test==7);
        CHECK(n3ds_generic_validate(&g));g.textures[0]=texture;g.point[0]=1;
        g.transform[0][0][0]=g.transform[0][1][1]=1;
        struct native_chicago_material m={.maps=1,.blend=0,.two_sided=1,.fade=1,.meter=1};
        for(int c=0;c<4;++c) m.meter_tint|=(unsigned int)(constants[4][c]*255+.5f)<<(8*c);
        const float bounds[4]={0,0,1,1};
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        if(test==11) {CHECK(n3ds_gpu_first_person_begin(.02f,100));weapon=1;}
        void *baked=n3ds_gpu_meter_bake(&g,bounds);CHECK(baked);CHECK(n3ds_gpu_texture_bind(0,baked));
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) v[i]=(struct native_chicago_vertex){{xy[i][0],xy[i][1],-1},{{.5f,.5f},{0,0},{0,0}}};
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        if(weapon) {n3ds_gpu_first_person_end();weapon=0;CHECK(opaque_quad(-2));}
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int expected=0xab;
        float b=blue/255.f,gradient=fminf(1,8*constants[1][3]*b);
        float flash=fmaxf(0,1-2*fmaxf(0,fminf(1,4*(constants[2][3]-b))));
        for(int c=0;c<3;++c) {
            float value=(1-gradient)*constants[0][c]+gradient*constants[1][c]+(test==7?-1:1)*flash*constants[2][c];
            if(b>=constants[0][3]) value=constants[3][c];
            value=fmaxf(0,fminf(1,value))*alpha/255.f;
            unsigned int baked_channel=(unsigned int)floorf(value*255+.5f);
            unsigned int channel=(unsigned int)floorf(fminf(255,baked_channel*constants[4][3]+(32+c*32)*constants[4][c])+.5f);
            if(!alpha) channel=32+c*32;
            expected|=channel<<(24-c*8);
        }
        if(test==11) expected=0x4080c0ff;
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>2) {snprintf(message,sizeof(message),"METER GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
        n3ds_gpu_texture_destroy(texture);texture=NULL;
    }
    proof=fopen("sdmc:/halo-native-meter.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: meter animated values, flash sign, brightness, alpha mask and stencil: 12 GPU vectors, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}

/* Compare the old per-quad submission with ordered batches. A half-screen
 * weapon mask exercises both disjoint stencil passes in the same image. */
static int effect_batch_gpu_tests(void)
{
    const unsigned int order[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    unsigned int *pixels=linearAlloc(240*400*4),*reference_pixels=linearAlloc(240*400*4);
    unsigned int texels[64],rows[32][8],failures=0;
    int active=0,weapon=0,result=1;void *texture=NULL;FILE *proof=NULL;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"EFFECT BATCH GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels && reference_pixels);
    for(unsigned int i=0;i<64;++i) texels[i]=0x5080c080;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    for(int test=0;test<32;++test) {
        int blend=test%8,mode=test/8;
        int unstable=(mode==1 || mode==2) && (blend==5 || blend==6);
        unsigned int batch_maximum=0;
        unsigned int flags=mode?128:0,shader_flags=mode==2?2:mode==3?4:0;
        for(int batched=0;batched<3;++batched) {
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060a5};
            CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
            CHECK(n3ds_gpu_first_person_begin(.02f,100));weapon=1;
            const unsigned short indices[6]={0,1,2,0,2,3};
            struct native_render_vertex *mask=n3ds_gpu_model_vertices_allocate(4);CHECK(mask);
            CHECK(n3ds_gpu_texture_bind(0,NULL));
            for(int i=0;i<4;++i) mask[i]=(struct native_render_vertex){{xy[i][0]>0?0:xy[i][0],xy[i][1],-.5f},{64/255.f,128/255.f,192/255.f},{0,0}};
            n3ds_gpu_geometry_flush(mask,4*sizeof(*mask));CHECK(n3ds_gpu_geometry_draw(mask,indices,6));
            n3ds_gpu_first_person_end();weapon=0;
            CHECK(n3ds_gpu_texture_bind(0,texture));
            struct native_widget_vertex vertices[18];
            for(int q=0;q<3;++q) for(int i=0;i<6;++i)
                vertices[q*6+i]=(struct native_widget_vertex){{xy[order[i]][0],xy[order[i]][1],q==1?-.2f:-1},
                    {(.2f+q*.2f),(.7f-q*.2f),(.3f+q*.1f),(.25f+q*.2f)},{.5f,.5f}};
            C3D_Tex *tex=texture;u32 sampler=tex->param,border=tex->border;
            if(batched==1) CHECK(n3ds_gpu_effect_draw(vertices,18,blend,shader_flags,7,flags));
            else for(int q=0;q<3;++q) CHECK(n3ds_gpu_effect_draw(vertices+q*6,6,blend,shader_flags,7,flags));
            CHECK(tex->param==sampler && tex->border==border);
            memset(vertices,0xa5,sizeof(vertices));
            n3ds_gpu_present();active=0;
            CHECK(n3ds_gpu_readback(batched?pixels:reference_pixels,240*400));
            if(batched==1) {
                unsigned int different=0;for(unsigned int i=0;i<240*400;++i) {
                    different+=pixels[i]!=reference_pixels[i];
                    for(unsigned int s=0;s<32;s+=8) {
                        unsigned int d=abs((int)((pixels[i]>>s)&255)-(int)((reference_pixels[i]>>s)&255));
                        if(d>batch_maximum) batch_maximum=d;
                    }
                }
                if(different) {
                    snprintf(message,sizeof(message),"EFFECT BATCH PAIR test=%d pixels=%u reference=%08x,%08x actual=%08x,%08x",test,different,reference_pixels[100*240+120],reference_pixels[300*240+120],pixels[100*240+120],pixels[300*240+120]);n3ds_log(message);
                }
                failures+=!unstable && different!=0;
            }
        }
        unsigned int maximum=0,changed=0;
        for(unsigned int i=0;i<240*400;++i) {
            changed+=pixels[i]!=0x204060a5 && pixels[i]!=0x4080c0ff;
            for(unsigned int shift=0;shift<32;shift+=8) {
                unsigned int delta=abs((int)((pixels[i]>>shift)&255)-(int)((reference_pixels[i]>>shift)&255));
                if(delta>maximum) maximum=delta;
            }
        }
        rows[test][0]=test;rows[test][1]=blend;rows[test][2]=flags;rows[test][3]=shader_flags;
        rows[test][4]=batch_maximum;rows[test][5]=maximum;rows[test][6]=changed;rows[test][7]=unstable;
        /* Min/max can legitimately preserve this destination. Alpha/additive
         * cases supply positive controls against two accidentally empty draws. */
        int positive=blend==0 || blend==3 || blend==7;
        if(maximum || (positive && changed<=10000)) {snprintf(message,sizeof(message),"EFFECT BATCH vector=%d blend=%d mode=%d maximum=%u changed=%u",test,blend,mode,maximum,changed);n3ds_log(message);}
        failures+=(!unstable && maximum!=0) || (positive && changed<=10000);
    }
    proof=fopen("sdmc:/halo-native-effect-batches.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    CHECK(!failures);
    n3ds_log("PASS: ordered effect batches: 28 exact full-screen GPU comparisons with repeated separate-draw controls; alpha/additive/depth/weapon stencil/nonlinear tint");
    n3ds_log("EFFECT BATCH LIMIT: 4 MIN/MAX weapon-overlap repeatability probes recorded separately; identical old calls vary in Azahar, MIN/MAX retains original per-quad submissions");result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);linearFree(reference_pixels);return result;
}

static int effect_gpu_tests(void)
{
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    const unsigned int order[6]={0,1,2,0,2,3};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[24][4],maximum=0;
    int active=0,weapon=0,result=1;void *texture=NULL;FILE *proof=NULL;char message[192];
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"EFFECT GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(unsigned int i=0;i<64;++i) texels[i]=0x5080c080;
    texture=n3ds_gpu_texture_create(texels,8,8);CHECK(texture);
    for(int test=0;test<24;++test) {
        int blend=test<16?test%8:test==22?0:3,nonlinear=(test>=8 && test<16)||test==23;
        unsigned int flags=test>=18&&test<=21?128:0,shader_flags=nonlinear?2:0;
        if(test==21) shader_flags|=4;
        if(test>=22) flags|=4;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060a5};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        unsigned int destination=0x204060a5;
        if(test>=16 && test<=21) {
            if(test!=16 && test!=20) {CHECK(n3ds_gpu_first_person_begin(.02f,100));weapon=1;}
            CHECK(opaque_quad(-.5f));destination=0x4080c0ff;
            if(weapon) {n3ds_gpu_first_person_end();weapon=0;}
        }
        CHECK(n3ds_gpu_texture_bind(0,texture));
        struct native_widget_vertex vertices[6];
        for(int i=0;i<6;++i) vertices[i]=(struct native_widget_vertex){{xy[order[i]][0],xy[order[i]][1],test==19?-.2f:test==18?-10:-1},
            {102/255.f,153/255.f,204/255.f,128/255.f},{.5f,.5f}};
        C3D_Tex *tex=texture;u32 sampler=tex->param,border=tex->border;
        CHECK(n3ds_gpu_effect_draw(vertices,6,blend,shader_flags,7,flags));
        if(test==0) {
            struct native_widget_vertex offscreen[1536];
            for(int i=0;i<1536;++i) offscreen[i]=(struct native_widget_vertex){{10000,10000,-1},{0,0,0,0},{0,0}};
            for(int i=0;i<5;++i) CHECK(n3ds_gpu_effect_draw(offscreen,1536,0,0,7,0));
            n3ds_log("PASS: submitted 7686 effect vertices without overwriting the first queued draw");
        }
        CHECK(tex->param==sampler && tex->border==border);
        memset(vertices,0xa5,sizeof(vertices));
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int expected=destination&255;
        for(int c=0;c<3;++c) {
            float t=(c==0?80:c==1?128:192)/255.f,color=(102+c*51)/255.f;
            float source=nonlinear?color*t+(1-color)*powf(t,4):color*t;
            float a=128/255.f,mask=128/255.f;
            if(!(flags&4)) {
                if(blend==0 || blend==7) a*=mask;
                if(blend!=0) source*=mask;
                if(blend==1 || blend==5) source+=1-mask;
                if(blend==2) source+=(1-mask)*.5f;
            }
            float d=((destination>>(24-c*8))&255)/255.f,out=0;
            switch(blend) {
            case 0:out=source*a+d*(1-a);break;case 1:out=source*d;break;case 2:out=2*source*d;break;
            case 3:out=source+d;break;case 4:out=d-source;break;case 5:out=fminf(source,d);break;
            case 6:out=fmaxf(source,d);break;case 7:out=source+d*(1-a);break;
            }
            if(test==16 || test==17 || test==18 || test==21) out=d;
            expected|=(unsigned int)floorf(fminf(1,fmaxf(0,out))*255+.5f)<<(24-c*8);
        }
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>3) {snprintf(message,sizeof(message),"EFFECT GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-effects.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: effect linear/nonlinear tint, 8 blends, depth and weapon overlap: 24 GPU vectors, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}

/* Independent reduced Xbox algebra, including BLUE-to-alpha routing. Vary
 * each map's UV independently; texture1 alpha intentionally differs from blue. */
static int shield_mask_gpu_tests(void)
{
    const unsigned int palette[2][4]={{0xc53a7100,0xa1295b55,0x149d23aa,0x893127ff},
        {0x436b00c3,0x8fc2802a,0x2766ff96,0x555555e1}};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    enum {CASES=176};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[CASES][4],maximum=0;
    void *textures[2]={0};int active=0,result=1;char message[224];FILE *proof=NULL;
#undef CHECK
#define CHECK(e) do {if(!(e)){snprintf(message,sizeof(message),"SHIELD MASK GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(pixels);
    for(int t=0;t<2;++t) {
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
            texels[morton]=palette[t][(x>=4)+2*(y<4)];
        }
        textures[t]=n3ds_gpu_texture_create(texels,8,8);CHECK(textures[t]);
    }
    for(int test=0;test<CASES;++test) {
        int q[2]={test%4,(test/4)%4};if(test>=144)q[1]=0;
        struct native_chicago_material m={.maps=2,.blend=(test/16)%8,.two_sided=1,
            .fade=test>=128?153/255.f:1,.shield_mask=1,.shield_tint=0x004b95df};
        m.vertex_fade=test>=128;m.alpha_test=test>=136;
        m.point[0]=test&1;m.point[1]=test>=160 || (test<144 && (test&2));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        u32 params[2];
        for(int t=0;t<2;++t){params[t]=((C3D_Tex*)textures[t])->param;CHECK(n3ds_gpu_texture_bind(t,textures[t]));}
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) {
            memset(v+i,0,sizeof(*v));v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[2]=-1;
            for(int t=0;t<2;++t){v[i].uv[t][0]=(q[t]&1)?.8125f:.1875f;v[i].uv[t][1]=(q[t]&2)?.8125f:.1875f;}
            if(test>=144)v[i].uv[1][0]=test>=160?.49f:.5f;
            v[i].uv[2][0]=.4f;
        }
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        for(int t=0;t<2;++t)CHECK(((C3D_Tex*)textures[t])->param==params[t]);
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        float intensity=(palette[0][q[0]]&255)/255.f,mask[4];
        for(int c=0;c<4;++c) {
            mask[c]=((palette[1][q[1]]>>(24-8*c))&255)/255.f;
            if(test>=144 && test<160)mask[c]=.5f*(mask[c]+((palette[1][1]>>(24-8*c))&255)/255.f);
        }
        float fade=m.fade*(m.vertex_fade?.4f:1),alpha=intensity;
        if(m.blend==0 || m.blend==7)alpha*=fade;
        unsigned int expected=0xab;
        for(int c=0;c<3;++c) {
            float tint=((m.shield_tint>>(8*c))&255)/255.f;
            float s=fminf(1,intensity*mask[c]+tint*(1-mask[3])),d=(32+c*32)/255.f,out;
            if(m.blend!=0)s*=fade;
            if(m.blend==1 || m.blend==5)s+=1-fade;
            if(m.blend==2)s+=(1-fade)*.5f;
            switch(m.blend) {
            case 0:out=s*alpha+d*(1-alpha);break;case 1:out=s*d;break;case 2:out=2*s*d;break;
            case 3:out=s+d;break;case 4:out=d-s;break;case 5:out=fminf(s,d);break;
            case 6:out=fmaxf(s,d);break;default:out=s+d*(1-alpha);break;
            }
            if(m.alpha_test && alpha<=127.f/255.f)out=d;
            expected|=(unsigned int)floorf(fminf(1,fmaxf(0,out))*255+.5f)<<(24-8*c);
        }
        unsigned int difference=0;
        for(int y=184;y<216;++y)for(int x=104;x<136;++x)for(int shift=0;shift<32;shift+=8){
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference)difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum)maximum=difference;
        if(difference>3){snprintf(message,sizeof(message),"SHIELD MASK GPU vector=%d expected=%08x actual=%08x difference=%u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-shield-mask.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: shield native mask: 176 GPU vectors, independent UV/alpha/color, all blends, linear/nearest mask, angular/animation fade and alpha test; max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(active)n3ds_gpu_present();if(proof)fclose(proof);
    if(!n3ds_gpu_texture_barrier())abort();for(int i=0;i<2;++i)n3ds_gpu_texture_destroy(textures[i]);linearFree(pixels);return result;
}

static int generic_lit_lerp_gpu_tests(void)
{
    const unsigned int palette[2][4]={{0x2b597900,0xcd1b5755,0x29cb41aa,0x89a3c5ff},
        {0x436b00c3,0x8fc2802a,0x2766ff96,0x555555e1}};
    const unsigned short indices[6]={0,1,2,0,2,3};
    const float xy[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],rows[27][4],maximum=0;
    void *textures[2]={0};int active=0,weapon=0,result=1;char message[224];FILE *proof=NULL;
#undef CHECK
#define CHECK(e) do { if(!(e)) {snprintf(message,sizeof(message),"GENERIC LIT LERP GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;} } while(0)
    CHECK(pixels);
    for(int t=0;t<2;++t) {
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            unsigned int morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
            texels[morton]=palette[t][(x>=4)+2*(y<4)];
        }
        textures[t]=n3ds_gpu_texture_create(texels,8,8);CHECK(textures[t]);
    }
    for(int test=0;test<27;++test) {
        int q[2]={test>=25?0:test%4,test>=25?0:test==24?2:(test/4)%4};float light[4];
        for(int c=0;c<3;++c) light[c]=test==22?0:((64+c*37+test*17)%256)/255.f;
        light[3]=test<16?1:128/255.f;
        struct native_chicago_material m={.maps=2,.blend=0,.two_sided=1,.fade=test<18?1:153/255.f,.generic_lit_lerp=1};
        m.alpha_test=test==20 || test==21 || test==24;
        if(test>=25) m.point[test-25]=1;
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        if(test==23 || test==24) {CHECK(n3ds_gpu_first_person_begin(.02f,100));weapon=1;}
        u32 params[2];
        for(int t=0;t<2;++t) {params[t]=((C3D_Tex *)textures[t])->param;CHECK(n3ds_gpu_texture_bind(t,textures[t]));}
        struct native_chicago_vertex *v=n3ds_gpu_chicago_allocate(4);CHECK(v);
        for(int i=0;i<4;++i) {
            memset(v+i,0,sizeof(*v));v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[2]=-1;
            memcpy(v[i].color,light,sizeof(light));
            for(int t=0;t<2;++t) {v[i].uv[t][0]=(q[t]&1)?.8125f:.1875f;v[i].uv[t][1]=(q[t]&2)?.8125f:.1875f;
                if(test>=25 && t==test-25) v[i].uv[t][0]=.49f;}
        }
        CHECK(n3ds_gpu_chicago_draw(&m,v,4,indices,6));
        for(int t=0;t<2;++t) CHECK(((C3D_Tex *)textures[t])->param==params[t]);
        if(weapon) {n3ds_gpu_first_person_end();weapon=0;CHECK(opaque_quad(-2));}
        n3ds_gpu_present();active=0;CHECK(n3ds_gpu_readback(pixels,240*400));
        unsigned int a=palette[0][q[0]],b=palette[1][q[1]],expected=0xab;
        float weight=(a&255)/255.f,alpha=(1-((b>>8)&255)/255.f)*light[3]*m.fade;
        for(int c=0;c<3;++c) {
            float rgb=(((a>>(24-c*8))&255)*weight+((b>>(24-c*8))&255)*(1-weight))*light[c];
            float out=rgb*alpha+(32+c*32)*(1-alpha);
            if(m.alpha_test && alpha<=127.f/255.f) out=32+c*32;
            expected|=(unsigned int)floorf(out+.5f)<<(24-c*8);
        }
        /* Visible weapon fragments block late world geometry; alpha-killed ones do not. */
        if(test==24) expected=0x4080c0ff;
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>3) {snprintf(message,sizeof(message),"GENERIC LIT LERP GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
    }
    proof=fopen("sdmc:/halo-native-generic-lit-lerp.bin","wb");CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: generic native lit lerp: 27 GPU vectors, independent UVs, nearest/linear sampling, inverse blue alpha, lighting/fog/fade, alpha test and weapon stencil; max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(weapon) n3ds_gpu_first_person_end();if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();
    for(int i=0;i<2;++i) n3ds_gpu_texture_destroy(textures[i]);linearFree(pixels);return result;
}


static int shield_scalar_bake_tests(void)
{
    struct native_generic_material m={.maps=4,.stages=7};
 struct native_generic_stage stages[7]={{4, {23,1,17,0,23,0,15,0,0,0,0,0,1,0,23,1,2,0,23,0,6,0,0,0,1,0}, {{0}}},{0, {18,0,2,0,18,1,11,0,0,0,0,0,1,0,8,0,2,0,8,1,11,0,0,0,1,0}, {{0}}},{0, {11,0,1,0,21,5,1,0,0,0,0,0,1,0,11,0,1,0,21,5,1,0,0,0,1,0}, {{0}}},{6, {23,1,13,0,23,0,14,0,0,0,0,0,4,0,11,0,11,0,21,0,21,0,0,0,1,3}, {{0}}},{2, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,11,2,11,2,0,0,1,0}, {{0}}},{0, {18,1,1,0,0,0,0,0,2,0,0,0,0,0,11,0,1,0,11,0,11,1,0,0,1,0}, {{0}}},{0, {21,0,8,0,10,0,12,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0}, {{0}}}};memcpy(m.stage,stages,sizeof(stages));
    unsigned int texels[64];void *textures[4]={0};int active=0,result=1;char message[192];
    struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0xff};
    const float bounds[4]={-.3f,-.4f,1.3f,1.4f};
#undef CHECK
#define CHECK(e) do{if(!(e)){snprintf(message,sizeof(message),"SHIELD SCALAR BAKE FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done;}}while(0)
    CHECK(n3ds_gpu_texture_barrier());n3ds_gpu_chicago_map_release();
    for(unsigned int t=0;t<4;++t) {
        for(unsigned int i=0;i<64;++i)texels[i]=0x7f392100|((i*37+t*59)&255);
        textures[t]=n3ds_gpu_texture_create(texels,8,8);CHECK(textures[t] && n3ds_gpu_texture_publish_immutable(textures[t]));
        m.textures[t]=textures[t];
        m.transform[t][0][0]=1+.3f*t;m.transform[t][0][1]=-.2f*t;m.transform[t][0][3]=.1f*t;
        m.transform[t][1][0]=.25f*t;m.transform[t][1][1]=.8f;m.transform[t][1][3]=-.15f*t;
    }
    m.stage[0].constant[0][3]=.73f;m.stage[0].constant[0][2]=.41f;
    m.stage[3].constant[0][0]=.8f;m.stage[3].constant[0][1]=.6f;m.stage[3].constant[0][2]=.4f;
    for(int test=0;test<16;++test) {
        for(int t=0;t<4;++t){m.point[t]=(test>>t)&1;m.clamp_u[t]=(test+t)&1;m.clamp_v[t]=((test+t)>>1)&1;}
        CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        C3D_Tex *scalar=n3ds_gpu_shield_bake(&m,bounds),*full=n3ds_gpu_generic_bake(&m,bounds);
        CHECK(scalar && full && scalar!=full);
        CHECK(n3ds_gpu_shield_bake(&m,bounds)==scalar && n3ds_gpu_generic_bake(&m,bounds)==full);
        for(unsigned int y=0;y<8;++y)for(unsigned int x=0;x<8;++x){
            float u=bounds[0]+(bounds[2]-bounds[0])*((x/2)+.5f)/4;
            float v=bounds[1]+(bounds[3]-bounds[1])*((y/2)+.5f)/4;
            float samples[4][4],expected[4];
            for(int t=0;t<4;++t)n3ds_gpu_generic_sample(&m,t,u,v,samples[t]);
            n3ds_generic_evaluate(&m,samples,expected);
            unsigned int ty=7-y,index=(x&1)|((ty&1)<<1)|((x&2)<<1)|((ty&2)<<2)|((x&4)<<2)|((ty&4)<<3);
            unsigned int actual=((unsigned int*)scalar->data)[index];
            CHECK(!(actual&0xffffff00));
            CHECK(abs((int)(actual&255)-(int)(expected[3]*255.f+.5f))<=1);
            CHECK(abs((int)(actual&255)-(int)(((unsigned int*)full->data)[index]&255))<=1);
        }
        n3ds_gpu_present();active=0;
    }
    n3ds_log("PASS: shield scalar bake: 1024 texels versus original interpreter, mixed filters/clamps/rotated UVs, separate RGBA/scalar cache identities and draw reuse");result=0;
done:
    if(active)n3ds_gpu_present();if(!n3ds_gpu_texture_barrier())abort();n3ds_gpu_chicago_map_release();
    for(int t=0;t<4;++t)n3ds_gpu_texture_destroy(textures[t]);return result;
}
