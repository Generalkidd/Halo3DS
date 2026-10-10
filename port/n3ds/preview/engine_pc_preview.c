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

void n3ds_log(const char *message);
enum { PREVIEW_TRIANGLES=2048, PREVIEW_VERTICES=PREVIEW_TRIANGLES*3,
       NAV_TRIANGLES=1024 };
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
static unsigned int triangles;
static struct native_widget_vertex *nav_vertices;
static unsigned int nav_triangles;
static int nav_visible=1,nav_touch_held;
static int selected_map=-1,selected_page=-1,selected_field=-1,selected_weapon=-1,selected_vehicle=-1;
static int state_loaded;
static float yaw=.65f,elevation=.72f,zoom=2.7f;
static unsigned int idle_frames;
static int dragging,last_x,last_y;
static void *white;

void n3ds_pc_preview_close(void)
{
    n3ds_gpu_geometry_free(vertices);n3ds_gpu_geometry_free(indices);
    free(nav_vertices);nav_vertices=NULL;nav_triangles=0;
    vertices=NULL;indices=NULL;triangles=0;state_loaded=0;
    if(white){n3ds_gpu_texture_destroy(white);white=NULL;}
}

static int allocate_mesh(void)
{
    vertices=n3ds_gpu_geometry_optional_allocate(PREVIEW_VERTICES*sizeof(*vertices));
    indices=n3ds_gpu_geometry_optional_allocate(PREVIEW_VERTICES*sizeof(*indices));
    return vertices && indices;
}
static void emit_triangle(const float *a,const float *b,const float *c,int kind)
{
    if(triangles>=PREVIEW_TRIANGLES)return;
    float e0[3],e1[3],n[3];
    for(int i=0;i<3;++i){e0[i]=b[i]-a[i];e1[i]=c[i]-a[i];}
    n[0]=e0[1]*e1[2]-e0[2]*e1[1];n[1]=e0[2]*e1[0]-e0[0]*e1[2];
    n[2]=e0[0]*e1[1]-e0[1]*e1[0];
    float area=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(!isfinite(area) || area<.00001f)return;
    float light=.46f+.43f*fmaxf(0.f,(n[0]*.38f+n[1]*.22f+n[2]*.90f)/area);
    const float base[3][3]={{.43f,.69f,.72f},{.77f,.61f,.38f},{.58f,.72f,.87f}};
    const float *points[3]={a,b,c};
    for(int j=0;j<3;++j){
        struct native_render_vertex *v=vertices+triangles*3+j;
        for(int i=0;i<3;++i){v->position[i]=points[j][i];v->color[i]=base[kind][i]*light;}
        v->uv[0]=v->uv[1]=0;
        indices[triangles*3+j]=(unsigned short)(triangles*3+j);
    }
    ++triangles;
}
struct preview_face {float points[9];float area;};
static void face_swap(struct preview_face *a,struct preview_face *b)
{struct preview_face tmp=*a;*a=*b;*b=tmp;}
/* Retain broad surfaces rather than every Nth triangle. The latter leaves
 * isolated specks across large Halo BSPs and hides their terrain shape. */
static void face_keep(struct preview_face *heap,unsigned int *count,unsigned int limit,
    const float *a,const float *b,const float *c)
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
    memcpy(face->points+6,c,3*sizeof(float));face->area=area;
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
            face_keep(heap,&kept,NAV_TRIANGLES,points[0],points[i],points[i+1]);
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
            float *positions=malloc((size_t)m.vertex_count*3*sizeof(float));
            unsigned short *surface=malloc((size_t)m.surface_count*3*sizeof(short));
            if(positions && surface && halo_pc_bsp_read_material(&bsp,&m,positions,
                (size_t)m.vertex_count*3,surface,(size_t)m.surface_count*3,error,sizeof(error))){
                for(unsigned int t=0;t<m.surface_count;++t)
                    face_keep(heap,&kept,PREVIEW_TRIANGLES,positions+surface[t*3]*3,
                        positions+surface[t*3+1]*3,positions+surface[t*3+2]*3);
            }
            free(positions);free(surface);
        }
    }
    for(unsigned int i=0;i<kept;++i)
        emit_triangle(heap[i].points,heap[i].points+3,heap[i].points+6,0);
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
                emit_triangle(positions+(t&1?b:a)*3,positions+(t&1?a:b)*3,positions+c*3,kind);
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
    normalize_mesh();snprintf(message,sizeof(message),"PC PREVIEW: %s kind=%d triangles=%u nav=%u",map_files[map],kind,triangles,nav_triangles);n3ds_log(message);
}
void n3ds_pc_preview_input(const struct native_input_sample *sample)
{
    int nav_touch=selected_page==0 && (sample->held&NATIVE_KEY_TOUCH) &&
        sample->touch_x>=249 && sample->touch_x<310 &&
        sample->touch_y>=26 && sample->touch_y<51;
    if(nav_touch && !nav_touch_held){nav_visible=!nav_visible;idle_frames=0;}
    nav_touch_held=nav_touch;
    if(sample->held&NATIVE_KEY_R){zoom=fmaxf(2.0f,zoom*.97f);idle_frames=0;}
    if(sample->held&NATIVE_KEY_L){zoom=fminf(7.f,zoom*1.03f);idle_frames=0;}
    if(!nav_touch && (sample->held&NATIVE_KEY_TOUCH) && sample->touch_x>=12 && sample->touch_x<308 &&
       sample->touch_y>=24 && sample->touch_y<145){
        if(dragging){yaw+=(sample->touch_x-last_x)*.013f;
            elevation=fmaxf(.28f,fminf(1.30f,elevation+(sample->touch_y-last_y)*.008f));}
        last_x=sample->touch_x;last_y=sample->touch_y;dragging=1;idle_frames=0;
    }else dragging=0;
    if(!dragging && !(sample->held&(NATIVE_KEY_L|NATIVE_KEY_R)) && idle_frames<100000)++idle_frames;
}
int n3ds_pc_preview_nav_enabled(void){return nav_visible;}
void n3ds_pc_preview_draw(int map,int page,int field,int weapon_set,int vehicle_choice)
{
    if(!state_loaded || map!=selected_map || page!=selected_page ||
       (page && field!=selected_field) || weapon_set!=selected_weapon || vehicle_choice!=selected_vehicle)
        load_selection(map,page,field,weapon_set,vehicle_choice);
    if(!triangles)return;
    if(idle_frames>100)yaw+=.004f;
    if(!white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=0xffffffffu;
        white=n3ds_gpu_texture_create(pixels,8,8);}
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
    n3ds_gpu_texture_bind(0,white);
    n3ds_gpu_geometry_draw(vertices,indices,triangles*3);
    if(!page && nav_visible && nav_triangles){
        n3ds_gpu_texture_bind(0,white);
        n3ds_gpu_effect_draw(nav_vertices,nav_triangles*3,NATIVE_BLEND_ALPHA,0,6,0);
    }
    n3ds_gpu_world_state_restore();
}
