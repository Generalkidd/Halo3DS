/* External owners for original header math used by wind and live AI. */
#define uniform_cubic_spline_vector3d n3ds_header_uniform_cubic_spline_vector3d
#define vector_from_points3d n3ds_header_vector_from_points3d
#define magnitude_squared3d n3ds_header_magnitude_squared3d
#define distance_squared3d n3ds_header_distance_squared3d
#define distance3d n3ds_header_distance3d
#define signed_angular_difference n3ds_header_signed_angular_difference
#define real_local_random_range n3ds_header_real_local_random_range
#include "cseries.h"
#include "real_math.h"
#undef uniform_cubic_spline_vector3d
#undef vector_from_points3d
#undef magnitude_squared3d
#undef distance_squared3d
#undef distance3d
#undef signed_angular_difference
#undef real_local_random_range
real signed_angular_difference(real a,real b) { return n3ds_header_signed_angular_difference(a,b); }
real real_local_random_range(real a,real b) { return n3ds_header_real_local_random_range(a,b); }
real_vector3d *vector_from_points3d(const real_point3d *a,const real_point3d *b,real_vector3d *out)
{ return n3ds_header_vector_from_points3d(a,b,out); }
real magnitude_squared3d(const real_vector3d *v)
{ return n3ds_header_magnitude_squared3d(v); }
real distance_squared3d(const real_point3d *a,const real_point3d *b)
{ return n3ds_header_distance_squared3d(a,b); }
real distance3d(const real_point3d *a,const real_point3d *b)
{ return n3ds_header_distance3d(a,b); }
void uniform_cubic_spline_vector3d(real_vector3d *result,const real_vector3d *f0,
    const real_vector3d *f1,const real_vector3d *f2,const real_vector3d *f3,
    real t0,real h,real t)
{ n3ds_header_uniform_cubic_spline_vector3d(result,f0,f1,f2,f3,t0,h,t); }

void n3ds_log(const char *message);
int halo_engine_control_math_tests(void)
{
    real (*volatile difference)(real,real)=signed_angular_difference;
    real (*volatile random_range)(real,real)=real_local_random_range;
    unsigned long *local=get_global_local_random_seed_address(),saved=*local;
    unsigned long global=*get_global_random_seed_address();
    int good;
    *local=0;
    good=fabsf(random_range(-2,6)-(-2.f+8.f*15470.f/65535.f))<.00001f && *local==1013904223u;
    *local=saved;
    if(!good || *get_global_random_seed_address()!=global ||
       difference(1,.5f)!=-.5f || fabsf(difference(6,.25f)-.5331853f)>.00001f ||
       difference(0,-_pi)!=_pi || difference(0,_pi)!=_pi) return 1;
    n3ds_log("PASS: original control math owners preserve angular wrap/ties and local random stream without changing the global seed");
    return 0;
}
int halo_engine_world_math_tests(void)
{
    real (*volatile distance_call)(const real_point3d *,const real_point3d *)=distance3d;
    real (*volatile squared_call)(const real_point3d *,const real_point3d *)=distance_squared3d;
    real (*volatile magnitude_call)(const real_vector3d *)=magnitude_squared3d;
    real_vector3d *(*volatile vector_call)(const real_point3d *,const real_point3d *,real_vector3d *)=vector_from_points3d;
    real_point3d a={{1,2,3}},b={{3,-1,9}};
    real_vector3d vector;
    if(vector_call(&a,&b,&vector)!=&vector || vector.i!=2 || vector.j!=-3 || vector.k!=6 ||
       magnitude_call(&vector)!=49 || squared_call(&a,&b)!=49 || squared_call(&b,&a)!=49 ||
       fabsf(distance_call(&a,&b)-7)>.00001f || distance_call(&a,&a)!=0) return 1;
    n3ds_log("PASS: original exported AI math computes 3D vector, squared magnitude and symmetric distances");
    return 0;
}
