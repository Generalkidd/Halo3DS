/* Screen-tile overdraw budget for cosmetic world sprites. Separate additive
 * flashes from blended smoke so either remains visible in a mixed explosion.
 * Small sparks/projectiles and every first-person/screen-space sprite bypass
 * the budget. Particle lifetimes, collision, damage and emitters are unchanged. */
enum { NATIVE_EFFECT_COLS=10,NATIVE_EFFECT_ROWS=6 };
static float native_effect_layers[2][NATIVE_EFFECT_ROWS][NATIVE_EFFECT_COLS];
static unsigned int native_effect_kept,native_effect_clipped;
static float native_effect_area_saved;
static int native_effect_rect(const real_rectangle3d *b,real_rectangle2d *r)
{
 if(b->z0>=0)return 0;
 if(b->z1>=-.001f){*r=(real_rectangle2d){-1,1,-1,1};return 1;}
 float iz[2]={-1.f/b->z0,-1.f/b->z1};
 float x0=REAL_MAX,x1=-REAL_MAX,y0=REAL_MAX,y1=-REAL_MAX;
 for(int z=0;z<2;++z) {
  float a=b->x0*render.frustum.projection_matrix[0][0]*iz[z]-render.frustum.projection_matrix[2][0];
  float c=b->x1*render.frustum.projection_matrix[0][0]*iz[z]-render.frustum.projection_matrix[2][0];
  float d=b->y0*render.frustum.projection_matrix[1][1]*iz[z]-render.frustum.projection_matrix[2][1];
  float e=b->y1*render.frustum.projection_matrix[1][1]*iz[z]-render.frustum.projection_matrix[2][1];
  x0=MIN(x0,MIN(a,c));x1=MAX(x1,MAX(a,c));y0=MIN(y0,MIN(d,e));y1=MAX(y1,MAX(d,e));
 }
 r->x0=MAX(-1.f,x0);r->x1=MIN(1.f,x1);r->y0=MAX(-1.f,y0);r->y1=MIN(1.f,y1);
 return r->x1>r->x0 && r->y1>r->y0;
}
static int native_effect_accept(const struct build_sprite_data *data,const real_rectangle3d *bounds)
{
 if(TEST_FLAG(data->flags,_build_sprites_first_person_bit))return 1;
 real_rectangle2d r;if(!native_effect_rect(bounds,&r))return 0;
 float area=(r.x1-r.x0)*(r.y1-r.y0)*.25f;
 if(area<.015f)return 1;
 extern int n3ds_input_platform_original_model(void);
 float limit=n3ds_input_platform_original_model()?2.f:6.f;
 int blend=data->shader && data->shader->framebuffer_blend_function==3;
 float saturated=0,width[NATIVE_EFFECT_COLS],height[NATIVE_EFFECT_ROWS];
 for(int x=0;x<NATIVE_EFFECT_COLS;++x){float left=-1.f+x*.2f;width[x]=MAX(0.f,MIN(r.x1,left+.2f)-MAX(r.x0,left))*5.f;}
 for(int y=0;y<NATIVE_EFFECT_ROWS;++y){float bottom=-1.f+y*(1.f/3.f);height[y]=MAX(0.f,MIN(r.y1,bottom+1.f/3.f)-MAX(r.y0,bottom))*3.f;}
 for(int y=0;y<NATIVE_EFFECT_ROWS;++y)if(height[y]>0)for(int x=0;x<NATIVE_EFFECT_COLS;++x)
  if(native_effect_layers[blend][y][x]+.0001f>=limit)saturated+=width[x]*height[y];
 if(saturated>area*30.f){++native_effect_clipped;native_effect_area_saved+=area;return 0;}
 for(int y=0;y<NATIVE_EFFECT_ROWS;++y)if(height[y]>0)for(int x=0;x<NATIVE_EFFECT_COLS;++x)native_effect_layers[blend][y][x]+=width[x]*height[y];
 ++native_effect_kept;return 1;
}
int n3ds_effect_coverage_tests(void)
{
 struct render_frustum saved=render.frustum;memset(&render.frustum,0,sizeof(render.frustum));render.frustum.projection_matrix[0][0]=render.frustum.projection_matrix[1][1]=1;
 struct build_sprite_data data={0};real_rectangle3d box={-1,1,-1,1,-1,-1};real_rectangle2d r;
 int ok=native_effect_rect(&box,&r) && r.x0==-1 && r.x1==1 && r.y0==-1 && r.y1==1;
 memset(native_effect_layers,0,sizeof(native_effect_layers));int accepted=0;
 for(int i=0;i<20;++i)accepted+=native_effect_accept(&data,&box);
 extern int n3ds_input_platform_original_model(void);
 ok=ok && accepted==(n3ds_input_platform_original_model()?2:6);
 data.flags=FLAG(_build_sprites_first_person_bit);ok=ok && native_effect_accept(&data,&box);
 data.flags=0;box=(real_rectangle3d){-.01f,.01f,-.01f,.01f,-1,-1};ok=ok && native_effect_accept(&data,&box);
 box=(real_rectangle3d){2,3,2,3,-1,-1};ok=ok && !native_effect_rect(&box,&r);
 box=(real_rectangle3d){-1,1,-1,1,1,2};ok=ok && !native_effect_rect(&box,&r);
 box=(real_rectangle3d){-1,1,-1,1,-1,1};ok=ok && native_effect_rect(&box,&r);
 render.frustum=saved;memset(native_effect_layers,0,sizeof(native_effect_layers));native_effect_kept=native_effect_clipped=0;native_effect_area_saved=0;return ok;
}
