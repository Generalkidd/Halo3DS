#!/usr/bin/env python3
"""Compare production BSP visibility span copies with a per-index oracle."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "port/n3ds/include/engine_bsp_index_visibility.inl").read_text()
function = source
header = (ROOT / "port/n3ds/include/engine_renderer.h").read_text()
declarations = header[header.index("struct native_bsp_index_run {"):header.index("void n3ds_engine_bsp_draw(")]
ownership_source = (ROOT / "port/n3ds/include/engine_batch_visibility.inl").read_text()
ownership = ownership_source[ownership_source.index("static int batch_add_surface("):ownership_source.index("static void batch_clusters_prepare(")]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MAXIMUM_CLUSTERS_PER_STRUCTURE 64
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
struct native_geometry {
 const unsigned short *index_source, *indices, *vertex_remap;
 unsigned int index_count, vertex_count;
 unsigned short *filtered[2];
 unsigned int filter_count[2],filter_key[2][2],filter_frame[2];
 int filter_ready[2];
};
/* PRODUCTION_VISIBILITY_DECLARATIONS */
static struct {unsigned int clusters[2];} batches[3];
static long count=3;
static struct {unsigned int visible_cluster_flags[2];} render;
static unsigned int eye,flushes,allocation_fail;
static unsigned int n3ds_gpu_stereo_view(void) {return eye;}
static int n3ds_input_platform_original_model(void) {return 0;}
static void *n3ds_gpu_geometry_optional_allocate(unsigned int bytes) {return allocation_fail?NULL:malloc(bytes);}
static void n3ds_gpu_geometry_flush(void *p,unsigned int bytes) {assert(p && bytes);++flushes;}
/* Stub the legacy selector to exercise its returned index stream and
 * remapping here; this fixture does not validate legacy PVS selection. */
unsigned int n3ds_bsp_visible_indices(const unsigned short *s,unsigned int n,unsigned short *o)
{unsigned int used=0;for(unsigned int i=0;i<n;i+=3) if((i/3)%2) {
 if(o) memcpy(o+used,s+i,3*sizeof(*s));used+=3;}return used;}
static uint32_t state=1234567;
static unsigned int random_u(void) {state=state*1664525u+1013904223u;return state;}
'''
suffix = r'''
int main(void) {
 unsigned short indices[768],remap[768],out[770],expected[768];
 struct native_bsp_index_run runs[256];
 for(unsigned int trial=0;trial<20000;++trial) {
  unsigned int key[2]={random_u(),random_u()},n=0,want=0;
  unsigned int run_count=1+random_u()%64;
  for(unsigned int r=0;r<run_count;++r) {
   unsigned int owner=random_u()%68,count=3*(random_u()%5);
   runs[r]=(struct native_bsp_index_run){.cluster=owner,.index_count=count};
   for(unsigned int i=0;i<count;++i) {
    unsigned short value=random_u()%768;indices[n++]=value;
    if(owner>=64 || (key[owner/32]&(1u<<(owner%32)))) expected[want++]=value;
   }
  }
  struct native_geometry g={indices,indices,NULL,n,768};
  struct native_bsp_material_visibility v={.runs=runs,.run_count=run_count};
  memset(out,0xa5,sizeof(out));
  assert(bsp_visible_material_indices(&g,&v,key,NULL)==want);
  assert(bsp_visible_material_indices(&g,&v,key,out+1)==want);
  assert(!memcmp(out+1,expected,want*sizeof(*out)));
  assert(out[0]==0xa5a5 && out[want+1]==0xa5a5);
 }
 for(unsigned int i=0;i<768;++i) {indices[i]=i;remap[i]=767-i;}
 for(unsigned int mapped=0;mapped<2;++mapped) {
  struct native_geometry g={indices,indices,mapped?remap:NULL,768,768};
  struct native_bsp_material_visibility v={0};unsigned int key[2]={0,0};
  unsigned int n=n3ds_bsp_visible_indices(indices,768,expected);
  if(mapped) for(unsigned int i=0;i<n;++i) expected[i]=remap[expected[i]];
  assert(bsp_visible_material_indices(&g,&v,key,NULL)==n);
  assert(bsp_visible_material_indices(&g,&v,key,out)==n);
  assert(!memcmp(out,expected,n*sizeof(*out)));
 }
 /* A changed view in the same GPU frame must not overwrite queued indices. */
 const unsigned short full[6]={0,1,2,3,4,5};
 struct native_bsp_index_run owners[2]={{.cluster=0,.index_count=3},{.cluster=1,.index_count=3}};
 unsigned int owner_mask[2]={3,0};
 struct native_bsp_material_visibility visibility={owner_mask,owners,2};
 struct native_geometry g={full,full,NULL,6,6};
 bsp_filter_frame=1;render.visible_cluster_flags[0]=1;
 unsigned int count=6;
 const unsigned short *first=bsp_visible_index_buffer(&g,&count,&visibility);
 assert(count==3 && first==g.filtered[0] && !memcmp(first,full,6));
 unsigned int old_flushes=flushes;count=6;
 assert(bsp_visible_index_buffer(&g,&count,&visibility)==first && count==3 && flushes==old_flushes);
 render.visible_cluster_flags[0]=2;count=6;
 assert(bsp_visible_index_buffer(&g,&count,&visibility)==full && count==6);
 assert(!memcmp(first,full,6) && flushes==old_flushes);
 eye=1;count=6;
 const unsigned short *second=bsp_visible_index_buffer(&g,&count,&visibility);
 assert(count==3 && second!=first && !memcmp(second,full+3,6));
 assert(!memcmp(first,full,6));
 eye=0;++bsp_filter_frame;count=6;
 assert(bsp_visible_index_buffer(&g,&count,&visibility)==first && count==3);
 assert(!memcmp(first,full+3,6));
 ++bsp_filter_frame;render.visible_cluster_flags[0]=0;count=6;
 bsp_visible_index_buffer(&g,&count,&visibility);assert(count==0);
 ++bsp_filter_frame;render.visible_cluster_flags[0]=3;count=6;
 assert(bsp_visible_index_buffer(&g,&count,&visibility)==full && count==6);
 free(g.filtered[0]);free(g.filtered[1]);
 g=(struct native_geometry){full,full,NULL,6,6};allocation_fail=1;
 ++bsp_filter_frame;render.visible_cluster_flags[0]=1;count=6;
 assert(bsp_visible_index_buffer(&g,&count,&visibility)==full && count==6);
 /* All distinct cluster pairs, including duplicate visits after sharing. */
 for(unsigned int a=0;a<64;++a) for(unsigned int b=0;b<64;++b) {
  unsigned short material[3]={0,1,65535},seen[3]={65535,65535,65535};
  memset(batches,0,sizeof(batches));
  assert(batch_add_surface(0,a,material,seen,3));
  assert(seen[0]==a && seen[1]==65535);
  assert(batch_add_surface(0,b,material,seen,3));
  assert(seen[0]==(a==b?a:65534));
  assert(batch_add_surface(0,a,material,seen,3));
  assert(seen[0]==(a==b?a:65534));
  assert((batches[0].clusters[a/32]&(1u<<(a%32))) &&
         (batches[0].clusters[b/32]&(1u<<(b%32))));
  assert(batch_add_surface(2,a,material,seen,3));
  assert(!batches[1].clusters[0] && !batches[1].clusters[1]);
  assert(!batch_add_surface(-1,a,material,seen,3));
  assert(!batch_add_surface(3,a,material,seen,3));
 }
 puts("PASS: 20000 BSP run filters; legacy/remapped streams; cache hits, queued-buffer preservation, stereo isolation, next-frame rebuild, empty/full visibility and allocation fallback");
 puts("PASS: 4096 ownership cluster pairs retain shared surfaces and material masks");
}
'''
with tempfile.TemporaryDirectory(prefix="halo-bsp-visibility-") as temporary:
    directory = Path(temporary)
    test = directory / "test.c"
    executable = directory / "test"
    test.write_text(prefix.replace("/* PRODUCTION_VISIBILITY_DECLARATIONS */", declarations) + ownership + function + suffix)
    subprocess.run(["clang", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
                    str(test), "-o", str(executable)], check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=60)
