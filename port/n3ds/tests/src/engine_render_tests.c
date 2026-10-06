#include "cseries.h"
#include "tag_groups.h"
#include "scenario.h"
#include "cache_files.h"
#include "scenario_definitions.h"
#include "structure_bsp_definitions.h"
#include "rasterizer/rasterizer_widgets.h"
#include "rasterizer/rasterizer.h"
#include "engine_cache.h"
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_bitmaps.h"
#include "engine_text.h"
#include "engine_widgets.h"
#include "engine_dynamic_geometry.h"
#include "engine_transparent.h"
#include "cache_reader.h"

void n3ds_log(const char *message);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
struct render_test_batch { struct structure_material *material; long triangles; const struct shader *shader; };

int halo_engine_render_tests(void)
{
    struct rasterizer_frame_begin_parameters frame={0};
    struct rasterizer_window_begin_parameters window={0};
    struct render_test_batch batches[256];
    struct scenario *scenario;
    struct scenario_structure_bsp_reference *reference;
    struct player_starting_location *start;
    struct structure_bsp *bsp;
    struct structure_surface *surfaces;
    long map, i, j, b, f, count=0, triangle_count=0, excluded=0;
    long previous_triangles=NONE, previous_vertices=NONE;
    char message[160];
#define CHECK(expr) do { if (!(expr)) { n3ds_log("RENDER ENGINE FAIL: " #expr); return 1; } } while (0)
    n3ds_log("Testing original rasterizer frame and environment APIs");
    CHECK(rasterizer_initialize());
    CHECK(!n3ds_gpu_widget_tests());
    CHECK(n3ds_dynamic_bitmap_count()==1 && n3ds_dynamic_bitmap_bytes()==128*128*4);
    CHECK(!halo_engine_dynamic_geometry_tests());
    CHECK(!halo_engine_model_tests());
    CHECK(rasterizer_dynamic_triangles_new(0)==NONE);
    CHECK(rasterizer_dynamic_triangles_new(0x20001)==NONE);
    map=scenario_tags_load("levels\\a10\\a10"); CHECK(map!=NONE);
    CHECK(!halo_engine_text_fixture_prepare());
    scenario=tag_get('scnr', map);
    CHECK(scenario->players.count>0);
    start=TAG_BLOCK_GET_ELEMENT(&scenario->players, 0, struct player_starting_location);
    CHECK(!halo_engine_model_fixture_prepare(scenario,start->position.n));
    CHECK(start->structure_bsp_reference_index>=0 && start->structure_bsp_reference_index<scenario->structure_bsp_references.count);
    reference=TAG_BLOCK_GET_ELEMENT(&scenario->structure_bsp_references, start->structure_bsp_reference_index, struct scenario_structure_bsp_reference);
    CHECK(scenario_structure_bsp_load(reference));
    bsp=tag_get('sbsp', reference->structure_bsp.index);
    surfaces=bsp->surfaces.address;
    CHECK(n3ds_cache_resolve(n3ds_engine_cache_view(1),surfaces,bsp->surfaces.count*sizeof(*surfaces))==surfaces);
    for (i=0; i<bsp->lightmaps.count; ++i) {
        struct structure_lightmap *lightmap=TAG_BLOCK_GET_ELEMENT(&bsp->lightmaps, i, struct structure_lightmap);
        for (j=0; j<lightmap->materials.count; ++j) {
            struct structure_material *material=TAG_BLOCK_GET_ELEMENT(&lightmap->materials,j,struct structure_material);
            if (!material->surface_count) continue;
            if (!n3ds_cache_tag(n3ds_engine_cache_view(0), material->shader.index, 'senv')) { ++excluded; continue; }
            CHECK(count<256 && material->surface_count>0 && material->first_surface_index>=0);
            CHECK(material->surface_count<=bsp->surfaces.count && material->first_surface_index<=bsp->surfaces.count-material->surface_count);
            batches[count].material=material;
            batches[count].shader=tag_get('senv', material->shader.index);
            triangle_count+=material->surface_count; ++count;
        }
    }
    CHECK(count>0 && triangle_count>0);
    window.camera.position=start->position; window.camera.position.z+=.65f;
    window.camera.forward.i=cosf(start->facing); window.camera.forward.j=sinf(start->facing);
    window.camera.up.k=1.f;
    window.camera.vertical_field_of_view=65.f*3.14159265359f/180.f;
    window.camera.z_near=.05f; window.camera.z_far=500.f;
    window.camera.viewport_bounds.x1=400; window.camera.viewport_bounds.y1=240;
    window.camera.window_bounds=window.camera.viewport_bounds;
    snprintf(message,sizeof(message),"Native opaque pass: %ld batches, %ld triangles, %ld unsupported material batches",count,triangle_count,excluded);
    n3ds_log(message);
    n3ds_log("Engine rendering test: no player/mission simulation");
    for (f=0; f<1800; ++f) {
        frame.game_time_sec=f/30.f; frame.dt=1.f/30.f;
        rasterizer_frame_begin(&frame); rasterizer_windows_begin(); rasterizer_window_begin(&window);
        if(!f) CHECK(!halo_engine_transparent_tests());
        CHECK(!n3ds_dynamic_triangles_get(previous_triangles) && !n3ds_dynamic_vertices_get(previous_vertices));
        CHECK(n3ds_dynamic_geometry_bytes()==0);
        previous_vertices=rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_model_compressed,1);
        CHECK(previous_vertices!=NONE);
        memset(rasterizer_dynamic_vertices_lock(previous_vertices),f&255,32);
        rasterizer_dynamic_vertices_unlock(previous_vertices);
        rasterizer_dynamic_vertices_delete(previous_vertices);
        CHECK(n3ds_dynamic_vertices_get(previous_vertices));
        rasterizer_environment_diffuse_textures_begin();
        for (b=0; b<count; ++b) {
            struct structure_material *material=batches[b].material;
            short *indices;
            batches[b].triangles=rasterizer_dynamic_triangles_new(material->surface_count);
            CHECK(batches[b].triangles!=NONE);
            indices=rasterizer_dynamic_triangles_lock(batches[b].triangles); CHECK(indices);
            memcpy(indices,surfaces+material->first_surface_index,material->surface_count*sizeof(*surfaces));
            rasterizer_dynamic_triangles_unlock(batches[b].triangles);
            /* Alternate full ranges with adjacent subranges, including empty
             * first ranges for one-triangle materials. Output must be identical. */
            if (f&1) {
                long split=material->surface_count/2;
                rasterizer_environment_diffuse_texture_draw(batches[b].shader,material->permutation_index,
                    batches[b].triangles,0,split,&material->vertices);
                rasterizer_environment_diffuse_texture_draw(batches[b].shader,material->permutation_index,
                    batches[b].triangles,split,material->surface_count-split,&material->vertices);
            } else {
                rasterizer_environment_diffuse_texture_draw(batches[b].shader,material->permutation_index,
                    batches[b].triangles,0,material->surface_count,&material->vertices);
            }
            /* The queued GPU draw must own its indices, not this CPU staging
             * memory. Poison it after submission to expose lifetime mistakes. */
            indices=rasterizer_dynamic_triangles_lock(batches[b].triangles);
            memset(indices,0,material->surface_count*sizeof(*surfaces));
            rasterizer_dynamic_triangles_unlock(batches[b].triangles);
            rasterizer_dynamic_triangles_delete(batches[b].triangles);
        }
        previous_triangles=batches[count-1].triangles;
        CHECK(n3ds_dynamic_geometry_bytes()==(unsigned long)triangle_count*6+32);
        rasterizer_environment_diffuse_textures_end();
        /* Transparent diagnostic sprites exercise the original APIs without
         * altering the independent static-scene image reference. GPU vectors
         * above separately verify visible color/alpha/depth behavior. */
        {
            struct environment_prefix {byte fields[0x88];struct tag_reference base_map;};
            const struct environment_prefix *shader=(const void *)batches[count-1].shader;
            real_point2d center={{200,120}};real_vector2d scale={{20,12}};
            real_point3d point=window.camera.position;
            for(unsigned int axis=0;axis<3;++axis) point.n[axis]+=window.camera.forward.n[axis]*2;
            rasterizer_widget_begin(5,0);
            CHECK(!rasterizer_widget_set_texture(0,shader->base_map.index,batches[count-1].material->permutation_index));
            rasterizer_widget_set_tint_factor(.5f);rasterizer_widget_set_zbuffer_enable(FALSE);
            rasterizer_widget_draw_sprite2d(&center,1,&scale,NULL,.25f,0x00ff8800);
            rasterizer_widget_draw_sprite3d(&point,.2f,NULL,30,0x0088ff00);
            rasterizer_widget_end();
        }
        if(f&1) halo_engine_text_fixture_draw();
        halo_engine_model_fixture_draw();
        if(!(f&1)) halo_engine_text_fixture_draw();
        rasterizer_window_end(); rasterizer_windows_end(); rasterizer_frame_end(); rasterizer_present(NULL,NULL);
        if (f%300==299) {
            snprintf(message,sizeof(message),"Native engine submitted %ld frames at %.2f seconds",f+1,(double)n3ds_engine_ticks()/n3ds_engine_tick_frequency());
            n3ds_log(message);
        }
    }
    CHECK(n3ds_engine_geometry_count()==(unsigned long)count);
    CHECK(!n3ds_gpu_screen_test());
    CHECK(n3ds_engine_texture_count()>0 && rasterizer_globals.frame_index==1800);
    scenario_structure_bsp_unload(reference);
    CHECK(n3ds_engine_geometry_count()==0);
    scenario_tags_unload();
    CHECK(n3ds_engine_texture_count()==0);
    CHECK(n3ds_dynamic_bitmap_count()==1); /* Map unload must retain the font atlas. */
    rasterizer_dispose();
    CHECK(!n3ds_dynamic_bitmap_count() && !n3ds_dynamic_bitmap_bytes());
    n3ds_log("PASS: original font atlas survives map unload and is reclaimed at renderer shutdown");
    n3ds_log("PASS: original font glyph cache and native text draws across 1800 scene frames");
    n3ds_log("PASS: alternating text/model pass order preserves world state and displayed pixels");
    n3ds_log("PASS: original 2D and 3D sprite APIs submit 3600 alpha-zero quads across 1800 frames and restore world/text/model state");
    CHECK(!rasterizer_globals.initialized && !n3ds_dynamic_geometry_bytes());
    CHECK(!n3ds_dynamic_triangles_get(previous_triangles) && !n3ds_dynamic_vertices_get(previous_vertices));
    n3ds_log("PASS: 1800 per-view arena resets and submitted-index mutation preserve rendered output");
    n3ds_log("PASS: original model transparent-submit API queues both scenery poses before drawing on 900 frames; base materials preserve the independently checked scene");
    n3ds_log("PASS: native indexed BSP draw preserves full and split triangle ranges over 1800 frames");
    n3ds_log("PASS: original rasterizer APIs presented 1800 frames; geometry and textures released");
    return 0;
#undef CHECK
}
