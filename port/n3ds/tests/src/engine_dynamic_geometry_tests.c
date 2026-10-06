#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "engine_dynamic_geometry.h"

void n3ds_log(const char *message);
int halo_engine_dynamic_geometry_tests(void)
{
    static const short types[]={_rasterizer_vertex_type_model_compressed,_rasterizer_vertex_type_dynamic_unlit,_rasterizer_vertex_type_debug};
    static const long limits[]={2048,8192,24576};
    long handles[1024], stale, first, second, i, j;
    byte *data;
    short *indices;
    unsigned int used;
#define CHECK(expr) do { if (!(expr)) { n3ds_log("DYNAMIC GEOMETRY FAIL: " #expr); return 1; } } while (0)
    first=rasterizer_dynamic_triangles_new(1);
    second=rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_model_compressed,1);
    CHECK(first!=NONE && second!=NONE);
    CHECK(n3ds_dynamic_triangles_get(first) && n3ds_dynamic_vertices_get(second));
    CHECK(!n3ds_dynamic_triangles_get(second) && !n3ds_dynamic_vertices_get(first));
    rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_triangles_get(first) && !n3ds_dynamic_vertices_get(second));
    CHECK(rasterizer_dynamic_vertices_new(-1,1)==NONE);
    CHECK(rasterizer_dynamic_vertices_new(NUMBER_OF_RASTERIZER_VERTEX_TYPES,1)==NONE);
    CHECK(rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_environment_compressed,1)==NONE);
    CHECK(rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_dynamic_unlit,0)==NONE);
    CHECK(rasterizer_dynamic_vertices_lock(NONE)==NULL);
    CHECK(rasterizer_dynamic_vertices_get_type(NONE)==NONE);
    rasterizer_dynamic_vertices_unlock(NONE); rasterizer_dynamic_vertices_delete(NONE);
    for (i=0; i<3; ++i) {
        long stride=rasterizer_geometry_get_vertex_size(types[i]);
        first=rasterizer_dynamic_vertices_new(types[i],limits[i]-1); CHECK(first!=NONE);
        second=rasterizer_dynamic_vertices_new(types[i],1); CHECK(second!=NONE && first!=second);
        data=rasterizer_dynamic_vertices_lock(first); CHECK(data && !n3ds_dynamic_vertices_get(first));
        memset(data,0x39,(limits[i]-1)*stride);
        rasterizer_dynamic_vertices_unlock(first);
        data=rasterizer_dynamic_vertices_lock(second); CHECK(data);
        memset(data,0xc6,stride); rasterizer_dynamic_vertices_unlock(second);
        CHECK(rasterizer_dynamic_vertices_get_type(first)==types[i]);
        CHECK(n3ds_dynamic_vertices_get(first)->count==(unsigned long)limits[i]-1);
        CHECK(!n3ds_dynamic_triangles_get(first)); /* Distinct handle namespaces. */
        used=n3ds_dynamic_geometry_bytes();
        CHECK(rasterizer_dynamic_vertices_new(types[i],1)==NONE && used==n3ds_dynamic_geometry_bytes());
        data=rasterizer_dynamic_vertices_lock(first);
        for (j=0; j<(limits[i]-1)*stride; ++j) CHECK(data[j]==0x39);
        rasterizer_dynamic_vertices_unlock(first);
        rasterizer_dynamic_vertices_delete(first);
        CHECK(n3ds_dynamic_vertices_get(first)); /* Deferred draw keeps its data. */
    }
    stale=first;
    rasterizer_dynamic_geometry_end(); rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_vertices_get(stale) && n3ds_dynamic_geometry_bytes()==0);
    for (i=0; i<1024; ++i) { handles[i]=rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug,1); CHECK(handles[i]!=NONE); }
    CHECK(handles[0]!=stale && rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug,1)==NONE);
    CHECK(!n3ds_dynamic_vertices_get(stale)); /* Slot has now been reused. */
    CHECK(n3ds_dynamic_vertices_get(handles[1023])->count==1);
    stale=handles[0]; rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_vertices_get(stale));
    first=rasterizer_dynamic_triangles_new(131072); CHECK(first!=NONE);
    CHECK(!n3ds_dynamic_vertices_get(first));
    indices=rasterizer_dynamic_triangles_lock(first); CHECK(indices && !n3ds_dynamic_triangles_get(first));
    indices[0]=123; indices[131072*3-1]=234;
    rasterizer_dynamic_triangles_unlock(first);
    CHECK(rasterizer_dynamic_triangles_new(1)==NONE);
    CHECK(n3ds_dynamic_triangles_get(first)->indices[0]==123 && n3ds_dynamic_triangles_get(first)->indices[131072*3-1]==234);
    rasterizer_dynamic_triangles_delete(first); CHECK(n3ds_dynamic_triangles_get(first));
    stale=first; rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_triangles_get(stale) && !n3ds_dynamic_geometry_bytes());
    for (i=0; i<1024; ++i) { handles[i]=rasterizer_dynamic_triangles_new(1); CHECK(handles[i]!=NONE); }
    CHECK(handles[0]!=stale && rasterizer_dynamic_triangles_new(1)==NONE);
    CHECK(!n3ds_dynamic_triangles_get(stale));
    stale=handles[0]; rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_triangles_get(stale) && !n3ds_dynamic_geometry_bytes());
    n3ds_log("PASS: original dynamic vertex/triangle APIs, bounded arenas, deferred deletion, stale handles and per-view reset");
    return 0;
#undef CHECK
}
