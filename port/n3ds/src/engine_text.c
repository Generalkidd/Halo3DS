#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_text.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "engine_renderer.h"
#include "engine_text.h"

extern struct rasterizer_window_begin_parameters global_window_parameters;
boolean rasterizer_set_texture_bitmap_data(short stage,const struct bitmap_data *bitmap);
void rasterizer_set_framebuffer_blend_function(short mode);
static struct rasterizer_dynamic_screen_geometry_parameters parameters;
static int active;
int n3ds_engine_text_active(void) { return active; }
static unsigned int channel(real value)
{
    assert(isfinite(value));
    if(value<=0) return 0; if(value>=1) return 255;
    return (unsigned int)(value*255.f+.5f);
}
void rasterizer_text_begin(const struct rasterizer_dynamic_screen_geometry_parameters *input)
{
    unsigned int rgba=0xffffffffU;
    assert(!active);
    if(!rasterizer_debug_options.dynamic_screen_geometry || global_window_parameters.rasterizer_target!=0) return;
    assert(n3ds_engine_window_active() && input && input->map[0]);
    /* All original string/unicode callers use one map. General HUD multi-map,
     * meter and plasma passes require their own shader implementations. */
    assert(!input->map[1] && !input->map[2] && !input->meter_parameters && !input->doing_plasma_effect);
    parameters=*input;
    if(input->map_tint[0]) {
        const real_rgb_color *t=input->map_tint[0];
        rgba=channel(t->red)|channel(t->green)<<8|channel(t->blue)<<16|0xff000000U;
    }
    if(input->map_fade[0]) rgba=(rgba&0xffffffU)|(channel(*input->map_fade[0])<<24);
    assert(rasterizer_set_texture_bitmap_data(0,input->map[0]));
    {
        const rectangle2d *bounds=&global_window_parameters.camera.viewport_bounds;
        assert(n3ds_gpu_text_begin(bounds->x1-bounds->x0,bounds->y1-bounds->y0,rgba,input->point_sampled,input->map_wrapped[0]));
    }
    rasterizer_set_framebuffer_blend_function(input->framebuffer_blend_function);
    active=1;
}
void rasterizer_text_draw_character(const struct dynamic_screen_vertex *vertices)
{
    struct native_text_vertex output[4];
    if(!active) return;
    assert(vertices && n3ds_engine_window_active());
    for(unsigned int i=0;i<4;++i) {
        real x=vertices[i].position.x,y=vertices[i].position.y;
        real u=parameters.map_anchor_screen[0] ? x : vertices[i].texture_coordinates.x;
        real v=parameters.map_anchor_screen[0] ? y : vertices[i].texture_coordinates.y;
        assert(isfinite(x) && isfinite(y) && isfinite(u) && isfinite(v));
        output[i].position[0]=x+(parameters.offset?parameters.offset->i:0);
        output[i].position[1]=y+(parameters.offset?parameters.offset->j:0);
        output[i].uv[0]=u*parameters.map_texture_scale[0].i*parameters.map_scale[0].i+(parameters.map_offset[0]?parameters.map_offset[0]->x:0);
        output[i].uv[1]=v*parameters.map_texture_scale[0].j*parameters.map_scale[0].j+(parameters.map_offset[0]?parameters.map_offset[0]->y:0);
        unsigned long color=vertices[i].color;
        output[i].color[0]=((color>>16)&255)/255.f;
        output[i].color[1]=((color>>8)&255)/255.f;
        output[i].color[2]=(color&255)/255.f;
        output[i].color[3]=(color>>24)/255.f;
    }
    assert(n3ds_gpu_text_draw(output));
}
void rasterizer_text_end(void)
{
    if(active) { active=0; n3ds_gpu_world_state_restore(); }
}
