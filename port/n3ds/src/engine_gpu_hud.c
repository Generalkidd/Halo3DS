#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine_hud.h"
#include "engine_generic.h"
#include "engine_textures.h"
#include "engine_renderer.h"
/* Native radar restores the original64px target. CPU allocation fallback
 * retains the prior32px New/16px original sampling budget. */
enum { SLOTS=24, SENSOR=32, SENSOR_GPU=64 };
static C3D_Tex textures[SLOTS];
static unsigned int count,used;
/* Exact content keys: resource addresses can be reused by the map cache.
 * Slot lifetime follows the waited FrameBegin; sensor writes invalidate it. */
static struct meter_cache_entry {
    void *pixels;
    unsigned int bytes;
    unsigned int source_id;
    int valid;
    struct native_hud_meter packet;
} meter_cache[SLOTS];
static unsigned int cache_hits,cache_misses;
static unsigned int identity_hits,content_comparisons;
static unsigned int texel_evaluations,texel_reuses,texel_kills;
long long n3ds_engine_ticks(void);
void n3ds_log(const char *);
static long long hud_ticks[4];
void n3ds_gpu_hud_profile_reset(void) { memset(hud_ticks,0,sizeof(hud_ticks)); }
void n3ds_gpu_hud_profile_add(unsigned int stage,long long ticks) { if(stage<4) hud_ticks[stage]+=ticks; }
void n3ds_gpu_hud_profile_read(long long ticks[4]) { memcpy(ticks,hud_ticks,sizeof(hud_ticks)); }
static int sensor_size=SENSOR;
static struct {C3D_Tex *texture;C3D_RenderTarget *target;int failed;} sensor_gpu[3];
static int sensor_native;
static unsigned int sensor_native_frames,sensor_native_blips,sensor_cpu_frames;
static void sensor_gpu_dispose(void);
static int sensor_native_tests(void);

static float sensor[SENSOR][SENSOR][3];
static struct {
    void *source;
    unsigned int bytes,width,height,hits,misses;
    int sample_size;
    int valid;
    float alpha[SENSOR][SENSOR];
} sensor_mask;
static float clamp(float x) { return x<0?0:x>1?1:x; }
static unsigned int tile(unsigned int x,unsigned int y,unsigned int width)
{
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    return ((y/8)*(width/8)+x/8)*64+spread[x&7]+2*spread[y&7];
}
static unsigned int pack(const float rgba[4])
{
    unsigned int p=0;
    for(int c=0;c<4;++c) p|=(unsigned int)(clamp(rgba[c])*255.f+.5f)<<(24-c*8);
    return p;
}
static void put(C3D_Tex *t,int x,int y,const float rgba[4])
{
    ((unsigned int *)t->data)[tile(x,t->height-1-y,t->width)]=pack(rgba);
}
static float signed_clamp(float x) { return x<-1?-1:x>1?1:x; }
static void meter_table(const float constants[5][4],int negative,float table[256][4])
{
    /* Specialize the original five HUD packets. Stage zero's red/green
     * scratch values are overwritten before use; only blue and base alpha
     * reach the result. Preserve stage clamps, operation order and the mux
     * comparison (including rounding at its boundary). */
    for(int i=0;i<256;++i) {
        float b=i/255.f,k=32.f/255.f;
        float difference=signed_clamp((constants[0][3]+ -b)*4);
        float gradient=clamp(signed_clamp((k*b+k*b)*4));
        float flash=clamp(signed_clamp(1-2*clamp(difference)));
        int background=signed_clamp(b+(.5f-constants[2][3]))>=.5f;
        for(int c=0;c<3;++c) {
            float blend=signed_clamp((1-gradient)*constants[0][c]+gradient*constants[1][c]);
            float color=signed_clamp(clamp(blend)+flash*(negative?-constants[2][c]:constants[2][c]));
            table[i][c]=clamp(background?constants[3][c]:color);
        }
        table[i][3]=clamp(background?constants[3][3]:constants[4][3]);
    }
}
static void bake_meter_pixels(const float constants[5][4],int negative,
    const unsigned int *source,unsigned int *destination,unsigned int count)
{
    float table[256][4];meter_table(constants,negative,table);
    unsigned int keys[1024],values[1024];unsigned char valid[1024]={0};
    /* Both textures have identical tiled layouts. The fixed HUD combiner only
     * consumes source blue and alpha; red/green are overwritten. Full blue/
     * alpha keys still check hash collisions and retain exact packed output. */
    for(unsigned int i=0;i<count;++i) {
        unsigned int p=source[i]&0xffffu,bucket=(p*2654435761u)>>22;
        if(!(p&255)) {destination[i]=0x000000ff;++texel_kills;continue;}
        if(valid[bucket] && keys[bucket]==p) {destination[i]=values[bucket];++texel_reuses;continue;}
        float rgba[4],alpha=(p&255)/255.f;const float *entry=table[(p>>8)&255];
        for(int c=0;c<3;++c) rgba[c]=entry[c]*alpha;
        rgba[3]=entry[3];
        destination[i]=values[bucket]=pack(rgba);keys[bucket]=p;valid[bucket]=1;++texel_evaluations;
    }
}
static int meter_texel_tests(void)
{
    struct native_generic_material program={0};
    const float constants[5][4]={{.2f,.8f,.4f,.6f},{.9f,.3f,.7f,.2f},{.5f,.7f,.1f,.8f},{.1f,.2f,.3f,.4f},{.8f,.6f,.4f,.2f}};
    unsigned int source[1024],actual[1024],state=0x12345678;
    for(int i=0;i<1024;++i) {
        state=state*1664525u+1013904223u;
        source[i]=i%5==0?(state&0xffffff00):i%3==0?0x12345678:state;
    }
    for(int negative=0;negative<2;++negative) {
        n3ds_hud_meter_program(&program,constants,negative);
        if(!n3ds_generic_validate(&program)) return 0;
        bake_meter_pixels(constants,negative,source,actual,1024);
        for(int i=0;i<1024;++i) {
            float samples[4][4]={{0}},rgba[4];
            for(int c=0;c<4;++c) samples[0][c]=((source[i]>>(24-c*8))&255)/255.f;
            n3ds_generic_evaluate(&program,samples,rgba);
            if(!samples[0][3]) {rgba[0]=rgba[1]=rgba[2]=0;rgba[3]=1;}
            if(actual[i]!=pack(rgba)) return 0;
        }
    }
    /* Red/green vary on every pixel while the shader-relevant channels stay
     * identical. Require one evaluation and 1023 reuses, and compare every
     * result with the retained original five-stage interpreter. */
    for(int negative=0;negative<2;++negative) {
        unsigned int before_evaluations=texel_evaluations,before_reuses=texel_reuses;
        for(int i=0;i<1024;++i) {
            state=state*1664525u+1013904223u;source[i]=(state&0xffff0000u)|0x739du;
        }
        n3ds_hud_meter_program(&program,constants,negative);
        bake_meter_pixels(constants,negative,source,actual,1024);
        if(texel_evaluations-before_evaluations!=1 || texel_reuses-before_reuses!=1023) return 0;
        for(int i=0;i<1024;++i) {
            float samples[4][4]={{0}},rgba[4];
            for(int c=0;c<4;++c) samples[0][c]=((source[i]>>(24-c*8))&255)/255.f;
            n3ds_generic_evaluate(&program,samples,rgba);
            if(actual[i]!=pack(rgba)) return 0;
        }
    }
    /* Independent interpreter oracle: all 256 blue levels, both flash
     * signs, varied alpha and 16 sets of byte-valued runtime constants. */
    for(int set=0;set<16;++set) for(int negative=0;negative<2;++negative) {
        float varied[5][4];
        for(int j=0;j<5;++j) for(int c=0;c<4;++c) {
            state=state*1664525u+1013904223u;
            varied[j][c]=(set==0?0:set==1?255:(state>>24))/255.f;
        }
        for(int i=0;i<256;++i) {
            state=state*1664525u+1013904223u;
            source[i]=(state&0xffff00ff)|((unsigned int)i<<8);
        }
        n3ds_hud_meter_program(&program,varied,negative);
        bake_meter_pixels(varied,negative,source,actual,256);
        for(int i=0;i<256;++i) {
            float samples[4][4]={{0}},rgba[4];
            for(int c=0;c<4;++c) samples[0][c]=((source[i]>>(24-c*8))&255)/255.f;
            n3ds_generic_evaluate(&program,samples,rgba);
            if(!samples[0][3]) {rgba[0]=rgba[1]=rgba[2]=0;rgba[3]=1;}
            if(actual[i]!=pack(rgba)) return 0;
        }
    }
    extern void n3ds_log(const char *);
    n3ds_log("PASS: HUD fixed-packet table vs interpreter: 8192 varied CPU pixels");
    n3ds_log("PASS: HUD blue-alpha keys: 2048 original-interpreter comparisons, two evaluations and 2046 red-green reuses");
    texel_evaluations=texel_reuses=texel_kills=0;
    return 1;
}
/* RGBA8 resources are PICA tiled and vertically inverted. Border sampling is
 * required by the original expanding motion sweep; clamp would leave trails. */
static void sample(const C3D_Tex *t,float u,float v,float border_alpha,float out[4])
{
    float x=u*t->width-.5f,y=v*t->height-.5f;
    int ix=(int)floorf(x),iy=(int)floorf(y);float fx=x-ix,fy=y-iy;
    memset(out,0,4*sizeof(float));
    for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
        int tx=ix+i,ty=iy+j;float w=(i?fx:1-fx)*(j?fy:1-fy);
        if(tx<0 || tx>=t->width || ty<0 || ty>=t->height) { out[3]+=border_alpha*w;continue; }
        unsigned int p=((unsigned int *)t->data)[tile(tx,t->height-1-ty,t->width)];
        for(int c=0;c<4;++c) out[c]+=((p>>(24-c*8))&255)*(w/255.f);
    }
}
int n3ds_gpu_hud_icon_bounds(void *texture,float bounds[4])
{
    const C3D_Tex *t=texture;if(!t || !t->data) return 0;
    static struct {unsigned int id;float input[4],output[4];int valid;} cache[4];
    static unsigned int next;
    unsigned int id=n3ds_gpu_texture_content_id(texture);
    for(int i=0;i<4;++i) if(id && cache[i].id==id && !memcmp(cache[i].input,bounds,sizeof(cache[i].input))) {
        memcpy(bounds,cache[i].output,sizeof(cache[i].output));return cache[i].valid;
    }
    float input[4];memcpy(input,bounds,sizeof(input));
    int x0=t->width,y0=t->height,x1=-1,y1=-1;
    for(int y=0;y<t->height;++y) for(int x=0;x<t->width;++x) {
        float u=(x+.5f)/t->width,v=(y+.5f)/t->height;
        if(u<bounds[0] || v<bounds[1] || u>=bounds[2] || v>=bounds[3]) continue;
        unsigned int p=((const unsigned int *)t->data)[tile(x,t->height-1-y,t->width)];
        if((p&255)<=16) continue;
        if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;
    }
    int valid=x1>=x0 && y1>=y0;
    if(valid) {bounds[0]=(float)x0/t->width;bounds[1]=(float)y0/t->height;
        bounds[2]=(float)(x1+1)/t->width;bounds[3]=(float)(y1+1)/t->height;}
    if(id) {unsigned int i=next++%4;cache[i].id=id;cache[i].valid=valid;
        memcpy(cache[i].input,input,sizeof(input));memcpy(cache[i].output,bounds,sizeof(input));}
    return valid;
}

static const float *sensor_mask_samples(const C3D_Tex *t)
{
    /* The mask coordinates never depend on the animated sweep. Compare exact
     * source bytes, including same-address mutations and changed dimensions.
     * Oversized textures/allocation failure use the original scalar path. */
    if(!t || !t->data || !t->width || !t->height || t->width>128 || t->height>128) return NULL;
    unsigned int bytes=t->width*t->height*4;
    if(sensor_mask.valid && sensor_mask.sample_size==sensor_size && sensor_mask.width==t->width && sensor_mask.height==t->height && !memcmp(sensor_mask.source,t->data,bytes)) {
        ++sensor_mask.hits;return &sensor_mask.alpha[0][0];
    }
    ++sensor_mask.misses;sensor_mask.valid=0;
    if(sensor_mask.bytes!=bytes) {
        free(sensor_mask.source);sensor_mask.source=malloc(bytes);
        sensor_mask.bytes=sensor_mask.source?bytes:0;
    }
    if(!sensor_mask.source) return NULL;
    const float inverse_size=1.f/sensor_size;
    for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
        float qx=(2.f*(x+.5f)*inverse_size-1+1.015625f)/2.0625f;
        float qy=(1.046875f-(1-2.f*(y+.5f)*inverse_size))/2.0625f,m[4];
        sample(t,1-qx,qy,70.f/255.f,m);sensor_mask.alpha[y][x]=m[3];
    }
    memcpy(sensor_mask.source,t->data,bytes);
    sensor_mask.sample_size=sensor_size;sensor_mask.width=t->width;sensor_mask.height=t->height;sensor_mask.valid=1;
    return &sensor_mask.alpha[0][0];
}
static int sensor_mask_tests(void)
{
    /* Tests also run after the native-radar fallback vectors, which populate
     * this cache. Each vector suite needs its own initial counter state. */
    free(sensor_mask.source);memset(&sensor_mask,0,sizeof(sensor_mask));
    unsigned int pixels[128],copy[128],state=0x5319;
    C3D_Tex t={0};t.data=pixels;t.width=t.height=8;
    for(int i=0;i<128;++i) {state=state*1664525u+1013904223u;pixels[i]=state;}
    for(int test=0;test<5;++test) {
        if(test==2) pixels[0]^=255;
        if(test==3) t.height=16;
        if(test==4) {t.width=16;t.height=8;}
        const float *cached=sensor_mask_samples(&t);if(!cached) return 0;
        const float inverse_size=1.f/sensor_size;
    for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
            float qx=(2.f*(x+.5f)*inverse_size-1+1.015625f)/2.0625f;
            float qy=(1.046875f-(1-2.f*(y+.5f)*inverse_size))/2.0625f,m[4];
            sample(&t,1-qx,qy,70.f/255.f,m);
            if(memcmp(cached+y*SENSOR+x,m+3,sizeof(float))) return 0;
        }
        unsigned int hits=sensor_mask.hits;
        if(sensor_mask_samples(&t)!=cached || sensor_mask.hits!=hits+1) return 0;
    }
    memcpy(copy,pixels,sizeof(copy));t.data=copy;
    if(!sensor_mask_samples(&t) || sensor_mask.hits!=7 || sensor_mask.misses!=4) return 0;
    t.width=256;if(sensor_mask_samples(&t)) return 0;
    free(sensor_mask.source);memset(&sensor_mask,0,sizeof(sensor_mask));
    extern void n3ds_log(const char *);
    n3ds_log("PASS: sensor mask cache: 1280 scalar samples, source mutation, dimensions, content identity, hits and oversize fallback");
    return 1;
}
static C3D_Tex *acquire(unsigned int width,unsigned int height)
{
    if(used>=SLOTS) {n3ds_log("HUD ALLOCATION: exhausted per-frame slots");return NULL;}
    C3D_Tex *t=&textures[used];
    if(used<count && (t->width!=width || t->height!=height)) {
        C3D_TexDelete(t);memset(t,0,sizeof(*t));
        meter_cache[used].valid=0;
    }
    if(!t->data && !C3D_TexInit(t,width,height,GPU_RGBA8)) {
        char message[160];snprintf(message,sizeof(message),"HUD ALLOCATION: slot=%u width=%u height=%u linear_free=%u",used,width,height,(unsigned int)linearSpaceFree());
        n3ds_log(message);return NULL;
    }
    ++used;if(count<used) count=used;
    return t;
}
void n3ds_gpu_hud_frame_begin(void) { used=0; }
static float quad_value(float a,float b,float c,float d,float u,float v)
{ return (a+(b-a)*u)*(1-v)+(d+(c-d)*u)*v; }
static int plasma_wrap(int x,int size,int wrap)
{ return wrap?((x%size)+size)%size:(x<0?0:x>=size?size-1:x); }
static void plasma_sample(const C3D_Tex *t,float u,float v,int point,int wrap,float out[4])
{
    /* Reduce wrapped coordinates first to keep long-running animation away
     * from integer overflow; clamp addressing retains edge texels. */
    u=wrap?u-floorf(u):clamp(u);v=wrap?v-floorf(v):clamp(v);
    float x=u*t->width-(point?0:.5f),y=v*t->height-(point?0:.5f);
    int ix=(int)floorf(x),iy=(int)floorf(y);float fx=x-ix,fy=y-iy;
    memset(out,0,4*sizeof(float));
    for(int j=0;j<(point?1:2);++j) for(int i=0;i<(point?1:2);++i) {
        int tx=plasma_wrap(ix+i,t->width,wrap),ty=plasma_wrap(iy+j,t->height,wrap);
        unsigned int p=((const unsigned int *)t->data)[tile(tx,t->height-1-ty,t->width)];
        float w=point?1:(i?fx:1-fx)*(j?fy:1-fy);
        for(int c=0;c<4;++c) out[c]+=((p>>(24-c*8))&255)*(w/255.f);
    }
}
int n3ds_gpu_screen_multiply(const struct native_menu_plasma *p,const struct native_text_vertex vertices[4],float width,float height)
{
    if(!p || !p->textures[0] || !p->textures[1] || p->textures[2]) return 0;
    C3D_Tex *a=p->textures[0],*b=p->textures[1];
    unsigned int sx=8,sy=8;
    while(sx<64 && (sx<a->width || sx<b->width)) sx*=2;
    while(sy<64 && (sy<a->height || sy<b->height)) sy*=2;
    unsigned int slot=used;C3D_Tex *t=acquire(sx,sy);if(!t) return 0;
    meter_cache[slot].valid=0;
    for(unsigned int y=0;y<sy;++y) for(unsigned int x=0;x<sx;++x) {
        float u=(x+.5f)/sx,v=(y+.5f)/sy,samples[2][4];unsigned int pixel=0;
        for(int m=0;m<2;++m) {
            float coords[2];
            for(int c=0;c<2;++c) coords[c]=quad_value(p->uv[m][0][c],p->uv[m][1][c],p->uv[m][2][c],p->uv[m][3][c],u,v);
            if(!isfinite(coords[0]) || !isfinite(coords[1])) return 0;
            plasma_sample(p->textures[m],coords[0],coords[1],p->point,p->wrap[m],samples[m]);
        }
        /* Original screen packets: map0*tint0*vertex, then map1*tint1.
         * Preserve independent animated UVs, tint/fade and framebuffer blend. */
        for(int c=0;c<4;++c) {
            float color=quad_value(vertices[0].color[c],vertices[1].color[c],vertices[2].color[c],vertices[3].color[c],u,v);
            float value=samples[0][c]*p->tint[0][c]*color*samples[1][c]*p->tint[1][c];
            pixel|=(unsigned int)(clamp(value)*255.f+.5f)<<(24-c*8);
        }
        ((unsigned int *)t->data)[tile(x,sy-1-y,sx)]=pixel;
    }
    C3D_TexFlush(t);void *saved=n3ds_gpu_texture_bound(0);
    if(!n3ds_gpu_text_begin(width,height,0xffffffff,0,0) || !n3ds_gpu_texture_bind(0,t) || !n3ds_gpu_framebuffer_blend(p->blend)) {n3ds_gpu_texture_bind(0,saved);return 0;}
    struct native_text_vertex q[4];memcpy(q,vertices,sizeof(q));
    static const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;++i) {memcpy(q[i].uv,uv[i],sizeof(uv[i]));for(int c=0;c<4;++c) q[i].color[c]=1;}
    int ok=n3ds_gpu_text_draw(q);n3ds_gpu_texture_bind(0,saved);
    static int reported;if(!reported) {extern void n3ds_log(const char *);n3ds_log("NATIVE SCREEN MULTIPLY: two animated maps, bounded 64x64 output, tint and vertex alpha");reported=1;}
    return ok;
}
int n3ds_gpu_menu_plasma(const struct native_menu_plasma *p,const struct native_text_vertex vertices[4],float width,float height)
{
    static int tested;
    if(!tested) {
        if(!n3ds_menu_plasma_tests()) return 0;
        extern void n3ds_log(const char *);
        n3ds_log("PASS: menu plasma: 2048 varied texels match original seven-stage combiner packets");tested=1;
    }
    float extent_x=vertices[1].position[0]-vertices[0].position[0];
    float extent_y=vertices[3].position[1]-vertices[0].position[1];
    if(extent_x<=0 || extent_y<=0) return 1;
    for(int i=0;i<3;++i) if(!p->textures[i]) return 0;
    /* Keep base artwork at its loaded resolution. Only the animated glow is
     * sampled on a coarse screen-space grid; PICA interpolates its intensity
     * and composites the base/tint/fade per fragment. This replaces the CPU
     * output-texture bake, including its second reduction of the title art. */
    unsigned int nx=(unsigned int)ceilf(extent_x*400.f/width/48.f);
    unsigned int ny=(unsigned int)ceilf(extent_y*240.f/height/24.f);
    if(nx<1) nx=1; if(nx>8) nx=8;
    if(ny<1) ny=1; if(ny>4) ny=4;
    struct native_text_vertex grid[5][9];
    for(unsigned int y=0;y<=ny;++y) for(unsigned int x=0;x<=nx;++x) {
        float u=(float)x/nx,v=(float)y/ny,samples[2][4];
        struct native_text_vertex *g=&grid[y][x];
        for(int m=0;m<2;++m) {
            float coords[2];
            for(int a=0;a<2;++a) coords[a]=quad_value(p->uv[m][0][a],p->uv[m][1][a],p->uv[m][2][a],p->uv[m][3][a],u,v);
            plasma_sample(p->textures[m],coords[0],coords[1],p->point,p->wrap[m],samples[m]);
        }
        for(int a=0;a<2;++a) {
            g->position[a]=quad_value(vertices[0].position[a],vertices[1].position[a],vertices[2].position[a],vertices[3].position[a],u,v);
            g->uv[a]=quad_value(p->uv[2][0][a],p->uv[2][1][a],p->uv[2][2][a],p->uv[2][3][a],u,v);
        }
        for(int c=0;c<3;++c) g->color[c]=quad_value(vertices[0].color[c],vertices[1].color[c],vertices[2].color[c],vertices[3].color[c],u,v);
        /* Original plasma output alpha does not consume vertex alpha. */
        g->color[3]=n3ds_menu_plasma_intensity(samples,p->tint);
    }
    void *saved=n3ds_gpu_texture_bound(0);
    if(!n3ds_gpu_text_begin(width,height,0xffffffff,p->point,p->wrap[2]) ||
       !n3ds_gpu_texture_bind(0,p->textures[2]) || !n3ds_gpu_framebuffer_blend(p->blend)) {
        n3ds_gpu_texture_bind(0,saved);return 0;
    }
    unsigned int tint=0,fade=0;
    for(int c=0;c<4;++c) {
        tint|=(unsigned int)(clamp(p->tint[2][c])*255.f+.5f)<<(8*c);
        fade|=(unsigned int)(clamp(p->fade[c])*255.f+.5f)<<(8*c);
    }
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);C3D_TexEnvColor(env,tint);
    env=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_CONSTANT);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvSrc(env,C3D_Alpha,GPU_PREVIOUS,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
    env=C3D_GetTexEnv(2);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvColor(env,fade);
    int ok=1;
    for(unsigned int y=0;y<ny && ok;++y) for(unsigned int x=0;x<nx && ok;++x) {
        struct native_text_vertex quad[4]={grid[y][x],grid[y][x+1],grid[y+1][x+1],grid[y+1][x]};
        ok=n3ds_gpu_text_draw(quad);
    }
    n3ds_gpu_texture_bind(0,saved);return ok;
}
/* Compare the actual PICA final blend with the retained original plasma math.
 * Constant UV probes isolate combiner/alpha/texture orientation correctness;
 * the coarser spatial interpolation is a deliberate fidelity reduction. */
int n3ds_gpu_menu_plasma_tests(void)
{
    extern void n3ds_log(const char *);
    unsigned int *pixels=linearAlloc(240*400*4),texels[64],maximum=0;
    void *texture[3]={0};int result=1,active=0;char message[192];
    if(!pixels || !n3ds_menu_plasma_tests()) goto done;
    /* Isolate hardware clear/presentation/readback from shader execution. */
    n3ds_log("STARTUP: clear-only display probe begin");
    struct native_render_camera probe={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
    if(!n3ds_gpu_frame_begin()) goto done;
    active=1;n3ds_gpu_window_begin(&probe);
    n3ds_log("STARTUP: clear-only display probe present");
    n3ds_gpu_present();active=0;
    if(!n3ds_gpu_readback(pixels,240*400)) goto done;
    if(pixels[200*240+120]!=probe.clear_color) {
        snprintf(message,sizeof(message),"STARTUP FAIL: clear-only readback expected=%08x actual=%08x",probe.clear_color,pixels[200*240+120]);n3ds_log(message);goto done;
    }
    n3ds_log("STARTUP: clear-only display probe passed");
    for(int m=0;m<3;++m) {
        C3D_Tex temporary={0};temporary.data=texels;temporary.width=temporary.height=8;
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            float rgba[4];
            for(int c=0;c<4;++c) rgba[c]=((m*67+c*41+(x/4)*97+(y/4)*53+19)%256)/255.f;
            put(&temporary,x,y,rgba);
        }
        texture[m]=n3ds_gpu_texture_create(texels,8,8);if(!texture[m]) goto done;
    }
    for(int test=0;test<32;++test) {
        struct native_menu_plasma p={0};float samples[3][4],expected[4],color[4];
        p.blend=test%3==0?NATIVE_BLEND_ALPHA:test%3==1?NATIVE_BLEND_ADD:NATIVE_BLEND_PREMULTIPLIED_ALPHA;
        p.point=test&1;
        for(int m=0;m<3;++m) {
            p.textures[m]=texture[m];p.wrap[m]=(test>>2)&1;
            for(int c=0;c<4;++c) p.tint[m][c]=test>=12?1:((test*31+m*43+c*59+71)%256)/255.f;
            for(int i=0;i<4;++i) {
                p.uv[m][i][0]=((test&1)? .75f:.25f)+(p.wrap[m]?2.f:0);
                p.uv[m][i][1]=((test&2)? .75f:.25f)-(p.wrap[m]?1.f:0);
            }
            plasma_sample(texture[m],p.uv[m][0][0],p.uv[m][0][1],p.point,p.wrap[m],samples[m]);
        }
        for(int c=0;c<4;++c) {
            p.fade[c]=test>=12?1:((test*23+c*61+83)%256)/255.f;
            color[c]=test>=12?1:((test*17+c*29+131)%256)/255.f;
        }
        /* Include fully transparent and opaque base-alpha cases. */
        if(test==10) p.tint[2][3]=0;
        struct native_text_vertex v[4]={{{0,0},{0},{0}},{{400,0},{0},{0}},{{400,240},{0},{0}},{{0,240},{0},{0}}};
        for(int i=0;i<4;++i) memcpy(v[i].color,color,sizeof(color));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
        if(!test) n3ds_log("STARTUP: first GPU overlay frame begin");
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        if(!test) n3ds_log("STARTUP: first GPU overlay draw");
        if(test<16) {if(!n3ds_gpu_menu_plasma(&p,v,400,240)) goto done;}
        else {p.textures[2]=NULL;if(!n3ds_gpu_screen_multiply(&p,v,400,240)) goto done;}
        if(!test) n3ds_log("STARTUP: first GPU overlay present");
        n3ds_gpu_present();active=0;
        if(!test) n3ds_log("STARTUP: first GPU overlay readback");
        if(!n3ds_gpu_readback(pixels,240*400)) goto done;
        if(!test) n3ds_log("STARTUP: first GPU overlay readback complete");
        if(test<16) n3ds_menu_plasma_evaluate(samples,p.tint,p.fade,color,expected);
        else for(int c=0;c<4;++c) expected[c]=samples[0][c]*p.tint[0][c]*color[c]*samples[1][c]*p.tint[1][c];
        float source_factor=p.blend==NATIVE_BLEND_ALPHA?expected[3]:1;
        float destination_factor=p.blend==NATIVE_BLEND_ADD?1:1-expected[3];
        unsigned int packed=0xab;
        for(int c=0;c<3;++c) packed|=(unsigned int)(255*clamp(expected[c]*source_factor+(32+c*32)/255.f*destination_factor)+.5f)<<(24-c*8);
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((packed>>shift)&255));
            if(delta>maximum) maximum=delta;
            if(delta>4) {
                snprintf(message,sizeof(message),"MENU GPU MISMATCH: vector=%d expected=%08x actual=%08x difference=%u",test,packed,pixels[y*240+x],delta);n3ds_log(message);result=2;goto done;
            }
        }
    }
    snprintf(message,sizeof(message),"PASS: menu GPU composite: 16 plasma + 16 two-map multiply vectors, orientation/wrap, tint/fade, three blends and retained alpha, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();
    if(!n3ds_gpu_texture_barrier()) abort();
    for(int m=0;m<3;++m) n3ds_gpu_texture_destroy(texture[m]);
    linearFree(pixels);return result;
}

void n3ds_gpu_hud_dispose(void)
{
    extern void n3ds_log(const char *);char message[128];unsigned int bytes=0;
    for(unsigned int i=0;i<SLOTS;++i) {bytes+=meter_cache[i].bytes;free(meter_cache[i].pixels);}
    snprintf(message,sizeof(message),"HUD BAKE CACHE: hits=%u misses=%u source_bytes=%u",cache_hits,cache_misses,bytes);n3ds_log(message);
    snprintf(message,sizeof(message),"HUD TEXELS: evaluations=%u reused=%u alpha_killed=%u",texel_evaluations,texel_reuses,texel_kills);n3ds_log(message);
    snprintf(message,sizeof(message),"HUD SOURCE CHECKS: immutable_hits=%u mutable_comparisons=%u",identity_hits,content_comparisons);n3ds_log(message);
    identity_hits=content_comparisons=0;
    snprintf(message,sizeof(message),"SENSOR MASK CACHE: hits=%u misses=%u bytes=%u",sensor_mask.hits,sensor_mask.misses,sensor_mask.bytes);n3ds_log(message);
    sensor_gpu_dispose();
    free(sensor_mask.source);memset(&sensor_mask,0,sizeof(sensor_mask));
    texel_evaluations=texel_reuses=texel_kills=0;
    memset(meter_cache,0,sizeof(meter_cache));cache_hits=cache_misses=0;
    for(unsigned int i=0;i<count;++i) C3D_TexDelete(&textures[i]);
    memset(textures,0,sizeof(textures));memset(sensor,0,sizeof(sensor));used=count=0;
}
int n3ds_gpu_hud_meter(const struct native_hud_meter *m,const struct native_text_vertex v[4],float width,float height,int point,int wrapped)
{
    long long stamp=n3ds_engine_ticks();
    C3D_Tex *source=n3ds_gpu_texture_bound(0);
    if(!source || !m || !m->mode2 || m->gradient!=1) return 0;
    unsigned int slot=used;
    C3D_Tex *t=acquire(source->width,source->height);if(!t) return 0;
    struct meter_cache_entry *cache=&meter_cache[slot];
    struct native_hud_meter key=*m;key.pad[0]=key.pad[1]=0;
    unsigned int bytes=source->width*source->height*4;
    unsigned int source_id=n3ds_gpu_texture_content_id(source);
    int same_source=0;
    if(cache->valid && cache->bytes==bytes && !memcmp(&cache->packet,&key,sizeof(key))) {
        if(source_id) {same_source=cache->source_id==source_id;if(same_source) ++identity_hits;}
        else {++content_comparisons;same_source=!memcmp(cache->pixels,source->data,bytes);}
    }
    if(same_source) ++cache_hits;
    else {
    ++cache_misses;cache->valid=0;
    float constants[5][4];
    const unsigned int colors[5]={m->minimum,m->maximum,m->flash,m->background,m->tint};
    for(int i=0;i<5;++i) {
        for(int c=0;c<3;++c) constants[i][c]=((colors[i]>>(16-c*8))&255)/255.f;
        constants[i][3]=(colors[i]>>24)/255.f;
    }
    bake_meter_pixels(constants,m->negative,source->data,t->data,bytes/4);
    C3D_TexFlush(t);
    if(cache->bytes!=bytes) {
        free(cache->pixels);cache->pixels=malloc(bytes);cache->bytes=cache->pixels?bytes:0;
    }
    if(cache->pixels) {memcpy(cache->pixels,source->data,bytes);cache->packet=key;cache->source_id=source_id;cache->valid=1;}
    }
    n3ds_gpu_hud_profile_add(0,n3ds_engine_ticks()-stamp);
    if(!n3ds_gpu_text_begin(width,height,0xffffffff,point,wrapped) || !n3ds_gpu_texture_bind(0,t)) return 0;
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_TexEnvInit(C3D_GetTexEnv(1));
    unsigned int tint=((m->tint>>16)&255)|(m->tint&0xff00)|((m->tint&255)<<16)|(m->tint&0xff000000);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_CONSTANT_COLOR,GPU_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    C3D_BlendingColor(tint);
    int ok=n3ds_gpu_text_draw(v);n3ds_gpu_texture_bind(0,source);return ok;
}
/* Original Xbox target4: contacts add RGB, then sweep ONE/SRCALPHA,
 * then mask ZERO/SRCALPHA. Alpha stays cleared. No texture feedback/readback. */
static int sensor_gpu_begin(void)
{
    unsigned int slot=sensor_size==16?0:sensor_size==32?1:2;
    if(sensor_gpu[slot].failed) return 0;
    if(!sensor_gpu[slot].target) {
        C3D_Tex *t=calloc(1,sizeof(*t));
        if(!t || !C3D_TexInitVRAM(t,sensor_size,sensor_size,GPU_RGBA8)) {
            free(t);sensor_gpu[slot].failed=1;return 0;
        }
        C3D_RenderTarget *target=C3D_RenderTargetCreateFromTex(t,GPU_TEXFACE_2D,0,-1);
        if(!target) {C3D_TexDelete(t);free(t);sensor_gpu[slot].failed=1;return 0;}
        sensor_gpu[slot].texture=t;sensor_gpu[slot].target=target;

    }
    /* FrameEnd(GX_CMDLIST_FLUSH) flushes only its final command segment.
     * This earlier world/HUD segment needs its own flush request or hardware
     * can execute stale commands when the motion tracker becomes visible.
     * Queue order and deferred vertex/index flushes at present are unchanged;
     * FrameSplit enqueues work, and FrameEnd starts the queue. */
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    C3D_RenderTargetClear(sensor_gpu[slot].target,C3D_CLEAR_COLOR,0,0);
    C3D_FrameDrawOn(sensor_gpu[slot].target);
    if(!n3ds_gpu_text_offscreen_begin(sensor_size,sensor_size)) {n3ds_gpu_hud_screen_restore();return 0;}
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ZERO,GPU_ONE);
    return 1;
}
static int sensor_gpu_quad(C3D_Tex *texture,float x0,float y0,float x1,float y1,
    float u0,float v0,float u1,float v1,const float color[3],unsigned int border)
{
    const int corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
    struct native_text_vertex v[4];
    for(int i=0;i<4;++i) {
        int x=corners[i][0],y=corners[i][1];
        v[i].position[0]=x?x1:x0;v[i].position[1]=y?y1:y0;
        v[i].uv[0]=x?u1:u0;v[i].uv[1]=y?v1:v0;
        for(int c=0;c<3;++c) v[i].color[c]=color[c];v[i].color[3]=1;
    }
    unsigned int saved=texture->border;texture->border=border;
    int result=n3ds_gpu_texture_bind(0,texture) && n3ds_gpu_text_offscreen_draw(v);
    texture->border=saved;n3ds_gpu_texture_bind(0,texture);
    return result;
}
static int sensor_gpu_blip(C3D_Tex *texture,float px,float py,float size,const float color[3])
{
    float cx=px*-.03125f,cy=py*-.03125f,r=size*.0625f;
    ++sensor_native_blips;
    return sensor_gpu_quad(texture,(cx-r+1)*.5f*sensor_size,(1-cy-r)*.5f*sensor_size,
        (cx+r+1)*.5f*sensor_size,(1-cy+r)*.5f*sensor_size,0,0,1,1,color,0);
}
static C3D_Tex *sensor_gpu_end(C3D_Tex *sweep,C3D_Tex *mask,float scale)
{
    const float tint[3]={.4588f,.7294f,1},white[3]={1,1,1};
    float x0=-.0078125f*sensor_size,y0=-.0234375f*sensor_size;
    float x1=1.0234375f*sensor_size,y1=1.0078125f*sensor_size;
    if(!n3ds_gpu_text_offscreen_begin(sensor_size,sensor_size)) return NULL;
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    if(!sensor_gpu_quad(sweep,x0,y0,x1,y1,.5f+.5f*scale,.5f-.5f*scale,.5f-.5f*scale,.5f+.5f*scale,tint,0x46000000)) return NULL;
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ZERO,GPU_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    if(!sensor_gpu_quad(mask,x0,y0,x1,y1,1,0,0,1,white,0x46000000)) return NULL;
    ++sensor_native_frames;
    n3ds_gpu_hud_screen_restore();
    return sensor_gpu[sensor_size==16?0:sensor_size==32?1:2].texture;
}
static void sensor_gpu_dispose(void)
{
    extern void n3ds_log(const char *);char message[160];
    snprintf(message,sizeof(message),"NATIVE RADAR: frames=%u contacts=%u cpu_fallback_frames=%u",sensor_native_frames,sensor_native_blips,sensor_cpu_frames);n3ds_log(message);
    for(int i=0;i<3;++i) {
        if(sensor_gpu[i].target) C3D_RenderTargetDelete(sensor_gpu[i].target);
        n3ds_gpu_texture_destroy(sensor_gpu[i].texture);
    }
    memset(sensor_gpu,0,sizeof(sensor_gpu));sensor_native=0;
    sensor_native_frames=sensor_native_blips=sensor_cpu_frames=0;
}
void n3ds_gpu_sensor_begin(void)
{
    extern int n3ds_input_platform_original_model(void);
    sensor_size=SENSOR_GPU;
    sensor_native=n3ds_gpu_frame_active() && sensor_gpu_begin();
    if(!sensor_native) sensor_size=n3ds_input_platform_original_model()?16:SENSOR;
    /* Xbox clears target4 on every begin. Contact history/fades are owned by
     * interface/motion_sensor.c, not by persistent render-target contents. */
    if(!sensor_native) memset(sensor,0,sizeof(sensor));
}
/* Factor the blip's separable coordinate calculation into two short scans.
 * Previously every contact scanned the whole tracker, including all pixels
 * outside its rectangle. Keep identical float operations for surviving pixels
 * and preserve contact order/saturation so overlapping blips look identical. */
static void sensor_blip_add(const C3D_Tex *texture,float px,float py,float size,const float color[3])
{
    float cx=px*-.03125f,cy=py*-.03125f,r=size*.0625f;
    const float inverse_size=1.f/sensor_size;
    float us[SENSOR],vs[SENSOR];int xs[SENSOR],ys[SENSOR],nx=0,ny=0;
    for(int x=0;x<sensor_size;++x) {
        float u=((2.f*(x+.5f)*inverse_size-1)-cx+r)/(2*r);
        if(u>=0 && u<=1) {xs[nx]=x;us[nx++]=u;}
    }
    for(int y=0;y<sensor_size;++y) {
        float v=(cy+r-(1-2.f*(y+.5f)*inverse_size))/(2*r);
        if(v>=0 && v<=1) {ys[ny]=y;vs[ny++]=v;}
    }
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        float rgba[4];sample(texture,us[i],vs[j],0,rgba);
        for(int c=0;c<3;++c) sensor[ys[j]][xs[i]][c]=clamp(sensor[ys[j]][xs[i]][c]+rgba[c]*color[c]);
    }
}
/* Original full-grid calculation retained as an independent pixel oracle. */
static void sensor_blip_reference(const C3D_Tex *texture,float px,float py,float size,const float color[3],float output[SENSOR][SENSOR][3])
{
    float cx=px*-.03125f,cy=py*-.03125f,r=size*.0625f;
    const float inverse_size=1.f/sensor_size;
    for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
        float u=((2.f*(x+.5f)*inverse_size-1)-cx+r)/(2*r);
        float v=(cy+r-(1-2.f*(y+.5f)*inverse_size))/(2*r),rgba[4];
        if(u<0 || u>1 || v<0 || v>1) continue;
        sample(texture,u,v,0,rgba);
        for(int c=0;c<3;++c) output[y][x][c]=clamp(output[y][x][c]+rgba[c]*color[c]);
    }
}
static int sensor_blip_tests(void)
{
    static float expected[SENSOR][SENSOR][3];
    unsigned int pixels[128],state=0x73519;
    C3D_Tex t={0};t.data=pixels;t.width=8;t.height=16;
    for(int i=0;i<128;++i) {state=state*1664525u+1013904223u;pixels[i]=state;}
    memset(sensor,0,sizeof(sensor));memset(expected,0,sizeof(expected));
    for(int i=0;i<256;++i) {
        if(i%8==0) {memset(sensor,0,sizeof(sensor));memset(expected,0,sizeof(expected));}
        state=state*1664525u+1013904223u;
        float x=(int)(state&255)-128,y=(int)((state>>8)&255)-128;
        float size=i%7==0?64.f:i%5==0?.001f:(1+(state>>16)%256)/16.f;
        const float color[3]={.17f,.43f,.79f};
        if(i%16==0) {x=0;y=0;size=8;}
        sensor_blip_reference(&t,x,y,size,color,expected);
        sensor_blip_add(&t,x,y,size,color);
        if(memcmp(sensor,expected,sizeof(sensor))) return 0;
    }
    /* Same 32-contact scene, same repetitions, guest CPU clock. Do not infer
     * hardware FPS from this bounded operation benchmark. */
    extern long long n3ds_engine_tick_frequency(void);
    extern void n3ds_log(const char *);
    const float color[3]={.1f,.2f,.3f};
    long long start=n3ds_engine_ticks();
    for(int repeat=0;repeat<8;++repeat) for(int i=0;i<32;++i)
        sensor_blip_reference(&t,(i%8)*8.f-28,(i/8)*8.f-12,2.f,color,expected);
    long long middle=n3ds_engine_ticks();
    for(int repeat=0;repeat<8;++repeat) for(int i=0;i<32;++i)
        sensor_blip_add(&t,(i%8)*8.f-28,(i/8)*8.f-12,2.f,color);
    long long end=n3ds_engine_ticks();char message[200];
    snprintf(message,sizeof(message),"RADAR BENCH: size=%d contacts=32 repeats=8 original_ms=%.3f bounded_ms=%.3f",sensor_size,
        (middle-start)*1000./n3ds_engine_tick_frequency(),(end-middle)*1000./n3ds_engine_tick_frequency());n3ds_log(message);
    memset(sensor,0,sizeof(sensor));
    n3ds_log("PASS: radar blip bounds: 256 exact full-grid comparisons including overlap, borders, offscreen and tiny/large blips");
    return 1;
}
int n3ds_gpu_sensor_profile_tests(void)
{
    sensor_size=16;if(!sensor_mask_tests() || !sensor_blip_tests()) return 0;
    sensor_size=SENSOR;if(!sensor_mask_tests() || !sensor_blip_tests()) return 0;
    n3ds_gpu_sensor_begin();return sensor_native_tests();
}
int n3ds_gpu_sensor_blip(void *texture,float px,float py,float size,const float color[3])
{
    long long stamp=n3ds_engine_ticks();
    if(!texture || !isfinite(px) || !isfinite(py) || !isfinite(size) || size<0) return 0;
    if(size==0) return 1;
    if(sensor_native) {if(!sensor_gpu_blip(texture,px,py,size,color)) return 0;}
    else sensor_blip_add(texture,px,py,size,color);
    n3ds_gpu_hud_profile_add(1,n3ds_engine_ticks()-stamp);
    return 1;
}
int n3ds_gpu_sensor_end(void *sweep,void *mask,float scale,float cx,float cy,float radius,float width,float height)
{
    long long stamp=n3ds_engine_ticks();
    if(!sweep || !mask || !isfinite(scale)) return 0;
    C3D_Tex *t;
    if(sensor_native) {t=sensor_gpu_end(sweep,mask,scale);if(!t) return 0;}
    else {
    ++sensor_cpu_frames;
    unsigned int slot=used;
    t=acquire(sensor_size,sensor_size);if(!t) return 0;
    meter_cache[slot].valid=0;
    const float tint[3]={.4588f,.7294f,1};
    const float *mask_alpha=sensor_mask_samples(mask);
    const float inverse_size=1.f/sensor_size;
    for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
        /* Original oversized clip-space quad, including its half-texel offset. */
        float qx=(2.f*(x+.5f)*inverse_size-1+1.015625f)/2.0625f;
        float qy=(1.046875f-(1-2.f*(y+.5f)*inverse_size))/2.0625f;
        float s[4],m[4],rgba[4]={0,0,0,0};
        sample(sweep,.5f+scale*(.5f-qx),.5f+scale*(qy-.5f),70.f/255.f,s);
        if(mask_alpha) m[3]=mask_alpha[y*SENSOR+x];
        else sample(mask,1-qx,qy,70.f/255.f,m);
        for(int c=0;c<3;++c) rgba[c]=sensor[y][x][c]=clamp(s[c]*tint[c]+sensor[y][x][c]*s[3])*m[3];
        put(t,x,y,rgba);
    }
    C3D_TexFlush(t);
    }
    n3ds_gpu_hud_profile_add(1,n3ds_engine_ticks()-stamp);
    if(!n3ds_gpu_text_begin(width,height,0xffffffff,0,0) || !n3ds_gpu_texture_bind(0,t)) return 0;
    /* Original surface RGB-only writes retain its cleared alpha=0, then
     * composite ONE/INVSRCALPHA. This adds its colored radar over the frame. */
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    struct native_text_vertex vertices[4];static const int corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;++i) {
        vertices[i].position[0]=cx+(2*corners[i][0]-1)*radius;
        vertices[i].position[1]=cy+(2*corners[i][1]-1)*radius;
        vertices[i].uv[0]=corners[i][0];vertices[i].uv[1]=corners[i][1];
        for(int c=0;c<4;++c) vertices[i].color[c]=1;
    }
    return n3ds_gpu_text_draw(vertices);
}
static float sensor_byte(float value) {return floorf(clamp(value)*255+.5f)/255.f;}
/* Independent CPU model of the original8-bit RGB framebuffer operations.
 * Readback is test-only; the live renderer never reads its radar target. */
static int sensor_native_tests(void)
{
    static float expected[SENSOR_GPU][SENSOR_GPU][3];
    unsigned int texels[64],*snapshot=linearAlloc(SENSOR_GPU*SENSOR_GPU*4),worst=0;
    C3D_Tex *blip=NULL,*sweep=NULL,*mask=NULL;int active=0,ok=0;
    for(int i=0;i<64;++i) texels[i]=0x804020ff;
    blip=n3ds_gpu_texture_create(texels,8,8);sweep=n3ds_gpu_texture_create(texels,8,8);mask=n3ds_gpu_texture_create(texels,8,8);
    if(!blip || !sweep || !mask || !snapshot) goto done;
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        float b[4]={(x+1)/16.f,(y+1)/16.f,.25f,1};put(blip,x,y,b);
        float w[4]={x/64.f,y/64.f,(x+y)/128.f,.3f+(x+y)/32.f};put(sweep,x,y,w);
        float m[4]={0,0,0,x==0 || y==0 || x==7 || y==7?.25f:.9f};put(mask,x,y,m);
    }
    C3D_TexFlush(blip);C3D_TexFlush(sweep);C3D_TexFlush(mask);
    for(int profile=0;profile<3;++profile) {
        sensor_size=16<<profile;memset(expected,0,sizeof(expected));
        for(int frame=0;frame<24;++frame) {
            memset(expected,0,sizeof(expected));
            struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
            if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
            if(!sensor_gpu_begin()) goto done;
            for(int contact=0;contact<(frame<16?3:0);++contact) {
                float px=contact==2?40:(frame%4)*8-12,py=contact*12-12,size=contact==2?12:8;
                float fade=1-(frame%16)/16.f;const float color[3]={.75f*fade,.5f*fade,fade};
                if(!sensor_gpu_blip(blip,px,py,size,color)) goto done;
                float cx=px*-.03125f,cy=py*-.03125f,r=size*.0625f;
                for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
                    float u=(2.f*(x+.5f)/sensor_size-1-cx+r)/(2*r),v=(cy+r-(1-2.f*(y+.5f)/sensor_size))/(2*r),rgba[4];
                    if(u<0 || u>1 || v<0 || v>1) continue;
                    sample(blip,u,v,0,rgba);
                    for(int c=0;c<3;++c) expected[y][x][c]=sensor_byte(expected[y][x][c]+sensor_byte(rgba[c]*color[c]));
                }
            }
            float scale=.5f+(frame%6)*.4f;
            C3D_Tex *out=sensor_gpu_end(sweep,mask,scale);if(!out) goto done;
            n3ds_gpu_present();active=0;if(!n3ds_gpu_texture_barrier()) goto done;
            memcpy(snapshot,out->data,out->size);
            const float tint[3]={.4588f,.7294f,1};
            unsigned int bad=0;
            for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {
                float qx=(2.f*(x+.5f)/sensor_size-1+1.015625f)/2.0625f;
                float qy=(1.046875f-(1-2.f*(y+.5f)/sensor_size))/2.0625f;
                float sw[4],ma[4];sample(sweep,.5f+scale*(.5f-qx),.5f+scale*(qy-.5f),70.f/255.f,sw);sample(mask,1-qx,qy,70.f/255.f,ma);
                unsigned int pixel=snapshot[tile(x,sensor_size-1-y,sensor_size)];
                if(pixel&255) ++bad;
                for(int c=0;c<3;++c) {
                    float value=sensor_byte(sensor_byte(sw[c]*tint[c])+expected[y][x][c]*sensor_byte(sw[3]));
                    expected[y][x][c]=sensor_byte(value*sensor_byte(ma[3]));
                    unsigned int expected_byte=(unsigned int)(expected[y][x][c]*255+.5f),actual=(pixel>>(24-c*8))&255;
                    unsigned int d=abs((int)expected_byte-(int)actual);if(d>worst) worst=d;if(d>3) ++bad;
                }
            }
            if(bad) {
                FILE *file=fopen("sdmc:/halo-radar-actual.rgba","wb");if(file) {fwrite(snapshot,4,sensor_size*sensor_size,file);fclose(file);}
                file=fopen("sdmc:/halo-radar-expected.rgba","wb");if(file) {for(int y=0;y<sensor_size;++y) for(int x=0;x<sensor_size;++x) {float rgba[4]={expected[y][x][0],expected[y][x][1],expected[y][x][2],0};unsigned int p=pack(rgba);fwrite(&p,4,1,file);}fclose(file);}
                char message[180];snprintf(message,sizeof(message),"FAIL: native radar profile=%d frame=%d bad=%u maximum=%u center=%08lx",profile,frame,bad,worst,(unsigned long)snapshot[tile(sensor_size/2,sensor_size/2,sensor_size)]);n3ds_log(message);goto done;}
        }
    }
    {char message[192];snprintf(message,sizeof(message),"PASS: native radar:72 multiframe GPU references,16/32/64px contacts/overlap/borders/sweep/mask/fade/clear/alpha; maximum=%u",worst);n3ds_log(message);}
    /* Same32 contacts and sweep values for all three paths. Measure only CPU
     * preparation/submission, excluding the queue wait and test readback. */
    long long totals[3]={0};unsigned int fallback_before=sensor_cpu_frames,native_before=sensor_native_frames;
    for(int mode=0;mode<3;++mode) for(int repeat=0;repeat<8;++repeat) {
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x102030ff};
        if(!n3ds_gpu_frame_begin()) goto done;active=1;n3ds_gpu_window_begin(&camera);
        sensor_gpu[2].failed=mode!=2;
        long long before=n3ds_engine_ticks();n3ds_gpu_sensor_begin();
        if(mode!=2) {if(sensor_native) goto done;sensor_size=mode?32:16;}
        else if(!sensor_native || sensor_size!=64) goto done;
        const float color[3]={.1f,.2f,.3f};
        for(int i=0;i<32;++i) if(!n3ds_gpu_sensor_blip(blip,(i%8)*8.f-28,(i/8)*8.f-12,2.f,color)) goto done;
        if(!n3ds_gpu_sensor_end(sweep,mask,1.3f,200,120,42,400,240)) goto done;
        totals[mode]+=n3ds_engine_ticks()-before;
        n3ds_gpu_present();active=0;
    }
    if(sensor_cpu_frames-fallback_before!=16 || sensor_native_frames-native_before!=8) goto done;
    {extern long long n3ds_engine_tick_frequency(void);char message[240];float factor=1000.f/(8*n3ds_engine_tick_frequency());
     snprintf(message,sizeof(message),"RADAR SUBMISSION BENCH: contacts=32 repeats=8 cpu16_ms=%.3f cpu32_ms=%.3f gpu64_ms=%.3f; CPU preparation only, not GPU time/FPS",totals[0]*factor,totals[1]*factor,totals[2]*factor);n3ds_log(message);}
    n3ds_log("PASS: native radar allocation fallback:16 CPU frames and8 GPU recovery frames,original tracking cadence retained");ok=1;
done:
    if(active) n3ds_gpu_present();if(!n3ds_gpu_texture_barrier()) abort();
    sensor_gpu_dispose();linearFree(snapshot);n3ds_gpu_texture_destroy(blip);n3ds_gpu_texture_destroy(sweep);n3ds_gpu_texture_destroy(mask);return ok;
}
/* Independent scalar HUD formula, compared against pixels produced by PICA.
 * Covers fill limits, positive/negative flash, opacity/fade, source alpha kill
 * and retention of framebuffer alpha. No combiner interpreter in reference. */
int n3ds_gpu_hud_tests(void)
{
    extern void n3ds_log(const char *);
    unsigned int texels[64],rows[14][4],*pixels=linearAlloc(240*400*4),maximum=0;
    int result=1,active=0;void *texture=NULL;FILE *proof=NULL;char message[192];
#define HUD_CHECK(e) do { if(!(e)) { snprintf(message,sizeof(message),"HUD GPU FAIL line %d: %s",__LINE__,#e);n3ds_log(message);goto done; } } while(0)
    HUD_CHECK(pixels);
    HUD_CHECK(n3ds_gpu_texture_identity_tests());
    n3ds_log("PASS: immutable texture identities: 256 slots, stable lookup, full fallback, same-address retirement and new identity");
    HUD_CHECK(meter_texel_tests());
    HUD_CHECK(sensor_native_tests());
    sensor_size=16;HUD_CHECK(sensor_mask_tests());
    sensor_size=SENSOR;HUD_CHECK(sensor_mask_tests());
    n3ds_log("PASS: HUD raw-texel reuse/alpha kill vs uncached combiner: 2048 CPU pixels");
    for(int test=0;test<14;++test) {
        struct native_hud_meter m={.minimum=0xc020b040,.maximum=0xc0d03080,.background=0x20406080,.flash=0xe0604020,.mode2=1,.tint=0x6060a0d0,.gradient=1};
        unsigned int blue=test<5?test*63:150,alpha=test==10?0:test==11?32:128;
        if(test==5) m.minimum=0x2020b040;
        if(test==6) m.flash=0x70604020;
        if(test==7) {m.negative=1;m.minimum=0x4020b040;}
        if(test==8) m.tint=0x0060a0d0;
        if(test==9) m.tint=0xff60a0d0;
        if(test==12) {m.minimum=0;m.maximum=0;m.flash=0;m.background=0xff000000;}
        if(test==13) {m.minimum=0xff20b040;m.flash=0xff604020;alpha=255;}
        for(int i=0;i<64;++i) texels[i]=0x30400000|(blue<<8)|alpha;
        if(!texture) texture=n3ds_gpu_texture_create(texels,8,8);
        else {memcpy(((C3D_Tex *)texture)->data,texels,sizeof(texels));C3D_TexFlush(texture);}
        HUD_CHECK(texture);
        /* The final vector stays immutable for its two GPU draws. Earlier
         * vectors still exercise same-address mutations and full comparisons. */
        if(test==13) HUD_CHECK(n3ds_gpu_texture_publish_immutable(texture));
        struct native_render_camera camera={.forward={0,0,-1},.up={0,1,0},.vertical_fov=1,.near_clip=.1f,.far_clip=100,.clear_color=0x204060ab};
        HUD_CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        HUD_CHECK(n3ds_gpu_texture_bind(0,texture));
        struct native_text_vertex v[4]={{{0,0},{1,1,1,1},{.5f,.5f}},{{400,0},{1,1,1,1},{.5f,.5f}},{{400,240},{1,1,1,1},{.5f,.5f}},{{0,240},{1,1,1,1},{.5f,.5f}}};
        HUD_CHECK(n3ds_gpu_hud_meter(&m,v,400,240,1,0));
        n3ds_gpu_present();active=0;HUD_CHECK(n3ds_gpu_readback(pixels,240*400));
        float b=blue/255.f,gradient=clamp(8*(32.f/255.f)*b);
        float flash=clamp(1-2*clamp(4*((m.minimum>>24)/255.f-b)));
        int background=b>=(m.flash>>24)/255.f;
        float dest_factor=((background?m.background:m.tint)>>24)/255.f;
        unsigned int expected=0xab;
        for(int c=0;c<3;++c) {
            int shift=16-c*8;
            float lo=((m.minimum>>shift)&255)/255.f,hi=((m.maximum>>shift)&255)/255.f;
            float f=((m.flash>>shift)&255)/255.f,tint=((m.tint>>shift)&255)/255.f;
            float value=(1-gradient)*lo+gradient*hi+(m.negative?-1:1)*flash*f;
            if(background) value=((m.background>>shift)&255)/255.f;
            unsigned int baked=(unsigned int)(clamp(value)*alpha+.5f);
            unsigned int channel=(unsigned int)(fminf(255,baked*tint+(32+c*32)*dest_factor)+.5f);
            if(!alpha) channel=32+c*32;
            expected|=channel<<(24-c*8);
        }
        unsigned int difference=0;
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) for(int shift=0;shift<32;shift+=8) {
            unsigned int delta=abs((int)((pixels[y*240+x]>>shift)&255)-(int)((expected>>shift)&255));
            if(delta>difference) difference=delta;
        }
        rows[test][0]=test;rows[test][1]=expected;rows[test][2]=pixels[200*240+120];rows[test][3]=difference;
        if(difference>maximum) maximum=difference;
        if(difference>2) {snprintf(message,sizeof(message),"HUD GPU vector %d expected %08x actual %08x difference %u",test,expected,rows[test][2],difference);n3ds_log(message);goto done;}
        /* Same-address source mutations above must miss; an identical next
         * frame must hit and produce identical GPU pixels. */
        unsigned int hits=cache_hits;
        HUD_CHECK(n3ds_gpu_frame_begin());active=1;n3ds_gpu_window_begin(&camera);
        HUD_CHECK(n3ds_gpu_texture_bind(0,texture));
        HUD_CHECK(n3ds_gpu_hud_meter(&m,v,400,240,1,0));
        HUD_CHECK(cache_hits==hits+1);
        if(test==13) HUD_CHECK(identity_hits>0);
        n3ds_gpu_present();active=0;HUD_CHECK(n3ds_gpu_readback(pixels,240*400));
        for(int y=184;y<216;++y) for(int x=104;x<136;++x) HUD_CHECK(pixels[y*240+x]==rows[test][2]);
    }
    proof=fopen("sdmc:/halo-native-hud.bin","wb");HUD_CHECK(proof && fwrite(rows,sizeof(rows),1,proof)==1);
    int close_result=fclose(proof);proof=NULL;HUD_CHECK(!close_result);
    snprintf(message,sizeof(message),"PASS: HUD mode2 fill, flash sign, tint, opacity, alpha kill and framebuffer alpha: 14 GPU vectors, max difference %u",maximum);n3ds_log(message);result=0;
done:
    if(active) n3ds_gpu_present();if(proof) fclose(proof);
    if(!n3ds_gpu_texture_barrier()) abort();n3ds_gpu_texture_destroy(texture);linearFree(pixels);return result;
}
