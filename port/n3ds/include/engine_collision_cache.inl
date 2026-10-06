/* Cache derived world collision features, never collision outcomes. Moving
 * actors still query the current BSP and every live object on every tick.
 * Identical ordered surface/edge/vertex sets and pill dimensions produce
 * identical world features. No enlarged spheres or skipped collision tests. */
#define COLLISION_CACHE_SLOTS 128
static struct world_feature_cache {
 const void *bsp;unsigned int hash,bytes,key_bytes,stamp;short counts[3];byte *data;
} world_feature_cache[COLLISION_CACHE_SLOTS];
static unsigned int world_feature_bytes,world_feature_hits,world_feature_misses,world_feature_verified,world_feature_stamp;
void n3ds_collision_cache_clear(void)
{
 for(unsigned int i=0;i<COLLISION_CACHE_SLOTS;++i)free(world_feature_cache[i].data);
 memset(world_feature_cache,0,sizeof(world_feature_cache));world_feature_bytes=0;
}
static unsigned int world_feature_size(const short *counts)
{return counts[0]*sizeof(struct collision_sphere)+counts[1]*sizeof(struct collision_cylinder)+counts[2]*sizeof(struct collision_prism);}
static void world_feature_copy(struct collision_feature_list *f,byte *data,int store)
{
 void *parts[3]={f->spheres,f->cylinders,f->prisms};
 unsigned int sizes[3]={sizeof(struct collision_sphere),sizeof(struct collision_cylinder),sizeof(struct collision_prism)};
 for(int i=0;i<3;++i){unsigned int bytes=f->count[i]*sizes[i];if(store)memcpy(data,parts[i],bytes);else memcpy(parts[i],data,bytes);data+=bytes;}
}
static unsigned int world_feature_key(unsigned int *key,const struct collision_bsp_test_sphere_result *r,real height,real width)
{
 unsigned int n=0;memcpy(key+n++,&height,4);memcpy(key+n++,&width,4);
 const long *indices[3]={r->vertex_indices,r->edge_indices,r->surface_indices};
 long counts[3]={r->vertex_count,r->edge_count,r->surface_count};
 for(int j=0;j<3;++j){if(counts[j]<0 || counts[j]>256)return 0;key[n++]=counts[j];
  memcpy(key+n,indices[j],counts[j]*4);n+=counts[j];}
 return n*4;
}
/* Two-way lookup prevents neighboring characters evicting each other's
 * features. Reuse allocation capacity: no per-tick free/malloc churn. */
static unsigned int world_feature_hash(const unsigned int *key,unsigned int size)
{unsigned int hash=2166136261u;for(unsigned int i=0;i<size/4;++i)hash=(hash^key[i])*16777619u;return hash;}
static struct world_feature_cache *world_feature_pair(unsigned int hash)
{
 extern int n3ds_input_platform_original_model(void);
 unsigned int sets=n3ds_input_platform_original_model()?32:64;
 return world_feature_cache+(hash&(sets-1))*2;
}
static int world_feature_reuse(const struct collision_bsp *bsp,const unsigned int *key,unsigned int size,struct collision_feature_list *features)
{
 unsigned int hash=world_feature_hash(key,size);struct world_feature_cache *pair=world_feature_pair(hash);
 for(int i=0;i<2;++i){struct world_feature_cache *slot=pair+i;
  if(slot->data && slot->bsp==bsp && slot->hash==hash && slot->key_bytes==size && !memcmp(slot->data,key,size)) {
   slot->stamp=++world_feature_stamp;memcpy(features->count,slot->counts,sizeof(slot->counts));world_feature_copy(features,slot->data+size,0);++world_feature_hits;return 1;
  }
 }
 ++world_feature_misses;return 0;
}
static void world_feature_store(const struct collision_bsp *bsp,const unsigned int *key,unsigned int size,struct collision_feature_list *features)
{
 extern int n3ds_input_platform_original_model(void);
 unsigned int bytes=size+world_feature_size(features->count),budget=n3ds_input_platform_original_model()?64*1024:192*1024;
 if(bytes>budget/4)return;
 unsigned int hash=world_feature_hash(key,size);struct world_feature_cache *pair=world_feature_pair(hash);
 struct world_feature_cache *slot=pair+(pair[0].stamp>pair[1].stamp);
 if(bytes>slot->bytes){
  if(world_feature_bytes-slot->bytes>budget-bytes)return;
  byte *data=(realloc)(slot->data,bytes);if(!data)return;
  world_feature_bytes+=bytes-slot->bytes;slot->data=data;slot->bytes=bytes;
 }
 slot->bsp=bsp;slot->hash=hash;slot->key_bytes=size;slot->stamp=++world_feature_stamp;
 memcpy(slot->counts,features->count,sizeof(slot->counts));memcpy(slot->data,key,size);world_feature_copy(features,slot->data+size,1);
}
void n3ds_collision_cache_report(void)
{
 extern void n3ds_log(const char *);char message[128];
 snprintf(message,sizeof(message),"WORLD COLLISION REUSE: hits=%u misses=%u verified=%u bytes=%u",world_feature_hits,world_feature_misses,world_feature_verified,world_feature_bytes);n3ds_log(message);
 world_feature_hits=world_feature_misses=world_feature_verified=0;
}
