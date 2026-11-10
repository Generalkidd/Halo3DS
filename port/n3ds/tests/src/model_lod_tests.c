/* New3DS integration additions by Kevin Boggs. */
/* Host test of the actual runtime pack loader using locally prepared assets. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
struct cache_file_header {
    unsigned int signature,version,length,reserved,tag_offset,tag_bytes;
    byte padding[8];char name[32];byte tail[1984];
};
struct n3ds_cache_view {struct cache_file_header header;byte *tags;unsigned int tag_bytes;};
struct model_vertex_compressed {byte data[32];};
struct vertex_buffer {int type,count;void *base_address;};
struct triangle_buffer {int type,count;void *base_address;};
static struct n3ds_cache_view view;
static int original,first_person_active,locks,opens;
static const char *pack_path;
/* Kevin Boggs: n3ds_input_platform_original_model */
int n3ds_input_platform_original_model(void){return original;}
/* Kevin Boggs: n3ds_files_lock */
void n3ds_files_lock(void){++locks;}
/* Kevin Boggs: n3ds_files_unlock */
void n3ds_files_unlock(void){--locks;}
/* Kevin Boggs: n3ds_log */
void n3ds_log(const char *s){puts(s);}
/* Kevin Boggs: n3ds_engine_cache_view */
static const struct n3ds_cache_view *n3ds_engine_cache_view(int unused){(void)unused;return &view;}
/* Kevin Boggs: n3ds_cache_resolve */
static void *n3ds_cache_resolve(const struct n3ds_cache_view *v,const void *p,unsigned int bytes)
{ const byte *b=p;return b>=v->tags && bytes<=v->tag_bytes && b-v->tags<=v->tag_bytes-bytes?(void *)p:NULL; }
/* Kevin Boggs: open_pack */
static FILE *open_pack(const char *path,const char *mode){assert(strstr(path,".hld"));++opens;return fopen(pack_path,mode);}
#define fopen open_pack
#include "engine_model_lod.inl"
#undef fopen
/* Kevin Boggs: main */
int main(int argc,char **argv)
{
    assert(argc==3 && sizeof(view.header)==2048);pack_path=argv[2];
    FILE *file=fopen(argv[1],"rb");assert(file);
    assert(fread(&view.header,1,sizeof(view.header),file)==2048);
    view.tag_bytes=view.header.tag_bytes;view.tags=malloc(view.tag_bytes);assert(view.tags);
    assert(!fseek(file,view.header.tag_offset,SEEK_SET) && fread(view.tags,1,view.tag_bytes,file)==view.tag_bytes);fclose(file);
    n3ds_model_lod_prepare();assert(!native_lod_pack && !opens && !locks);
    original=1;n3ds_model_lod_prepare();assert(native_lod_pack && native_lod_count && !locks);
    unsigned int tested=0;
    for(unsigned int i=0;i<native_lod_count;++i) {
        const struct native_lod_entry *e=(const void *)(native_lod_pack+32+i*40);
        struct vertex_buffer v={5,(int)e->source_vertices,view.tags+e->vertex_offset};
        struct triangle_buffer t={1,(int)e->source_triangles,view.tags+e->index_offset};
        first_person_active=1;assert(!native_lod_get(&v,&t));first_person_active=0;
        struct native_lod_mesh *mesh=native_lod_get(&v,&t);assert(mesh);
        const unsigned short *strip=t.base_address;unsigned int real_triangles=0;
        for(unsigned int k=0;k<e->source_triangles;++k)
            if(strip[k]!=strip[k+1] && strip[k]!=strip[k+2] && strip[k+1]!=strip[k+2])++real_triangles;
        assert(mesh->saved_triangles==real_triangles-(unsigned int)mesh->triangles.count);
        const unsigned short *remap=(const void *)(native_lod_pack+e->offset);
        for(unsigned int k=0;k<e->vertices;++k)assert(!memcmp((byte *)mesh->vertices.base_address+k*32,(byte *)v.base_address+remap[k]*32,32));
        assert(native_lod_get(&v,&t)==mesh);++tested;
    }
    native_lod_meshes_release();assert(!native_lod_bytes);
    const struct native_lod_entry *e=(const void *)(native_lod_pack+32);
    struct vertex_buffer v={5,(int)e->source_vertices,view.tags+e->vertex_offset};
    struct triangle_buffer t={1,(int)e->source_triangles,view.tags+e->index_offset};
    view.tags[e->vertex_offset]^=1;assert(!native_lod_get(&v,&t));
    n3ds_model_lod_close();assert(!native_lod_pack && !native_lod_bytes && !locks);
    free(view.tags);printf("PASS %u runtime meshes: New-model/first-person exclusion, exact attributes, source hash and teardown\n",tested);
    return 0;
}
