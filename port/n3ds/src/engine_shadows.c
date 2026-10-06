/* Native fidelity policy: projected object shadows are unavailable. The
 * original renderer checks begin's boolean and skips the entire shadow body.
 * No Xbox render target, shadow texture or output radius is fabricated. */
#include "cseries.h"
#include "rasterizer/rasterizer.h"
void n3ds_log(const char *message);
static boolean announced;
static boolean shadow_list;
void _rasterizer_environment_shadows_begin(void)
{ assert(!shadow_list); shadow_list=TRUE; }
void _rasterizer_environment_shadows_end(void)
{ assert(shadow_list); shadow_list=FALSE; }
boolean _rasterizer_environment_shadow_begin(long object_index,
    const real_matrix4x3 *matrix,const real_rgb_color *color,real radius,real *volume_radius)
{
    (void)object_index; (void)matrix; (void)color; (void)radius; (void)volume_radius;
    if(!announced) { n3ds_log("NATIVE SHADOWS: projected object shadows unavailable; original renderer skips rejected pass"); announced=TRUE; }
    return FALSE;
}
int halo_engine_shadow_capability_tests(void)
{
    real sentinel=123.5f;
    _rasterizer_environment_shadows_begin();
    if(_rasterizer_environment_shadow_begin(NONE,NULL,NULL,1.f,&sentinel) || sentinel!=123.5f) return 1;
    if(_rasterizer_environment_shadow_begin(NONE,NULL,NULL,1.f,&sentinel) || sentinel!=123.5f) return 1;
    _rasterizer_environment_shadows_end();
    n3ds_log("PASS: unavailable projected shadows reject repeated begins without changing output radius");
    return 0;
}
/* Reaching these after begin returned FALSE is a caller contract violation. */
void _rasterizer_environment_shadow_model_begin(const struct rasterizer_model_begin_parameters *parameters)
{ (void)parameters; assert(!"Native projected shadow pass is unavailable"); }
void _rasterizer_environment_shadow_model_draw(const struct shader *shader,short bitmap,
    const struct triangle_buffer *triangles,const struct vertex_buffer *vertices)
{ (void)shader; (void)bitmap; (void)triangles; (void)vertices; assert(!"Native projected shadow pass is unavailable"); }
void _rasterizer_environment_shadow_model_end(void)
{ assert(!"Native projected shadow pass is unavailable"); }
void _rasterizer_environment_shadow_end(void)
{ assert(!"Native projected shadow pass is unavailable"); }
void _rasterizer_environment_shadow_draw(const struct shader *shader,short bitmap,
    long triangles,long first,long count,const struct vertex_buffer *vertices)
{ (void)shader; (void)bitmap; (void)triangles; (void)first; (void)count; (void)vertices; assert(!"Native projected shadow pass is unavailable"); }
