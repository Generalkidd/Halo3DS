/* Eight rigid instances share one PICA bone-palette draw. Geometry is copied
 * once per map into a bounded optional cache; current transforms remain uniforms.
 * Exact material/light keys, opaque scenery only, no transparent reordering. */
#define SCENERY_INSTANCES 8
#define SCENERY_BATCH_CACHE 32
#define SCENERY_BATCH_GROUPS 16
static struct scenery_batch_mesh {const void *source,*key;unsigned int count,indices;struct native_skin_vertex *v;struct native_render_vertex *rigid;unsigned short *i;} scenery_batch_meshes[SCENERY_BATCH_CACHE];
static struct scenery_batch_group {
 struct scenery_batch_mesh *mesh;struct native_model_material material;struct native_model_lighting lighting;
 float rows[SCENERY_INSTANCES*12],uv[2],center[3];unsigned int count;int sides;
} scenery_batch_groups[SCENERY_BATCH_GROUPS];
static unsigned int scenery_batch_bytes,scenery_batch_avoided,scenery_batch_draws,scenery_batch_instances;
static struct scenery_batch_mesh *scenery_batch_mesh_get(const void *source,const void *key,const struct native_render_vertex *v,unsigned int count,const unsigned short *indices,unsigned int ni)
{
 struct scenery_batch_mesh *slot=NULL;
 for(unsigned int i=0;i<SCENERY_BATCH_CACHE;++i){struct scenery_batch_mesh *s=scenery_batch_meshes+i;
  if(s->source==source && s->key==key && s->count==count && s->indices==ni)return s->v?s:NULL;
  if(!s->source && !slot)slot=s;
 }
 if(!slot || !count || count>512 || !ni || ni>4096)return NULL;
 *slot=(struct scenery_batch_mesh){.source=source,.key=key,.count=count,.indices=ni};
 extern int n3ds_input_platform_original_model(void);
 unsigned int bytes=SCENERY_INSTANCES*(count*sizeof(*slot->v)+ni*2)+count*sizeof(*slot->rigid),budget=n3ds_input_platform_original_model()?128*1024:768*1024;
 if(bytes>budget || scenery_batch_bytes>budget-bytes)return NULL;
 slot->v=n3ds_gpu_geometry_optional_allocate(count*SCENERY_INSTANCES*sizeof(*slot->v));
 slot->i=n3ds_gpu_geometry_optional_allocate(ni*SCENERY_INSTANCES*2);
 slot->rigid=n3ds_gpu_geometry_optional_allocate(count*sizeof(*slot->rigid));
 if(!slot->v || !slot->i || !slot->rigid){n3ds_gpu_geometry_free(slot->rigid);slot->rigid=NULL;n3ds_gpu_geometry_free(slot->v);n3ds_gpu_geometry_free(slot->i);slot->v=NULL;slot->i=NULL;return NULL;}
 memcpy(slot->rigid,v,count*sizeof(*v));n3ds_gpu_geometry_flush(slot->rigid,count*sizeof(*v));
 for(unsigned int j=0;j<SCENERY_INSTANCES;++j){
  for(unsigned int k=0;k<count;++k){struct native_skin_vertex *out=slot->v+j*count+k;memcpy(out,v+k,sizeof(*v));out->bones[0]=j*3;out->bones[1]=0;out->bones[2]=1;out->bones[3]=0;}
  for(unsigned int k=0;k<ni;++k){assert(indices[k]<count);slot->i[j*ni+k]=indices[k]+j*count;}
 }
 n3ds_gpu_geometry_flush(slot->v,count*SCENERY_INSTANCES*sizeof(*slot->v));n3ds_gpu_geometry_flush(slot->i,ni*SCENERY_INSTANCES*2);scenery_batch_bytes+=bytes;return slot;
}
static void scenery_batch_submit(struct scenery_batch_group *g)
{
 if(!g->count)return;
 g->material.lighting=&g->lighting;
 if(g->count==1){assert(n3ds_gpu_model_rigid_draw(g->mesh->rigid,g->mesh->i,g->mesh->indices,g->rows,g->uv,0,0,g->sides,&g->material));}
 else {assert(n3ds_gpu_model_skin_draw(g->mesh->v,g->mesh->i,g->mesh->indices*g->count,g->rows,g->count,g->uv,0,0,g->sides,&g->material));}
 scenery_batch_avoided+=g->count-1;scenery_batch_instances+=g->count;++scenery_batch_draws;g->count=0;
}
static void scenery_batch_flush(void)
{for(unsigned int i=0;i<SCENERY_BATCH_GROUPS;++i)scenery_batch_submit(scenery_batch_groups+i);}
static void scenery_batch_release(void)
{
 assert(!scenery_batch_allowed);
 if(scenery_batch_bytes){assert(n3ds_gpu_texture_barrier());}
 for(unsigned int i=0;i<SCENERY_BATCH_CACHE;++i){n3ds_gpu_geometry_free(scenery_batch_meshes[i].rigid);n3ds_gpu_geometry_free(scenery_batch_meshes[i].v);n3ds_gpu_geometry_free(scenery_batch_meshes[i].i);}
 memset(scenery_batch_meshes,0,sizeof(scenery_batch_meshes));memset(scenery_batch_groups,0,sizeof(scenery_batch_groups));scenery_batch_bytes=0;
}
static int scenery_batch_add(const void *source,const void *key,const struct native_render_vertex *v,unsigned int count,const unsigned short *indices,unsigned int ni,const float *rows,const float *uv,int sides,const struct native_model_material *material)
{
#ifdef HALO_N3DS_SCENERY_NO_BATCH
 return 0;
#endif
 if(!scenery_batch_allowed || !material->lighting || !material->base)return 0;
 struct scenery_batch_mesh *mesh=scenery_batch_mesh_get(source,key,v,count,indices,ni);if(!mesh)return 0;
 struct native_model_material compare=*material;compare.lighting=NULL;
 struct scenery_batch_group *empty=NULL;
 for(unsigned int i=0;i<SCENERY_BATCH_GROUPS;++i){struct scenery_batch_group *g=scenery_batch_groups+i;
  if(!g->count){if(!empty)empty=g;continue;}
  /* Each palette matrix is independently camera-relative. Exact shared
   * lighting/materials are sufficient; world distance adds no correctness
   * constraint and unnecessarily splits rows of identical scenery. */
  if(g->mesh!=mesh || g->sides!=sides || memcmp(g->uv,uv,8) || memcmp(&g->material,&compare,sizeof(compare)) || memcmp(&g->lighting,material->lighting,sizeof(g->lighting)))continue;
  memcpy(g->rows+g->count*12,rows,48);if(++g->count==SCENERY_INSTANCES)scenery_batch_submit(g);return 1;
 }
 if(!empty){scenery_batch_flush();empty=scenery_batch_groups;}
 *empty=(struct scenery_batch_group){.mesh=mesh,.material=compare,.lighting=*material->lighting,.count=1,.sides=sides};
 memcpy(empty->rows,rows,48);memcpy(empty->uv,uv,8);for(int a=0;a<3;++a)empty->center[a]=rows[a*4+3];return 1;
}
void n3ds_scenery_report(void)
{
 char line[200];snprintf(line,sizeof(line),"SCENERY WORK: triangles_saved=%u vertices_saved=%u instanced=%u draws=%u avoided=%u mesh_bytes=%u batch_bytes=%u",scenery_saved_triangles,scenery_saved_vertices,scenery_batch_instances,scenery_batch_draws,scenery_batch_avoided,scenery_mesh_bytes,scenery_batch_bytes);n3ds_log(line);
 scenery_saved_triangles=scenery_saved_vertices=scenery_batch_instances=scenery_batch_draws=scenery_batch_avoided=0;
}

int n3ds_scenery_batch_tests(void)
{
 struct native_render_vertex v[4]={{{-1,-1,-2},{0,0,1},{0,0}},{{1,-1,-2},{0,0,1},{1,0}},{{1,1,-2},{0,0,1},{1,1}},{{-1,1,-2},{0,0,1},{0,1}}};
 const unsigned short ids[6]={0,1,2,0,2,3};
 struct scenery_batch_mesh *m=scenery_batch_mesh_get(v,ids,v,4,ids,6);
 int ok=m!=NULL;
 if(m)for(unsigned int j=0;j<SCENERY_INSTANCES;++j){
  for(unsigned int i=0;i<4;++i)if(memcmp(m->v+j*4+i,v+i,sizeof(v[0])) || m->v[j*4+i].bones[0]!=j*3 || m->v[j*4+i].bones[2]!=1 || m->v[j*4+i].bones[3]!=0)ok=0;
  for(unsigned int i=0;i<6;++i)if(m->i[j*6+i]!=ids[i]+j*4)ok=0;
 }
 if(m && memcmp(m->rigid,v,sizeof(v)))ok=0;
 if(scenery_batch_mesh_get(v,ids,v,4,ids,6)!=m)ok=0;
 scenery_batch_release();return ok;
}
