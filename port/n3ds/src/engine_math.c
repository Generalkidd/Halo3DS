/* ARM11 scalar VFP kernel. Keep all B rows in caller-saved registers;
 * each A column is consumed before its output is written, including aliases.
 * VMLA on VFPv2 rounds multiply and add separately (no fused contraction). */
#include "cseries.h"
#include "math/matrix_math.h"
#include "memory/data.h"
void n3ds_log(const char *);
long long n3ds_engine_ticks(void),n3ds_engine_tick_frequency(void);
__attribute__((naked)) void n3ds_matrix4x3_multiply(const real_matrix4x3 *a,const real_matrix4x3 *b,real_matrix4x3 *out)
{
 __asm__ volatile(
 "add ip, r1, #4\n"
 "vldmia ip, {s0-s11}\n"
 "vldr s12, [r0, #4]\n"
 "vldr s13, [r0, #16]\n"
 "vldr s14, [r0, #28]\n"
 "vmul.f32 s15, s0, s12\n"
 "vmla.f32 s15, s1, s13\n"
 "vmla.f32 s15, s2, s14\n"
 "vstr s15, [r2, #4]\n"
 "vmul.f32 s15, s3, s12\n"
 "vmla.f32 s15, s4, s13\n"
 "vmla.f32 s15, s5, s14\n"
 "vstr s15, [r2, #16]\n"
 "vmul.f32 s15, s6, s12\n"
 "vmla.f32 s15, s7, s13\n"
 "vmla.f32 s15, s8, s14\n"
 "vstr s15, [r2, #28]\n"
 "vmul.f32 s15, s9, s12\n"
 "vmla.f32 s15, s10, s13\n"
 "vmla.f32 s15, s11, s14\n"
 "vldr s12, [r0]\n"
 "vmul.f32 s15, s15, s12\n"
 "vldr s13, [r0, #40]\n"
 "vadd.f32 s15, s15, s13\n"
 "vstr s15, [r2, #40]\n"
 "vldr s12, [r0, #8]\n"
 "vldr s13, [r0, #20]\n"
 "vldr s14, [r0, #32]\n"
 "vmul.f32 s15, s0, s12\n"
 "vmla.f32 s15, s1, s13\n"
 "vmla.f32 s15, s2, s14\n"
 "vstr s15, [r2, #8]\n"
 "vmul.f32 s15, s3, s12\n"
 "vmla.f32 s15, s4, s13\n"
 "vmla.f32 s15, s5, s14\n"
 "vstr s15, [r2, #20]\n"
 "vmul.f32 s15, s6, s12\n"
 "vmla.f32 s15, s7, s13\n"
 "vmla.f32 s15, s8, s14\n"
 "vstr s15, [r2, #32]\n"
 "vmul.f32 s15, s9, s12\n"
 "vmla.f32 s15, s10, s13\n"
 "vmla.f32 s15, s11, s14\n"
 "vldr s12, [r0]\n"
 "vmul.f32 s15, s15, s12\n"
 "vldr s13, [r0, #44]\n"
 "vadd.f32 s15, s15, s13\n"
 "vstr s15, [r2, #44]\n"
 "vldr s12, [r0, #12]\n"
 "vldr s13, [r0, #24]\n"
 "vldr s14, [r0, #36]\n"
 "vmul.f32 s15, s0, s12\n"
 "vmla.f32 s15, s1, s13\n"
 "vmla.f32 s15, s2, s14\n"
 "vstr s15, [r2, #12]\n"
 "vmul.f32 s15, s3, s12\n"
 "vmla.f32 s15, s4, s13\n"
 "vmla.f32 s15, s5, s14\n"
 "vstr s15, [r2, #24]\n"
 "vmul.f32 s15, s6, s12\n"
 "vmla.f32 s15, s7, s13\n"
 "vmla.f32 s15, s8, s14\n"
 "vstr s15, [r2, #36]\n"
 "vmul.f32 s15, s9, s12\n"
 "vmla.f32 s15, s10, s13\n"
 "vmla.f32 s15, s11, s14\n"
 "vldr s12, [r0]\n"
 "vmul.f32 s15, s15, s12\n"
 "vldr s13, [r0, #48]\n"
 "vadd.f32 s15, s15, s13\n"
 "vstr s15, [r2, #48]\n"
 "vldr s12, [r0]\n"
 "vldr s13, [r1]\n"
 "vmul.f32 s15, s12, s13\n"
 "vstr s15, [r2]\n"
 "bx lr\n");
}
static __attribute__((noinline)) void reference(const real_matrix4x3 *a,const real_matrix4x3 *b,real_matrix4x3 *r)
{
 real_matrix4x3 t;
 for(int row=0;row<4;++row) for(int col=0;col<3;++col) {
  float v=b->n[row][0]*a->n[0][col]+b->n[row][1]*a->n[1][col]+b->n[row][2]*a->n[2][col];
  if(row==3) v=v*a->scale+a->n[3][col];
  t.n[row][col]=v;
 }
 t.scale=a->scale*b->scale;*r=t;
}
int n3ds_math_tests(void)
{
 extern int n3ds_datum_lookup_tests(void);
 if(!n3ds_datum_lookup_tests())return 90;
 extern int n3ds_transform_tests(void);
 int transforms=n3ds_transform_tests();if(transforms)return 100+transforms;
 unsigned int random=0x716259U;real_matrix4x3 a,b,r,t,alias;
 for(int i=0;i<4096;++i) {
  for(int j=0;j<13;++j) {random=random*1664525U+1013904223U;((float *)&a)[j]=((int)(random&65535)-32768)/1024.f;random=random*1664525U+1013904223U;((float *)&b)[j]=((int)(random&65535)-32768)/1024.f;}
  if(i&1) for(int j=0;j<13;++j) {
   random=random*1664525U+1013904223U;unsigned int bits=(random&0x807fffffU)|0x41000000U;memcpy((float *)&a+j,&bits,4);
   random=random*1664525U+1013904223U;bits=(random&0x807fffffU)|0x3e800000U;memcpy((float *)&b+j,&bits,4);
  }
  if(i%8==0) a.scale=0; if(i%8==1)b.scale=-1;
  reference(&a,&b,&r);n3ds_matrix4x3_multiply(&a,&b,&t);if(memcmp(&r,&t,sizeof(r))) return 1;
  alias=a;n3ds_matrix4x3_multiply(&alias,&b,&alias);if(memcmp(&r,&alias,sizeof(r)))return 2;
  alias=b;n3ds_matrix4x3_multiply(&a,&alias,&alias);if(memcmp(&r,&alias,sizeof(r)))return 3;
  reference(&a,&a,&r);alias=a;n3ds_matrix4x3_multiply(&alias,&alias,&alias);if(memcmp(&r,&alias,sizeof(r)))return 4;
 }
 long long times[2];volatile float proof;
 for(int mode=0;mode<2;++mode) {long long start=n3ds_engine_ticks();for(int i=0;i<20000;++i) {a.n[0][0]=(i&31)*.25f;if(mode)n3ds_matrix4x3_multiply(&a,&b,&t);else reference(&a,&b,&t);proof=t.n[0][0];}times[mode]=n3ds_engine_ticks()-start;}
 char msg[180];snprintf(msg,sizeof(msg),"PASS: native matrix: 4096 exact comparisons with both alias directions; reference_ms=%.3f native_ms=%.3f",times[0]*1000./n3ds_engine_tick_frequency(),times[1]*1000./n3ds_engine_tick_frequency());n3ds_log(msg);return 0;
}

int n3ds_datum_lookup_tests(void)
{
 struct {short salt;char padding[6];} slots[8];
 struct data_array data={0};data.valid=TRUE;data.data=slots;data.size=sizeof(slots[0]);data.count=8;data.maximum_count=8;
 for(int i=0;i<8;++i)slots[i].salt=(short)(0x8100+i);
 for(int i=0;i<8;++i) {
  long index=((unsigned int)(unsigned short)slots[i].salt<<16)|i;
  if(n3ds_datum_get_checked(&data,index)!=datum_get(&data,index) || n3ds_datum_lookup(&data,i)!=slots+i) return 0;
  if(n3ds_datum_lookup(&data,index^0x10000))return 0;
 }
 if(n3ds_datum_lookup(&data,NONE) || n3ds_datum_lookup(&data,8) || n3ds_datum_lookup(&data,0x8000))return 0;
 data.identifier_zero_invalid=TRUE;if(n3ds_datum_lookup(&data,0))return 0;data.identifier_zero_invalid=FALSE;
 data.valid=FALSE;if(n3ds_datum_lookup(&data,0))return 0;data.valid=TRUE;
 slots[0].salt=0;if(n3ds_datum_lookup(&data,0))return 0;slots[0].salt=(short)0x8100;
 long long times[2]={0};void *volatile proof;
 for(int round=0;round<4;++round)for(int order=0;order<2;++order) {
  int mode=(round+order)&1;long long begin=n3ds_engine_ticks();
  for(int i=0;i<20000;++i) {long index=((unsigned int)(0x8100+(i&7))<<16)|(i&7);proof=mode?n3ds_datum_get_checked(&data,index):datum_get(&data,index);}
  times[mode]+=n3ds_engine_ticks()-begin;
 }
 char msg[192];snprintf(msg,sizeof(msg),"PASS: checked datum lookup: salt, reused slots, zero identifiers, negative/out-of-range and invalid arrays; 80000 calls reference_ms=%.3f inline_ms=%.3f",times[0]*1000./n3ds_engine_tick_frequency(),times[1]*1000./n3ds_engine_tick_frequency());n3ds_log(msg);return 1;
}

extern real_point3d *n3ds_reference_matrix4x3_transform_point(const real_matrix4x3 *,const real_point3d *,real_point3d *);
extern real_vector3d *n3ds_reference_matrix4x3_transform_vector(const real_matrix4x3 *,const real_vector3d *,real_vector3d *);
extern real_point3d *n3ds_matrix4x3_transform_point(const real_matrix4x3 *,const real_point3d *,real_point3d *);
extern real_vector3d *n3ds_matrix4x3_transform_vector(const real_matrix4x3 *,const real_vector3d *,real_vector3d *);
int n3ds_transform_tests(void)
{
 unsigned int random=0x91f076U;real_matrix4x3 m;real_point3d p,r,t,alias;real_vector3d vr,vt,va;
 for(int i=0;i<16384;++i) {
  for(int j=0;j<13;++j) {
   random=random*1664525U+1013904223U;unsigned int bits=(random&0x807fffffU)|(((random>>25)%24+112)<<23);memcpy((float *)&m+j,&bits,4);
  }
  for(int j=0;j<3;++j) {random=random*1664525U+1013904223U;p.n[j]=((int)(random&65535)-32768)/1024.f;}
  if(i%8==0)m.scale=1;else if(i%8==1)m.scale=0;else if(i%8==2)m.scale=-1;
  if(i%32==0)p.x=-0.f;
  n3ds_reference_matrix4x3_transform_point(&m,&p,&r);
  if(n3ds_matrix4x3_transform_point(&m,&p,&t)!=&t || memcmp(&r,&t,sizeof(r)))return 1;
  alias=p;n3ds_matrix4x3_transform_point(&m,&alias,&alias);if(memcmp(&r,&alias,sizeof(r)))return 2;
  n3ds_reference_matrix4x3_transform_vector(&m,(const real_vector3d *)&p,&vr);
  if(n3ds_matrix4x3_transform_vector(&m,(const real_vector3d *)&p,&vt)!=&vt || memcmp(&vr,&vt,sizeof(vr)))return 3;
  memcpy(&va,&p,sizeof(p));n3ds_matrix4x3_transform_vector(&m,&va,&va);if(memcmp(&vr,&va,sizeof(vr)))return 4;
 }
 long long times[2][2]={{0}};volatile float proof=0;
 real_point3d *(*point_functions[2])(const real_matrix4x3 *,const real_point3d *,real_point3d *)={n3ds_reference_matrix4x3_transform_point,n3ds_matrix4x3_transform_point};
 real_vector3d *(*vector_functions[2])(const real_matrix4x3 *,const real_vector3d *,real_vector3d *)={n3ds_reference_matrix4x3_transform_vector,n3ds_matrix4x3_transform_vector};
 for(int round=0;round<4;++round)for(int order=0;order<2;++order) {
  int mode=(round+order)&1;long long begin=n3ds_engine_ticks();
  for(int i=0;i<20000;++i) {m.scale=(i&1)?1.f:.75f;p.x=(i&127)*.125f;point_functions[mode](&m,&p,&t);proof=t.x;}
  times[0][mode]+=n3ds_engine_ticks()-begin;begin=n3ds_engine_ticks();
  for(int i=0;i<20000;++i) {m.scale=(i&1)?1.f:.75f;p.x=(i&127)*.125f;vector_functions[mode](&m,(const real_vector3d *)&p,&vt);proof=vt.i;}
  times[1][mode]+=n3ds_engine_ticks()-begin;
 }
 char msg[240];snprintf(msg,sizeof(msg),"PASS: ARM11 transforms: 16384 exact poses, scales and in-place aliases; 80000 calls reference_point_ms=%.3f arm_point_ms=%.3f reference_vector_ms=%.3f arm_vector_ms=%.3f",times[0][0]*1000./n3ds_engine_tick_frequency(),times[0][1]*1000./n3ds_engine_tick_frequency(),times[1][0]*1000./n3ds_engine_tick_frequency(),times[1][1]*1000./n3ds_engine_tick_frequency());n3ds_log(msg);return 0;
}

/* Load the complete transform once into caller-saved VFPv2 registers.
 * Preserve CE evaluation order, non-unit scales and in-place inputs. */
__attribute__((naked)) real_point3d *n3ds_matrix4x3_transform_point(const real_matrix4x3 *m,const real_point3d *v,real_point3d *out)
{
 __asm__ volatile(
 "vldmia r0, {s0-s12}\n"
 "vldr s13, 1f\n"
 "vcmp.f32 s0, s13\n"
 "vmrs APSR_nzcv, FPSCR\n"
 "vldmia r1, {s13-s15}\n"
 "vmulne.f32 s13, s13, s0\n"
 "vmulne.f32 s14, s14, s0\n"
 "vmulne.f32 s15, s15, s0\n"
 "vmul.f32 s7, s7, s15\n"
 "vmla.f32 s7, s4, s14\n"
 "vmla.f32 s7, s1, s13\n"
 "vadd.f32 s7, s7, s10\n"
 "vstr s7, [r2, #0]\n"
 "vmul.f32 s8, s8, s15\n"
 "vmla.f32 s8, s5, s14\n"
 "vmla.f32 s8, s2, s13\n"
 "vadd.f32 s8, s8, s11\n"
 "vstr s8, [r2, #4]\n"
 "vmul.f32 s9, s9, s15\n"
 "vmla.f32 s9, s6, s14\n"
 "vmla.f32 s9, s3, s13\n"
 "vadd.f32 s9, s9, s12\n"
 "vstr s9, [r2, #8]\n"
 "mov r0, r2\n"
 "bx lr\n"
 ".align 2\n"
 "1: .float 1.0\n"
);
}

/* Load the complete transform once into caller-saved VFPv2 registers.
 * Preserve CE evaluation order, non-unit scales and in-place inputs. */
__attribute__((naked)) real_vector3d *n3ds_matrix4x3_transform_vector(const real_matrix4x3 *m,const real_vector3d *v,real_vector3d *out)
{
 __asm__ volatile(
 "vldmia r0, {s0-s12}\n"
 "vldr s13, 1f\n"
 "vcmp.f32 s0, s13\n"
 "vmrs APSR_nzcv, FPSCR\n"
 "vldmia r1, {s13-s15}\n"
 "vmulne.f32 s13, s13, s0\n"
 "vmulne.f32 s14, s14, s0\n"
 "vmulne.f32 s15, s15, s0\n"
 "vmul.f32 s1, s13, s1\n"
 "vmla.f32 s1, s14, s4\n"
 "vmla.f32 s1, s15, s7\n"
 "vstr s1, [r2, #0]\n"
 "vmul.f32 s2, s13, s2\n"
 "vmla.f32 s2, s14, s5\n"
 "vmla.f32 s2, s15, s8\n"
 "vstr s2, [r2, #4]\n"
 "vmul.f32 s3, s13, s3\n"
 "vmla.f32 s3, s14, s6\n"
 "vmla.f32 s3, s15, s9\n"
 "vstr s3, [r2, #8]\n"
 "mov r0, r2\n"
 "bx lr\n"
 ".align 2\n"
 "1: .float 1.0\n"
);
}
