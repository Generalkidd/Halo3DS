/* Ordinary screen quads share the text shader; HUD mode2 meters use their
 * original combiner math and a separate native blend path. */
#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_text.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "engine_renderer.h"
#include "engine_text.h"
#include "engine_hud.h"
#include "engine_textures.h"
#include "bitmaps/bitmap_group.h"
void n3ds_log(const char *);
boolean rasterizer_set_texture_bitmap_data(short,const struct bitmap_data *);
_Static_assert(sizeof(struct native_hud_meter)==28,"HUD meter packet ABI");
extern struct rasterizer_window_begin_parameters global_window_parameters;
int n3ds_engine_screen_quad_supported(const struct rasterizer_dynamic_screen_geometry_parameters *parameters)
{
    if(!parameters || !parameters->map[0]) return 0;
    if(parameters->doing_plasma_effect)
        return parameters->map[1] && parameters->map[2] && !parameters->meter_parameters &&
            parameters->map0_to_1_blend_function==5 && parameters->map1_to_2_blend_function==0;
    if(parameters->map[1]) return !parameters->map[2] && !parameters->meter_parameters && parameters->map0_to_1_blend_function==1;
    return !parameters->map[2];
}
static float byte_constant(real value) { return (float)__builtin_rint((double)MAX(0.f,MIN(1.f,value))*255.0)/255.f; }
static void plasma_draw(const struct rasterizer_dynamic_screen_geometry_parameters *p,const struct dynamic_screen_vertex *vertices)
{
    struct native_menu_plasma packet={0};struct native_text_vertex v[4];
    const rectangle2d *r=&global_window_parameters.camera.viewport_bounds;
    /* UI widgets issue axis-aligned rectangles. Reject other geometry until
     * its interpolation is implemented, instead of drawing it incorrectly. */
    assert(vertices[0].position.y==vertices[1].position.y && vertices[1].position.x==vertices[2].position.x &&
        vertices[2].position.y==vertices[3].position.y && vertices[3].position.x==vertices[0].position.x);
    packet.point=p->point_sampled;packet.blend=p->framebuffer_blend_function;
    packet.fade[0]=byte_constant(p->plasma_fade.red);packet.fade[1]=byte_constant(p->plasma_fade.green);
    packet.fade[2]=byte_constant(p->plasma_fade.blue);packet.fade[3]=byte_constant(p->plasma_fade.alpha);
    for(int m=0;m<3;++m) {
        if(!p->map[m]) continue;
        if(!rasterizer_set_texture_bitmap_data(m,p->map[m])) {
            const struct bitmap_data *b=p->map[m];char message[256];
            snprintf(message,sizeof(message),"MENU BITMAP FAIL: stage=%d tag=%08lx type=%d format=%d size=%dx%dx%d flags=%04x mips=%d offset=%ld bytes=%ld cache=%u/%u",m,b->tag_index,b->type,b->format,b->width,b->height,b->depth,b->flags,b->mipmap_count,b->pixels_offset,b->pixels_size,n3ds_engine_texture_count(),n3ds_engine_texture_bytes());n3ds_log(message);
            assert(FALSE);return;
        }
        packet.textures[m]=n3ds_gpu_texture_bound(m);
        packet.wrap[m]=p->map_wrapped[m];
        for(int c=0;c<3;++c) packet.tint[m][c]=byte_constant(p->map_tint[m]?p->map_tint[m]->n[c]:1);
        packet.tint[m][3]=byte_constant(p->map_fade[m]?*p->map_fade[m]:1);
        for(int i=0;i<4;++i) for(int a=0;a<2;++a) {
            float coordinate=p->map_anchor_screen[m]?vertices[i].position.n[a]:vertices[i].texture_coordinates.n[a];
            packet.uv[m][i][a]=(coordinate*p->map_scale[m].n[a]+(p->map_offset[m]?p->map_offset[m]->n[a]:0))*p->map_texture_scale[m].n[a];
        }
    }
    for(int i=0;i<4;++i) {
        for(int a=0;a<2;++a) v[i].position[a]=vertices[i].position.n[a]+(p->offset?p->offset->n[a]:0);
        for(int c=0;c<3;++c) v[i].color[c]=((vertices[i].color>>(16-c*8))&255)/255.f;
        v[i].color[3]=(vertices[i].color>>24)/255.f;
    }
    if(p->doing_plasma_effect) {assert(n3ds_gpu_menu_plasma(&packet,v,r->x1-r->x0,r->y1-r->y0));}
    else {assert(n3ds_gpu_screen_multiply(&packet,v,r->x1-r->x0,r->y1-r->y0));}
    n3ds_gpu_world_state_restore();
}
void _rasterizer_psuedo_dynamic_screen_quad_draw(struct rasterizer_dynamic_screen_geometry_parameters *parameters,
    struct dynamic_screen_vertex *vertices)
{
    if(!rasterizer_debug_options.dynamic_screen_geometry || global_window_parameters.rasterizer_target!=0) return;
    assert(n3ds_engine_window_active() && !n3ds_engine_text_active());
    if(!n3ds_engine_screen_quad_supported(parameters)) {
        char message[240];
        snprintf(message,sizeof(message),"SCREEN QUAD REJECT: maps=%d,%d,%d plasma=%d meter=%d functions=%d,%d blend=%d caller=%p",!!parameters->map[0],!!parameters->map[1],!!parameters->map[2],parameters->doing_plasma_effect,!!parameters->meter_parameters,parameters->map0_to_1_blend_function,parameters->map1_to_2_blend_function,parameters->framebuffer_blend_function,__builtin_return_address(0));n3ds_log(message);
        for(int m=0;m<3;++m) if(parameters->map[m]) {const struct bitmap_data *b=parameters->map[m];snprintf(message,sizeof(message),"SCREEN QUAD BITMAP: stage=%d tag=%08lx type=%d format=%d size=%dx%d",m,b->tag_index,b->type,b->format,b->width,b->height);n3ds_log(message);}
        display_assert("Unsupported native screen quad map/blend combination",__FILE__,__LINE__,TRUE);
        halt_and_catch_fire(); return;
    }
    if(parameters->doing_plasma_effect || parameters->map[1]) {plasma_draw(parameters,vertices);return;}
    if(parameters->meter_parameters) {
        struct native_text_vertex v[4];
        const rectangle2d *r=&global_window_parameters.camera.viewport_bounds;
        for(int i=0;i<4;++i) {
            float x=vertices[i].position.x,y=vertices[i].position.y;
            float u=parameters->map_anchor_screen[0]?x:vertices[i].texture_coordinates.x;
            float t=parameters->map_anchor_screen[0]?y:vertices[i].texture_coordinates.y;
            v[i].position[0]=x+(parameters->offset?parameters->offset->i:0);
            v[i].position[1]=y+(parameters->offset?parameters->offset->j:0);
            v[i].uv[0]=u*parameters->map_texture_scale[0].i*parameters->map_scale[0].i+(parameters->map_offset[0]?parameters->map_offset[0]->x:0);
            v[i].uv[1]=t*parameters->map_texture_scale[0].j*parameters->map_scale[0].j+(parameters->map_offset[0]?parameters->map_offset[0]->y:0);
            for(int c=0;c<4;++c) v[i].color[c]=1;
        }
        assert(rasterizer_set_texture_bitmap_data(0,parameters->map[0]));
        assert(n3ds_gpu_hud_meter(parameters->meter_parameters,v,r->x1-r->x0,r->y1-r->y0,parameters->point_sampled,parameters->map_wrapped[0]));
        n3ds_gpu_world_state_restore();return;
    }
    rasterizer_text_begin(parameters);
    rasterizer_text_draw_character(vertices);
    rasterizer_text_end();
}
