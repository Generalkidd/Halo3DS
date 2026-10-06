/* Portable NV2A register-combiner arithmetic. Source enum/register mapping:
 * shader_transparent_generic_preprocessor.c. Arithmetic reference:
 * registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt
 * No per-stage [0,1] clamp: signed scratch values survive until final output. */
#include <string.h>
#include <math.h>
#include "engine_generic.h"
#include "engine_hud.h"
static float clamp(float x,float lo,float hi) { return x<lo?lo:x>hi?hi:x; }
static float mapping(float x,int mode)
{
    float u=clamp(x,0,1);
    switch(mode) {
    case 0:return u;case 1:return 1-u;case 2:return 2*u-1;case 3:return 1-2*u;
    case 4:return u-.5f;case 5:return .5f-u;case 6:return x;default:return -x;
    }
}
static float output(float x,int mode)
{
    switch(mode) {
    case 1:x*=.5f;break;case 2:x*=2;break;case 3:x*=4;break;
    case 4:x-=.5f;break;case 5:x=2*x-1;break;
    }
    return clamp(x,-1,1);
}
static int reg(int source)
{
    if(source>=15) source-=10;
    if(source<9) return source; /* map0..3 => registers5..8 */
    if(source<11) return source-6; /* vertex0..1 */
    if(source<13) return source-10; /* scratch0..1 */
    return source-4; /* constants */
}
static int input_register(unsigned int code,int alpha)
{
    static const int lookup[16]={0,13,14,-1,9,10,-1,-1,5,6,7,8,11,12,-1,-1};
    int source=lookup[code&15];
    if(source>0 && (!!(code&16)!=alpha)) source+=10;
    return source;
}
static int output_register(unsigned int code)
{
    static const int lookup[16]={0,-1,-1,-1,3,4,-1,-1,5,6,7,8,1,2,-1,-1};
    return lookup[code&15];
}
static void packet(struct native_generic_stage *s,unsigned int rgb,unsigned int ro,unsigned int alpha,unsigned int ao)
{
    for(int channel=0;channel<2;++channel) {
        unsigned int inputs=channel?alpha:rgb,outputs=channel?ao:ro,flags=outputs>>12;
        int start=channel?14:0;
        for(int i=0;i<4;++i) {
            unsigned int code=(inputs>>(24-i*8))&255;
            s->op[start+i*2]=input_register(code,channel);s->op[start+i*2+1]=code>>5;
        }
        if(flags&4) s->flags|=1<<channel;
        int map=(flags&0x38)==0?0:(flags&0x38)==8?4:(flags&0x38)==16?2:
            (flags&0x38)==24?5:(flags&0x38)==32?3:1;
        if(channel) {
            s->op[22]=output_register(outputs>>4);s->op[23]=output_register(outputs);
            s->op[24]=output_register(outputs>>8);s->op[25]=map;
        } else {
            s->op[8]=output_register(outputs>>4);s->op[10]=output_register(outputs);
            s->op[12]=output_register(outputs>>8);s->op[13]=map;
            s->op[9]=!!(flags&2);s->op[11]=!!(flags&1);
        }
    }
}
/* Exact input packets from shader_transparent_chicago_preprocessor.c. Keeping
 * the packet translation shared also preserves alpha-replicate and all four
 * alpha interpolation variants, including their previous-stage alpha input. */
int n3ds_chicago_program(struct native_generic_material *m,int maps,const int color[3],const int alpha[3],const int replicate[3])
{
    static const unsigned int rgb[2][13]={
        {0x0c200000,0x08200000,0x080c0000,0x080c080c,0x08200c20,0x08204c20,0x0c204820,0x08200c40,0x0c200840,0x081c0c3c,0x0c1c083c,0x08180c38,0x0c180838},
        {0x0c200000,0x18200000,0x180c0000,0x180c180c,0x18200c20,0x18204c20,0x0c205820,0x18200c40,0x0c201840,0x181c0c3c,0x0c1c183c,0x18180c38,0x0c181838}};
    static const unsigned int a[13]={0x1c200000,0x18200000,0x181c0000,0x181c181c,0x18201c20,0x18205c20,0x1c205820,0x18201c40,0x1c201840,0x181c1c3c,0x1c1c183c,0x18181c38,0x1c181838};
    static const unsigned int increment[13]={0,0x01000000,0x01000000,0x01000100,0x01000000,0x01000000,0x00000100,0x01000000,0x00000100,0x01000000,0x00000100,0x01010001,0x00010101};
    if(!m || maps<1 || maps>4) return 0;
    for(int i=0;i<maps-1;++i) if(color[i]<0 || color[i]>12 || alpha[i]<0 || alpha[i]>12 || replicate[i]<0 || replicate[i]>1) return 0;
    memset(m->stage,0,sizeof(m->stage));m->maps=m->stages=maps;
    packet(m->stage,0x08200000,0xc00,0x18200000,0xc00);
    for(int i=0;i<maps-1;++i) packet(m->stage+i+1,rgb[replicate[i]][color[i]]+increment[color[i]]*(i+1),0xc00,a[alpha[i]]+increment[alpha[i]]*(i+1),0xc00);
    return 1;
}
void n3ds_meter_program(struct native_generic_material *m,const float constants[5][4],int negative_flash)
{
    /* Exact packets from original rasterizer_xbox_transparent_geometry.c.
     * Final RGB multiplication by base alpha is represented as stage5. */
    memset(m->stage,0,sizeof(m->stage));m->maps=1;m->stages=5;
    packet(&m->stage[0],0x1120e820,0x20c00,0x12081208,0x20c00);
    packet(&m->stage[1],0x3c011c02,0xc00,0x6c200000,0xc0);
    packet(&m->stage[2],negative_flash?0x0c201ce2:0x0c201c02,0xc00,0x0820b120,0xc00);
    packet(&m->stage[3],0x0c200120,0x4c00,0x12201120,0x4c00);
    packet(&m->stage[4],0x0c180000,0xc0,0x1c200000,0xc0);
    const int order[4][2]={{2,1},{0,1},{0,2},{3,4}};
    for(int i=0;i<4;++i) for(int j=0;j<2;++j)
        memcpy(m->stage[i].constant[j],constants[order[i][j]],4*sizeof(float));
}
void n3ds_hud_meter_program(struct native_generic_material *m,const float constants[5][4],int negative_flash)
{
    n3ds_meter_program(m,constants,negative_flash);
    /* HUD mode2 differs from model mode1 in its constant routing and flash
     * extension alpha input. Keep the original raw packet values. */
    packet(&m->stage[2],negative_flash?0x0c201ce2:0x0c201c02,0xc00,0x0820b220,0xc00);
    const int order[4][2]={{0,1},{0,1},{0,2},{3,4}};
    for(int i=0;i<4;++i) for(int j=0;j<2;++j)
        memcpy(m->stage[i].constant[j],constants[order[i][j]],4*sizeof(float));
    m->stage[0].constant[1][3]=32.f/255.f;
}
static float input(const float r[11][4],int source,int mode,int component)
{
    /* Original preprocessor encodes literal inputs through zero-register
     * mappings; preserve its special negative-literal rules exactly. */
    static const unsigned char literals[5][8]={
        {0,1,2,1,4,5,0,0},{1,0,1,2,5,4,1,2},
        {5,5,0,0,0,0,5,4},{0,1,2,1,4,5,2,1},
        {0,1,2,1,4,5,4,5}};
    if(source<5) return mapping(0,literals[source][mode]);
    if(component==3) component=source>=15?2:3;
    else if(source>=15) component=3;
    return mapping(r[reg(source)][component],mode);
}
int n3ds_generic_validate_inputs(const struct native_generic_material *m,unsigned int vertex_mask,unsigned int output_mask)
{
    unsigned int known[11]={0};
    if(!m || m->maps<0 || m->maps>4 || m->stages<1 || m->stages>8 ||
        vertex_mask>3 || !output_mask || output_mask>15) return 0;
    /* Unused Xbox texture stages are PS_TEXTUREMODES_NONE: RGB zero,
     * alpha one (xemu hw/xbox/nv2a/pgraph/glsl/psh.c). They are defined
     * combiner inputs, even when the material declares fewer maps. */
    for(int i=0;i<4;++i) known[5+i]=15;
    known[1]=8; /* initial r0.a = texture0.a, or one without texture0 */
    for(int i=0;i<2;++i) if(vertex_mask&(1u<<i)) known[3+i]=15;
    known[9]=known[10]=15;
    for(int s=0;s<m->stages;++s) {
        const struct native_generic_stage *t=&m->stage[s];
        const int *p=t->op;
        if(t->flags&~7 || p[9]<0 || p[9]>1 || p[11]<0 || p[11]>1 ||
            p[13]<0 || p[13]>5 || p[25]<0 || p[25]>5) return 0;
        for(int c=0;c<2;++c) for(int i=0;i<4;++i) {
            int src=p[c*14+i*2],map=p[c*14+i*2+1];
            if(src<0 || src>24 || map<0 || map>7) return 0;
            unsigned int mask=c?(src>=15?4:8):(src>=15?8:7);
            if(src>=5 && (known[reg(src)]&mask)!=mask) return 0;
        }
        const int destinations[6]={p[8],p[10],p[12],p[22],p[23],p[24]};
        for(int i=0;i<6;++i) {
            int d=destinations[i];if(d<0 || d>8) return 0;
            if(d) known[d]|=i<3?7:8;
        }
    }
    return (known[1]&output_mask)==output_mask;
}
int n3ds_generic_validate(const struct native_generic_material *m)
{ return m && m->maps>0 && n3ds_generic_validate_inputs(m,0,15); }
int n3ds_generic_lit_lerp(const struct native_generic_material *m)
{
    static const int op[2][26]={
        {6,0,15,1,5,0,15,0,0,0,0,0,1,0,16,1,1,0,0,0,0,0,1,0,0,0},
        {11,0,9,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
    if(!m || m->maps!=2 || m->stages!=2) return 0;
    for(int i=0;i<2;++i)
        if((m->stage[i].flags&~4) || memcmp(m->stage[i].op,op[i],sizeof(op[i]))) return 0;
    return 1;
}
int n3ds_generic_hologram(const struct native_generic_material *m)
{
    static const int hair[2][26]={
      {0,0,0,0,0,0,0,0,0,0,0,0,0,0,13,0,15,0,0,0,0,0,1,0,0,0},
      {6,0,21,0,0,0,0,0,1,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0}};
    static const int body[3][26]={
      {16,0,7,0,16,1,8,0,0,0,0,0,1,0,13,0,15,0,0,0,0,0,1,0,0,0},
      {11,0,1,0,13,0,6,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
      {11,0,21,0,0,0,0,0,1,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0}};
    int kind=m && m->maps==2 && m->stages==2?1:m && m->maps==4 && m->stages==3?2:0;
    if(!kind) return 0;
    for(int i=0;i<m->stages;++i)
        if((m->stage[i].flags&~4) || memcmp(m->stage[i].op,kind==1?hair[i]:body[i],sizeof(hair[0]))) return 0;
    return kind;
}
int n3ds_generic_shield(const struct native_generic_material *m)
{
    static const int flags[7]={0,0,0,2,2,0,0};
    static const int op[7][26]={
        {23,1,17,0,23,0,15,0,0,0,0,0,1,0,23,1,2,0,23,0,6,0,0,0,1,0},
        {18,0,2,0,18,1,11,0,0,0,0,0,1,0,8,0,2,0,8,1,11,0,0,0,1,0},
        {11,0,1,0,21,5,1,0,0,0,0,0,1,0,11,0,1,0,21,5,1,0,0,0,1,0},
        {23,1,13,0,23,0,14,0,0,0,0,0,4,0,11,0,11,0,21,0,21,0,0,0,1,3},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,11,2,11,2,0,0,1,0},
        {18,1,1,0,0,0,0,0,2,0,0,0,0,0,11,0,1,0,11,0,11,1,0,0,1,0},
        {21,0,8,0,10,0,12,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0}};
    if(!m || m->maps!=4 || m->stages!=7) return 0;
    for(int i=0;i<7;++i)
        if((m->stage[i].flags&~4)!=flags[i] || memcmp(m->stage[i].op,op[i],sizeof(op[i]))) return 0;
    return 1;
}
float n3ds_generic_shield_intensity(const struct native_generic_material *m,const float alpha[4])
{
    float ka=clamp(m->stage[0].constant[0][3],0,1),kb=clamp(m->stage[0].constant[0][2],0,1);
    float inverse_mask=1-clamp(alpha[3],0,1);
    float a=(1-ka)*clamp(alpha[2],0,1)+ka*clamp(alpha[0],0,1);
    float b=(1-kb)*.5f+kb*clamp(alpha[1],0,1);
    float v=clamp(.5f-fabsf(inverse_mask*(a-b)),0,.5f);
    float z=clamp(8*v*v-1,0,1);z*=z;
    return 2*z-z*z;
}
void n3ds_generic_shield_evaluate(const struct native_generic_material *m,const float s[4][4],float result[4])
{
    const float alpha[4]={s[0][3],s[1][3],s[2][3],s[3][3]};
    float intensity=n3ds_generic_shield_intensity(m,alpha);
    float inverse_mask=1-clamp(s[3][3],0,1);
    const float *c=m->stage[3].constant[0],*other=m->stage[3].constant[1];
    float blend=clamp(c[3],0,1);
    for(int i=0;i<3;++i) {
        float tint=(1-blend)*clamp(c[i],0,1)+blend*clamp(other[i],0,1);
        result[i]=clamp(intensity*clamp(s[3][i],0,1)+tint*inverse_mask,0,1);
    }
    result[3]=intensity;
}
void n3ds_generic_evaluate_inputs(const struct native_generic_material *m,const float samples[4][4],const float vertex[2][4],float result[4])
{
    float r[11][4]={{0}};
    for(int i=0;i<4;++i) {
        if(i<m->maps) memcpy(r[5+i],samples[i],4*sizeof(float));
        else r[5+i][3]=1;
    }
    r[1][3]=r[5][3];
    if(vertex) memcpy(r+3,vertex,2*4*sizeof(float));
    for(int s=0;s<m->stages;++s) {
        const struct native_generic_stage *t=&m->stage[s];const int *p=t->op;
        float in[4][4],products[2][4],values[3][4],selector=r[1][3];
        memcpy(r+9,t->constant,sizeof(t->constant));
        for(int i=0;i<4;++i) for(int c=0;c<4;++c) {
            int offset=(c==3?14:0)+i*2;
            in[i][c]=input(r,p[offset],p[offset+1],c);
        }
        for(int j=0;j<2;++j) for(int c=0;c<4;++c)
            products[j][c]=in[j*2][c]*in[j*2+1][c];
        for(int c=0;c<4;++c) {
            int mux=t->flags&(c==3?2:1);
            values[2][c]=mux?products[selector>=.5f][c]:products[0][c]+products[1][c];
            values[0][c]=products[0][c];values[1][c]=products[1][c];
        }
        for(int j=0;j<2;++j) if(p[9+j*2]) {
            float dot=products[j][0]+products[j][1]+products[j][2];
            for(int c=0;c<3;++c) values[j][c]=dot;
        }
        /* Fetch all inputs before publishing any RGB/alpha destination. */
        for(int j=0;j<3;++j) for(int c=0;c<4;++c) {
            int d=p[c==3?22+j:8+j*2];
            if(d) r[d][c]=output(values[j][c],p[c==3?25:13]);
        }
    }
    for(int c=0;c<4;++c) result[c]=clamp(r[1][c],0,1);
}
void n3ds_generic_evaluate(const struct native_generic_material *m,const float samples[4][4],float result[4])
{ n3ds_generic_evaluate_inputs(m,samples,NULL,result); }
float n3ds_menu_plasma_intensity(const float s[2][4],const float tint[3][4])
{
    /* Specialization of dynavobgeom's MIN/plasma seven-stage packet. MIN
     * here is a historical enum: the shader computes a plasma interference
     * curve, not the component-wise minimum framebuffer operation. */
    float a=s[0][2]*tint[0][2]+(.5f-s[1][2]*tint[1][2]);
    float b=s[1][3]*tint[1][3]+(.5f-s[0][3]*tint[0][3]);
    a=clamp(a,-1,1);b=clamp(b,-1,1);
    float selected=clamp(a>=.5f?b:a,0,1);
    float curve=clamp(4*selected*selected,0,1);
    float expanded=2*curve-1;
    float second=curve>=.5f?expanded*expanded:0;
    return curve*.5f+second*.5f;
}
void n3ds_menu_plasma_evaluate(const float s[3][4],const float tint[3][4],
    const float fade[4],const float vertex[4],float out[4])
{
    float intensity=n3ds_menu_plasma_intensity(s,tint);
    for(int c=0;c<3;++c) out[c]=clamp(intensity*fade[c]+s[2][c]*tint[2][c]*vertex[c],0,1);
    out[3]=clamp(s[2][3]*tint[2][3],0,1);
}
int n3ds_menu_plasma_tests(void)
{
    struct native_generic_material m={.maps=3,.stages=7};
    /* Original raw packets, with vertex0 substituted by constant1 in the
     * two stages that read it. Constants are quantized like Xbox ARGB8. */
    packet(&m.stage[0],0x08010902,0x89,0x18111912,0x89);
    packet(&m.stage[1],0x0a010802,0xac,0x1a111812,0xac);
    packet(&m.stage[2],0x1920b820,0xc00,0x0820a920,0xc00);
    packet(&m.stage[3],0,0,0x1c1c0c0c,0x24c00);
    packet(&m.stage[4],0,0,0x00005c5c,0x4d00);
    packet(&m.stage[5],0x1ca01da0,0xc00,0,0xc00);
    packet(&m.stage[6],0x0c010a02,0xc00,0x1c201a20,0xc00);
    if(!n3ds_generic_validate(&m)) return 0;
    unsigned int state=0x627391;
    for(int test=0;test<2048;++test) {
        float s[4][4]={{0}},tint[3][4],fade[4],vertex[4],actual[4],expected[4];
        for(int i=0;i<3;++i) for(int c=0;c<4;++c) {
            state=state*1664525u+1013904223u;s[i][c]=(state>>24)/255.f;
            state=state*1664525u+1013904223u;tint[i][c]=(state>>24)/255.f;
        }
        for(int c=0;c<4;++c) {
            state=state*1664525u+1013904223u;fade[c]=(state>>24)/255.f;
            state=state*1664525u+1013904223u;vertex[c]=(state>>24)/255.f;
        }
        memcpy(m.stage[0].constant[0],tint[0],sizeof(fade));
        memcpy(m.stage[0].constant[1],tint[1],sizeof(fade));
        memcpy(m.stage[1].constant[0],tint[2],sizeof(fade));
        memcpy(m.stage[1].constant[1],vertex,sizeof(fade));
        memcpy(m.stage[6].constant[0],fade,sizeof(fade));
        memcpy(m.stage[6].constant[1],vertex,sizeof(fade));
        n3ds_generic_evaluate(&m,s,expected);
        n3ds_menu_plasma_evaluate(s,tint,fade,vertex,actual);
        for(int c=0;c<4;++c) if(fabsf(actual[c]-expected[c])>.00001f) return 0;
    }
    return 1;
}
static int generic_cryo_tests(void)
{
    /* Actual A10 cryo glass and tint stage enums. Independent reduced algebra
     * checks the new vertex inputs, mapless RGB and premultiplied source alpha. */
    const int tint_ops[26]={13,0,10,0,14,0,10,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const int cube_ops[2][26]={
        {5,0,14,0,6,0,24,0,1,0,2,0,0,0,15,0,10,0,15,0,13,0,0,1,0,0},
        {11,0,12,1,21,0,6,0,0,0,0,0,1,0,6,0,14,0,0,0,0,0,1,0,0,0}};
    struct native_generic_material tint={.maps=0,.stages=1},cube={.maps=2,.stages=2};
    memcpy(tint.stage[0].op,tint_ops,sizeof(tint_ops));
    for(int i=0;i<2;++i) memcpy(cube.stage[i].op,cube_ops[i],sizeof(cube_ops[i]));
    if(n3ds_generic_validate(&tint) || n3ds_generic_validate_inputs(&tint,0,7) ||
        n3ds_generic_validate_inputs(&tint,1,7) || !n3ds_generic_validate_inputs(&tint,2,15) ||
        !n3ds_generic_validate_inputs(&tint,2,7) || !n3ds_generic_validate_inputs(&cube,2,15)) return 0;
    for(int k=0;k<256;++k) {
        float v[2][4]={{0}},s[4][4]={{0}},out[4],a=k/255.f;
        for(int c=0;c<4;++c) {
            v[1][c]=(k+c*41)%256/255.f;
            s[0][c]=(k+c*17)%256/255.f;s[1][c]=(k*3+c*23)%256/255.f;
            tint.stage[0].constant[0][c]=.1f*c;
            tint.stage[0].constant[1][c]=.6f-.1f*c;
            cube.stage[0].constant[1][c]=.2f+.1f*c;
        }
        cube.stage[0].constant[0][3]=a;cube.stage[1].constant[1][3]=.125f;
        n3ds_generic_evaluate_inputs(&tint,s,v,out);
        for(int c=0;c<3;++c) if(fabsf(out[c]-(.1f*c*v[1][c]+(.6f-.1f*c)*(1-v[1][c])))>1e-6f) return 0;
        n3ds_generic_evaluate_inputs(&cube,s,v,out);
        for(int c=0;c<3;++c) {
            /* In alpha combiners source15 reads texture0 BLUE, not alpha. */
            float expected=clamp(s[0][c]*(.2f+.1f*c)*(1-s[1][c]*.5f)+s[0][2]*a*s[1][c],0,1);
            if(fabsf(out[c]-expected)>1e-6f) return 0;
        }
        if(fabsf(out[3]-s[1][3]*.125f)>1e-6f) return 0;
    }
    return 1;
}
static int generic_rifle_bolt_tests(void)
{
    /* Actual rifle bolt: mix both maps by map0 alpha, then diffuse lighting.
     * Alpha is one minus map1 blue (cross-channel alpha source16). */
    const int op[2][26]={
        {6,0,15,1,5,0,15,0,0,0,0,0,1,0,16,1,1,0,0,0,0,0,1,0,0,0},
        {11,0,9,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
    struct native_generic_material m={.maps=2,.stages=2};
    for(int i=0;i<2;++i) memcpy(m.stage[i].op,op[i],sizeof(op[i]));
    if(n3ds_generic_validate_inputs(&m,2,15) || !n3ds_generic_validate_inputs(&m,3,15)) return 0;
    if(!n3ds_generic_lit_lerp(&m)) return 0;
    for(int i=0;i<2;++i) for(int j=0;j<26;++j) {
        ++m.stage[i].op[j];int accepted=n3ds_generic_lit_lerp(&m);--m.stage[i].op[j];
        if(accepted) return 0;
    }
    m.stage[0].flags=1;if(n3ds_generic_lit_lerp(&m)) return 0;
    m.stage[0].flags=2;if(n3ds_generic_lit_lerp(&m)) return 0;
    m.stage[0].flags=4;if(!n3ds_generic_lit_lerp(&m)) return 0;
    m.stage[0].flags=0;m.maps=3;if(n3ds_generic_lit_lerp(&m)) return 0;m.maps=2;
    for(int k=0;k<256;++k) {
        float s[4][4]={{0}},v[2][4]={{0}},out[4];
        for(int c=0;c<4;++c) {
            s[0][c]=(k+c*17)%256/255.f;s[1][c]=(k*3+c*23)%256/255.f;
            v[0][c]=(k*7+c*31)%256/255.f;
        }
        n3ds_generic_evaluate_inputs(&m,s,v,out);
        for(int c=0;c<3;++c) if(fabsf(out[c]-(s[1][c]*(1-s[0][3])+s[0][c]*s[0][3])*v[0][c])>1e-6f) return 0;
        if(fabsf(out[3]-(1-s[1][2]))>1e-6f) return 0;
    }
    return 1;
}
static int generic_disabled_texture_tests(void)
{
    /* Actual D40 control-panel 'sd text' material from the second cutscene.
     * Texture1 is disabled, but its RGB participates with animated alpha. */
    const int ops[26]={5,0,24,0,6,0,23,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0};
    struct native_generic_material m={.maps=1,.stages=1};
    memcpy(m.stage[0].op,ops,sizeof(ops));
    if(!n3ds_generic_validate(&m)) return 0;
    for(int k=0;k<256;++k) {
        float samples[4][4],out[4];
        for(int i=0;i<4;++i) for(int c=0;c<4;++c)
            samples[i][c]=(k+i*43+c*17)%256/255.f;
        m.stage[0].constant[0][3]=.75f+k/1020.f;
        m.stage[0].constant[1][3]=128.f/255.f;
        n3ds_generic_evaluate(&m,samples,out);
        for(int c=0;c<3;++c) if(fabsf(out[c]-samples[0][c]*128.f/255.f)>1e-6f) return 0;
        if(out[3]!=samples[0][3]) return 0;
    }
    float samples[4][4]={{.2f,.3f,.4f,.25f},{.8f,.9f,.7f,.1f}},out[4];
    /* Replicate disabled texture1 alpha into RGB, then its blue into alpha. */
    memset(m.stage,0,sizeof(m.stage));
    m.stage[0].op[0]=16;m.stage[0].op[2]=1;m.stage[0].op[8]=1;
    m.stage[0].op[14]=16;m.stage[0].op[16]=1;m.stage[0].op[22]=1;
    if(!n3ds_generic_validate(&m)) return 0;
    n3ds_generic_evaluate(&m,samples,out);
    if(out[0]!=1 || out[1]!=1 || out[2]!=1 || out[3]!=0) return 0;
    m.maps=0;m.stage[0].op[22]=0;
    if(!n3ds_generic_validate_inputs(&m,0,15)) return 0;
    n3ds_generic_evaluate(&m,samples,out);
    if(out[3]!=1) return 0;
    m.stage[0].op[0]=12; /* Undefined scratch RGB must still be rejected. */
    if(n3ds_generic_validate_inputs(&m,0,15)) return 0;
    m.stage[0].op[0]=9; /* Missing vertex inputs must still be rejected. */
    return !n3ds_generic_validate_inputs(&m,0,15);
}
int n3ds_generic_tests(void)
{
    struct native_generic_material m={.maps=4,.stages=1};
    float samples[4][4]={{.2f,.3f,.4f,.25f},{.8f,.6f,.2f,.75f},{.1f,.2f,.3f,.4f},{.7f,.6f,.5f,.4f}},out[4];
    int *p=m.stage[0].op;
    /* Sum map0*map1 + map2*map3, with independent alpha calculation. */
    p[0]=5;p[2]=6;p[4]=7;p[6]=8;p[12]=1;
    p[14]=5;p[16]=6;p[18]=7;p[20]=8;p[24]=1;
    if(!n3ds_generic_validate(&m)) return 0;
    n3ds_generic_evaluate(&m,samples,out);
    const float expected[4]={.23f,.30f,.23f,.3475f};
    for(int i=0;i<4;++i) if(fabsf(out[i]-expected[i])>.00001f) return 0;
    /* Mux uses entering r0.a, not alpha written concurrently by this stage. */
    m.stage[0].flags=3;n3ds_generic_evaluate(&m,samples,out);
    if(fabsf(out[0]-.16f)>.00001f || fabsf(out[3]-.1875f)>.00001f) return 0;
    samples[0][3]=.75f;n3ds_generic_evaluate(&m,samples,out);
    if(fabsf(out[0]-.07f)>.00001f || fabsf(out[3]-.16f)>.00001f) return 0;
    /* Reject unavailable vertex inputs; disabled texture stages are defined. */
    p[0]=9;if(n3ds_generic_validate(&m)) return 0;
    p[0]=5;m.maps=3;if(!n3ds_generic_validate(&m)) return 0;
    for(int i=0;i<8;++i) {
        const float expected_map[8]={0,1,-1,1,-.5f,.5f,-.25f,.25f};
        if(mapping(-.25f,i)!=expected_map[i]) return 0;
    }
    return generic_cryo_tests() && generic_rifle_bolt_tests() && generic_disabled_texture_tests();
}
