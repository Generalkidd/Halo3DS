/* Runtime scenery LOD, map-generation lifetime. Small disconnected foliage
 * islands are kept whole, with position seams joined for connectivity; no edits to
 * collision or original game data. Compact vertices actually reduce shading.
 * All allocation failures retain the original mesh. */
#define SCENERY_MESH_SLOTS 96
static struct scenery_mesh {
 const void *source,*source_indices;unsigned int source_count,source_index_count,quality;
 struct model_vertex_compressed *vertices;unsigned short *indices;
 unsigned int count,index_count,bytes;
} scenery_meshes[SCENERY_MESH_SLOTS];
static unsigned int scenery_mesh_bytes,scenery_saved_triangles,scenery_saved_vertices;
static void scenery_mesh_release(void)
{
 for(unsigned int i=0;i<SCENERY_MESH_SLOTS;++i){free(scenery_meshes[i].vertices);free(scenery_meshes[i].indices);}
 memset(scenery_meshes,0,sizeof(scenery_meshes));scenery_mesh_bytes=0;
}
static unsigned int scenery_root(unsigned short *roots,unsigned int i)
{while(roots[i]!=i){roots[i]=roots[roots[i]];i=roots[i];}return i;}
static unsigned int scenery_position_hash(const struct model_vertex_compressed *v)
{
 unsigned int bits[3];float xyz[3]={v->position.x,v->position.y,v->position.z};
 /* Equal +0/-0 positions must hash identically. */
 for(unsigned int k=0;k<3;++k){if(xyz[k]==0.f)xyz[k]=0.f;memcpy(bits+k,xyz+k,4);}
 return (bits[0]*0x9e3779b1u)^(bits[1]*0x85ebca6bu)^(bits[2]*0xc2b2ae35u);
}
static struct scenery_mesh *scenery_mesh_get(const void *source,const void *key,unsigned int n,const unsigned short *indices,unsigned int ni,unsigned int quality)
{
 struct scenery_mesh *slot=NULL;
 for(unsigned int i=0;i<SCENERY_MESH_SLOTS;++i){struct scenery_mesh *s=scenery_meshes+i;
  if(s->source==source && s->source_indices==key && s->source_count==n && s->source_index_count==ni && s->quality==quality)return s->vertices?s:NULL;
  if(!s->source && !slot)slot=s;
 }
 if(!slot || n<12 || n>8192 || ni<24 || ni>49152 || ni%3 || quality<2 || quality>6)return NULL;
 *slot=(struct scenery_mesh){.source=source,.source_indices=key,.source_count=n,.source_index_count=ni,.quality=quality};
 unsigned int buckets=1;while(buckets<n*2)buckets*=2;
 unsigned short *roots=malloc(n*2),*sizes=calloc(n,2),*remap=malloc(n*2),*positions=malloc(buckets*2);
 if(!roots || !sizes || !remap || !positions){free(roots);free(sizes);free(remap);free(positions);return NULL;}
 for(unsigned int i=0;i<n;++i){roots[i]=i;remap[i]=65535;}
 memset(positions,255,buckets*2);
 const struct model_vertex_compressed *v=source;
 /* UV and normal seams duplicate vertices in solid geometry. Join coincident
  * positions for classification only; retain original vertices when drawing. */
 for(unsigned int i=0;i<n;++i){
  unsigned int h=scenery_position_hash(v+i)&(buckets-1);
  while(positions[h]!=65535){unsigned int j=positions[h];
   if(v[i].position.x==v[j].position.x && v[i].position.y==v[j].position.y && v[i].position.z==v[j].position.z){roots[i]=scenery_root(roots,j);break;}
   h=(h+1)&(buckets-1);
  }
  if(positions[h]==65535)positions[h]=i;
 }
 for(unsigned int i=0;i<ni;i+=3){
  if(indices[i]>=n || indices[i+1]>=n || indices[i+2]>=n)goto done;
  unsigned int a=scenery_root(roots,indices[i]),b=scenery_root(roots,indices[i+1]),c=scenery_root(roots,indices[i+2]);roots[b]=a;roots[c]=a;
 }
 for(unsigned int i=0;i<n;++i)++sizes[scenery_root(roots,i)];
 /* Retain at least two small islands and every connected structural piece. */
 unsigned int islands=0;
 for(unsigned int i=0;i<n;++i)if(sizes[i]>=3 && sizes[i]<=12)++islands;
 if(islands<8)goto done;
 unsigned int island=0;
 for(unsigned int i=0;i<n;++i)if(sizes[i]>=3 && sizes[i]<=12){if(island++%quality)sizes[i]=0;}
 unsigned int used=0,verts=0;
 for(unsigned int i=0;i<ni;i+=3)if(sizes[scenery_root(roots,indices[i])]){
  used+=3;for(unsigned int k=0;k<3;++k)if(remap[indices[i+k]]==65535)remap[indices[i+k]]=verts++;
 }
 if(used>=ni || used<6 || verts>=n)goto done;
 extern int n3ds_input_platform_original_model(void);
 unsigned int bytes=verts*sizeof(struct model_vertex_compressed)+used*2,budget=n3ds_input_platform_original_model()?192*1024:768*1024;
 if(bytes>budget || scenery_mesh_bytes>budget-bytes)goto done;
 slot->vertices=malloc(verts*sizeof(*slot->vertices));slot->indices=malloc(used*2);
 if(!slot->vertices || !slot->indices){free(slot->vertices);free(slot->indices);slot->vertices=NULL;slot->indices=NULL;goto done;}
 for(unsigned int i=0;i<n;++i)if(remap[i]!=65535)slot->vertices[remap[i]]=v[i];
 unsigned int out=0;
 for(unsigned int i=0;i<ni;i+=3)if(sizes[scenery_root(roots,indices[i])])for(unsigned int k=0;k<3;++k)slot->indices[out++]=remap[indices[i+k]];
 slot->count=verts;slot->index_count=used;slot->bytes=bytes;scenery_mesh_bytes+=bytes;
 {char line[160];snprintf(line,sizeof(line),"SCENERY MESH: vertices=%u->%u triangles=%u->%u islands=%u quality=%u storage=%u",n,verts,ni/3,used/3,islands,quality,scenery_mesh_bytes);n3ds_log(line);}
 done:free(roots);free(sizes);free(remap);free(positions);return slot->vertices?slot:NULL;
}
int n3ds_scenery_mesh_tests(void)
{
 struct model_vertex_compressed v[40]={0};unsigned short ids[48];
 for(unsigned int i=0;i<40;++i){v[i].position.x=i;v[i].nodes[0]=0;v[i].nodes[1]=253;v[i].node_weight=32767;}
 for(unsigned int i=0;i<8;++i){unsigned int b=i*4;unsigned short q[6]={b,b+1,b+2,b,b+2,b+3};memcpy(ids+i*6,q,12);}
 struct scenery_mesh *s=scenery_mesh_get(v,ids,40,ids,48,2);
 int ok=s && s->count==16 && s->index_count==24 && !memcmp(s->vertices,v,4*sizeof(*v)) && !memcmp(s->vertices+4,v+8,4*sizeof(*v));
 if(s)for(unsigned int i=0;i<s->index_count;++i)if(s->indices[i]>=s->count)ok=0;
 if(scenery_mesh_get(v,ids,40,ids,48,2)!=s)ok=0;
 /* The Original distant profile keeps two complete leaf islands, never
  * individual disconnected triangles or out-of-range compacted indices. */
 s=scenery_mesh_get(v,ids,40,ids,48,6);
 if(!s || s->count!=8 || s->index_count!=12 || memcmp(s->vertices,v,4*sizeof(*v)) || memcmp(s->vertices+4,v+24,4*sizeof(*v)))ok=0;
 scenery_mesh_release();
 /* A solid grid with every triangle split at UV seams must remain intact. */
 struct model_vertex_compressed seam[86]={0};unsigned short seam_ids[102];unsigned int out=0;
 for(unsigned int y=0;y<3;++y)for(unsigned int x=0;x<3;++x){
  unsigned int dx[6]={0,1,1,0,1,0},dy[6]={0,0,1,0,1,1};
  for(unsigned int k=0;k<6;++k){seam[out].position.x=x+dx[k];seam[out].position.y=y+dy[k];seam_ids[out]=out;++out;}
 }
 for(unsigned int i=0;i<32;++i){seam[54+i].position.x=100+i;seam[54+i].position.y=10;}
 for(unsigned int i=0;i<8;++i){unsigned int b=54+i*4;unsigned short q[6]={b,b+1,b+2,b,b+2,b+3};memcpy(seam_ids+54+i*6,q,12);}
 s=scenery_mesh_get(seam,seam_ids,86,seam_ids,102,2);
 if(!s || s->count!=70 || s->index_count!=78 || memcmp(s->vertices,seam,54*sizeof(*seam)))ok=0;
 s=scenery_mesh_get(seam,seam_ids,86,seam_ids,102,6);
 if(!s || s->count!=62 || s->index_count!=66 || memcmp(s->vertices,seam,54*sizeof(*seam)))ok=0;
 scenery_mesh_release();return ok;
}
