#include "cseries.h"
#include "real_math.h"
#include "rasterizer/rasterizer_widgets.h"
#include "rasterizer/rasterizer.h"
#include "game/game_globals.h"
#include "rasterizer/common/rasterizer_common.h"
#include "engine_widgets.h"
#include "engine_renderer.h"
#include "engine_text.h"
extern struct rasterizer_window_begin_parameters global_window_parameters;
boolean rasterizer_set_texture_non_blocking(short stage,short type,short usage,long group,short index);
boolean rasterizer_set_texture_direct_non_blocking(short stage,long group,short index);
static int active,texture_ready;
static unsigned int flags;
static float tint;
int n3ds_engine_widgets_active(void) {return active;}
void _rasterizer_widget_begin(short type,word input_flags)
{
    /* Occlusion-query widgets require a separate native visibility path. */
    assert(type==5 && !(input_flags&~3U) && !active && n3ds_engine_window_active());
    assert(!n3ds_engine_text_active() && !n3ds_engine_models_active());
    active=1;texture_ready=0;flags=input_flags;tint=1;
}
void _rasterizer_widget_end(void)
{
    assert(active);active=0;texture_ready=0;n3ds_gpu_world_state_restore();
}
boolean _rasterizer_widget_set_texture(short stage,long group,short sequence)
{
    assert(active && stage==0);
    boolean pending;
    if(group!=NONE) pending=rasterizer_set_texture_non_blocking(stage,0,1,group,sequence);
    else pending=global_rasterizer_data ? rasterizer_set_texture_direct_non_blocking(stage,global_rasterizer_data->glow.index,sequence) : TRUE;
    texture_ready=!pending;return pending;
}
void _rasterizer_widget_set_tint_factor(real factor) {assert(active && isfinite(factor));tint=factor;}
void _rasterizer_widget_set_zbuffer_enable(boolean enable) {assert(active);flags=(flags&~1U)|(enable?1:0);}
static void emit(const real_point3d corners[4],float u,float v,unsigned long color,int world)
{
    struct native_widget_vertex vertices[4];
    static const unsigned char uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    if(!texture_ready) return;
    for(unsigned int i=0;i<4;++i) {
        memcpy(vertices[i].position,corners[i].n,12);
        vertices[i].color[0]=((color>>16)&255)/255.f;vertices[i].color[1]=((color>>8)&255)/255.f;
        vertices[i].color[2]=(color&255)/255.f;vertices[i].color[3]=(color>>24)/255.f;
        vertices[i].uv[0]=uv[i][0]*u;vertices[i].uv[1]=uv[i][1]*v;
    }
    const rectangle2d *bounds=&global_window_parameters.camera.viewport_bounds;
    assert(n3ds_gpu_widget_draw(vertices,world,bounds->x1-bounds->x0,bounds->y1-bounds->y0,tint,flags));
}
void _rasterizer_widget_draw_sprite2d(const real_point2d *point,real radius,
    const real_vector2d *scale,const real_vector2d *texture_scale,real rotation,unsigned long color)
{
    assert(active && point && isfinite(radius) && isfinite(rotation));if(radius<=0) return;
    /* Preserve upstream's screen-space sprite convention: scale contains the
     * pixel half-extent, radius gates emission, and rotation is in radians. */
    float x=cosf(rotation)-sinf(rotation),y=sinf(rotation)+cosf(rotation);
    float sx=scale?scale->i:1,sy=scale?scale->j:1;
    real_point3d corners[4]={{{point->x-sx*x,point->y-sy*y,0}},{{point->x+sx*y,point->y-sy*x,0}},
        {{point->x+sx*x,point->y+sy*y,0}},{{point->x-sx*y,point->y+sy*x,0}}};
    emit(corners,texture_scale?(short)texture_scale->i:1,texture_scale?(short)texture_scale->j:1,color,0);
}
void _rasterizer_widget_draw_sprite3d(const real_point3d *point,real radius,
    const real_vector2d *scale,real rotation,unsigned long color)
{
    assert(active && point && isfinite(radius) && isfinite(rotation));if(radius<=0) return;
    const real_vector3d *forward=&global_window_parameters.camera.forward,*up=&global_window_parameters.camera.up;
    real_vector3d right,billboard_up;
    cross_product3d(forward,up,&right);assert(normalize3d(&right)>0);
    cross_product3d(&right,forward,&billboard_up);assert(normalize3d(&billboard_up)>0);
    float angle=rotation*3.14159265359f/180.f;
    float x=radius*(cosf(angle)-sinf(angle)),y=radius*(sinf(angle)+cosf(angle));
    float sx=scale?scale->i:1,sy=scale?scale->j:1;
    float offsets[4][2]={{-sx*x,sy*y},{sx*y,sy*x},{sx*x,-sy*y},{-sx*y,-sy*x}};
    real_point3d corners[4];
    for(unsigned int i=0;i<4;++i) for(unsigned int j=0;j<3;++j)
        corners[i].n[j]=point->n[j]+right.n[j]*offsets[i][0]+billboard_up.n[j]*offsets[i][1];
    emit(corners,1,1,color,1);
}
