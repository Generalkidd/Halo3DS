/* Production adapter against a minimal command/state model. This verifies
 * command ownership and transitions, not physical PICA sampling or speed. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define BIT(n) (1u << (n))
enum { C3DiF_TexAll=7, C3DiF_TexStatus=8, GPUREG_TEXUNIT_CONFIG=0x80 };
typedef struct { unsigned int flags, texConfig; void *tex[3]; } C3D_Context;
static C3D_Context context;
static C3D_Context *C3Di_GetContext(void) { return &context; }
static unsigned int hardware_config, wanted=7, commands, invalidations, observations;
static unsigned int last_omitted, last_write;
static void GPUCMD_AddMaskedWrite(unsigned int reg, unsigned int mask, unsigned int value)
{
    assert(reg==GPUREG_TEXUNIT_CONFIG);
    ++commands;
    if(mask==4) { assert(value==BIT(16)); ++invalidations; return; }
    assert(mask==1 || mask==0xB);
    unsigned int bits=mask==1?0xFFu:0xFF00FFFFu;
    hardware_config=(hardware_config&~bits)|(value&bits);
}
unsigned int n3ds_gpu_texture_unit_mask(void) { return wanted; }
void n3ds_gpu_texture_unit_observe(unsigned int omitted, int write)
{ ++observations; last_omitted=omitted; last_write=write; }
void __real_C3Di_UpdateContext(void)
{
    if(context.flags&C3DiF_TexAll) {
        unsigned int enabled=0;
        for(unsigned int i=0;i<3;++i) if(context.tex[i]) enabled|=BIT(i);
        context.texConfig=(context.texConfig&~7u)|enabled;
        context.flags=(context.flags&~C3DiF_TexAll)|C3DiF_TexStatus;
    }
    if(context.flags&C3DiF_TexStatus) {
        GPUCMD_AddMaskedWrite(GPUREG_TEXUNIT_CONFIG,0xB,context.texConfig);
        context.flags&=~C3DiF_TexStatus;
    }
}
#define HALO_N3DS_UNUSED_TEXTURE_UNITS
#include "../../src/engine_citro3d_color_read.c"

static void draw(unsigned int expected)
{
    unsigned int config=context.texConfig;
    void *textures[3]={context.tex[0],context.tex[1],context.tex[2]};
    __wrap_C3Di_UpdateContext();
    assert((hardware_config&7)==expected);
    assert((hardware_config&0xF8)==(config&0xF8));
    assert((context.texConfig&~7u)==(config&~7u));
    for(unsigned int i=0;i<3;++i) assert(context.tex[i]==textures[i]);
}
int main(void)
{
    unsigned int descriptor[3]={0};
    context=(C3D_Context){.flags=C3DiF_TexAll,.texConfig=0xA0,
        .tex={descriptor,descriptor+1,descriptor+2}};
    draw(7); assert(!last_omitted && !last_write);
    wanted=1; draw(1); assert(last_omitted==6 && last_write);
    unsigned int before=commands;
    draw(1); assert(commands==before && !last_write);
    /* No descriptor change: later materials must regain both samplers. */
    wanted=7; draw(7); assert(!last_omitted && last_write);
    for(unsigned int mask=1;mask<8;mask+=2) {
        wanted=mask; draw(mask);
        /* Stock rebind, APT restore and canceled commands dirty TexAll. */
        context.flags|=C3DiF_TexAll; draw(mask);
        assert(last_write==(mask!=7));
        context.flags|=C3DiF_TexStatus; draw(mask);
        assert(last_write==(mask!=7));
        wanted=7; draw(7);
    }
    /* Exercise every consecutive terrain-mask pair, including an emission-like
     * mask 5, against every subset of stock bindings and both stock dirty paths.
     * This proves adapter transitions only, not material sampling correctness. */
    unsigned int transitions=0;
    for(unsigned int bindings=0;bindings<8;++bindings)
    for(unsigned int first=1;first<8;first+=2)
    for(unsigned int second=1;second<8;second+=2)
    for(unsigned int dirty=0;dirty<3;++dirty) {
        for(unsigned int unit=0;unit<3;++unit)
            context.tex[unit]=(bindings&BIT(unit))?descriptor+unit:NULL;
        context.flags=C3DiF_TexAll;
        wanted=first; draw(bindings&first);
        unsigned int writes_before=commands, invalidations_before=invalidations;
        context.flags=dirty==1?C3DiF_TexAll:dirty==2?C3DiF_TexStatus:0;
        wanted=second; draw(bindings&second);
        unsigned int prior=dirty?bindings:(bindings&first);
        unsigned int change=prior!=(bindings&second);
        assert(last_write==change);
        assert(commands-writes_before==(dirty?1u:0u)+2u*change);
        assert(invalidations-invalidations_before==change);
        ++transitions;
    }
    assert(transitions==384);
    context.tex[0]=descriptor; context.tex[1]=descriptor+1;
    /* Never enable a unit absent from stock state. */
    context.tex[2]=NULL; context.flags|=C3DiF_TexAll;
    wanted=5; draw(1); assert(last_omitted==2);
    wanted=7; draw(3);
    assert(invalidations && observations);
    puts("PASS: production texture-unit adapter: retained descriptors, 384 mask/binding/dirty transitions, all terrain masks, stock rebind/resume/discard, restoration, cache invalidation and missing bindings");
}
