#include "cseries.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "scenario/scenario_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_models.h"
#include "shaders/shader_definitions.h"
#include "engine_models.h"
#include "engine_transparent.h"
#include "engine_dynamic_geometry.h"
#include "engine_cache.h"
#include "cache_reader.h"

void n3ds_log(const char *message);
void _rasterizer_models_begin(boolean skip_obscurer_test);
void _rasterizer_models_end(void);

/* Private fixture views match models.c. No replacement gameplay object type. */
struct fixture_shader { struct tag_reference shader; short permutation; word pad; long unused[3]; };
struct fixture_geometry { byte reserved[0x24]; struct tag_block parts; };
struct fixture_part {
    unsigned long flags; short shader; char previous,next;
    short primary,secondary; real primary_weight,secondary_weight; real_point3d centroid;
    struct tag_block uncompressed,compressed,triangles;
    struct triangle_buffer triangle_buffer; struct vertex_buffer vertex_buffer;
};
struct fixture_scenery { struct scenario_object_datum object; struct scenario_object_permutation permutation; };
_Static_assert(sizeof(struct fixture_scenery)==72,"Cached scenery stride");
_Static_assert(sizeof(struct fixture_part)==104,"Model part stride");
_Static_assert(sizeof(struct fixture_shader)==32,"Model shader stride");
static struct { struct model *model; real_matrix4x3 matrix; } fixtures[2];
static unsigned int fixture_count,fixture_frame;

/* Focused world bootstrap: the original object constructor owns these poses.
 * Still a limited two-model renderer, not render_objects or a game tick. */
int halo_engine_model_fixture_prepare_objects(const long *handles, unsigned int count)
{
    if(count!=2) return 1;
    fixture_count=fixture_frame=0;
    for(unsigned int i=0;i<count;++i) {
        struct object_datum *object=object_get(handles[i]);
        struct object_definition *definition=object_definition_get(object->definition_index);
        struct model *model=model_definition_get(definition->object.model.index);
        struct model_node *node=TAG_BLOCK_GET_ELEMENT(&model->nodes,0,struct model_node);
        assert(model->nodes.count==1 && node->parent_node_index==NONE);
        matrix4x3_multiply(object_get_node_matrix(handles[i],0),&node->runtime_default_inverse_matrix,&fixtures[i].matrix);
        fixtures[i].model=model; ++fixture_count;
    }
    n3ds_log("World model pass: two original object instances supply their computed node matrices; no game ticks");
    return 0;
}

int halo_engine_model_tests(void)
{
    struct model_vertex_compressed packed={0}; struct model_vertex_uncompressed vertex={0};
    struct native_render_vertex output,sentinel;
    real_matrix4x3 bones[2]; struct rasterizer_model_skinning skin={bones,2,0};
    real_vector2d scale={2,3};
    real_point3d origins[2]={{10,0,0},{0,20,0}};
    real_vector3d forward={0,1,0},up={0,0,1};
    unsigned short strip[]={0,1,2,2,3,3,4,5},triangles[18],expected[]={0,1,2,4,3,5};
#define CHECK(x) do { if (!(x)) { n3ds_log("MODEL ENGINE FAIL: " #x); return 1; } } while (0)
    CHECK(n3ds_model_triangle_list(1,strip,6,6,triangles,18)==6);
    CHECK(!memcmp(triangles,expected,sizeof(expected)));
    memset(triangles,0x55,sizeof(triangles)); strip[7]=6;
    CHECK(n3ds_model_triangle_list(1,strip,6,6,triangles,18)==NONE && triangles[0]==0x5555);
    strip[7]=5;
    CHECK(n3ds_model_triangle_list(1,strip,6,6,triangles,17)==NONE && triangles[0]==0x5555);
    CHECK(n3ds_model_triangle_list(0,expected,2,6,triangles,18)==6 && !memcmp(triangles,expected,sizeof(expected)));
    packed.nodes[0]=0; packed.nodes[1]=253; packed.node_weight=32767;
    CHECK(n3ds_model_decode_vertex(&packed,&vertex) && vertex.nodes[1]==NONE && vertex.node_weights[0]==1.f);
    packed.node_weight=16384; packed.nodes[1]=3;
    CHECK(n3ds_model_decode_vertex(&packed,&vertex) && fabsf(vertex.node_weights[0]-.5f)<.0001f);
    packed.nodes[1]=1; CHECK(!n3ds_model_decode_vertex(&packed,&vertex));
    matrix4x3_from_point_and_vectors(&bones[0],origins,global_forward3d,global_up3d);
    matrix4x3_from_point_and_vectors(&bones[1],origins+1,&forward,&up);
    vertex.position=(real_point3d){1,2,3}; vertex.normal=(real_vector3d){1,0,0};
    vertex.texcoord=(real_point2d){.25f,.5f}; vertex.nodes[0]=0; vertex.nodes[1]=1;
    vertex.node_weights[0]=.25f; vertex.node_weights[1]=.75f;
    CHECK(n3ds_model_skin_vertex(&vertex,&skin,&scale,&output));
    CHECK(fabsf(output.position[0]-1.25f)<.0001f && fabsf(output.position[1]-16.25f)<.0001f && output.position[2]==3.f);
    CHECK(output.uv[0]==.5f && output.uv[1]==1.5f);
    sentinel=output; vertex.nodes[1]=2;
    CHECK(!n3ds_model_skin_vertex(&vertex,&skin,&scale,&output) && !memcmp(&output,&sentinel,sizeof(output)));
    vertex.nodes[1]=NONE; vertex.node_weights[0]=1; vertex.node_weights[1]=0;
    CHECK(n3ds_model_skin_vertex(&vertex,&skin,&scale,&output) && output.position[0]==11.f);
    vertex.node_weights[0]=.5f; CHECK(!n3ds_model_skin_vertex(&vertex,&skin,&scale,&output));
    CHECK(n3ds_model_decode_cache_tests());
    n3ds_log("PASS: model stream cache: 1024 decoded vertices, source mutation, invalid inputs, hits and eviction");
    n3ds_log("PASS: model strip parity, invalid indices, retail packed weights, weighted bone transforms and unused bone sentinel");
    return 0;
#undef CHECK
}

static void *checked_block(const struct tag_block *block, unsigned int stride, unsigned int maximum)
{
    assert(block->count>=0 && (unsigned long)block->count<=maximum);
    assert(!block->count || (block->address && n3ds_cache_resolve(n3ds_engine_cache_view(0),block->address,block->count*stride)==block->address));
    return block->address;
}
int halo_engine_model_fixture_prepare(void *scenario_pointer, const float *position)
{
    struct scenario *scenario=scenario_pointer;
    struct fixture_scenery *scenery=checked_block(&scenario->scenery,sizeof(*scenery),2048);
    struct scenario_object_palette_entry *palette=checked_block(&scenario->scenery_palette,sizeof(*palette),256);
    long i; fixture_count=fixture_frame=0;
    for (i=0;i<scenario->scenery.count;++i) {
        struct scenario_object_datum *placement=&scenery[i].object;
        struct object_definition *object; struct model *model; struct model_node *node;
        real_matrix4x3 pose; real_vector3d forward,up; real distance=0; int j;
        if (placement->palette_entry_index==NONE || (placement->placement_flags&1)) continue;
        assert(placement->palette_entry_index>=0 && placement->palette_entry_index<scenario->scenery_palette.count);
        long object_index=palette[placement->palette_entry_index].reference.index;
        if (strcmp(tag_get_name(object_index),"levels\\a10\\devices\\h computer bank\\h computer bank")) continue;
        for (j=0;j<3;++j) distance+=(placement->position.n[j]-position[j])*(placement->position.n[j]-position[j]);
        if (distance>36) continue;
        assert(fixture_count<2);
        object=object_definition_get(object_index); model=model_definition_get(object->object.model.index);
        node=checked_block(&model->nodes,sizeof(*node),64);
        /* This chosen real model has exactly one root, not an unchecked graph. */
        assert(model->nodes.count==1 && node->parent_node_index==NONE && node->first_child_node_index==NONE && node->next_sibling_node_index==NONE);
        vectors3d_from_euler_angles3d(&forward,&up,&placement->rotation);
        model_get_node_matrices(model,&pose,&placement->position,&forward,&up);
        matrix4x3_multiply(&pose,&node->runtime_default_inverse_matrix,&fixtures[fixture_count].matrix);
        fixtures[fixture_count++].model=model;
    }
    if (fixture_count!=2) return 1;
    n3ds_log("Model fixture: 2 actual a10 cryobay computer placements, original default poses; no object simulation");
    return 0;
}
void halo_engine_model_fixture_draw(void)
{
    unsigned int f; long batches=0,vertices=0,triangle_count=0;
    /* The public group wrapper also roots world collision obscurer detection.
     * That world service is not bootstrapped by this isolated renderer test. */
    _rasterizer_models_begin(TRUE);
    for (f=0;f<fixture_count;++f) {
        struct model *model=fixtures[f].model;
        struct model_region *region=checked_block(&model->regions,sizeof(*region),32);
        struct model_region_permutation *permutation;
        struct fixture_geometry *geometries=checked_block(&model->geometries,sizeof(*geometries),256);
        struct fixture_shader *shaders=checked_block(&model->shaders,sizeof(*shaders),32);
        struct fixture_part *parts; struct rasterizer_model_begin_parameters begin={0};
        long g,p;
        assert(model->regions.count==1);
        permutation=checked_block(&region->permutations,sizeof(*permutation),32);
        assert(region->permutations.count==1);
        g=permutation->geometry_indices[4]; assert(g>=0 && g<model->geometries.count);
        parts=checked_block(&geometries[g].parts,sizeof(*parts),32);
        begin.skinning.node_matrices=&fixtures[f].matrix; begin.skinning.node_matrix_count=1;
        begin.base_map_scale=model->base_map_scale;
        rasterizer_model_begin(&begin,FALSE);
        for (p=0;p<geometries[g].parts.count;++p) {
            struct fixture_part *part=parts+p; struct fixture_shader *material;
            struct shader *shader;
            assert(!part->flags && part->shader>=0 && part->shader<model->shaders.count);
            assert(part->triangle_buffer.type==1 && part->triangle_buffer.count>0 && part->triangle_buffer.count<=65535);
            assert(part->vertex_buffer.type==5 && part->vertex_buffer.count>0 && part->vertex_buffer.count<=65535);
            assert(n3ds_cache_resolve(n3ds_engine_cache_view(0),part->triangle_buffer.base_address,(part->triangle_buffer.count+2)*2)==part->triangle_buffer.base_address);
            assert(n3ds_cache_resolve(n3ds_engine_cache_view(0),part->vertex_buffer.base_address,part->vertex_buffer.count*32)==part->vertex_buffer.base_address);
            material=shaders+part->shader; shader=tag_get('shdr',material->shader.index);
            assert(shader->base.type==3 || shader->base.type==4);
            if (fixture_frame&1) {
                unsigned short converted[1024]; long indices,th,vh; void *memory;
                indices=n3ds_model_triangle_list(part->triangle_buffer.type,part->triangle_buffer.base_address,part->triangle_buffer.count,part->vertex_buffer.count,converted,1024);
                assert(indices>0 && part->vertex_buffer.type==5);
                th=rasterizer_dynamic_triangles_new(indices/3); vh=rasterizer_dynamic_vertices_new(5,part->vertex_buffer.count);
                assert(th!=NONE && vh!=NONE);
                memory=rasterizer_dynamic_triangles_lock(th); memcpy(memory,converted,indices*2); rasterizer_dynamic_triangles_unlock(th);
                memory=rasterizer_dynamic_vertices_lock(vh); memcpy(memory,part->vertex_buffer.base_address,part->vertex_buffer.count*32); rasterizer_dynamic_vertices_unlock(vh);
                rasterizer_model_draw(shader,material->permutation,NULL,th,indices/3,NULL,vh);
                /* A later object/effect can overwrite CPU staging after drawing. */
                memory=rasterizer_dynamic_vertices_lock(vh); memset(memory,0xa5,part->vertex_buffer.count*32); rasterizer_dynamic_vertices_unlock(vh);
                memory=rasterizer_dynamic_triangles_lock(th); memset(memory,0,indices*2); rasterizer_dynamic_triangles_unlock(th);
                rasterizer_dynamic_vertices_delete(vh); rasterizer_dynamic_triangles_delete(th);
            } else rasterizer_model_transparent_geometry_submit(shader,material->permutation,&part->triangle_buffer,NONE,part->triangle_buffer.count,&part->vertex_buffer,NONE,&begin.centroid,NULL);
            ++batches; vertices+=part->vertex_buffer.count; triangle_count+=part->triangle_buffer.count;
        }
        rasterizer_model_end();
    }
    _rasterizer_models_end();
    if(!(fixture_frame&1)) n3ds_transparent_draw_queued();
    if (!fixture_frame) {
        char message[160]; snprintf(message,sizeof(message),"Native model pass: %ld batches, %ld skinned vertices, %ld strip triangles; alternating static and poisoned dynamic staging",batches,vertices,triangle_count); n3ds_log(message);
    }
    ++fixture_frame;
}
