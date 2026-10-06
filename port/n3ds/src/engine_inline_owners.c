/* MSVC emits externally callable copies of these header helpers. Our ARM
 * adapter makes header inlines static, while several original source units
 * deliberately request external declarations. Export wrappers around the
 * exact original bodies, without duplicating their math/collision semantics. */
#define normalize2d n3ds_header_normalize2d
#define normalize3d n3ds_header_normalize3d
#define point_from_line3d n3ds_header_point_from_line3d
#define real_random_range n3ds_header_real_random_range
#define real_argb_color_to_pixel32 n3ds_header_real_argb_color_to_pixel32
#define collision_test_line n3ds_header_collision_test_line
#include "cseries.h"
#include "math/real_math.h"
#include "bitmaps/bitmaps_inlines.h"
#include "physics/collisions.h"
#undef normalize2d
#undef normalize3d
#undef point_from_line3d
#undef real_random_range
#undef real_argb_color_to_pixel32
#undef collision_test_line

real normalize2d(real_vector2d *v) { return n3ds_header_normalize2d(v); }
real normalize3d(real_vector3d *v) { return n3ds_header_normalize3d(v); }
real_point3d *point_from_line3d(const real_point3d *p,const real_vector3d *v,real t,real_point3d *out)
{ return n3ds_header_point_from_line3d(p,v,t,out); }
real real_random_range(real lower,real upper) { return n3ds_header_real_random_range(lower,upper); }
pixel32 real_argb_color_to_pixel32(const real_argb_color *color) { return n3ds_header_real_argb_color_to_pixel32(color); }
boolean collision_test_line(unsigned long flags,const real_point3d *a,const real_point3d *b,
    long ignore,struct collision_result *result)
{ return n3ds_header_collision_test_line(flags,a,b,ignore,result); }

void n3ds_log(const char *);
int halo_engine_inline_owner_tests(void)
{
    /* Volatile pointers force calls to exported symbols, not inline copies. */
    real (*volatile norm2)(real_vector2d *)=normalize2d;
    real (*volatile norm3)(real_vector3d *)=normalize3d;
    real_point3d *(*volatile line)(const real_point3d *,const real_vector3d *,real,real_point3d *)=point_from_line3d;
    real (*volatile random_range_call)(real,real)=real_random_range;
    pixel32 (*volatile pixel)(const real_argb_color *)=real_argb_color_to_pixel32;
    real_vector2d v2={{3,4}};
    real_vector3d v3={{2,-3,6}},zero={{0,0,0}},direction={{2,-4,8}};
    real_point3d point={{1,2,3}};
    real_argb_color color;
    unsigned long *seed=get_global_random_seed_address(),saved_seed=*seed;
#define CHECK(e) do { if(!(e)) { n3ds_log("INLINE OWNER FAIL: " #e); return 1; } } while(0)
    CHECK(fabsf(norm2(&v2)-5)<.00001f && fabsf(v2.i-.6f)<.00001f && fabsf(v2.j-.8f)<.00001f);
    CHECK(fabsf(norm3(&v3)-7)<.00001f && fabsf(v3.k-6.f/7.f)<.00001f);
    CHECK(norm3(&zero)==0 && zero.i==0 && zero.j==0 && zero.k==0);
    CHECK(line(&point,&direction,-.5f,&point)==&point && point.x==0 && point.y==4 && point.z==-1);
    color.alpha=.5f;color.red=1;color.green=.25f;color.blue=0;
    CHECK(pixel(&color)==0x80ff4000u);
    *seed=0;
    CHECK(fabsf(random_range_call(-2,6)-(-2.f+8.f*15470.f/65535.f))<.00001f);
    CHECK(*seed==1013904223u); *seed=saved_seed;
    n3ds_log("PASS: external original inline owners normalize vectors, preserve zero/alias cases, convert ARGB ties and advance the original random seed");
    return 0;
#undef CHECK
}
