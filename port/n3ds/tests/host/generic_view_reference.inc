/* Frozen pre-optimization generic view reference. Keep independent of the
 * optimized implementation so missing output writes and changed accumulation
 * order remain observable. Included only by the host verifier. */
static float generic_fog_visibility(const struct generic_fog_constants *constants,float depth,float plane)
{
    const struct render_fog *fog=&global_window_parameters.fog;
    float visible=1;
    if(constants->atmospheric)
        visible*=1-PIN((depth-fog->atmospheric_minimum_distance)/constants->range,0.f,1.f)*constants->atmospheric_density;
    if(constants->planar) {
        float x=PIN(1+(plane-fog->plane.d)/fog->planar_maximum_depth,0.f,1.f);
        float y=PIN(1-depth/fog->planar_maximum_distance,0.f,1.f);
        float q=1-MIN(1.f,x*x+y*y),r=1-y*y;
        float density=q*q+(r*r-q*q)*constants->camera_depth;
        visible*=1-density*constants->planar_density;
    }
    return visible;
}
/* Original generic vertex programs47/57: oD1.rgb=abs(dot(normal,-forward))
 * times fog visibility; oD1.a=(1-facing)*visibility. Reflection uses the
 * positional eye vector instead. These are intentionally different vectors. */
static void generic_view(real_vector3d normal,const float position[3],
    float vertex[2][4],float reflected[3],const struct generic_fog_constants *fog_constants)
{
    float n2=0,dot=0,facing=0,depth=0,plane=0;
    const struct render_fog *fog=&global_window_parameters.fog;
    for(int c=0;c<3;++c) n2+=normal.n[c]*normal.n[c];
    assert(n2>1e-12f);
    const float normal_length=sqrtf(n2);
    for(int c=0;c<3;++c) {
        normal.n[c]/=normal_length;
        reflected[c]=global_window_parameters.camera.position.n[c]-position[c];
        dot+=normal.n[c]*reflected[c];
        facing-=normal.n[c]*global_window_parameters.camera.forward.n[c];
        depth-=reflected[c]*global_window_parameters.camera.forward.n[c];
        plane+=fog->plane.n.n[c]*position[c];
    }
    for(int c=0;c<3;++c) reflected[c]=2*dot*normal.n[c]-reflected[c];
    float visible=generic_fog_visibility(fog_constants,depth,plane);
    facing=PIN(fabsf(facing),0.f,1.f);
    memset(vertex,0,2*4*sizeof(float));vertex[0][3]=visible;
    for(int c=0;c<3;++c) vertex[1][c]=facing*visible;
    vertex[1][3]=(1-facing)*visible;
}
