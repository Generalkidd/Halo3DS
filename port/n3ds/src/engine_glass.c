/* Native constant-tint glass combines tint/reflection with filtered cube
 * sampling on PICA, then preserves diffuse alpha and intermediate saturation.
 * Textured/extended-range tints retain the CPU sampled fallback.
 * Optional bump/detail maps retain smooth base glass as a bounded approximation. */
#include "cseries.h"
#include "engine_glass.h"
#include "engine_models.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "engine_textures.h"
#include "engine_bitmaps.h"
#include "s3tc.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_runtime.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "shaders/shader_definitions.h"
struct glass_shader {
    struct shader shader;word flags,pad;byte reserved[40];real_rgb_color tint;
    real tint_scale;struct tag_reference tint_map;byte reserved2[20];
    word reflection_flags;short reflection_type;real_argb_color perpendicular,parallel;
    struct tag_reference reflection_map;real bump_scale;struct tag_reference bump_map;
    byte reserved3[128];word diffuse_flags,pad2;real diffuse_scale;struct tag_reference diffuse_map;
    real detail_scale;struct tag_reference detail_map;byte reserved4[100];
};
_Static_assert(offsetof(struct glass_shader,reflection_type)==138,"Glass ABI");
_Static_assert(offsetof(struct glass_shader,detail_map)==364,"Glass detail ABI");
extern struct rasterizer_window_begin_parameters global_window_parameters;
boolean rasterizer_set_texture_direct(short,long,short);
void n3ds_log(const char *);
#define CUBE_SIZE 32
#define CUBE_COUNT 8
static struct { const struct bitmap_data *bitmap; byte rgba[6*CUBE_SIZE*CUBE_SIZE*4]; } cubes[CUBE_COUNT];
static unsigned int cube_generation,cube_count;
int n3ds_cube_coordinates(const float r[3],int *face,float uv[2])
{
    float x=r[0],y=r[1],z=r[2],a=fabsf(x),b=fabsf(y),c=fabsf(z),u,v,major;
    if(!isfinite(x) || !isfinite(y) || !isfinite(z) || MAX(a,MAX(b,c))<1e-20f) return 0;
    if(a>=b && a>=c) { *face=x>=0?0:1;major=a;u=x>=0?-z:z;v=-y; }
    else if(b>=c) { *face=y>=0?2:3;major=b;u=x;v=y>=0?z:-z; }
    else { *face=z>=0?4:5;major=c;u=z>=0?x:-x;v=-y; }
    uv[0]=.5f*(u/major+1);uv[1]=.5f*(v/major+1);return 1;
}
const byte *n3ds_cube_load(long tag,short permutation)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct bitmap_group *group=bitmap_group_get(tag);
    assert(group && permutation>=0 && group->bitmaps.count>0);
    struct bitmap_data *b=bitmap_group_try_and_get_bitmap(tag,permutation%group->bitmaps.count);
    if(cube_generation!=n3ds_engine_cache_generation()) {
        memset(cubes,0,sizeof(cubes));cube_count=0;cube_generation=n3ds_engine_cache_generation();
    }
    for(unsigned int i=0;i<cube_count;++i) if(cubes[i].bitmap==b) return cubes[i].rgba;
    assert(cube_count<CUBE_COUNT && n3ds_cache_resolve(view,b,sizeof(*b))==b);
    assert(b->signature=='bitm' && b->type==2 && b->depth==1 && b->width==b->height);
    assert(b->width>=8 && b->width<=256 && !(b->width&(b->width-1)) && b->mipmap_count>=0 && b->mipmap_count<=8);
    boolean raw=b->format==6 || b->format==10 || b->format==11;
    unsigned int pixel_bytes=b->format==6?2:4;
    assert((raw && (b->flags&8)) || (!raw && b->format>=14 && b->format<=16 && !(b->flags&8)));
    assert(b->flags&0x80);
    unsigned int block=b->format==14?8:16,chain=0,skip=0,side=b->width,level=0;
    for(int mip=0;mip<=b->mipmap_count;++mip) {
        unsigned int n=MAX(1,b->width>>mip);
        chain+=raw?n*n*pixel_bytes:((n+3)/4)*((n+3)/4)*block;
    }
    unsigned int face_stride=(chain+127)&~127u;
    assert(b->pixels_size==(long)(face_stride*6) && b->pixels_offset>=2048);
    while(side>CUBE_SIZE && level<(unsigned int)b->mipmap_count) {
        skip+=raw?side*side*pixel_bytes:((side+3)/4)*((side+3)/4)*block;side>>=1;++level;
    }
    unsigned int bytes=raw?side*side*pixel_bytes:((side+3)/4)*((side+3)/4)*block;
    assert(skip+bytes<=face_stride);
    byte *encoded=malloc(bytes),*decoded=malloc(side*side*4);assert(encoded && decoded);
    byte *destination=cubes[cube_count].rgba;
    for(unsigned int face=0;face<6;++face) {
        assert(n3ds_engine_cache_read(b->pixels_offset+face*face_stride+skip,bytes,encoded));
        if(raw) {
            for(unsigned int y=0;y<side;++y) for(unsigned int x=0;x<side;++x) {
                long address[2];bitmap_swizzle_vector2d(side,side,x,y,address);
                unsigned int pixel=n3ds_bitmap_decode_pixel(encoded+pixel_bytes*(address[0]|address[1]),b->format);
                for(int c=0;c<4;++c) decoded[(y*side+x)*4+c]=pixel>>(24-c*8);
            }
        } else assert(!halo_s3tc_decode(b->format==14?0xc:b->format==15?0xe:0xf,
            encoded,bytes,side,side,decoded,side*side*4,side*4));
        for(unsigned int y=0;y<CUBE_SIZE;++y) for(unsigned int x=0;x<CUBE_SIZE;++x)
            memcpy(destination+((face*CUBE_SIZE+y)*CUBE_SIZE+x)*4,
                decoded+((y*side/CUBE_SIZE)*side+x*side/CUBE_SIZE)*4,4);
    }
    free(decoded);free(encoded);cubes[cube_count++].bitmap=b;
    char text[160];snprintf(text,sizeof(text),"NATIVE CUBE: tag=%08lx format=%d faces=6 size=32 level=%u stride=%u",tag,b->format,level,face_stride);n3ds_log(text);
    /* Read-only decode evidence, captured once per resource generation. */
    snprintf(text,sizeof(text),"z:\\native-cube-%08lx.bin",tag);
    FILE *proof=fopen(text,"wb");assert(proof);
    assert(fwrite(destination,6*CUBE_SIZE*CUBE_SIZE*4,1,proof)==1);assert(!fclose(proof));
    return destination;
}
void n3ds_cube_sample(const byte *pixels,const float direction[3],float rgb[4])
{
    int face;float uv[2];assert(n3ds_cube_coordinates(direction,&face,uv));
    float x=uv[0]*CUBE_SIZE-.5f,y=uv[1]*CUBE_SIZE-.5f;
    int ix=(int)floorf(x),iy=(int)floorf(y);float fx=x-ix,fy=y-iy;
    memset(rgb,0,4*sizeof(float));
    for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
        int px=PIN(ix+i,0,CUBE_SIZE-1),py=PIN(iy+j,0,CUBE_SIZE-1);
        const byte *sample=pixels+((face*CUBE_SIZE+py)*CUBE_SIZE+px)*4;
        float weight=(i?fx:1-fx)*(j?fy:1-fy)/255.f;
        for(int c=0;c<4;++c) rgb[c]+=sample[c]*weight;
    }
}

static int glass_native(const struct glass_shader *g,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,unsigned int count,
    short type,const unsigned short *indices,unsigned int index_count)
{
    if(g->tint_map.index!=NONE) return 0;
    struct native_glass_material m={.two_sided=!!(g->flags&4),.tint=0xff000000u};
    int tint=g->tint.red || g->tint.green || g->tint.blue;
    for(int c=0;c<3;++c) {
        float value=tint?g->tint.n[c]:1;
        if(!(value>=0 && value<=1)) return 0;
        m.tint|=(unsigned int)(value*255.f+.5f)<<(c*8);
    }
    if(g->reflection_map.index!=NONE && (g->perpendicular.alpha>0 || g->parallel.alpha>0)) {
        for(int c=0;c<4;++c) {
            m.perpendicular[c]=c==3?g->perpendicular.alpha:g->perpendicular.rgb.n[c];
            m.parallel[c]=c==3?g->parallel.alpha:g->parallel.rgb.n[c];
            if(!(m.perpendicular[c]>=0 && m.perpendicular[c]<=1 && m.parallel[c]>=0 && m.parallel[c]<=1)) return 0;
        }
        if(!(m.cube=n3ds_engine_bitmap_resource(g->reflection_map.index,permutation))) return 0;
    }
    if(g->diffuse_map.index!=NONE && !(m.diffuse=n3ds_engine_bitmap_resource(g->diffuse_map.index,permutation))) return 0;
    if(!m.cube && !m.diffuse && (m.tint&0xffffffu)==0xffffffu) return 1;
    const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
    float rows[12];
    m.uv_scale[0]=g->diffuse_scale*p->base_map_scale.i;
    m.uv_scale[1]=g->diffuse_scale*p->base_map_scale.j;
    assert(isfinite(m.uv_scale[0]) && isfinite(m.uv_scale[1]));
    const struct native_render_vertex *rigid=n3ds_model_rigid_stream(cached,count,&p->skinning,rows);
    struct native_render_vertex *out=NULL;
    if(rigid) {m.pose=rows;}
    else {
    const real_vector2d unit={1,1};
    const struct native_render_vertex *pose=n3ds_model_skin_stream(cached,count,&p->skinning,&unit,p->unique_identifier);
    out=n3ds_gpu_model_vertices_allocate(count);assert(out);
    for(unsigned int i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
        const struct model_vertex_uncompressed *v=cached?cached+i:&decoded;
        if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded));}
        else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
        if(!pose) assert(n3ds_model_skin_vertex(v,&p->skinning,&unit,&temporary));
        memcpy(out[i].position,pose?pose[i].position:temporary.position,12);
        real_vector3d normal={0};
        if(m.cube || m.diffuse) {
            for(int k=0;k<2;++k) if(v->node_weights[k]) {
                assert(v->nodes[k]>=0 && v->nodes[k]<p->skinning.node_matrix_count);
                real_vector3d n;matrix4x3_transform_vector(p->skinning.node_matrices+v->nodes[k],&v->normal,&n);
                for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*v->node_weights[k];
            }
        } else normal.k=1;
        memcpy(out[i].color,normal.n,12);
        out[i].uv[0]=v->texcoord.x*g->diffuse_scale*p->base_map_scale.i;
        out[i].uv[1]=v->texcoord.y*g->diffuse_scale*p->base_map_scale.j;
        assert(isfinite(out[i].uv[0]) && isfinite(out[i].uv[1]));
    }
    n3ds_gpu_geometry_flush(out,count*sizeof(*out));
    }
    const unsigned short *gpu_indices=n3ds_gpu_indices_upload(indices,index_count);assert(gpu_indices);
    assert(n3ds_gpu_glass_native_sky_draw(rigid?rigid:out,gpu_indices,index_count,&m,!!(p->geometry_flags&(1<<4))));
    static unsigned int generation,seen_count;static const void *seen[32];
    if(generation!=n3ds_engine_cache_generation()) {generation=n3ds_engine_cache_generation();seen_count=0;}
    unsigned int i;for(i=0;i<seen_count && seen[i]!=g;++i) {}
    if(i==seen_count && seen_count<32) {
        seen[seen_count++]=g;char line[180];
        snprintf(line,sizeof(line),"NATIVE GLASS GPU: vertices=%u triangles=%u cube=%d diffuse=%d passes=%d; constant tint/native cube,%s pose",count,index_count/3,!!m.cube,!!m.diffuse,!!m.diffuse+(!!m.cube || (m.tint&0xffffffu)!=0xffffffu),rigid?"GPU rigid":"CPU");n3ds_log(line);
    }
    return 1;
}

void n3ds_glass_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *p,const void *data,unsigned int count,
    short type,const unsigned short *indices,unsigned int index_count)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    const struct glass_shader *g=(const void *)shader;
    assert(n3ds_cache_resolve(view,g,sizeof(*g))==g && g->reflection_type>=0 && g->reflection_type<=1);
    /* Legal Xbox glass variants must not terminate gameplay. Preserve the
     * tint, reflection and diffuse layers; optional bump/detail modulation is
     * approximated by smooth glass until a native combiner is implemented. */
    static unsigned int variant_generation,variant_count;
    static const void *variants[32];
    if(variant_generation!=n3ds_engine_cache_generation()) {variant_generation=n3ds_engine_cache_generation();variant_count=0;}
    if(g->bump_map.index!=NONE || g->detail_map.index!=NONE || (p->geometry_flags&(1<<4))) {
        unsigned int i;for(i=0;i<variant_count && variants[i]!=g;++i){}
        if(i==variant_count && variant_count<32) {
            variants[variant_count++]=g;char text[180];
            snprintf(text,sizeof(text),"GLASS VARIANT: smooth approximation bump=%d detail=%d sky=%d",g->bump_map.index!=NONE,g->detail_map.index!=NONE,!!(p->geometry_flags&(1<<4)));n3ds_log(text);
        }
    }
    if(glass_native(g,permutation,p,data,count,type,indices,index_count)) return;
    const byte *cube=g->reflection_map.index!=NONE && (g->perpendicular.alpha>0 || g->parallel.alpha>0)?n3ds_cube_load(g->reflection_map.index,permutation):NULL;
    for(int pass=0;pass<3;++pass) {
        if(pass==0 && g->tint_map.index==NONE && !g->tint.red && !g->tint.green && !g->tint.blue) continue;
        if(pass==1 && !cube) continue;
        if(pass==2 && g->diffuse_map.index==NONE) continue;
        long bitmap=pass==0?g->tint_map.index:pass==2?g->diffuse_map.index:NONE;
        if(bitmap!=NONE) { assert(rasterizer_set_texture_direct(0,bitmap,permutation)); }
        else assert(n3ds_gpu_texture_bind(0,NULL));
        struct native_render_vertex *v=n3ds_gpu_model_vertices_allocate(count);assert(v);
        const struct model_vertex_uncompressed *cached=type==5?n3ds_model_decode_stream(data,count):NULL;
        const real_vector2d unit_scale={1,1};
        const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&p->skinning,&unit_scale,p->unique_identifier);
        if(posed) memcpy(v,posed,count*sizeof(*v));
        real scale=pass==0?g->tint_scale:g->diffuse_scale;
        real_vector2d uv={scale*p->base_map_scale.i,scale*p->base_map_scale.j};
        for(unsigned int i=0;i<count;++i) {
            struct model_vertex_uncompressed decoded;
            const struct model_vertex_uncompressed *vertex=cached?cached+i:&decoded;
            if(!cached && type==5) { assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*32),&decoded)); }
            else if(!cached) memcpy(&decoded,(const byte *)data+i*68,sizeof(decoded));
            if(!posed) assert(n3ds_model_skin_vertex(vertex,&p->skinning,&unit_scale,v+i));
            /* Cache pure pose data only. Each pass still applies its current
             * UV scale and view-dependent reflection/tint below. */
            v[i].uv[0]=vertex->texcoord.x*uv.i;v[i].uv[1]=vertex->texcoord.y*uv.j;
            assert(isfinite(v[i].uv[0]) && isfinite(v[i].uv[1]));
            if(pass==0) memcpy(v[i].color,g->tint.n,12);
            if(pass==1) {
                real_vector3d normal={0},eye;float n2=0,e2=0,dot=0,reflected[3];
                for(int k=0;k<2;++k) if(vertex->node_weights[k]) {
                    real_vector3d n;matrix4x3_transform_vector(p->skinning.node_matrices+vertex->nodes[k],&vertex->normal,&n);
                    for(int c=0;c<3;++c) normal.n[c]+=n.n[c]*vertex->node_weights[k];
                }
                for(int c=0;c<3;++c) {
                    eye.n[c]=global_window_parameters.camera.position.n[c]-v[i].position[c];
                    n2+=normal.n[c]*normal.n[c];e2+=eye.n[c]*eye.n[c];
                }
                assert(n2>1e-12f && e2>1e-12f);
                for(int c=0;c<3;++c) {normal.n[c]/=sqrtf(n2);eye.n[c]/=sqrtf(e2);dot+=normal.n[c]*eye.n[c];}
                for(int c=0;c<3;++c) reflected[c]=2*dot*normal.n[c]-eye.n[c];
                float facing=PIN(fabsf(dot),0.f,1.f),rgb[4];n3ds_cube_sample(cube,reflected,rgb);
                float alpha=g->parallel.alpha+(g->perpendicular.alpha-g->parallel.alpha)*facing;
                for(int c=0;c<3;++c) v[i].color[c]=rgb[c]*alpha*(g->parallel.rgb.n[c]+(g->perpendicular.rgb.n[c]-g->parallel.rgb.n[c])*facing);
            }
        }
        n3ds_gpu_geometry_flush(v,count*sizeof(*v));
        assert(n3ds_gpu_glass_sky_draw(v,indices,index_count,pass,!!(g->flags&4),!!(p->geometry_flags&(1<<4))));
    }
    static const void *seen[16];static int seen_count;int i;
    for(i=0;i<seen_count && seen[i]!=shader;++i) {}
    if(i==seen_count && seen_count<16) {
        seen[seen_count++]=shader;
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==shader) {
            char text[224];snprintf(text,sizeof(text),"NATIVE GLASS: tint/cube/diffuse triangles=%u name=%.160s",index_count/3,view->instances[t].name);n3ds_log(text);break;
        }
    }
}
