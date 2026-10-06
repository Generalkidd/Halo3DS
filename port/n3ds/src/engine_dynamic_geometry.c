/* CPU staging arenas for the engine's temporary per-view geometry.
 * GPU draws must copy the referenced data before the next view resets it. */
#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "engine_dynamic_geometry.h"

enum { SLOTS=1024, NATIVE_TRIANGLES=131072, DEBUG_VERTICES=24576 };
struct triangle_slot { struct native_triangle_view view; unsigned int generation; boolean live, locked; };
struct vertex_slot { struct native_vertex_view view; unsigned int generation; boolean live, locked; };
struct vertex_arena { byte *data; unsigned int used, capacity, stride; };
static struct triangle_slot triangles[SLOTS];
static struct vertex_slot vertices[SLOTS];
static struct vertex_arena arenas[NUMBER_OF_RASTERIZER_VERTEX_TYPES];
static unsigned short *triangle_arena;
static unsigned int triangle_used, triangle_slots, vertex_slots;
static boolean initialized;

static unsigned int next_generation(unsigned int previous) { return previous%0xfffff+1; }
static struct triangle_slot *triangle_slot(long handle)
{
    unsigned int index=(unsigned long)handle&1023, generation=(unsigned long)handle>>10;
    if (handle<0 || ((unsigned long)handle&0x40000000) || !generation || index>=triangle_slots || !triangles[index].live || triangles[index].generation!=generation) return NULL;
    return &triangles[index];
}
static struct vertex_slot *vertex_slot(long handle)
{
    unsigned int index=(unsigned long)handle&1023, generation=((unsigned long)handle&0x3fffffff)>>10;
    if (handle<0 || !((unsigned long)handle&0x40000000) || !generation || index>=vertex_slots || !vertices[index].live || vertices[index].generation!=generation) return NULL;
    return &vertices[index];
}
const struct native_triangle_view *n3ds_dynamic_triangles_get(long handle)
{
    struct triangle_slot *slot=triangle_slot(handle);
    return slot && !slot->locked ? &slot->view : NULL;
}
const struct native_vertex_view *n3ds_dynamic_vertices_get(long handle)
{
    struct vertex_slot *slot=vertex_slot(handle);
    return slot && !slot->locked ? &slot->view : NULL;
}
unsigned int n3ds_dynamic_geometry_bytes(void)
{
    unsigned int i, bytes=triangle_used*6;
    for (i=0; i<NUMBER_OF_RASTERIZER_VERTEX_TYPES; ++i) bytes+=arenas[i].used*arenas[i].stride;
    return bytes;
}
void rasterizer_dynamic_geometry_begin(void)
{
    unsigned int i;
    assert(initialized);
    for (i=0; i<triangle_slots; ++i) { assert(!triangles[i].locked); triangles[i].live=FALSE; }
    for (i=0; i<vertex_slots; ++i) { assert(!vertices[i].locked); vertices[i].live=FALSE; }
    triangle_used=triangle_slots=vertex_slots=0;
    for (i=0; i<NUMBER_OF_RASTERIZER_VERTEX_TYPES; ++i) arenas[i].used=0;
}
void rasterizer_dynamic_geometry_end(void)
{
    unsigned int i;
    assert(initialized);
    for (i=0; i<triangle_slots; ++i) assert(!triangles[i].locked);
    for (i=0; i<vertex_slots; ++i) assert(!vertices[i].locked);
    /* Transparent/model passes may still reference this view's buffers. */
}
void rasterizer_dynamic_geometry_dispose(void)
{
    unsigned int i;
    if (initialized) rasterizer_dynamic_geometry_begin();
    free(triangle_arena); triangle_arena=NULL;
    for (i=0; i<NUMBER_OF_RASTERIZER_VERTEX_TYPES; ++i) { free(arenas[i].data); memset(&arenas[i],0,sizeof(arenas[i])); }
    initialized=FALSE;
}
boolean rasterizer_dynamic_geometry_initialize(void)
{
    unsigned int i;
    assert(!initialized);
    triangle_arena=malloc(NATIVE_TRIANGLES*6);
    if (!triangle_arena) return FALSE;
    /* These are the three format groups allocated by the original Xbox owner. */
    arenas[_rasterizer_vertex_type_model_compressed].capacity=RASTERIZER_MAXIMUM_DYNAMIC_MODEL_VERTICES;
    arenas[_rasterizer_vertex_type_dynamic_unlit].capacity=RASTERIZER_MAXIMUM_DYNAMIC_UNLIT_VERTICES;
    arenas[_rasterizer_vertex_type_debug].capacity=DEBUG_VERTICES;
    for (i=0; i<NUMBER_OF_RASTERIZER_VERTEX_TYPES; ++i) if (arenas[i].capacity) {
        arenas[i].stride=rasterizer_geometry_get_vertex_size(i);
        arenas[i].data=malloc(arenas[i].capacity*arenas[i].stride);
        if (!arenas[i].data) { rasterizer_dynamic_geometry_dispose(); return FALSE; }
    }
    initialized=TRUE; rasterizer_dynamic_geometry_begin(); return TRUE;
}
long _rasterizer_dynamic_triangles_new(long count)
{
    struct triangle_slot *slot;
    unsigned int index;
    assert(initialized);
    if (count<=0 || (unsigned long)count>NATIVE_TRIANGLES-triangle_used || triangle_slots==SLOTS) return NONE;
    index=triangle_slots++; slot=&triangles[index];
    slot->view.indices=triangle_arena+triangle_used*3; slot->view.count=count;
    triangle_used+=count; slot->generation=next_generation(slot->generation); slot->live=TRUE; slot->locked=FALSE;
    return (slot->generation<<10)|index;
}
short *_rasterizer_dynamic_triangles_lock(long handle)
{
    struct triangle_slot *slot;
    if (handle==NONE) return NULL;
    slot=triangle_slot(handle); assert(slot && !slot->locked); slot->locked=TRUE;
    return (short *)slot->view.indices;
}
void _rasterizer_dynamic_triangles_unlock(long handle)
{
    struct triangle_slot *slot;
    if (handle==NONE) return;
    slot=triangle_slot(handle); assert(slot && slot->locked); slot->locked=FALSE;
}
void _rasterizer_dynamic_triangles_delete(long handle)
{
    struct triangle_slot *slot;
    if (handle==NONE) return;
    slot=triangle_slot(handle); assert(slot && !slot->locked);
    /* Original delete defers reclamation until the next view. Queued passes
     * may still hold this handle, so keep its data and identity valid. */
}
long _rasterizer_dynamic_vertices_new(short type,long count)
{
    struct vertex_arena *arena;
    struct vertex_slot *slot;
    unsigned int index;
    assert(initialized);
    if (type<0 || type>=NUMBER_OF_RASTERIZER_VERTEX_TYPES || count<=0) return NONE;
    arena=&arenas[type];
    if (!arena->data || (unsigned long)count>arena->capacity-arena->used || vertex_slots==SLOTS) return NONE;
    index=vertex_slots++; slot=&vertices[index];
    slot->view.data=arena->data+arena->used*arena->stride; slot->view.count=count; slot->view.type=type;
    arena->used+=count; slot->generation=next_generation(slot->generation); slot->live=TRUE; slot->locked=FALSE;
    return 0x40000000|(slot->generation<<10)|index;
}
short _rasterizer_dynamic_vertices_get_type(long handle)
{
    struct vertex_slot *slot;
    if (handle==NONE) return NONE;
    slot=vertex_slot(handle); assert(slot); return slot->view.type;
}
void *_rasterizer_dynamic_vertices_lock(long handle)
{
    struct vertex_slot *slot;
    if (handle==NONE) return NULL;
    slot=vertex_slot(handle); assert(slot && !slot->locked); slot->locked=TRUE;
    return (void *)slot->view.data;
}
void _rasterizer_dynamic_vertices_unlock(long handle)
{
    struct vertex_slot *slot;
    if (handle==NONE) return;
    slot=vertex_slot(handle); assert(slot && slot->locked); slot->locked=FALSE;
}
void _rasterizer_dynamic_vertices_delete(long handle)
{
    struct vertex_slot *slot;
    if (handle==NONE) return;
    slot=vertex_slot(handle); assert(slot && !slot->locked);
    /* Keep queued transparent/model references alive through this view. */
}
