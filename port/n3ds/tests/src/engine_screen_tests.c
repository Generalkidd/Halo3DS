#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "engine_renderer.h"
#include "engine_bitmaps.h"
#include "engine_text.h"
int n3ds_engine_screen_quad_supported(const struct rasterizer_dynamic_screen_geometry_parameters *parameters);
void n3ds_log(const char *message);
#define CHECK(x) do { if(!(x)) { n3ds_log("FAIL: native screen quad: " #x); return 1; } } while(0)
static void rectangle(struct dynamic_screen_vertex vertices[4],real x,real y,real width,real height,real uv,unsigned long color)
{
    static const int corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(unsigned int i=0;i<4;++i) {
        vertices[i].position.x=x+corners[i][0]*width; vertices[i].position.y=y+corners[i][1]*height;
        vertices[i].texture_coordinates.x=corners[i][0]*uv; vertices[i].texture_coordinates.y=corners[i][1]*uv;
        vertices[i].color=color;
    }
}
int halo_engine_screen_tests(void)
{
    struct rasterizer_frame_begin_parameters frame={0};
    struct rasterizer_window_begin_parameters window={0};
    struct rasterizer_dynamic_screen_geometry_parameters parameters={0};
    struct dynamic_screen_vertex vertices[4];
    real_rgb_color tint={.25f,.5f,.75f}; real fade=.5f;
    real_vector2d offset={4,2}; real_point2d uv_offset={-116.f/80.f,-114.f/80.f};
    unsigned int prior=n3ds_dynamic_bitmap_count();
    struct bitmap_data *white=bitmap_2d_new(8,8,0,11),*checker=bitmap_2d_new(8,8,0,11);
    CHECK(white && checker);
    for(unsigned int y=0;y<8;++y) for(unsigned int x=0;x<8;++x) {
        static const unsigned int colors[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
        ((unsigned int *)white->base_address)[y*8+x]=0xffffffff;
        ((unsigned int *)checker->base_address)[y*8+x]=colors[(x>=4)+2*(y>=4)];
    }
    CHECK(rasterizer_bitmap_new(white) && rasterizer_bitmap_new(checker));
    rasterizer_bitmap_changed(white); rasterizer_bitmap_changed(checker);
    parameters.map[0]=white;
    CHECK(n3ds_engine_screen_quad_supported(&parameters));
    parameters.map[1]=white; CHECK(!n3ds_engine_screen_quad_supported(&parameters)); parameters.map[1]=NULL;
    parameters.map[1]=white;parameters.map0_to_1_blend_function=1;CHECK(n3ds_engine_screen_quad_supported(&parameters));parameters.map[1]=NULL;parameters.map0_to_1_blend_function=0;
    /* Meter packet correctness has a separate native GPU test. */
    parameters.doing_plasma_effect=TRUE; CHECK(!n3ds_engine_screen_quad_supported(&parameters));
    parameters.map[1]=parameters.map[2]=white;parameters.map0_to_1_blend_function=5;
    CHECK(n3ds_engine_screen_quad_supported(&parameters));
    parameters.map1_to_2_blend_function=1;CHECK(!n3ds_engine_screen_quad_supported(&parameters));
    parameters.map[1]=parameters.map[2]=NULL;parameters.map0_to_1_blend_function=parameters.map1_to_2_blend_function=0;
    parameters.doing_plasma_effect=FALSE;
    window.camera.forward.i=1; window.camera.up.k=1; window.camera.vertical_field_of_view=1;
    window.camera.z_near=.05f; window.camera.z_far=100;
    window.camera.viewport_bounds.x1=400; window.camera.viewport_bounds.y1=240;
    window.camera.window_bounds=window.camera.viewport_bounds;
    for(unsigned int f=0;f<45;++f) {
        rasterizer_frame_begin(&frame); rasterizer_windows_begin(); rasterizer_window_begin(&window);
        memset(&parameters,0,sizeof(parameters)); parameters.map[0]=white; parameters.point_sampled=TRUE;
        parameters.map_scale[0].i=parameters.map_scale[0].j=parameters.map_texture_scale[0].i=parameters.map_texture_scale[0].j=1;
        parameters.map_tint[0]=&tint; parameters.map_fade[0]=&fade;
        for(unsigned int mode=0;mode<8;++mode) {
            parameters.framebuffer_blend_function=mode;
            rectangle(vertices,12+(mode%4)*96,12+(mode/4)*48,80,32,1,0x80604020);
            rasterizer_psuedo_dynamic_screen_quad_draw(&parameters,vertices);
            CHECK(!n3ds_engine_text_active());
        }
        parameters.map[0]=checker; parameters.map_tint[0]=NULL; parameters.map_fade[0]=NULL;
        parameters.framebuffer_blend_function=0; parameters.map_wrapped[0]=TRUE; parameters.offset=&offset;
        rectangle(vertices,12,112,80,80,2,0xffffffff);
        rasterizer_psuedo_dynamic_screen_quad_draw(&parameters,vertices);
        parameters.map_wrapped[0]=FALSE; parameters.offset=NULL; parameters.map_anchor_screen[0]=TRUE;
        parameters.map_texture_scale[0].i=parameters.map_texture_scale[0].j=1.f/80.f; parameters.map_offset[0]=&uv_offset;
        rectangle(vertices,116,114,80,80,0,0xffffffff);
        rasterizer_psuedo_dynamic_screen_quad_draw(&parameters,vertices);
        boolean enabled=rasterizer_debug_options.dynamic_screen_geometry;
        rasterizer_debug_options.dynamic_screen_geometry=FALSE;
        rectangle(vertices,216,114,80,80,0,0xffffffff);
        rasterizer_psuedo_dynamic_screen_quad_draw(&parameters,vertices);
        rasterizer_debug_options.dynamic_screen_geometry=enabled;
        rasterizer_window_end(); rasterizer_windows_end(); rasterizer_frame_end(); rasterizer_present(NULL,NULL);
    }
    CHECK(!n3ds_gpu_screen_quad_capture());
    bitmap_delete(white); bitmap_delete(checker);
    CHECK(n3ds_dynamic_bitmaps_collect() && n3ds_dynamic_bitmap_count()==prior);
    n3ds_log("PASS: native ordinary screen quads exercise eight framebuffer blends, tint/fade/vertex alpha, offset, wrapping, screen anchoring and debug disable; HUD meters and menu plasma have separate coverage; other multi-map modes rejected");
    return 0;
}
