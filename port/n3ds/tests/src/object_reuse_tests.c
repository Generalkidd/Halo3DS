/* New3DS integration additions by Kevin Boggs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int boolean;
typedef float real;
typedef struct {float x,y,z;} real_point3d;
typedef struct {float i,j,k;} real_vector3d;
#define NONE (-1)
#define TRUE 1
#define FALSE 0
#define TEST_FLAG(v,b) (((v)>>(b))&1)
enum { _object_type_scenery=5, _object_at_rest_bit=0, _object_mirrored_bit=1, _object_mask_item=1<<2 };
struct object_datum {
    long definition_index;
    struct {long parent_object_index;int type,flags;struct {int interpolation_frame_count;} animation;
        real_point3d position;real_vector3d forward,up;real scale;} object;
};
struct object_definition { struct {struct {int index;} animation_graph;} object; };
static unsigned int generation;
/* Kevin Boggs: n3ds_engine_cache_generation */
unsigned int n3ds_engine_cache_generation(void) {return generation;}
/* Kevin Boggs: n3ds_log */
void n3ds_log(const char *s) {(void)s;}
#include "engine_object_reuse.inl"
/* Kevin Boggs: main */
int main(void)
{
    struct object_datum object={0};struct object_definition definition={0};
    object.object.parent_object_index=NONE;object.object.type=_object_type_scenery;
    definition.object.animation_graph.index=NONE;
    assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    assert(native_stationary_pose_reuse(7,&object,&definition,&object));
    ++object.object.position.x;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    ++object.object.scale;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    ++object.object.up.k;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    object.object.flags|=1<<_object_mirrored_bit;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    assert(!native_stationary_pose_reuse(135,&object,&definition,&object));
    assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    ++generation;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    object.object.parent_object_index=1;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    object.object.parent_object_index=NONE;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    definition.object.animation_graph.index=0;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    definition.object.animation_graph.index=NONE;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    object.object.type=2;object.object.flags=0;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    object.object.flags=1;assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    assert(native_stationary_pose_reuse(7,&object,&definition,&object));
    n3ds_stationary_pose_clear();assert(!native_stationary_pose_reuse(7,&object,&definition,&object));
    puts("PASS: stationary pose reuse, motion, scale, attachment, animation, object identity and map invalidation");
    return 0;
}
