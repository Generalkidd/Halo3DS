/*
MODELS.C

symbols in this file:
00112DB0 0430:
	_render_model_parts (0000)
001131E0 0110:
	_model_interpolate_node_orientations (0000)
001132F0 0090:
	_model_get_node_orientations (0000)
00113380 0140:
	_model_get_node_matrices (0000)
001134C0 0110:
	_model_node_matrices_from_orientations (0000)
001135D0 00a0:
	_model_find_marker (0000)
00113670 0030:
	_model_get_default_inverse_matrix (0000)
001136A0 0070:
	_model_find_node (0000)
00113710 0010:
	_model_geometry_part_build_tangent_matrices (0000)
00113720 0860:
	_render_model (0000)
00113F80 01d0:
	_model_get_marker_by_name (0000)
00114150 0070:
	_model_build_tangent_matrices (0000)
0027F98C 000d:
	??_C@_0N@PBHIAIOK@render_model?$AA@ (0000)
0027F9A0 0061:
	??_C@_0GB@CAHPCM@part?9?$DOcentroid_secondary_node_in@ (0000)
0027FA08 005d:
	??_C@_0FN@MLJHGCND@part?9?$DOcentroid_primary_node_inde@ (0000)
0027FA68 002c:
	??_C@_0CM@MBIKHGBC@?$CBTEST_FLAG?$CIflags?0?5_render_model_@ (0000)
0027FA94 001f:
	??_C@_0BP@PJEILNMK@c?3?2halo?2SOURCE?2models?2models?4c?$AA@ (0000)
0027FAB4 001e:
	??_C@_0BO@FFDGBNA@node?9?$DOparent_node_index?$CB?$DNNONE?$AA@ (0000)
0027FAD8 0064:
	??_C@_0GE@EAOMEKOC@?$CIactual_detail_level_index?5?$DO?$DN?50?$CJ@ (0000)
0027FB3C 001e:
	??_C@_0BO@INPHBEBB@actual_detail_level_index?5?$DO?50?$AA@ (0000)
0027FB60 0060:
	??_C@_0GA@CEBIGMGH@geometry_detail_level_index?$DO?$DN0?5?$CG@ (0000)
0027FBC0 0009:
	??_C@_08OEDKDIOB@lighting?$AA@ (0000)
0027FBD0 0073:
	??_C@_0HD@NKPBNIND@object_marker?9?$DOnode_index?$DO?$DN0?5?$CG?$CG?5@ (0000)
0027FC44 0008:
	??_C@_07LGFKJAB@markers?$AA@ (0000)
0027FC4C 000e:
	??_C@_0O@BKFGBDDJ@node_matrices?$AA@ (0000)
0030A390 05f8:
	_render_model_section (0000)
00456650 0088:
	_default_function_values (0000)
	_default_render_model_change_colors (0000)
	_default_render_model_effect (0000)
	_default_render_model_region_permutation_indices (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/profile.h"
#include "models.h"

#include "model_definitions.h"

#include "game/game.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"

/* ---------- constants */

enum
{
	NUMBER_OF_DETAIL_LEVELS_PER_MODEL = 5,
	CORTANA_MODEL_NODE_LIST_CHECKSUM = 124371095,
};

enum
{
	_scenario_cortana_hack_bit = 0,
	_scenario_demo_ui_bit,
	NUMBER_OF_SCENARIO_FLAGS
};

enum
{
	_rasterizer_geometry_no_sort_bit = 0,
	_rasterizer_geometry_no_queue_bit,
	_rasterizer_geometry_no_fog_bit,
	_rasterizer_geometry_no_zbuffer_bit,
	_rasterizer_geometry_sky_bit,
	_rasterizer_geometry_viewspace_bit,
	_rasterizer_geometry_atmospheric_fog_but_no_planar_fog_bit,
	_rasterizer_geometry_first_person_bit,
	_rasterizer_geometry_parts_define_local_nodes_bit,
	NUMBER_OF_RASTERIZER_GEOMETRY_FLAGS
};

enum
{
	_render_model_immediate_bit = 0,
	_render_model_shadow_bit,
	_render_model_no_planar_fog_bit,
	_render_model_first_person_bit,
#ifdef HALO_N3DS
    _render_model_native_scenery_bit,
#endif
	NUMBER_OF_RENDER_MODEL_FLAGS
};

enum
{
	_render_model_pass_solid = 0,
	_render_model_pass_decal,
	_render_model_pass_transparent,
	NUMBER_OF_RENDER_MODEL_PASSES
};

enum
{
	_model_geometry_part_stripped_bit = 0,
	_model_geometry_part_local_nodes_bit,
	NUMBER_OF_MODEL_GEOMETRY_PART_FLAGS
};

enum
{
	_shader_type_screen = 0,
	_shader_type_effect,
	_shader_type_decal,
	_shader_type_environment,
	_shader_type_model,
	_shader_type_transparent_generic,
	_shader_type_transparent_chicago,
	_shader_type_transparent_water,
	_shader_type_transparent_glass,
	_shader_type_transparent_meter,
	_shader_type_transparent_plasma,
	NUMBER_OF_SHADER_TYPES
};

enum
{
	_shader_model_detail_after_reflection_bit = 0,
	_shader_model_two_sided_bit,
	_shader_model_not_alpha_tested_bit,
	_shader_model_alpha_blended_decal_bit,
	_shader_model_true_atmospheric_fog_bit,
	_shader_model_nocull_two_sided_bit,
	NUMBER_OF_SHADER_MODEL_FLAGS
};

/* ---------- macros */

/* ---------- structures */

struct model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

struct model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

typedef char verify_model_shader_reference_size[sizeof(struct model_shader_reference) == 0x20 ? 1 : -1];
typedef char verify_model_geometry_part_size[sizeof(struct model_geometry_part) == 0x68 ? 1 : -1];

struct shader_model_definition
{
	struct shader shader;
	word flags;
	short type;
	byte reserved_before_translucency[0xC];
	real translucency;
};

struct rasterizer_model_skinning
{
	real_matrix4x3 const *node_matrices;
	short node_matrix_count;
	word pad;
};

struct render_sort_filth
{
	short *previous_group_presorted_index_reference;
	short *next_group_presorted_index_reference;
	short group_index;
	short next_part_index;
	short part_index;
	word pad;
};

struct render_model_effect
{
	short type;
	word pad;
	real intensity;
	byte reserved[0x20];
};

struct rasterizer_model_begin_parameters
{
	unsigned long geometry_flags;
	long unique_identifier;
	struct rasterizer_model_skinning skinning;
	struct render_lighting lighting;
	struct render_animation animation;
	struct render_model_effect effect;
	real_point3d centroid;
	real radius;
	real_vector2d base_map_scale;
};

typedef char verify_render_model_effect_size[sizeof(struct render_model_effect) == 0x28 ? 1 : -1];
typedef char verify_rasterizer_model_begin_parameters_size[sizeof(struct rasterizer_model_begin_parameters) == 0xCC ? 1 : -1];

struct rasterizer_debug_options
{
	byte reserved[8];
	short debug_model_lod;
	byte trailing[0x5E];
};

typedef char verify_rasterizer_debug_options_size[sizeof(struct rasterizer_debug_options) == 0x68 ? 1 : -1];

/* ---------- prototypes */

#include "rasterizer/rasterizer_models.h"

static void render_model_parts(
	struct model const *model,
	char const *region_permutation_indices,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short geometry_detail_level_index,
	short forced_shader_permutation_index,
	long flags);

/* ---------- globals */

extern struct rasterizer_debug_options rasterizer_debug_options;
extern boolean rasterizer_model_cortana_hack;

extern boolean render_model_nodes;
extern boolean render_model_markers;
extern boolean render_model_vertex_counts;
extern boolean render_model_index_counts;
extern boolean render_model_no_geometry;

static real default_function_values[MAXIMUM_FUNCTION_VALUES_PER_MODEL] = { 0 };
static real_rgb_color default_render_model_change_colors[MAXIMUM_CHANGE_COLORS_PER_MODEL] = { 0 };
static struct render_model_effect default_render_model_effect = { 0 };
static char default_render_model_region_permutation_indices[MAXIMUM_REGIONS_PER_MODEL] = { 0 };

struct profile_section render_model_section = { "render_model", NONE, TRUE };

/* ---------- private code */


#ifdef HALO_N3DS
extern unsigned int n3ds_engine_cache_generation(void);
static int native_foliage_model(long index)
{
 static struct {long index;unsigned int generation;int valid,foliage;} cache[64];
 unsigned int slot=(unsigned int)index&63,generation=n3ds_engine_cache_generation();
 if(cache[slot].valid && cache[slot].index==index && cache[slot].generation==generation)return cache[slot].foliage;
 const char *name=tag_get_name(index);
 int foliage=name && (strstr(name,"tree") || strstr(name,"bush") || strstr(name,"fern") || strstr(name,"plant") || strstr(name,"vine") || strstr(name,"lilypad"));
 cache[slot].index=index;cache[slot].generation=generation;cache[slot].valid=1;cache[slot].foliage=foliage;return foliage;
}
/* Map-owned geometry is immutable. Keep the selected region/LOD list once,
 * including hidden permutations; never reorder decals or transparency. */
#define NATIVE_PARTS 96
struct native_part {struct model_geometry_part *part;struct model_shader_reference *ref;struct shader *shader;short index,pass;};
static struct native_part_list {
 const struct model *model;unsigned int generation;short lod,count;
 char permutations[MAXIMUM_REGIONS_PER_MODEL];struct native_part parts[NATIVE_PARTS];
} native_part_lists[8];
static unsigned int native_part_clock;
static struct native_part_list *native_parts(const struct model *model,const char *perms,short lod)
{
 unsigned int generation=n3ds_engine_cache_generation();
 if(model->regions.count>MAXIMUM_REGIONS_PER_MODEL)return NULL;
 for(unsigned int k=0;k<NUMBEROF(native_part_lists);++k) {
  struct native_part_list *c=native_part_lists+k;
  if(c->model==model && c->generation==generation && c->lod==lod && !memcmp(c->permutations,perms,model->regions.count))return c;
 }
 struct native_part_list *c=native_part_lists+(native_part_clock++%NUMBEROF(native_part_lists));c->model=NULL;c->count=0;
 for(short r=0;r<model->regions.count;++r) {
  if(perms[r]==NONE)continue;
  struct model_region *region=TAG_BLOCK_GET_ELEMENT(&model->regions,r,struct model_region);
  struct model_region_permutation *perm=TAG_BLOCK_GET_ELEMENT(&region->permutations,perms[r],struct model_region_permutation);
  short g=perm->geometry_indices[lod];if(g==NONE)continue;
  struct model_geometry *geometry=TAG_BLOCK_GET_ELEMENT(&model->geometries,g,struct model_geometry);
  for(short i=0;i<geometry->parts.count;++i) {
   struct model_geometry_part *part=TAG_BLOCK_GET_ELEMENT(&geometry->parts,i,struct model_geometry_part);
   struct model_shader_reference *ref=TAG_BLOCK_GET_ELEMENT(&model->shaders,part->shader_index,struct model_shader_reference);
   struct shader *shader=shader_definition_get(ref->shader.index);
   if(!shader_type_is_valid_for_model(shader->base.type) || TEST_FLAG(part->flags,_model_geometry_part_stripped_bit))continue;
   if(c->count==NATIVE_PARTS)return NULL;
   short pass=shader_type_is_transparent(shader->base.type)?_render_model_pass_transparent:
    shader->base.type==_shader_type_model && TEST_FLAG(((struct shader_model_definition *)shader)->flags,_shader_model_alpha_blended_decal_bit)?_render_model_pass_decal:_render_model_pass_solid;
   c->parts[c->count++]=(struct native_part){part,ref,shader,i,pass};
  }
 }
 c->generation=generation;c->lod=lod;memcpy(c->permutations,perms,model->regions.count);c->model=model;return c;
}

extern int n3ds_model_draw_merged(struct shader *,short,const struct triangle_buffer *const *,const struct vertex_buffer *const *,unsigned int);
#endif
static void render_model_parts_uncached(
	struct model const *model,
	char const *region_permutation_indices,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short geometry_detail_level_index,
	short forced_shader_permutation_index,
	long flags)
{
	boolean immediate = TEST_FLAG(flags, _render_model_immediate_bit);
	short last_pass = TEST_FLAG(flags, _render_model_shadow_bit) ? _render_model_pass_solid : _render_model_pass_transparent;
	struct render_sort_filth sort_filth[MAXIMUM_PARTS_PER_MODEL_GEOMETRY+1];
	real_point3d centroid;
	short pass;

	for (pass = _render_model_pass_solid; pass<=last_pass; pass++)
	{
		short sort_filth_count = 0;
		short region_index;
		short i, j;

		for (region_index = 0; region_index<model->regions.count; region_index++)
		{
			struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
			char permutation_index = region_permutation_indices[region_index];

			if (permutation_index!=NONE)
			{
				struct model_region_permutation *permutation = TAG_BLOCK_GET_ELEMENT(
					&region->permutations,
					permutation_index,
					struct model_region_permutation);
				short geometry_index = permutation->geometry_indices[geometry_detail_level_index];

				if (!render_model_no_geometry && geometry_index!=NONE)
				{
					struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct model_geometry);
					short part_index;

					for (part_index = 0; part_index<geometry->parts.count; part_index++)
					{
						struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct model_geometry_part);
						struct model_shader_reference *shader_reference = TAG_BLOCK_GET_ELEMENT(
							&model->shaders,
							part->shader_index,
							struct model_shader_reference);
						struct shader *shader = shader_definition_get(shader_reference->shader.index);

						if (shader_type_is_valid_for_model(shader->base.type) &&
							!TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
						{
							if (shader_type_is_transparent(shader->base.type))
							{
								if (pass==_render_model_pass_transparent)
								{
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 442, !TEST_FLAG(flags, _render_model_shadow_bit));
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										445,
										part->centroid_primary_node_index>=0 && part->centroid_primary_node_index<model->nodes.count);
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										446,
										part->centroid_secondary_node_index>=0 && part->centroid_secondary_node_index<model->nodes.count);

									matrix4x3_transform_point(
										&skinning->node_matrices[part->centroid_primary_node_index],
										&part->centroid,
										&centroid);
									rasterizer_model_transparent_geometry_submit(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE,
										&centroid,
										&sort_filth[sort_filth_count]);

									if (sort_filth_count<MAXIMUM_PARTS_PER_MODEL_GEOMETRY &&
										sort_filth[sort_filth_count].group_index!=NONE &&
										!immediate &&
										(part->next_part_index>0 || part->previous_part_index>0))
									{
										sort_filth[sort_filth_count].part_index = part_index;
										sort_filth[sort_filth_count].next_part_index = part->next_part_index;
										sort_filth_count++;
									}
								}
							}
							else if (shader->base.type==_shader_type_model &&
								TEST_FLAG(((struct shader_model_definition *)shader_get_and_verify_type(shader, _shader_type_model))->flags, _shader_model_alpha_blended_decal_bit))
							{
								if (pass==_render_model_pass_decal)
								{
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 491, !TEST_FLAG(flags, _render_model_shadow_bit));

									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
								}
							}
							else if (pass==_render_model_pass_solid)
							{
								if (TEST_FLAG(flags, _render_model_shadow_bit))
								{
									rasterizer_environment_shadow_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										&part->vertex_buffer);
								}
								else
								{
									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
									rasterizer_debug_model_vertices(object_index, skinning, part);
								}
							}
						}
					}
				}
			}
		}

		for (i = 0; i<sort_filth_count; i++)
		{
			for (j = 0; j<sort_filth_count; j++)
			{
				if (sort_filth[i].next_part_index==sort_filth[j].part_index && sort_filth[i].next_part_index>0)
				{
					*sort_filth[i].next_group_presorted_index_reference = sort_filth[j].group_index;
					*sort_filth[j].previous_group_presorted_index_reference = sort_filth[i].group_index;
					break;
				}
			}
		}
	}

	return;
}

static void render_model_parts(
	struct model const *model,
	char const *region_permutation_indices,
	struct rasterizer_model_skinning const *skinning,
	long object_index,
	short geometry_detail_level_index,
	short forced_shader_permutation_index,
	long flags)
{
	boolean immediate = TEST_FLAG(flags, _render_model_immediate_bit);
	short last_pass = TEST_FLAG(flags, _render_model_shadow_bit) ? _render_model_pass_solid : _render_model_pass_transparent;
	struct render_sort_filth sort_filth[MAXIMUM_PARTS_PER_MODEL_GEOMETRY+1];
	real_point3d centroid;
	short pass;

#ifndef HALO_N3DS
 render_model_parts_uncached(model,region_permutation_indices,skinning,object_index,geometry_detail_level_index,forced_shader_permutation_index,flags);return;
#else
 if(render_model_no_geometry)return;
 struct native_part_list *list=native_parts(model,region_permutation_indices,geometry_detail_level_index);
 if(!list){render_model_parts_uncached(model,region_permutation_indices,skinning,object_index,geometry_detail_level_index,forced_shader_permutation_index,flags);return;}
 for(pass=_render_model_pass_solid;pass<=last_pass;++pass) {
  short sort_filth_count=0,i,j;
  for(short entry=0;entry<list->count;++entry) {
   struct native_part *item=list->parts+entry;if(item->pass!=pass)continue;
   struct model_geometry_part *part=item->part;
   struct model_shader_reference *shader_reference=item->ref;struct shader *shader=item->shader;short part_index=item->index;
   if(pass==_render_model_pass_solid && !TEST_FLAG(flags,_render_model_shadow_bit)) {
    const struct triangle_buffer *tri[16];const struct vertex_buffer *vert[16];unsigned int n=1;
    tri[0]=&part->triangle_buffer;vert[0]=&part->vertex_buffer;
    while(n<16 && entry+n<list->count) {
     struct native_part *next=list->parts+entry+n;
     if(next->pass!=pass || next->shader!=shader || (!forced_shader_permutation_index && next->ref->permutation_index!=shader_reference->permutation_index))break;
     tri[n]=&next->part->triangle_buffer;vert[n]=&next->part->vertex_buffer;++n;
    }
    if(n>1 && n3ds_model_draw_merged(shader,forced_shader_permutation_index?forced_shader_permutation_index:shader_reference->permutation_index,tri,vert,n)){entry+=n-1;continue;}
   }
						if (shader_type_is_valid_for_model(shader->base.type) &&
							!TEST_FLAG(part->flags, _model_geometry_part_stripped_bit))
						{
							if (shader_type_is_transparent(shader->base.type))
							{
								if (pass==_render_model_pass_transparent)
								{
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 442, !TEST_FLAG(flags, _render_model_shadow_bit));
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										445,
										part->centroid_primary_node_index>=0 && part->centroid_primary_node_index<model->nodes.count);
									match_assert(
										"c:\\halo\\SOURCE\\models\\models.c",
										446,
										part->centroid_secondary_node_index>=0 && part->centroid_secondary_node_index<model->nodes.count);

									matrix4x3_transform_point(
										&skinning->node_matrices[part->centroid_primary_node_index],
										&part->centroid,
										&centroid);
									rasterizer_model_transparent_geometry_submit(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE,
										&centroid,
										&sort_filth[sort_filth_count]);

									if (sort_filth_count<MAXIMUM_PARTS_PER_MODEL_GEOMETRY &&
										sort_filth[sort_filth_count].group_index!=NONE &&
										!immediate &&
										(part->next_part_index>0 || part->previous_part_index>0))
									{
										sort_filth[sort_filth_count].part_index = part_index;
										sort_filth[sort_filth_count].next_part_index = part->next_part_index;
										sort_filth_count++;
									}
								}
							}
							else if (shader->base.type==_shader_type_model &&
								TEST_FLAG(((struct shader_model_definition *)shader_get_and_verify_type(shader, _shader_type_model))->flags, _shader_model_alpha_blended_decal_bit))
							{
								if (pass==_render_model_pass_decal)
								{
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 491, !TEST_FLAG(flags, _render_model_shadow_bit));

									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
								}
							}
							else if (pass==_render_model_pass_solid)
							{
								if (TEST_FLAG(flags, _render_model_shadow_bit))
								{
									rasterizer_environment_shadow_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										&part->vertex_buffer);
								}
								else
								{
									rasterizer_model_draw(
										shader,
										forced_shader_permutation_index ? forced_shader_permutation_index : shader_reference->permutation_index,
										&part->triangle_buffer,
										NONE,
										part->triangle_buffer.count,
										&part->vertex_buffer,
										NONE);
									rasterizer_debug_model_vertices(object_index, skinning, part);
								}
							}

   }
  }

		for (i = 0; i<sort_filth_count; i++)
		{
			for (j = 0; j<sort_filth_count; j++)
			{
				if (sort_filth[i].next_part_index==sort_filth[j].part_index && sort_filth[i].next_part_index>0)
				{
					*sort_filth[i].next_group_presorted_index_reference = sort_filth[j].group_index;
					*sort_filth[j].previous_group_presorted_index_reference = sort_filth[i].group_index;
					break;
				}
			}
		}
	}

#endif
	return;
}

/* ---------- public code */

void model_interpolate_node_orientations(
	struct model const *model,
	struct real_orientation *original_node_orientations,
	struct real_orientation *target_node_orientations,
	short frame_index,
	short frame_count)
{
	real fraction = (real)(frame_index + 1) / (real)frame_count;
	real inverse_fraction = 1.f - fraction;
	short node_index;

	match_assert(
		"c:\\halo\\SOURCE\\models\\models.c",
		579,
		frame_count>0);
	match_assert(
		"c:\\halo\\SOURCE\\models\\models.c",
		580,
		frame_index<frame_count);

	for (node_index = 0; node_index < model->nodes.count; node_index++)
	{
		struct real_orientation *target = &target_node_orientations[node_index];
		struct real_orientation *original = &original_node_orientations[node_index];

		target->scale = original->scale * inverse_fraction + target->scale * fraction;
		quaternions_interpolate_and_normalize(
			&original->rotation,
			&target->rotation,
			fraction,
			&target->rotation);
		target->translation.x = original->translation.x * inverse_fraction + target->translation.x * fraction;
		target->translation.y = original->translation.y * inverse_fraction + target->translation.y * fraction;
		target->translation.z = original->translation.z * inverse_fraction + target->translation.z * fraction;
	}

	return;
}

void model_get_node_orientations(
	struct model const *model,
	real_orientation *node_orientations)
{
	short node_index;

	for (node_index = 0; node_index<model->nodes.count; node_index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

		node_orientations[node_index].rotation = node->default_rotation;
		node_orientations[node_index].translation = node->default_translation;
		node_orientations[node_index].scale = 1.f;
	}

	return;
}

void model_get_node_matrices(
	struct model const *model,
	real_matrix4x3 *node_matrices,
	real_point3d const *origin,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	short node_queue[MAXIMUM_NODES_PER_MODEL];
	short read_index, write_index;

	node_queue[0] = 0;
	read_index = 0;
	write_index = 1;

	while (read_index!=write_index)
	{
		short node_index = node_queue[read_index++];
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		real_matrix4x3 node_matrix;

		matrix4x3_from_point_and_quaternion(&node_matrix, &node->default_translation, &node->default_rotation);

		if (node_index == 0)
		{
			matrix4x3_from_point_and_vectors(
				&node_matrices[node_index],
				origin ? origin : global_origin3d,
				forward ? forward : global_forward3d,
				up ? up : global_up3d);
			matrix4x3_multiply(&node_matrices[node_index], &node_matrix, &node_matrices[node_index]);
		}
		else
		{
			match_assert("c:\\halo\\SOURCE\\models\\models.c", 650, node->parent_node_index!=NONE);
			matrix4x3_multiply(&node_matrices[node->parent_node_index], &node_matrix, &node_matrices[node_index]);
		}

		if (node->next_sibling_node_index!=NONE)
		{
			node_queue[write_index++] = node->next_sibling_node_index;
		}
		if (node->first_child_node_index!=NONE)
		{
			node_queue[write_index++] = node->first_child_node_index;
		}
	}

	return;
}

void model_node_matrices_from_orientations(
	struct model const *model,
	real_matrix4x3 *node_matrices,
	real_orientation const *node_orientations,
	real_point3d const *origin,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	real_matrix4x3 root_matrix;

	matrix4x3_from_point_and_vectors(&root_matrix, origin, forward, up);

	if (model->nodes.count>0)
	{
		short node_queue[MAXIMUM_NODES_PER_MODEL];
		short read_index = 0;
		short write_index = 1;

		node_queue[0] = 0;

		do
		{
			short node_index = node_queue[read_index++];
			struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
			real_matrix4x3 const *parent_matrix = node_index==0 ? &root_matrix : &node_matrices[node->parent_node_index];
			real_matrix4x3 node_matrix;

			matrix4x3_from_orientation(&node_matrix, &node_orientations[node_index]);
			matrix4x3_multiply(parent_matrix, &node_matrix, &node_matrices[node_index]);

			if (node->next_sibling_node_index!=NONE)
			{
				node_queue[write_index++] = node->next_sibling_node_index;
			}
			if (node->first_child_node_index!=NONE)
			{
				node_queue[write_index++] = node->first_child_node_index;
			}
		}
		while (read_index!=write_index);
	}

	return;
}

short model_find_marker(
	long model_index,
	char const *name)
{
	if (model_index != NONE && name && *name)
	{
		struct model *model = model_definition_get(model_index);
		short lower_bound = 0;
		short upper_bound = (short)model->markers.count - 1;

		while (lower_bound <= upper_bound)
		{
			short marker_index = (short)((lower_bound + upper_bound) / 2);
			struct model_marker *marker = TAG_BLOCK_GET_ELEMENT(
				&model->markers,
				marker_index,
				struct model_marker);
			long comparison = _stricmp(name, marker->name);

			if (comparison == 0)
			{
				return marker_index;
			}
			if (comparison < 0)
			{
				upper_bound = marker_index - 1;
			}
			else
			{
				lower_bound = marker_index + 1;
			}
		}
	}

	return NONE;
}

real_matrix4x3 *model_get_default_inverse_matrix(
	struct model *model,
	short node_index)
{
	struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

	return &node->runtime_default_inverse_matrix;
}

short model_find_node(
	long model_index,
	char const *name)
{
	if (model_index!=NONE)
	{
		short node_index;
		struct model *model = model_definition_get(model_index);

		for (node_index = 0; node_index<model->nodes.count; node_index++)
		{
			struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

			if (!csstrcmp(node->name, name))
			{
				return node_index;
			}
		}
	}

	return NONE;
}

#ifdef HALO_N3DS
extern unsigned int n3ds_engine_cache_generation(void);
/* Markers are sampled by perception, lighting, sound and weapon attachments.
 * Reuse only exact pose/local-transform matches; no stale simulation pose. */
struct native_marker_pose {
    const struct model_marker_instance *instance;
    const real_matrix4x3 *address;
    unsigned int generation;int valid;
    real_point3d translation;real_quaternion rotation;
    real_matrix4x3 pose,local,world;
};
static struct native_marker_pose native_marker_poses[128];
static unsigned int native_marker_hits,native_marker_misses;
static void native_marker_transform(const struct model_marker_instance *instance,const real_matrix4x3 *pose,
    real_matrix4x3 *local,real_matrix4x3 *world)
{
    unsigned int hash=(((unsigned long)pose>>2)^((unsigned long)instance>>4))&127;
    struct native_marker_pose *c=&native_marker_poses[hash];
    unsigned int generation=n3ds_engine_cache_generation();
    int same=c->valid && c->generation==generation && c->instance==instance &&
        !memcmp(&c->translation,&instance->translation,sizeof(c->translation)) &&
        !memcmp(&c->rotation,&instance->rotation,sizeof(c->rotation));
    if(!same) {
        matrix4x3_from_point_and_quaternion(&c->local,&instance->translation,&instance->rotation);
        c->translation=instance->translation;c->rotation=instance->rotation;
    }
    if(same && c->address==pose && !memcmp(&c->pose,pose,sizeof(*pose))) ++native_marker_hits;
    else {matrix4x3_multiply(pose,&c->local,&c->world);c->pose=*pose;++native_marker_misses;}
    c->instance=instance;c->address=pose;c->generation=generation;c->valid=1;
    *local=c->local;*world=c->world;
#ifdef HALO_N3DS_CROWD_VERIFY
    real_matrix4x3 expected_local,expected_world;
    matrix4x3_from_point_and_quaternion(&expected_local,&instance->translation,&instance->rotation);
    matrix4x3_multiply(pose,&expected_local,&expected_world);
    assert(!memcmp(local,&expected_local,sizeof(*local)) && !memcmp(world,&expected_world,sizeof(*world)));
#endif
}
void n3ds_marker_cache_counters(unsigned int *hits,unsigned int *misses) {*hits=native_marker_hits;*misses=native_marker_misses;}
int n3ds_marker_cache_tests(void)
{
    struct model_marker_instance instance={0};real_matrix4x3 poses[2],local,world,expected_local,expected_world;
    matrix4x3_identity(&poses[0]);matrix4x3_identity(&poses[1]);instance.rotation.w=1;
    for(int i=0;i<256;++i) {
        real_matrix4x3 *pose=&poses[(i/8)&1];
        if(i%3==0)pose->position.x=i*.125f;
        if(i%7==0)instance.translation.z=i*.0625f;
        if(i%11==0)pose->scale=pose->scale==1.f?-1.f:1.f;
        if(i%13==0)instance.rotation.w=-instance.rotation.w;
        native_marker_transform(&instance,pose,&local,&world);
        matrix4x3_from_point_and_quaternion(&expected_local,&instance.translation,&instance.rotation);
        matrix4x3_multiply(pose,&expected_local,&expected_world);
        if(memcmp(&local,&expected_local,sizeof(local)) || memcmp(&world,&expected_world,sizeof(world)))return 0;
    }
    memset(native_marker_poses,0,sizeof(native_marker_poses));native_marker_hits=native_marker_misses=0;return 1;
}
#endif

short model_get_marker_by_name(
	long model_index,
	char const *name,
	byte const *region_permutations,
	short const *node_remapping_table,
	short node_count,
	real_matrix4x3 const *node_matrices,
	boolean mirrored_flag,
	struct object_marker *markers,
	short maximum_marker_count)
{
	short result = 0;
	short marker_index = model_find_marker(model_index, name);

	match_assert("c:\\halo\\SOURCE\\models\\models.c", 760, node_matrices);
	match_assert("c:\\halo\\SOURCE\\models\\models.c", 761, markers);

	if (marker_index!=NONE)
	{
		short i;

		struct model *model = model_definition_get(model_index);
		struct model_marker* marker = TAG_BLOCK_GET_ELEMENT(&model->markers, marker_index, struct model_marker);

		for (i =0; i<marker->instances.count; i++)
		{
			struct model_marker_instance* instance = TAG_BLOCK_GET_ELEMENT(&marker->instances, i, struct model_marker_instance);
			if (!region_permutations ||
				region_permutations[instance->region_index]==instance->permutation_index)
			{
				struct object_marker *object_marker;

				if (result>=maximum_marker_count)
				{
					break;
				}

				object_marker = &markers[result++];
				object_marker->node_index = node_remapping_table ? node_remapping_table[instance->node_index] : instance->node_index;
#ifndef HALO_N3DS
				matrix4x3_from_point_and_quaternion(&object_marker->node_matrix, &instance->translation, &instance->rotation);
#endif
				match_assert(
					"c:\\halo\\SOURCE\\models\\models.c",
					785,
					object_marker->node_index>=0 && object_marker->node_index<(node_remapping_table ? node_count : model->nodes.count));
				
#ifdef HALO_N3DS
                native_marker_transform(instance,&node_matrices[object_marker->node_index],&object_marker->node_matrix,&object_marker->matrix);
#else
				matrix4x3_multiply(&node_matrices[object_marker->node_index], &object_marker->node_matrix, &object_marker->matrix);
#endif
				if (mirrored_flag)
				{
					negate_vector3d(&object_marker->matrix.left, &object_marker->matrix.left);
				}
			}
		}
	}

	return result;
}

void model_build_tangent_matrices(
	struct model *model)
{
	short geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(
			&model->geometries,
			geometry_index,
			struct model_geometry);
		short part_index;

		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(
				&geometry->parts,
				part_index,
				struct model_geometry_part);
		}
	}

	return;
}

void model_geometry_part_build_tangent_matrices(
	struct model_geometry_part *part)
{
	return;
}

void render_model(
	long model_index,
	real level_of_detail_pixels,
	real_matrix4x3 const *node_matrices,
	char const *region_permutation_indices,
	real_rgb_color const *change_colors,
	real const *function_values,
	struct render_lighting const *lighting,
	real_point3d const *centroid,
	real radius,
	struct render_model_effect const *model_effect,
	long unique_identifier,
	short forced_shader_permutation_index,
	unsigned long flags)
{
	struct model *model = model_definition_get(model_index);

	profile_enter(render_model_section);

	match_assert("c:\\halo\\SOURCE\\models\\models.c", 82, lighting);

	if (model->node_list_checksum==CORTANA_MODEL_NODE_LIST_CHECKSUM &&
		TEST_FLAG(global_scenario_get()->flags, _scenario_cortana_hack_bit))
	{
		rasterizer_model_cortana_hack = TRUE;
	}
	else
	{
		rasterizer_model_cortana_hack = FALSE;
	}

	if (level_of_detail_pixels>=model->detail_cutoff_pixels[0] || TEST_FLAG(flags, _render_model_shadow_bit))
	{
		real_matrix4x3 relative_node_matrices[MAXIMUM_NODES_PER_MODEL];
		struct rasterizer_model_begin_parameters model_parameters;
		short geometry_detail_level_index;
		short node_index;

		if (!region_permutation_indices)
		{
			region_permutation_indices = default_render_model_region_permutation_indices;
		}
		if (!model_effect)
		{
			model_effect = &default_render_model_effect;
		}
		if (!change_colors)
		{
			change_colors = default_render_model_change_colors;
		}
		if (!function_values)
		{
			function_values = default_function_values;
		}
		if (!centroid)
		{
			centroid = &node_matrices->position;
		}

		if (node_matrices)
		{
			for (node_index = 0; node_index<model->nodes.count; node_index++)
			{
				struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

				matrix4x3_multiply(
					&node_matrices[node_index],
					&node->runtime_default_inverse_matrix,
					&relative_node_matrices[node_index]);
			}
		}
		else
		{
			for (node_index = 0; node_index<model->nodes.count; node_index++)
			{
				relative_node_matrices[node_index] = render.frustum.world_to_view;
			}
		}

		geometry_detail_level_index = NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1;
		while (geometry_detail_level_index>0 &&
			level_of_detail_pixels<model->detail_cutoff_pixels[geometry_detail_level_index])
		{
			geometry_detail_level_index--;
		}
		if (rasterizer_debug_options.debug_model_lod!=NONE)
		{
			geometry_detail_level_index = PIN(rasterizer_debug_options.debug_model_lod, 0, NUMBER_OF_DETAIL_LEVELS_PER_MODEL-1);
		}
        #ifdef HALO_N3DS
        /* Combat keeps full simulation/animation and collision meshes. Bound
         * render mesh complexity only. Original models use a stricter visual
         * budget; the New profile and all animation/collision nodes are kept. */
        extern int n3ds_input_platform_original_model(void);
        extern boolean cinematic_in_progress(void);
        int original=n3ds_input_platform_original_model();
        if(TEST_FLAG(flags, _render_model_first_person_bit)) {
            if(original)geometry_detail_level_index=MIN(geometry_detail_level_index,2);
        } else {
            if(cinematic_in_progress()) {
                if(original) geometry_detail_level_index=MIN(geometry_detail_level_index,1);
            } else geometry_detail_level_index=MIN(geometry_detail_level_index,original?1:3);
            /* Small projected models do not need closeup geometry at 400x240.
             * Preserve first-person weapons, closeups and all simulation nodes. */
            if(level_of_detail_pixels<(original?192.f:64.f)) geometry_detail_level_index=MIN(geometry_detail_level_index,original?0:1);
            else if(!original && level_of_detail_pixels<96.f) geometry_detail_level_index=MIN(geometry_detail_level_index,2);
            /* Small scenery uses its authored lowest-detail mesh. Cinematic
             * actors, vehicles and held weapons retain their own policy. */
            if(TEST_FLAG(flags,_render_model_native_scenery_bit) && level_of_detail_pixels<(original?320.f:112.f))geometry_detail_level_index=0;
        }
        #endif
		match_assert(
			"c:\\halo\\SOURCE\\models\\models.c",
			169,
			geometry_detail_level_index>=0 && geometry_detail_level_index<NUMBER_OF_DETAIL_LEVELS_PER_MODEL);

		if (!TEST_FLAG(flags, _render_model_shadow_bit))
		{
			if (render_model_nodes)
			{
				for (node_index = 0; node_index<model->nodes.count; node_index++)
				{
					struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

					if (node->parent_node_index!=NONE)
					{
						render_debug_line(
							TRUE,
							&node_matrices[node_index].position,
							&node_matrices[node->parent_node_index].position,
							global_real_argb_white);
					}
					render_debug_matrix(TRUE, &node_matrices[node_index], 0.05f);
				}
			}

			if (render_model_markers)
			{
				short marker_index;

				for (marker_index = 0; marker_index<model->markers.count; marker_index++)
				{
					struct model_marker *marker = TAG_BLOCK_GET_ELEMENT(&model->markers, marker_index, struct model_marker);
					short instance_index;

					for (instance_index = 0; instance_index<marker->instances.count; instance_index++)
					{
						struct model_marker_instance *instance = TAG_BLOCK_GET_ELEMENT(
							&marker->instances,
							instance_index,
							struct model_marker_instance);

						if (region_permutation_indices[instance->region_index]==instance->permutation_index)
						{
							real_matrix4x3 marker_matrix;

							matrix4x3_from_point_and_quaternion(&marker_matrix, &instance->translation, &instance->rotation);
							matrix4x3_multiply(&node_matrices[instance->node_index], &marker_matrix, &marker_matrix);
							render_debug_matrix(FALSE, &marker_matrix, 0.05f);
							render_debug_string_at_point(FALSE, &marker_matrix.position, marker->name, global_real_argb_white);
						}
					}
				}
			}

			if (render_model_vertex_counts || render_model_index_counts)
			{
				short maximum_actual_detail_level_index = geometry_detail_level_index;
				boolean has_unstripped_parts = FALSE;
				short vertex_count = 0;
				short index_count = 0;
				short region_index;
				real distance;

				for (region_index = 0; region_index<model->regions.count; region_index++)
				{
					struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
					char permutation_index = region_permutation_indices[region_index];

					if (permutation_index!=NONE)
					{
						struct model_region_permutation *permutation = TAG_BLOCK_GET_ELEMENT(
							&region->permutations,
							permutation_index,
							struct model_region_permutation);
						short actual_detail_level_index;
						short geometry_index;

						for (actual_detail_level_index = geometry_detail_level_index+1;
							actual_detail_level_index<NUMBER_OF_DETAIL_LEVELS_PER_MODEL;
							actual_detail_level_index++)
						{
							if (permutation->geometry_indices[actual_detail_level_index]!=
								permutation->geometry_indices[geometry_detail_level_index])
							{
								break;
							}
						}
						match_assert("c:\\halo\\SOURCE\\models\\models.c", 247, actual_detail_level_index > 0);
						actual_detail_level_index--;
						match_assert(
							"c:\\halo\\SOURCE\\models\\models.c",
							249,
							(actual_detail_level_index >= 0) && (actual_detail_level_index < NUMBER_OF_DETAIL_LEVELS_PER_MODEL));
						maximum_actual_detail_level_index = MAX(maximum_actual_detail_level_index, actual_detail_level_index);

						geometry_index = permutation->geometry_indices[geometry_detail_level_index];
						if (geometry_index!=NONE)
						{
							struct model_geometry *geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct model_geometry);
							short part_index;

							for (part_index = 0; part_index<geometry->parts.count; part_index++)
							{
								struct model_geometry_part *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct model_geometry_part);

								vertex_count+= part->vertex_buffer.count;
								switch (part->triangle_buffer.type)
								{
								case _triangle_buffer_type_triangles:
									index_count+= 3*part->triangle_buffer.count;
									has_unstripped_parts = TRUE;
									break;
								case _triangle_buffer_type_precompiled_strip:
									index_count+= part->triangle_buffer.count+2;
									break;
								default:
									match_assert("c:\\halo\\SOURCE\\models\\models.c", 276, !"unreachable");
								}
							}
						}
					}
				}

				distance = fabs(
					render.frustum.world_to_view.forward.k*centroid->x +
					render.frustum.world_to_view.left.k*centroid->y +
					render.frustum.world_to_view.up.k*centroid->z +
					render.frustum.world_to_view.position.z);
				level_of_detail_pixels = distance/render.frustum.projection_world_to_screen.j*level_of_detail_pixels*0.5f;
				if (level_of_detail_pixels>0.0001f)
				{
					real_argb_color const *detail_level_colors[NUMBER_OF_DETAIL_LEVELS_PER_MODEL];
					real_argb_color const *color;
					char string[256];
					real_point3d point;

					detail_level_colors[0] = global_real_argb_blue;
					detail_level_colors[1] = global_real_argb_green;
					detail_level_colors[2] = global_real_argb_yellow;
					detail_level_colors[3] = global_real_argb_orange;
					detail_level_colors[4] = global_real_argb_red;
					color = detail_level_colors[maximum_actual_detail_level_index];
					if (has_unstripped_parts && (game_time_get()+model_index)%30<15)
					{
						color = global_real_argb_white;
					}

					csstrcpy(string, "");
					if (render_model_vertex_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "%d", vertex_count);
					}
					if (render_model_vertex_counts && render_model_index_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "/");
					}
					if (render_model_index_counts)
					{
						_snprintf(string+csstrlen(string), sizeof(string)-csstrlen(string), "%d", index_count);
					}

					set_real_point3d(&point, centroid->x, centroid->y, centroid->z+level_of_detail_pixels);
					render_debug_string_at_point(FALSE, &point, string, color);
				}
			}
		}

		model_parameters.unique_identifier = unique_identifier;
		model_parameters.lighting = *lighting;
		model_parameters.centroid = *centroid;
		model_parameters.radius = radius;
		model_parameters.effect = *model_effect;
		model_parameters.animation.colors = change_colors;
		model_parameters.animation.values = function_values;
		model_parameters.skinning.node_matrices = relative_node_matrices;
		model_parameters.skinning.node_matrix_count = model->nodes.count;
		model_parameters.geometry_flags = 0;
#ifdef HALO_N3DS
        /* Native-only packet bits survive the transparent queue. Never apply
         * scenery reductions to actors, first-person weapons or sky packets. */
        if(TEST_FLAG(flags,_render_model_native_scenery_bit) && !TEST_FLAG(flags,_render_model_immediate_bit)) {
            extern int n3ds_input_platform_original_model(void);
            int foliage=native_foliage_model(model_index);
            model_parameters.geometry_flags|=1u<<16;
            if(foliage)model_parameters.geometry_flags|=1u<<17;
#ifdef HALO_N3DS_RENDERER_TESTS
            const char *name=tag_get_name(model_index);
            static long seen_models[64];static unsigned int seen_count;unsigned int n;
            for(n=0;n<seen_count && seen_models[n]!=model_index;++n){}
            if(n==seen_count && seen_count<64){seen_models[seen_count++]=model_index;char text[256];extern void n3ds_log(const char *);
             snprintf(text,sizeof(text),"SCENERY TAG: foliage=%d pixels=%.1f name=%.180s",foliage,level_of_detail_pixels,name);n3ds_log(text);}
#endif

            int old=n3ds_input_platform_original_model();
            if(old || level_of_detail_pixels<128.f)model_parameters.geometry_flags|=1u<<18;
            if(level_of_detail_pixels<(old?160.f:64.f))model_parameters.geometry_flags|=1u<<19;
        }
#endif

		model_parameters.base_map_scale = model->base_map_scale;

		if (TEST_FLAG(flags, _render_model_immediate_bit))
		{
			model_parameters.geometry_flags = FLAG(_rasterizer_geometry_no_sort_bit) |
				FLAG(_rasterizer_geometry_no_queue_bit) |
				FLAG(_rasterizer_geometry_no_fog_bit) |
				FLAG(_rasterizer_geometry_no_zbuffer_bit) |
				FLAG(_rasterizer_geometry_sky_bit);
		}
		SET_FLAG(model_parameters.geometry_flags, _rasterizer_geometry_atmospheric_fog_but_no_planar_fog_bit, TEST_FLAG(flags, _render_model_no_planar_fog_bit));
		SET_FLAG(model_parameters.geometry_flags, _rasterizer_geometry_first_person_bit, TEST_FLAG(flags, _render_model_first_person_bit));

		if (TEST_FLAG(flags, _render_model_shadow_bit))
		{
			rasterizer_environment_shadow_model_begin(&model_parameters);
		}
		else
		{
			rasterizer_model_begin(&model_parameters, FALSE);
		}
		render_model_parts(
			model,
			region_permutation_indices,
			&model_parameters.skinning,
			unique_identifier,
			geometry_detail_level_index,
			forced_shader_permutation_index,
			flags);
		if (TEST_FLAG(flags, _render_model_shadow_bit))
		{
			rasterizer_environment_shadow_model_end();
		}
		else
		{
			rasterizer_model_end();
		}
	}

	rasterizer_model_cortana_hack = FALSE;

	profile_exit(render_model_section);

	return;
}
