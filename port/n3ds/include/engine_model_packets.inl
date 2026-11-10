/* New3DS integration additions by Kevin Boggs. */
/* Immutable opaque object submissions. Only geometry owned by the current map
 * (or the map-lifetime merged-mesh cache) may be borrowed. Poses, colours and
 * function values and material view inputs are owned by the packet. This first
 * stage replays on the GPU owner thread inside the SAME view. It deliberately
 * does not claim that particles, HUD, texture residency or map transitions are
 * safe to overlap with the next simulation tick yet. */
enum { MODEL_PACKET_DRAWS = 512, MODEL_PACKET_BYTES = 192 * 1024 };
struct model_packet_pose {
    struct rasterizer_model_begin_parameters parameters;
    struct native_model_view view;
    real_rgb_color colors[4];
    real values[4];
    real_rgb_color modifier_colors[4];
    real modifier_values[4];
    real_matrix4x3 nodes[];
};
struct model_packet_draw {
    struct model_packet_pose *pose;
    struct shader *shader;
    const struct triangle_buffer *triangles;
    const struct vertex_buffer *vertices;
    const struct merged_model *merged;
    long triangle_count;
    short permutation;
};
static struct model_packet_draw model_packet_draws[MODEL_PACKET_DRAWS];
static struct model_packet_pose *model_packet_poses[MODEL_PACKET_DRAWS];
static unsigned int model_packet_poses_count;
static byte *model_packet_storage;
static unsigned int model_packet_used, model_packet_count, model_packet_capacity;
static unsigned int model_packet_submitted, model_packet_flushes, model_packet_peak;
static unsigned int model_packet_last_sequence;
static struct model_packet_pose *model_packet_last_pose;
static int model_packet_allocation_attempted;

/* Kevin Boggs: model_packet_rows */
static float (*model_packet_rows(struct model_packet_pose *pose))[12]
{
    return (float (*)[12])(pose->nodes+pose->parameters.skinning.node_matrix_count);
}

/* Kevin Boggs: model_packet_prepare_range */
static int model_packet_prepare_range(void *context,unsigned int begin,unsigned int end)
{
    struct model_packet_pose *const *poses=context;
    for(unsigned int i=begin;i<end;++i) {
        struct model_packet_pose *pose=poses[i];
        float (*rows)[12]=model_packet_rows(pose);
        for(int node=0;node<pose->parameters.skinning.node_matrix_count;++node) {
            const real_matrix4x3 *matrix=pose->nodes+node;
            for(int axis=0;axis<3;++axis) {
                rows[node][axis*4]=matrix->forward.n[axis]*matrix->scale;
                rows[node][axis*4+1]=matrix->left.n[axis]*matrix->scale;
                rows[node][axis*4+2]=matrix->up.n[axis]*matrix->scale;
                rows[node][axis*4+3]=matrix->position.n[axis];
            }
        }
    }
    return 1;
}

/* Kevin Boggs: model_packet_copy_animation */
static void model_packet_copy_animation(struct render_animation *out,
    const struct render_animation *source,real_rgb_color colors[4],real values[4])
{
    *out=*source;
    if(source->colors) {
        memcpy(colors,source->colors,4*sizeof(*colors));out->colors=colors;
    }
    if(source->values) {
        memcpy(values,source->values,4*sizeof(*values));out->values=values;
    }
}

/* Kevin Boggs: model_packet_copy_pose */
static struct model_packet_pose *model_packet_copy_pose(void *memory,
    const struct rasterizer_model_begin_parameters *source,const struct native_model_view *view)
{
    struct model_packet_pose *pose=memory;
    pose->parameters=*source;
    pose->view=*view;
    memcpy(pose->nodes,source->skinning.node_matrices,
        source->skinning.node_matrix_count*sizeof(*pose->nodes));
    pose->parameters.skinning.node_matrices=pose->nodes;
    model_packet_copy_animation(&pose->parameters.animation,&source->animation,pose->colors,pose->values);
    model_packet_copy_animation(&pose->parameters.effect.modifier_animation,
        &source->effect.modifier_animation,pose->modifier_colors,pose->modifier_values);
    return pose;
}

/* Kevin Boggs: n3ds_model_packets_flush */
void n3ds_model_packets_flush(void)
{
    if(!model_packet_count)return;
    /* Keep the producer's current packet intact when capacity or an ordered
     * material forces a mid-object drain. No GPU calls from a worker. */
    struct rasterizer_model_begin_parameters saved=parameters;
    struct native_model_view saved_view=model_view;
    const struct merged_model *saved_merged=active_merged;
    int saved_active=model_active;
    model_active=1;
#ifdef HALO_N3DS_PACKET_PREPARE
    /* Immutable input; disjoint row outputs. No engine reads, allocations,
     * texture operations or Citro3D calls occur on the worker. */
    extern int n3ds_input_platform_original_model(void);
    if(n3ds_input_platform_original_model())
        model_packet_prepare_range(model_packet_poses,0,model_packet_poses_count);
    else n3ds_parallel_range(model_packet_prepare_range,model_packet_poses,
        model_packet_poses_count,8,3);
#endif
    const struct model_packet_pose *previous=NULL;
    for(unsigned int i=0;i<model_packet_count;++i) {
        const struct model_packet_draw *draw=model_packet_draws+i;
        if(draw->pose!=previous) {
            parameters=draw->pose->parameters;
            model_view=draw->pose->view;
            model_lighting_prepare();
            memset(prepared_bone_valid,0,sizeof(prepared_bone_valid));
            prepared_bone_source=NULL;packet_material_count=0;
            previous=draw->pose;
#ifdef HALO_N3DS_PACKET_PREPARE
            packet_prepared_rows=model_packet_rows(draw->pose);
#endif
        }
        active_merged=draw->merged;
        model_draw_immediate(draw->shader,draw->permutation,draw->triangles,
            NONE,draw->triangle_count,draw->vertices,NONE);
    }
    parameters=saved;model_view=saved_view;active_merged=saved_merged;model_active=saved_active;
    packet_prepared_rows=NULL;
    model_lighting_prepare();
    memset(prepared_bone_valid,0,sizeof(prepared_bone_valid));
    prepared_bone_source=NULL;packet_material_count=0;
    model_packet_peak=MAX(model_packet_peak,model_packet_used);
    model_packet_submitted+=model_packet_count;++model_packet_flushes;
    model_packet_used=model_packet_count=model_packet_poses_count=0;model_packet_last_pose=NULL;
}

/* Kevin Boggs: model_packets_release */
static void model_packets_release(void)
{
    /* A map boundary is a lifetime fence, never permission to replay stale
     * commands after the view or textures have been destroyed. */
    assert(!model_packet_count);
    free(model_packet_storage);model_packet_storage=NULL;
    model_packet_used=model_packet_capacity=0;model_packet_last_pose=NULL;
    model_packet_allocation_attempted=0;
}

/* Kevin Boggs: model_packet_enqueue */
static int model_packet_enqueue(struct shader *shader,short permutation,
    const struct triangle_buffer *triangles,long count,const struct vertex_buffer *vertices)
{
    if(!model_packet_allocation_attempted) {
        extern int n3ds_input_platform_original_model(void);
        model_packet_allocation_attempted=1;
        model_packet_capacity=n3ds_input_platform_original_model()?48*1024:MODEL_PACKET_BYTES;
        model_packet_storage=(malloc)(model_packet_capacity);
    }
    if(!model_packet_storage)return 0;
    unsigned int bytes=sizeof(struct model_packet_pose)+parameters.skinning.node_matrix_count*sizeof(real_matrix4x3);
#ifdef HALO_N3DS_PACKET_PREPARE
    bytes+=parameters.skinning.node_matrix_count*12*sizeof(float);
#endif
    if(model_packet_count==MODEL_PACKET_DRAWS ||
        ((!model_packet_last_pose || model_packet_last_sequence!=model_packet_sequence) &&
         bytes>model_packet_capacity-model_packet_used))n3ds_model_packets_flush();
    if(!model_packet_last_pose || model_packet_last_sequence!=model_packet_sequence) {
        if(bytes>model_packet_capacity-model_packet_used)return 0;
        model_packet_last_pose=model_packet_copy_pose(model_packet_storage+model_packet_used,&parameters,&model_view);
        model_packet_poses[model_packet_poses_count++]=model_packet_last_pose;
        model_packet_used+=bytes;model_packet_last_sequence=model_packet_sequence;
    }
    model_packet_draws[model_packet_count++]=(struct model_packet_draw){
        .pose=model_packet_last_pose,.shader=shader,.triangles=triangles,.vertices=vertices,
        .merged=active_merged,.triangle_count=count,.permutation=permutation};
    return 1;
}

/* Kevin Boggs: n3ds_model_packets_report */
void n3ds_model_packets_report(void)
{
    char message[192];
    snprintf(message,sizeof(message),"RESULT: model packets draws=%u drains=%u peak_bytes=%u pending=%u",
        model_packet_submitted,model_packet_flushes,model_packet_peak,model_packet_count);
    n3ds_log(message);
}

/* Kevin Boggs: n3ds_model_packets_tests */
int n3ds_model_packets_tests(void)
{
    /* Mutate all live inputs after capture. Owned values must survive, including
     * the maximum-size pose, and NULL animation arrays must remain NULL. */
    union { double alignment; byte bytes[sizeof(struct model_packet_pose)+64*(sizeof(real_matrix4x3)+12*sizeof(float))]; } storage;
    real_matrix4x3 nodes[64];real_rgb_color colors[4];real values[4];
    memset(nodes,0x35,sizeof(nodes));memset(colors,0x27,sizeof(colors));memset(values,0x19,sizeof(values));
    struct rasterizer_model_begin_parameters input={0};
    struct native_model_view view={.game_time=12.5f,.ambient_override=.25f,
        .camera_position={1,2,3},.camera_forward={0,1,0},.reduced_materials=1,
        .cinematic=1,.fog={.atmospheric_maximum_density=.5f},
        .world_to_view={.scale=1,.position={4,5,6}},.projection={{1,2,3,4}}};
    struct native_model_view expected_view=view;
    input.skinning.node_matrices=nodes;input.skinning.node_matrix_count=64;
    input.animation.colors=colors;input.animation.values=values;
    input.effect.modifier_animation=input.animation;
    struct model_packet_pose *pose=model_packet_copy_pose(storage.bytes,&input,&view);
    memset(&view,0,sizeof(view));
    if(memcmp(&pose->view,&expected_view,sizeof(expected_view)))return 0;
    memset(nodes,0,sizeof(nodes));memset(colors,0,sizeof(colors));memset(values,0,sizeof(values));
    for(unsigned int i=0;i<sizeof(nodes);++i)if(((byte *)pose->nodes)[i]!=0x35)return 0;
    for(unsigned int i=0;i<sizeof(colors);++i)if(((byte *)pose->colors)[i]!=0x27)return 0;
    for(unsigned int i=0;i<sizeof(values);++i)if(((byte *)pose->values)[i]!=0x19)return 0;
    for(unsigned int i=0;i<sizeof(colors);++i)if(((byte *)pose->modifier_colors)[i]!=0x27)return 0;
    for(unsigned int i=0;i<sizeof(values);++i)if(((byte *)pose->modifier_values)[i]!=0x19)return 0;
    if(pose->parameters.skinning.node_matrices!=pose->nodes ||
        pose->parameters.animation.colors!=pose->colors || pose->parameters.animation.values!=pose->values ||
        pose->parameters.effect.modifier_animation.colors!=pose->modifier_colors ||
        pose->parameters.effect.modifier_animation.values!=pose->modifier_values)return 0;
    input.animation.colors=NULL;input.animation.values=NULL;input.skinning.node_matrix_count=1;
    input.effect.modifier_animation=input.animation;
    pose=model_packet_copy_pose(storage.bytes,&input,&view);
    if(pose->parameters.animation.colors || pose->parameters.animation.values ||
        pose->parameters.effect.modifier_animation.colors || pose->parameters.effect.modifier_animation.values)return 0;
    nodes[0]=(real_matrix4x3){.scale=2,.forward={1,2,3},.left={4,5,6},.up={7,8,9},.position={10,11,12}};
    pose=model_packet_copy_pose(storage.bytes,&input,&view);
    struct model_packet_pose *jobs[]={pose};
    n3ds_parallel_range(model_packet_prepare_range,jobs,1,1,3);
    const float expected[]={2,8,14,10,4,10,16,11,6,12,18,12};
    if(memcmp(model_packet_rows(pose),expected,sizeof(expected)))return 0;
    n3ds_log("RESULT: PASS immutable model pose, colours, function values, camera, time and quality inputs");return 1;
}

/* Kevin Boggs: _rasterizer_model_draw */
void _rasterizer_model_draw(struct shader *shader,short permutation,
    const struct triangle_buffer *triangles,long dynamic_triangles,long count,
    const struct vertex_buffer *vertices,long dynamic_vertices)
{
#ifdef HALO_N3DS_MODEL_PACKETS
    /* Preserve transparent, sky, view-space, stencil, effect and nested-widget
     * ordering. Dynamic geometry has a shorter lifetime and remains immediate. */
    if(scenery_batch_allowed && !first_person_active && !depth_only && !parameters.effect.type &&
        !(parameters.geometry_flags&((1u<<3)|(1u<<4)|(1u<<7))) &&
        dynamic_triangles==NONE && dynamic_vertices==NONE &&
        (shader->base.type==3 || shader->base.type==4) && count>0 &&
        model_packet_enqueue(shader,permutation,triangles,count,vertices))return;
#endif
    n3ds_model_packets_flush();
    model_draw_immediate(shader,permutation,triangles,dynamic_triangles,count,vertices,dynamic_vertices);
}
