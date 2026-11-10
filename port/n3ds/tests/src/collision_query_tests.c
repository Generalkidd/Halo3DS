/* New3DS integration additions by Kevin Boggs. */
/* Host differential tests for the production spatial traversal cache. */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NONE (-1L)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MAXIMUM_BSP3D_DEPTH 128
typedef float real;
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
typedef struct {float x,y;} real_point2d;
typedef struct { float x,y,z; } real_point3d;
typedef struct { struct { float i,j,k; } n; float d; } real_plane3d;
struct block { void *address; };
struct bsp3d_node { long plane_designator,children[2]; };
struct collision_bsp { struct { struct block nodes,planes; } bsp3d; };
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (((t *)(b)->address)+(i))
static struct collision_bsp *global_collision_bsp;
static int original;
/* Kevin Boggs: n3ds_input_platform_original_model */
int n3ds_input_platform_original_model(void) { return original; }
/* Kevin Boggs: n3ds_log */
void n3ds_log(const char *s) { (void)s; }
/* Kevin Boggs: plane3d_distance_to_point */
static real plane3d_distance_to_point(const real_plane3d *p,const real_point3d *v)
{ return p->n.i*v->x+p->n.j*v->y+p->n.k*v->z-p->d; }
/* Kevin Boggs: distance_squared3d */
static real distance_squared3d(const real_point3d *a,const real_point3d *b)
{ real x=a->x-b->x,y=a->y-b->y,z=a->z-b->z;return x*x+y*y+z*z; }
#include "engine_collision_query.inl"
static struct bsp3d_node nodes[1023];
static real_plane3d planes[1023];
static unsigned int seed=1234;
/* Kevin Boggs: random_float */
static float random_float(void) { seed=seed*1664525u+1013904223u;return (seed>>8)*(1.f/16777216.f); }
/* Kevin Boggs: trace_query */
static void trace_query(const struct native_query_tree *tree,long index,const real_point3d *p,
    float radius,long *out,unsigned int *count)
{
    if(index & LONG_MIN) { out[(*count)++]=index;return; }
    long source=tree?tree->nodes[index].original:index;
    const struct bsp3d_node *node=nodes+source;
    const long *children=tree?tree->nodes[index].children:node->children;
    float distance=plane3d_distance_to_point(planes+node->plane_designator,p);
    int front=distance>-radius,back=distance<radius;
    if(front && back) {
        /* Include intersected planes and their order, not just leaf membership. */
        out[(*count)++]=source;
        trace_query(tree,children[0],p,radius,out,count);
        trace_query(tree,children[1],p,radius,out,count);
    } else trace_query(tree,children[front],p,radius,out,count);
}
/* Kevin Boggs: main */
int main(void)
{
    assert(sizeof(long)==4);
    struct collision_bsp bsp={{ {nodes},{planes} }};global_collision_bsp=&bsp;
    for(unsigned int i=0;i<1023;++i) {
        nodes[i].plane_designator=i;
        nodes[i].children[0]=i<511?(long)(i*2+1):(LONG_MIN | (long)(i*2));
        nodes[i].children[1]=i<511?(long)(i*2+2):(LONG_MIN | (long)(i*2+1));
        planes[i].n.i=random_float()*2-1;planes[i].n.j=random_float()*2-1;planes[i].n.k=random_float()*2-1;
        planes[i].d=random_float()*12-6;
    }
    unsigned int hits=0;
    for(original=0;original<2;++original) {
        n3ds_collision_query_clear();
        for(unsigned int trial=0;trial<10000;++trial) {
            real_point3d p={random_float()*8-4,random_float()*8-4,random_float()*8-4};
            float radius=.1f+random_float()*1.1f;
            for(unsigned int step=0;step<5;++step) {
                p.x+=.02f;p.y-=.01f;
                const struct native_query_tree *tree=native_query_get(&bsp,&p,radius);
                long a[3072],b[3072];unsigned int na=0,nb=0;
                trace_query(NULL,0,&p,radius,a,&na);
                trace_query(tree,tree?tree->root:0,&p,radius,b,&nb);
                assert(na==nb && !memcmp(a,b,na*sizeof(long)));
            }
        }
        hits+=native_query_hits;
    }
    assert(hits>0);
    /* A dense overlapping tree falls back exactly and remembers the failed
     * construction, rather than rebuilding it for every nearby query. */
    for(unsigned int i=0;i<1023;++i) { planes[i].n.i=1;planes[i].n.j=planes[i].n.k=0;planes[i].d=0; }
    n3ds_collision_query_clear();real_point3d origin={0};
    assert(!native_query_get(&bsp,&origin,.5f));
    unsigned int misses=native_query_misses;
    assert(!native_query_get(&bsp,&origin,.5f) && native_query_misses==misses && native_query_hits==1);
    assert(!native_query_get(&bsp,&origin,10.f));
    n3ds_collision_query_clear();
    for(unsigned int i=0;i<32;++i){origin.x=(float)i*5;native_query_get(&bsp,&origin,.5f);}
    assert(native_query_cooldown==1024);
    misses=native_query_misses;
    for(unsigned int i=0;i<1024;++i)assert(!native_query_get(&bsp,&origin,.5f));
    assert(!native_query_cooldown && native_query_misses==misses && native_query_bypassed==1024);
    native_query_get(&bsp,&origin,.5f);assert(native_query_misses==misses+1);
    /* Capacity saturation, mixed lists, duplicates and stamp rollover. */
    for(unsigned int trial=0;trial<50;++trial) {
        long a[3][256],b[3][256],ca[3]={0},cb[3]={0};native_feature_begin();
        for(unsigned int j=0;j<10000;++j) {
            unsigned int list=j%3;long v=(long)(random_float()*400);
            native_feature_insert(ca+list,a[list],v);
            long k=0;while(k<cb[list] && b[list][k]!=v)++k;
            if(k==cb[list] && cb[list]<256)b[list][cb[list]++]=v;
        }
        assert(!memcmp(ca,cb,sizeof(ca)) && !memcmp(a,b,sizeof(a)));
        if(trial==24)native_feature_stamp=0xffff0000u;
    }
    /* Exact keys, interleaved queries, hash collisions and rollover. A new
     * generation must not inherit visits even at the same map/vertex address. */
    real_point2d center={1.f,2.f};
    unsigned int generation=native_surface_begin();
    assert(!native_surface_repeated(generation,42,0,0,&center));
    assert(native_surface_repeated(generation,42,0,0,&center));
    assert(!native_surface_repeated(generation,42,1,0,&center));
    assert(!native_surface_repeated(generation,42,1,1,&center));
    center.x=3.f;assert(!native_surface_repeated(generation,42,1,1,&center));
    unsigned int next=native_surface_begin();
    assert(!native_surface_repeated(next,42,1,1,&center));
    assert(!native_surface_repeated(generation,42,1,1,&center));
    long collision=43;
    while((((unsigned int)collision*2654435761u)>>24)!=((42u*2654435761u)>>24))++collision;
    assert(!native_surface_repeated(generation,collision,1,1,&center));
    assert(!native_surface_repeated(generation,42,1,1,&center));
    native_surface_generation=~0u;generation=native_surface_begin();assert(generation==1);
    assert(!native_surface_repeated(generation,42,1,1,&center));
    center.x=0.f;assert(!native_surface_repeated(generation,42,1,1,&center));
    center.x=-0.f;assert(!native_surface_repeated(generation,42,1,1,&center));
    assert(native_surface_repeated(generation,42,1,1,&center));
    puts("PASS: query-local surface reuse, projection/center changes, nested generation, hash collision and rollover");
    n3ds_collision_query_clear();assert(!native_query_trees[0].bsp);
    printf("PASS: 100000 exact ordered traversals, %u cache hits, feature saturation/rollover\n",hits);
    return 0;
}
