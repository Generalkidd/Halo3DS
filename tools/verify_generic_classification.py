#!/usr/bin/env python3
"""Check production generic material classification and lit-lerp admission."""
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / 'port/n3ds/src/engine_generic_model.c').read_text()
    classifier = source[source.index('struct generic_classification {'):
                        source.index('static real_vector3d generic_normal(')]
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_generic.h"
struct generic_shader {int type,flags,blend,fade_mode;};
static unsigned int generation=1;
static unsigned int n3ds_engine_cache_generation(void){return generation;}
''' + classifier + r'''
int main(void) {
    assert(n3ds_generic_tests());
    const int op[2][26]={
        {6,0,15,1,5,0,15,0,0,0,0,0,1,0,16,1,1,0,0,0,0,0,1,0,0,0},
        {11,0,9,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
    struct native_generic_material m={.maps=2,.stages=2};
    for(int i=0;i<2;++i) memcpy(m.stage[i].op,op[i],sizeof(op[i]));
    struct generic_shader g={0};unsigned int checked=0;
    for(int type=0;type<2;++type) for(int flags=0;flags<256;++flags)
    for(int blend=0;blend<8;++blend) for(int fade=0;fade<3;++fade) {
        g=(struct generic_shader){type,flags,blend,fade};
        int expected=type==0 && !(flags&8) && blend==0;
        struct generic_classification c=generic_classify(&g,&m);
        assert(c.lit_lerp==expected);
        if(expected) assert(c.per_vertex && c.valid && !c.native.channel[0].function && !c.hologram);
        struct generic_classification hit=generic_classify(&g,&m);
        assert(!memcmp(&hit,&c,sizeof(c)));
        ++checked;
    }
    g=(struct generic_shader){0};
    for(int stage=0;stage<2;++stage) for(int field=0;field<26;++field) {
        assert(generic_classify(&g,&m).lit_lerp);
        ++m.stage[stage].op[field];
        assert(!generic_classify(&g,&m).lit_lerp);
        --m.stage[stage].op[field];
        ++generation;
        assert(generic_classify(&g,&m).lit_lerp);
    }
    printf("PASS: generic arithmetic self-tests, %u classifier combinations, cache hits, 52 opcode mutations and generation changes\n",checked);
}
'''
    with tempfile.TemporaryDirectory(prefix='halo-generic-classification-') as directory:
        work = Path(directory)
        (work / 'check.c').write_text(harness)
        subprocess.run(['clang', '-O2', '-ffp-contract=off', '-fsanitize=address,undefined',
                        '-I', str(root / 'port/n3ds/include'), str(work / 'check.c'),
                        str(root / 'port/n3ds/src/engine_generic.c'), '-o', str(work / 'check')],
                       check=True, timeout=60)
        subprocess.run([str(work / 'check')], check=True, timeout=60)


if __name__ == '__main__':
    main()
