/* New3DS selector preview, adapted from the clean-room Halo PC model viewer
 * in Halo3DS-Port. All geometry is read from the owner's installed map. */
#include <3ds.h>
#include <citro3d.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_input.h"
#include "engine_renderer.h"
#include "engine_textures.h"
#include "engine_widgets.h"
#include "engine_pc_preview.h"
#include "halo_cache_reader.h"
#include "halo_pc_bsp.h"
#include "halo_pc_model.h"
#include "s3tc.h"

void n3ds_log(const char *message);
long long n3ds_engine_ticks(void);
long long n3ds_engine_tick_frequency(void);
enum { PREVIEW_TRIANGLES=2048, PREVIEW_VERTICES=PREVIEW_TRIANGLES*3,
       NAV_TRIANGLES=1024, PREVIEW_TEXTURE_SIZE=64, PREVIEW_TEXTURE_LIMIT=48,
       VIEW_LIT=0, VIEW_XRAY=1, VIEW_WIRE=2, VIEW_XRAY_WIRE=3,
       TOUCH_NONE=0, TOUCH_DRAG=1, TOUCH_NAV=2, TOUCH_VIEW=3 };
struct preview_batch {uint32_t shader_datum;unsigned int first,count;void *texture;};
static struct preview_batch batches[PREVIEW_TRIANGLES];
static unsigned int batch_count,texture_count;
static const char *const map_files[]={"beavercreek","bloodgulch","boardingaction","carousel","chillout",
    "damnation","dangercanyon","deathisland","gephyrophobia","hangemhigh","icefields",
    "infinity","longest","prisoner","putput","ratrace","sidewinder","timberland","wizard"};
static const char *const weapons[]={"weapons\\pistol\\pistol","weapons\\assault rifle\\assault rifle",
    "weapons\\plasma rifle\\plasma rifle","weapons\\sniper rifle\\sniper rifle",
    "weapons\\rocket launcher\\rocket launcher","weapons\\shotgun\\shotgun",
    "weapons\\flamethrower\\flamethrower","weapons\\plasma_cannon\\plasma_cannon"};
static const char *const vehicles[]={"vehicles\\warthog\\warthog","vehicles\\rwarthog\\rwarthog",
    "vehicles\\ghost\\ghost","vehicles\\scorpion\\scorpion","vehicles\\banshee\\banshee"};
static struct native_render_vertex *vertices;
static unsigned short *indices;
static float *material_uv;
static unsigned int triangles;
static struct native_widget_vertex *nav_vertices;
static unsigned int nav_triangles;
static int nav_visible=1,view_mode=VIEW_LIT,touch_held,touch_owner;
static int selected_map=-1,selected_page=-1,selected_field=-1,selected_weapon=-1,selected_vehicle=-1;
static int state_loaded;
static int uv_mode;
static float yaw=.65f,elevation=.72f,zoom=2.7f;
static unsigned int idle_frames;
static int dragging,last_x,last_y;
static void *white,*xray_texture,*wire_texture;

void n3ds_pc_preview_close(void)
{
    for(unsigned int i=0;i<batch_count;++i)if(batches[i].texture)
        n3ds_gpu_texture_destroy(batches[i].texture);
    batch_count=texture_count=0;
    n3ds_gpu_geometry_free(vertices);n3ds_gpu_geometry_free(indices);
    free(material_uv);material_uv=NULL;
    free(nav_vertices);nav_vertices=NULL;nav_triangles=0;
    vertices=NULL;indices=NULL;triangles=0;state_loaded=0;
    uv_mode=0;
    if(white){n3ds_gpu_texture_destroy(white);white=NULL;}
    if(xray_texture){n3ds_gpu_texture_destroy(xray_texture);xray_texture=NULL;}
    if(wire_texture){n3ds_gpu_texture_destroy(wire_texture);wire_texture=NULL;}
}

static int allocate_mesh(void)
{
    vertices=n3ds_gpu_geometry_optional_allocate(PREVIEW_VERTICES*sizeof(*vertices));
    indices=n3ds_gpu_geometry_optional_allocate(PREVIEW_VERTICES*sizeof(*indices));
    return vertices && indices;
}
static void emit_triangle(const float *a,const float *b,const float *c,int kind,const float *uv)
{
    if(triangles>=PREVIEW_TRIANGLES)return;
    float e0[3],e1[3],n[3];
    for(int i=0;i<3;++i){e0[i]=b[i]-a[i];e1[i]=c[i]-a[i];}
    n[0]=e0[1]*e1[2]-e0[2]*e1[1];n[1]=e0[2]*e1[0]-e0[0]*e1[2];
    n[2]=e0[0]*e1[1]-e0[1]*e1[0];
    float area=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(!isfinite(area) || area<.00001f)return;
    float light=.46f+.43f*fmaxf(0.f,(n[0]*.38f+n[1]*.22f+n[2]*.90f)/area);
    const float base[3][3]={{1.f,1.f,1.f},{.77f,.61f,.38f},{.58f,.72f,.87f}};
    const float *points[3]={a,b,c};
    for(int j=0;j<3;++j){
        struct native_render_vertex *v=vertices+triangles*3+j;
        for(int i=0;i<3;++i){v->position[i]=points[j][i];v->color[i]=base[kind][i]*light;}
        v->uv[0]=uv?uv[j*2]:0.f;v->uv[1]=uv?uv[j*2+1]:0.f;
        indices[triangles*3+j]=(unsigned short)(triangles*3+j);
    }
    ++triangles;
}
struct preview_face {float points[9],uv[6],area;uint32_t shader_datum;};
static void face_swap(struct preview_face *a,struct preview_face *b)
{struct preview_face tmp=*a;*a=*b;*b=tmp;}
/* Retain broad surfaces rather than every Nth triangle. The latter leaves
 * isolated specks across large Halo BSPs and hides their terrain shape. */
static void face_keep(struct preview_face *heap,unsigned int *count,unsigned int limit,
    const float *a,const float *b,const float *c,const float *uv,uint32_t shader_datum)
{
    float u[3],v[3],cross[3];
    for(int i=0;i<3;++i){u[i]=b[i]-a[i];v[i]=c[i]-a[i];}
    cross[0]=u[1]*v[2]-u[2]*v[1];cross[1]=u[2]*v[0]-u[0]*v[2];
    cross[2]=u[0]*v[1]-u[1]*v[0];
    float area=cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2];
    if(!isfinite(area) || area<.000001f ||
       (*count==limit && area<=heap[0].area))return;
    unsigned int index;
    if(*count<limit){index=(*count)++;}
    else index=0;
    struct preview_face *face=heap+index;
    memcpy(face->points,a,3*sizeof(float));memcpy(face->points+3,b,3*sizeof(float));
    memcpy(face->points+6,c,3*sizeof(float));
    if(uv)memcpy(face->uv,uv,sizeof(face->uv));else memset(face->uv,0,sizeof(face->uv));
    face->shader_datum=shader_datum;face->area=area;
    if(index){
        while(index){unsigned int parent=(index-1)/2;
            if(heap[parent].area<=heap[index].area)break;
            face_swap(heap+parent,heap+index);index=parent;}
    }else{
        while(1){unsigned int child=index*2+1;
            if(child>=*count)break;
            if(child+1<*count && heap[child+1].area<heap[child].area)++child;
            if(heap[index].area<=heap[child].area)break;
            face_swap(heap+index,heap+child);index=child;}
    }
}
static void load_nav(const struct halo_pc_bsp *bsp)
{
    if(!bsp->nav_surface_count)return;
    struct preview_face *heap=malloc(NAV_TRIANGLES*sizeof(*heap));
    if(!heap)return;
    unsigned int kept=0;
    for(uint32_t surface=0;surface<bsp->nav_surface_count;++surface){
        float points[8][3];
        int count=halo_pc_bsp_nav_polygon(bsp,surface,points);
        for(int i=1;i<count-1;++i)
            face_keep(heap,&kept,NAV_TRIANGLES,points[0],points[i],points[i+1],NULL,0);
    }
    if(kept){
        nav_vertices=malloc((size_t)kept*3*sizeof(*nav_vertices));
        if(nav_vertices){
            nav_triangles=kept;
            for(unsigned int i=0;i<kept;++i)for(int corner=0;corner<3;++corner){
                struct native_widget_vertex *v=nav_vertices+i*3+corner;
                memcpy(v->position,heap[i].points+corner*3,3*sizeof(float));
                v->color[0]=.13f;v->color[1]=.66f;v->color[2]=1.f;v->color[3]=.42f;
                v->uv[0]=v->uv[1]=0.f;
            }
        }
    }
    free(heap);
}
static uint32_t preview_u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int face_material_compare(const void *left,const void *right)
{
    const struct preview_face *a=left,*b=right;
    return a->shader_datum<b->shader_datum?-1:a->shader_datum>b->shader_datum?1:0;
}
static int find_tag(const struct halo_cache *cache,uint32_t datum,uint32_t group,
    struct halo_cache_tag *out)
{
    char error[96];
    if(datum==0xffffffffu)return 0;
    /* Retail datum low bits are its directory slot. Validate the full datum
     * before using it; malformed or unusual caches keep the bounded scan. */
    uint32_t slot=datum&0xffffu;
    if(slot<cache->tag_count && halo_cache_tag_at(cache,slot,out,error,sizeof(error)) &&
       out->datum==datum && out->group==group)return 1;
    for(uint32_t i=0;i<cache->tag_count;++i){
        if(halo_cache_tag_at(cache,i,out,error,sizeof(error)) && out->datum==datum && out->group==group)
            return 1;
    }
    return 0;
}
/* A small, disposable base-map sample keeps the selector independent of the
 * live game texture cache. Read only owner-installed PC bitmap data. */
static void *load_material_texture(struct halo_cache *cache,uint32_t shader_datum)
{
    struct halo_cache_tag shader,bitmap_tag;
    struct halo_cache_bitmap bitmap;
    char error[96];
    if(!find_tag(cache,shader_datum,0x73656e76u,&shader) ||
       shader.metadata_available<0x98u ||
       !find_tag(cache,preview_u32(shader.metadata+0x94u),0x6269746du,&bitmap_tag) ||
       !halo_cache_bitmap_at(cache,&bitmap_tag,0,&bitmap,error,sizeof(error)))return NULL;
    unsigned char *compressed=malloc(bitmap.first_mip_bytes);
    if(!compressed)return NULL;
    if(!halo_cache_read_bitmap(cache,&bitmap,
        "sdmc:/halo-source/pc-retail/bitmaps.map",compressed,bitmap.first_mip_bytes,
        error,sizeof(error))){free(compressed);return NULL;}
    unsigned int *pixels=malloc(PREVIEW_TEXTURE_SIZE*PREVIEW_TEXTURE_SIZE*sizeof(*pixels));
    if(!pixels){free(compressed);return NULL;}
    size_t rgba_bytes=(size_t)bitmap.width*bitmap.height*4u;
    unsigned char *decoded=rgba_bytes<=256u*256u*4u?malloc(rgba_bytes):NULL;
    if(decoded && halo_s3tc_decode(bitmap.format==14?0x0cu:bitmap.format==15?0x0eu:0x0fu,
        compressed,bitmap.first_mip_bytes,bitmap.width,bitmap.height,
        decoded,rgba_bytes,bitmap.width*4u)){
        free(decoded);decoded=NULL;
    }
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    unsigned char block[64];size_t prior=(size_t)-1;
    const size_t block_bytes=bitmap.format==14?8u:16u;
    int ok=1;
    for(unsigned int y=0;y<PREVIEW_TEXTURE_SIZE && ok;++y)
        for(unsigned int x=0;x<PREVIEW_TEXTURE_SIZE;++x){
            unsigned int sx=x*bitmap.width/PREVIEW_TEXTURE_SIZE;
            unsigned int sy=y*bitmap.height/PREVIEW_TEXTURE_SIZE;
            size_t at=((size_t)(sy/4u)*((bitmap.width+3u)/4u)+sx/4u)*block_bytes;
            if(!decoded && at!=prior){
                if(at>bitmap.first_mip_bytes || block_bytes>bitmap.first_mip_bytes-at ||
                   halo_s3tc_decode(bitmap.format==14?0x0cu:bitmap.format==15?0x0eu:0x0fu,
                       compressed+at,block_bytes,4,4,block,sizeof(block),16)){ok=0;break;}
                prior=at;
            }
            const unsigned char *rgba=decoded?decoded+((size_t)sy*bitmap.width+sx)*4u:
                block+((sy&3u)*4u+(sx&3u))*4u;
            unsigned int native_y=PREVIEW_TEXTURE_SIZE-1u-y;
            unsigned int tile=((native_y/8u)*(PREVIEW_TEXTURE_SIZE/8u)+x/8u)*64u+
                spread[x&7u]+2u*spread[native_y&7u];
            pixels[tile]=(uint32_t)rgba[0]<<24|(uint32_t)rgba[1]<<16|(uint32_t)rgba[2]<<8|255u;
        }
    void *result=ok?n3ds_gpu_texture_create(pixels,PREVIEW_TEXTURE_SIZE,PREVIEW_TEXTURE_SIZE):NULL;
    free(decoded);free(pixels);free(compressed);return result;
}
static int load_bsp(struct halo_cache *cache)
{
    struct halo_pc_bsp bsp={0};char error[128];
    if(!halo_pc_bsp_open(&bsp,cache,0,error,sizeof(error)))return 0;
    struct preview_face *heap=malloc(PREVIEW_TRIANGLES*sizeof(*heap));
    if(!heap){halo_pc_bsp_close(&bsp);return 0;}
    unsigned int kept=0;
    for(unsigned int lm=0;lm<bsp.lightmap_count;++lm){
        uint32_t material_count=0;
        if(!halo_pc_bsp_material_count(&bsp,lm,&material_count,error,sizeof(error)))break;
        for(unsigned int mi=0;mi<material_count;++mi){
            struct halo_pc_bsp_material m;
            if(!halo_pc_bsp_material_at(&bsp,lm,mi,&m,error,sizeof(error)))continue;
            if(!m.surface_count || !m.vertex_count || m.vertex_count>65535)continue;
            float *positions=malloc((size_t)m.vertex_count*5*sizeof(float));
            unsigned short *surface=malloc((size_t)m.surface_count*3*sizeof(short));
            if(positions && surface && halo_pc_bsp_read_material_uv(&bsp,&m,positions,
                (size_t)m.vertex_count*5,surface,(size_t)m.surface_count*3,error,sizeof(error))){
                for(unsigned int t=0;t<m.surface_count;++t){
                    const float *a=positions+surface[t*3]*5;
                    const float *b=positions+surface[t*3+1]*5;
                    const float *c=positions+surface[t*3+2]*5;
                    float uv[6]={a[3],a[4],b[3],b[4],c[3],c[4]};
                    for(int channel=0;channel<6;++channel)
                        if(!isfinite(uv[channel]) || fabsf(uv[channel])>100000.f)uv[channel]=0.f;
                    face_keep(heap,&kept,PREVIEW_TRIANGLES,a,b,c,uv,
                        m.shader_group==0x73656e76u?m.shader_datum:0xffffffffu);
                }
            }
            free(positions);free(surface);
        }
    }
    qsort(heap,kept,sizeof(*heap),face_material_compare);
    for(unsigned int i=0;i<kept;++i){
        if(!batch_count || batches[batch_count-1].shader_datum!=heap[i].shader_datum){
            struct preview_batch *batch=batches+batch_count++;
            *batch=(struct preview_batch){heap[i].shader_datum,triangles*3u,0,NULL};
        }
        unsigned int before=triangles;
        emit_triangle(heap[i].points,heap[i].points+3,heap[i].points+6,0,heap[i].uv);
        batches[batch_count-1].count+=(triangles-before)*3u;
    }
    material_uv=malloc((size_t)triangles*3u*2u*sizeof(float));
    if(material_uv)for(unsigned int i=0;i<triangles*3u;++i){
        material_uv[i*2]=vertices[i].uv[0];material_uv[i*2+1]=vertices[i].uv[1];
    }
    for(unsigned int i=0;i<batch_count && texture_count<PREVIEW_TEXTURE_LIMIT;++i){
        if(!batches[i].count)continue;
        batches[i].texture=load_material_texture(cache,batches[i].shader_datum);
        if(batches[i].texture)++texture_count;
    }
    load_nav(&bsp);
    free(heap);halo_pc_bsp_close(&bsp);return triangles>0;
}
static int load_model(struct halo_cache *cache,const char *wanted,int kind)
{
    char error[128];struct halo_cache_tag tag={0};int found=0;
    for(unsigned int i=0;i<cache->tag_count;++i){
        if(halo_cache_tag_at(cache,i,&tag,error,sizeof(error)) &&
            tag.group==0x6d6f6432u && !strcmp(tag.name,wanted)){found=1;break;}
    }
    if(!found)return 0;
    uint32_t parts=0,total=0;
    if(!halo_pc_model_part_count(cache,&tag,0,&parts,error,sizeof(error)))return 0;
    for(unsigned int p=0;p<parts;++p){struct halo_pc_model_part part;
        if(halo_pc_model_part_at(cache,&tag,0,p,&part,error,sizeof(error)))total+=part.strip_triangle_count;
    }
    unsigned int step=total/PREVIEW_TRIANGLES+1,global=0;
    for(unsigned int p=0;p<parts && triangles<PREVIEW_TRIANGLES;++p){
        struct halo_pc_model_part part;
        if(!halo_pc_model_part_at(cache,&tag,0,p,&part,error,sizeof(error)) ||
           !part.vertex_count || part.vertex_count>65535)continue;
        float *positions=malloc((size_t)part.vertex_count*3*sizeof(float));
        unsigned short *strip=malloc(((size_t)part.strip_triangle_count+2)*sizeof(short));
        if(positions && strip && halo_pc_model_read_part(cache,&part,positions,
            (size_t)part.vertex_count*3,strip,part.strip_triangle_count+2,error,sizeof(error))){
            for(unsigned int t=0;t<part.strip_triangle_count && triangles<PREVIEW_TRIANGLES;++t,++global){
                if(global%step)continue;
                unsigned short a=strip[t],b=strip[t+1],c=strip[t+2];
                if(a==b || b==c || c==a)continue;
                emit_triangle(positions+(t&1?b:a)*3,positions+(t&1?a:b)*3,positions+c*3,kind,NULL);
            }
        }else global+=part.strip_triangle_count;
        free(positions);free(strip);
    }
    return triangles>0;
}
static void normalize_mesh(void)
{
    float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(unsigned int i=0;i<triangles*3;++i)for(int a=0;a<3;++a){
        float x=vertices[i].position[a];if(x<lo[a])lo[a]=x;if(x>hi[a])hi[a]=x;
    }
    float span=fmaxf(hi[0]-lo[0],fmaxf(hi[1]-lo[1],hi[2]-lo[2]));
    if(span<.0001f)return;
    for(unsigned int i=0;i<triangles*3;++i)for(int a=0;a<3;++a)
        vertices[i].position[a]=(vertices[i].position[a]-(hi[a]+lo[a])*.5f)*2.f/span;
    for(unsigned int i=0;i<nav_triangles*3;++i){
        for(int a=0;a<3;++a)
            nav_vertices[i].position[a]=(nav_vertices[i].position[a]-(hi[a]+lo[a])*.5f)*2.f/span;
        nav_vertices[i].position[2]+=.012f;
    }
    n3ds_gpu_geometry_flush(vertices,triangles*3*sizeof(*vertices));
    n3ds_gpu_geometry_flush(indices,triangles*3*sizeof(*indices));
}
static void load_selection(int map,int page,int field,int weapon_set,int vehicle_choice)
{
    long long begun=n3ds_engine_ticks();
    n3ds_pc_preview_close();state_loaded=1;
    selected_map=map;selected_page=page;selected_field=field;
    selected_weapon=weapon_set;selected_vehicle=vehicle_choice;
    if(map<0 || map>=19 || (page && ((field==0 && weapon_set==10) ||
       (field==1 && vehicle_choice==2))) || !allocate_mesh())return;
    char path[128],message[180],error[128];
    snprintf(path,sizeof(path),"sdmc:/halo-source/pc-retail/%s.map",map_files[map]);
    struct halo_cache cache={0};
    if(!halo_cache_open(&cache,path,error,sizeof(error))){
        snprintf(message,sizeof(message),"PC PREVIEW: %s: %s",map_files[map],error);n3ds_log(message);return;
    }
    int kind=page?(field==1?2:1):0;
    int ok=0;
    if(kind==0)ok=load_bsp(&cache);
    else if(kind==1){
        static const unsigned char choice[]={0,0,1,2,3,1,4,5,5,1,0,6,7};
        ok=load_model(&cache,weapons[choice[weapon_set<13?weapon_set:0]],kind);
        if(!ok && weapon_set!=0)ok=load_model(&cache,weapons[0],kind);
    }else{
        static const unsigned char choice[]={0,4,0,1,2,3};
        ok=load_model(&cache,vehicles[choice[vehicle_choice<6?vehicle_choice:0]],kind);
        if(!ok && vehicle_choice!=0)ok=load_model(&cache,vehicles[0],kind);
    }
    halo_cache_close(&cache);
    if(!ok){triangles=0;snprintf(message,sizeof(message),"PC PREVIEW: no mesh for %s kind=%d",map_files[map],kind);n3ds_log(message);return;}
    normalize_mesh();snprintf(message,sizeof(message),
        "PC PREVIEW: %s kind=%d triangles=%u nav=%u batches=%u textures=%u load_ms=%.1f",
        map_files[map],kind,triangles,nav_triangles,batch_count,texture_count,
        1000.0*(n3ds_engine_ticks()-begun)/n3ds_engine_tick_frequency());n3ds_log(message);
}
void n3ds_pc_preview_input(const struct native_input_sample *sample)
{
    if(sample->held&NATIVE_KEY_R){zoom=fmaxf(2.0f,zoom*.97f);idle_frames=0;}
    if(sample->held&NATIVE_KEY_L){zoom=fminf(7.f,zoom*1.03f);idle_frames=0;}
    int touching=(sample->held&NATIVE_KEY_TOUCH)!=0;
    if(!touching){touch_held=0;touch_owner=TOUCH_NONE;dragging=0;}
    else if(!touch_held){
        touch_held=1;touch_owner=TOUCH_NONE;
        if(selected_page==0 && sample->touch_y>=26 && sample->touch_y<51){
            if(sample->touch_x>=12 && sample->touch_x<145){
                touch_owner=TOUCH_VIEW;view_mode=(view_mode+1)&3;idle_frames=0;
            }else if(sample->touch_x>=249 && sample->touch_x<310){
                touch_owner=TOUCH_NAV;nav_visible=!nav_visible;idle_frames=0;
            }
        }
        if(touch_owner==TOUCH_NONE && sample->touch_x>=12 && sample->touch_x<308 &&
           sample->touch_y>=24 && sample->touch_y<145){
            touch_owner=TOUCH_DRAG;dragging=1;
            last_x=sample->touch_x;last_y=sample->touch_y;idle_frames=0;
        }
    }else if(touch_owner==TOUCH_DRAG){
        if(sample->touch_x>=12 && sample->touch_x<308 && sample->touch_y>=24 && sample->touch_y<145){
            yaw+=(sample->touch_x-last_x)*.013f;
            elevation=fmaxf(.28f,fminf(1.30f,elevation+(sample->touch_y-last_y)*.008f));
            last_x=sample->touch_x;last_y=sample->touch_y;dragging=1;idle_frames=0;
        }else dragging=0;
    }
    if(!dragging && !(sample->held&(NATIVE_KEY_L|NATIVE_KEY_R)) && idle_frames<100000)++idle_frames;
}
int n3ds_pc_preview_nav_enabled(void){return nav_visible;}
int n3ds_pc_preview_view_mode(void){return view_mode;}
static void preview_aux_textures(void)
{
    if(!white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=0xffffffffu;
        white=n3ds_gpu_texture_create(pixels,8,8);}
    if(!xray_texture){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=0x54b8ff32u;
        xray_texture=n3ds_gpu_texture_create(pixels,8,8);}
    if(!wire_texture){
        unsigned int *pixels=malloc(PREVIEW_TEXTURE_SIZE*PREVIEW_TEXTURE_SIZE*sizeof(*pixels));
        if(pixels){
            static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
            for(unsigned int y=0;y<PREVIEW_TEXTURE_SIZE;++y)
                for(unsigned int x=0;x<PREVIEW_TEXTURE_SIZE;++x){
                    unsigned int native_y=PREVIEW_TEXTURE_SIZE-1u-y;
                    unsigned int tile=((native_y/8u)*(PREVIEW_TEXTURE_SIZE/8u)+x/8u)*64u+
                        spread[x&7u]+2u*spread[native_y&7u];
                    int edge=x<2u || y<2u || x+y>=PREVIEW_TEXTURE_SIZE-2u;
                    pixels[tile]=edge?0x75dbffffu:0x75dbff00u;
                }
            wire_texture=n3ds_gpu_texture_create(pixels,PREVIEW_TEXTURE_SIZE,PREVIEW_TEXTURE_SIZE);
            free(pixels);
        }
    }
}
static void preview_set_wire_uv(int wire)
{
    if(uv_mode==wire || !triangles)return;
    if(!wire && !material_uv)return;
    for(unsigned int i=0;i<triangles*3u;++i){
        if(wire){vertices[i].uv[0]=i%3u==1u?1.f:0.f;
            vertices[i].uv[1]=i%3u==2u?1.f:0.f;}
        else {vertices[i].uv[0]=material_uv[i*2u];
            vertices[i].uv[1]=material_uv[i*2u+1u];}
    }
    n3ds_gpu_geometry_flush(vertices,triangles*3u*sizeof(*vertices));uv_mode=wire;
}
void n3ds_pc_preview_draw(int map,int page,int field,int weapon_set,int vehicle_choice)
{
    if(!state_loaded || map!=selected_map || page!=selected_page ||
       (page && field!=selected_field) || weapon_set!=selected_weapon || vehicle_choice!=selected_vehicle)
        load_selection(map,page,field,weapon_set,vehicle_choice);
    if(!triangles)return;
    if(idle_frames>100)yaw+=.004f;
    preview_aux_textures();
    if(!white)return;
    float distance=zoom*(page?1.8f:1.f);
    float eye[3]={distance*cosf(yaw)*cosf(elevation),distance*sinf(yaw)*cosf(elevation),
        distance*sinf(elevation)};
    /* The preview occupies the upper 120 pixels; aim below mesh center so
     * its visual center sits above the controls instead of behind them. */
    float forward[3]={-eye[0],-eye[1],-.45f-eye[2]};
    float length=sqrtf(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
    struct native_render_camera camera={0};
    for(int i=0;i<3;++i){camera.position[i]=eye[i];camera.forward[i]=forward[i]/length;}
    camera.up[2]=1.f;camera.vertical_fov=.72f;camera.near_clip=.1f;camera.far_clip=20.f;
    camera.clear_color=0xff171c28u;
    n3ds_gpu_hud_screen(1);n3ds_gpu_window_begin(&camera);
    if(page){
        n3ds_gpu_texture_bind(0,white);
        n3ds_gpu_geometry_draw(vertices,indices,triangles*3u);
    }else if(view_mode==VIEW_LIT){
        preview_set_wire_uv(0);
        if(!batch_count){n3ds_gpu_texture_bind(0,white);
            n3ds_gpu_preview_material_draw(vertices,indices,triangles*3u,0,1);}
        for(unsigned int i=0;i<batch_count;++i)if(batches[i].count){
            n3ds_gpu_texture_bind(0,batches[i].texture?batches[i].texture:white);
            n3ds_gpu_preview_material_draw(vertices,indices+batches[i].first,batches[i].count,0,1);
        }
    }else{
        int wire=view_mode==VIEW_WIRE || view_mode==VIEW_XRAY_WIRE;
        if(wire)preview_set_wire_uv(1);
        if(view_mode==VIEW_WIRE)n3ds_gpu_model_depth_draw(vertices,indices,triangles*3u);
        if((view_mode==VIEW_XRAY || view_mode==VIEW_XRAY_WIRE) && xray_texture){
            n3ds_gpu_texture_bind(0,xray_texture);
            n3ds_gpu_preview_material_draw(vertices,indices,triangles*3u,1,0);
        }
        if(wire && wire_texture){
            n3ds_gpu_texture_bind(0,wire_texture);
            n3ds_gpu_preview_material_draw(vertices,indices,triangles*3u,1,view_mode==VIEW_WIRE);
        }
    }
    if(!page && nav_visible && nav_triangles){
        n3ds_gpu_texture_bind(0,white);
        n3ds_gpu_effect_draw(nav_vertices,nav_triangles*3,NATIVE_BLEND_ALPHA,0,6,0);
    }
    n3ds_gpu_world_state_restore();
}
