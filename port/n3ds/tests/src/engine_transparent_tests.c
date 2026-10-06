#include "cseries.h"
#include "engine_transparent.h"
#include "engine_renderer.h"
#include "shaders/shader_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_models.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "rasterizer/rasterizer_transparent_geometry.h"
extern struct rasterizer_window_begin_parameters global_window_parameters;
void _rasterizer_models_begin(boolean);
void _rasterizer_models_end(void);
void n3ds_log(const char *);

int halo_engine_transparent_tests(void)
{
    struct { struct shader shader; word flags; } material={0};
    struct shader_effect_definition effect={0};
    struct rasterizer_model_begin_parameters begin={0};
    struct transparent_geometry_group *first, *second, *group;
    struct render_sort_filth sort={0};
    real_matrix4x3 nodes[64];
    real_point3d near_point=global_window_parameters.camera.position,far_point=near_point;
    long i, count;
    boolean previous;
#define CHECK(e) do { if(!(e)) { n3ds_log("TRANSPARENT QUEUE FAIL: " #e); return 1; } } while(0)
    memset(nodes,0x3e,sizeof(nodes));
    for(i=0;i<3;++i) {
        near_point.n[i]+=global_window_parameters.camera.forward.n[i]*2.f;
        far_point.n[i]+=global_window_parameters.camera.forward.n[i]*7.f;
    }
    material.shader.base.type=4;
    begin.skinning.node_matrices=nodes; begin.skinning.node_matrix_count=2;
    begin.base_map_scale.i=2; begin.base_map_scale.j=3; begin.unique_identifier=123;
    memset(&begin.lighting,0x3e,sizeof(begin.lighting));
    memset(&begin.animation,0x3d,sizeof(begin.animation));
    _rasterizer_models_begin(TRUE);
    rasterizer_model_begin(&begin,FALSE);
    first=_rasterizer_model_transparent_geometry_submit(&material.shader,2,NULL,NONE,0,NULL,NONE,&near_point,&sort);
    CHECK(first && sort.group_index==0 && sort.previous_group_presorted_index_reference==&first->previous_group_presorted_index);
    CHECK(first->object_index==123 && first->node_matrix_count==2 && first->model_base_map_scale.i==2);
    CHECK(first->node_matrices!=nodes && !memcmp(first->node_matrices,nodes,2*sizeof(*nodes)));
    CHECK(!memcmp(first->lighting,&begin.lighting,sizeof(begin.lighting)) && !memcmp(first->animation,&begin.animation,sizeof(begin.animation)));
    second=_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&far_point,NULL);
    CHECK(second && first->node_matrices==second->node_matrices && first->lighting==second->lighting);
    rasterizer_model_end();
    memset(nodes,0x44,sizeof(nodes)); begin.unique_identifier=456;
    memset(&begin.lighting,0x42,sizeof(begin.lighting));
    rasterizer_model_begin(&begin,FALSE);
    group=_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,NULL);
    CHECK(group && group->object_index==456 && group->node_matrices!=first->node_matrices);
    CHECK(((const byte *)first->node_matrices)[0]==0x3e && ((const byte *)first->lighting)[0]==0x3e);
    CHECK(((const byte *)group->node_matrices)[0]==0x44 && ((const byte *)group->lighting)[0]==0x42);
    rasterizer_model_end(); _rasterizer_models_end();
    rasterizer_sort_external(); CHECK(rasterizer_transparent_geometry_first_group()==second);
    count=0; for(group=rasterizer_transparent_geometry_first_group();group;group=rasterizer_transparent_geometry_next_group(group)) ++count;
    CHECK(count==3 && fabsf(second->z_sort+7)<.001f && fabsf(first->z_sort+2)<.001f);
    CHECK(rasterizer_transparent_geometry_get_group_pending_status(first));
    rasterizer_transparent_geometry_set_group_pending_status(first,FALSE);
    CHECK(!rasterizer_transparent_geometry_get_group_pending_status(first));

    n3ds_transparent_begin_view();
    _rasterizer_models_begin(TRUE); rasterizer_model_begin(&begin,FALSE);
    previous=rasterizer_debug_options.model_transparents; rasterizer_debug_options.model_transparents=FALSE;
    CHECK(!_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,&sort));
    CHECK(sort.group_index==NONE && !sort.previous_group_presorted_index_reference);
    rasterizer_debug_options.model_transparents=previous;
    material.flags=8;
    CHECK(!_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,NULL));
    material.flags=0;
    for(i=0;i<384;++i) CHECK(_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,&sort));
    CHECK(sort.group_index==383);
    CHECK(!_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,&sort));
    CHECK(sort.group_index==NONE && !sort.previous_group_presorted_index_reference && !sort.next_group_presorted_index_reference);
    rasterizer_model_end(); _rasterizer_models_end();

    n3ds_transparent_begin_view(); begin.skinning.node_matrix_count=64;
    _rasterizer_models_begin(TRUE);
    for(i=0;i<100;++i) {
        rasterizer_model_begin(&begin,FALSE);
        group=_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,NULL);
        rasterizer_model_end(); if(!group) break;
    }
    CHECK(i==RASTERIZER_MEMORY_POOL_SIZE/(sizeof(begin)+64*sizeof(*nodes)));
    _rasterizer_models_end(); n3ds_transparent_begin_view();
    begin.geometry_flags=2; begin.skinning.node_matrix_count=1;
    _rasterizer_models_begin(TRUE); rasterizer_model_begin(&begin,FALSE);
    CHECK(!_rasterizer_model_transparent_geometry_submit(&material.shader,0,NULL,NONE,0,NULL,NONE,&near_point,&sort));
    CHECK(sort.group_index==NONE);
    rasterizer_model_end(); _rasterizer_models_end();
    n3ds_transparent_begin_view(); rasterizer_sort_external();
    CHECK(!rasterizer_transparent_geometry_first_group());
    effect.shader.base.type=1; effect.flags=1;
    rasterizer_dynamic_unlit_geometry_draw(&effect.shader,NULL,NULL,123,456,17,&far_point,0);
    rasterizer_sort_external(); group=rasterizer_transparent_geometry_first_group();
    CHECK(group && group->shader==&effect.shader && group->dynamic_triangle_buffer_index==123);
    CHECK(group->dynamic_vertex_buffer_index==456 && group->triangle_count==17);
    CHECK(!group->node_matrices && !group->node_matrix_count && !group->lighting && !group->animation);
    CHECK(fabsf(group->z_sort+6.75f)<.001f && group->model_base_map_scale.i==1.f);
    CHECK(group->previous_group_presorted_index==NONE && group->next_group_presorted_index==NONE);
    n3ds_transparent_begin_view();
    n3ds_log("PASS: native model queue copies poses/lighting/animation, preserves sort links and depth order, handles immediate submission and bounds original group/pose pools");
    n3ds_log("PASS: original dynamic unlit submission preserves geometry handles, centroid depth, effect sort bias and null model state on native window");
    return 0;
#undef CHECK
}
