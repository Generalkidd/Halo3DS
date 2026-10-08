/*
CACHE_FILE_FORMATS.C

Halo Custom Edition caches, OpenSauce extensions and Custom Edition resource
maps (cache_file_formats.h; port/n3ds/pc/FORMAT-REFERENCE.md).

Sources for the layouts, cited per structure below:
- OpenSauce (GPL-3.0, Kornner Studios; the snapshot examined is the
  OpenSauce-master archive of the upstream Mercurial repository, whose newest
  version file names OpenSauce 4.0.0): the cache header, the OpenSauce header,
  tag index, tag instance, resource map ("data file") header and item, the
  structure BSP header and reference, and the bitmap, sound and HUD message
  tag layouts, all with static size assertions.
- BlamLib, in the same archive: the font and unicode string list layouts.
- Reclaimer (GPL-3.0, Gravemind2401; the Reclaimer-master archive): how
  bitmap tags and pixel data are found in bitmaps.map.
No code from either project is used: they are documentation here, and this
repository is CC0. Facts that neither source states were established by
reading the sample Custom Edition maps; the comments below say so where they
are used ("observed", "every map examined"), and
port/n3ds/pc/FORMAT-REFERENCE.md lists each with its evidence.
*/

/* ---------- headers */

#include "cache_file_formats.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* four-character codes, as the files store them (little-endian) */
#define CACHE_HEADER_SIGNATURE 'head'
#define CACHE_FOOTER_SIGNATURE 'foot'
#define TAG_INDEX_SIGNATURE 'tags'
#define STRUCTURE_BSP_SIGNATURE 'sbsp'
#define OPENSAUCE_HEADER_SIGNATURE 'yelo'

#define BITMAP_GROUP_TAG 'bitm'
#define SOUND_GROUP_TAG 'snd!'
#define FONT_GROUP_TAG 'font'
#define UNICODE_STRING_LIST_GROUP_TAG 'ustr'
#define HUD_MESSAGE_TEXT_GROUP_TAG 'hmt '
#define SCENARIO_GROUP_TAG 'scnr'
#define EFFECT_GROUP_TAG 'effe'
#define UI_WIDGET_GROUP_TAG 'DeLa'
#define SHADER_MODEL_GROUP_TAG 'soso'
#define PARTICLE_GROUP_TAG 'part'
#define PARTICLE_SYSTEM_GROUP_TAG 'pctl'
#define MATERIAL_EFFECTS_GROUP_TAG 'foot'
#define CONTRAIL_GROUP_TAG 'cont'
#define PHYSICS_GROUP_TAG 'phys'
#define LIGHT_VOLUME_GROUP_TAG 'mgs2'
#define COLOR_TABLE_GROUP_TAG 'colo'
#define ANTENNA_GROUP_TAG 'ant!'
#define TRANSPARENT_GLASS_GROUP_TAG 'sgla'
#define TRANSPARENT_METER_GROUP_TAG 'smet'
#define TRANSPARENT_PLASMA_GROUP_TAG 'spla'
#define STRING_LIST_GROUP_TAG 'str#'
#define HUD_NUMBER_GROUP_TAG 'hud#'
#define SOUND_ENVIRONMENT_GROUP_TAG 'snde'
#define WIND_GROUP_TAG 'wind'
#define FLAG_GROUP_TAG 'flag'
#define DAMAGE_EFFECT_GROUP_TAG 'jpt!'
#define LIGHT_GROUP_TAG 'ligh'
#define DECAL_GROUP_TAG 'deca'
#define LENS_FLARE_GROUP_TAG 'lens'
#define LOOPING_SOUND_GROUP_TAG 'lsnd'
#define POINT_PHYSICS_GROUP_TAG 'pphy'
#define COLLISION_MODEL_GROUP_TAG 'coll'
#define ANIMATION_GRAPH_GROUP_TAG 'antr'
#define SHADER_ENVIRONMENT_GROUP_TAG 'senv'
#define INPUT_DEVICE_DEFAULTS_GROUP_TAG 'devc'
#define PROJECTILE_GROUP_TAG 'proj'
#define WEAPON_GROUP_TAG 'weap'
#define VEHICLE_GROUP_TAG 'vehi'
#define BIPED_GROUP_TAG 'bipd'
#define SCENERY_GROUP_TAG 'scen'
#define EQUIPMENT_GROUP_TAG 'eqip'
#define ITEM_COLLECTION_GROUP_TAG 'itmc'
#define TAG_COLLECTION_GROUP_TAG 'tagc'
#define METER_GROUP_TAG 'metr'
#define SOUND_SCENERY_GROUP_TAG 'ssce'
#define DEVICE_GROUP_TAG 'devi'
#define MACHINE_GROUP_TAG 'mach'
#define CONTROL_GROUP_TAG 'ctrl'
#define LIGHT_FIXTURE_GROUP_TAG 'lifi'
#define GARBAGE_GROUP_TAG 'garb'
#define FOG_GROUP_TAG 'fog '
#define GLOW_GROUP_TAG 'glw!'
#define DETAIL_OBJECT_COLLECTION_GROUP_TAG 'dobc'
#define VIRTUAL_KEYBOARD_GROUP_TAG 'vcky'
#define MULTIPLAYER_SCENARIO_DESCRIPTION_GROUP_TAG 'mply'
#define LIGHTNING_GROUP_TAG 'elec'
#define TRANSPARENT_WATER_GROUP_TAG 'swat'
#define WEATHER_PARTICLE_SYSTEM_GROUP_TAG 'rain'
#define SKY_GROUP_TAG 'sky '
#define CAMERA_TRACK_GROUP_TAG 'trak'
#define ACTOR_VARIANT_GROUP_TAG 'actv'
#define ACTOR_GROUP_TAG 'actr'
#define DIALOGUE_GROUP_TAG 'udlg'
#define GAME_GLOBALS_GROUP_TAG 'matg'
#define UI_WIDGET_COLLECTION_GROUP_TAG 'Soul'
#define STRUCTURE_BSP_GROUP_TAG 'sbsp'
#define GBXMODEL_GROUP_TAG 'mod2'
#define PROJECT_YELLOW_GROUP_TAG 'yelo'
#define PROJECT_YELLOW_GLOBALS_GROUP_TAG 'gelo'

#define NO_TAG_INDEX (-1)
#define ABSOLUTE_INDEX_MASK 0xFFFFUL

/* the cache header (OpenSauce cache_files_structures.hpp, s_cache_header) */
#define CACHE_HEADER_VERSION_OFFSET 0x04
#define CACHE_HEADER_FILE_LENGTH_OFFSET 0x08
#define CACHE_HEADER_COMPRESSED_LENGTH_OFFSET 0x0C
#define CACHE_HEADER_TAG_DATA_OFFSET_OFFSET 0x10
#define CACHE_HEADER_TAG_DATA_SIZE_OFFSET 0x14
#define CACHE_HEADER_NAME_OFFSET 0x20
#define CACHE_HEADER_BUILD_OFFSET 0x40
#define CACHE_HEADER_SCENARIO_TYPE_OFFSET 0x60
#define CACHE_HEADER_CHECKSUM_OFFSET 0x64
#define CACHE_HEADER_OPENSAUCE_OFFSET 0x70
#define CACHE_HEADER_FOOTER_OFFSET 0x7FC

/* the OpenSauce header (OpenSauce cache_files_structures_yelo.hpp,
s_cache_header_yelo). Its build_info holds a time_t: the source does not
define _USE_32BIT_TIME_T, so it is 64 bits aligned to 8, and in every
OpenSauce cache examined the build string does start 8 bytes after the
timestamp. */
#define OPENSAUCE_VERSION_OFFSET 0x04
#define OPENSAUCE_FLAGS_OFFSET 0x06
#define OPENSAUCE_PROJECT_YELLOW_VERSION_OFFSET 0x08
#define OPENSAUCE_PROJECT_YELLOW_GLOBALS_VERSION_OFFSET 0x09
#define OPENSAUCE_MEMORY_UPGRADE_AMOUNT_OFFSET 0x0C
#define OPENSAUCE_DEFINITIONS_SIZE_OFFSET 0x10
#define OPENSAUCE_DEFINITIONS_DECOMPRESSED_SIZE_OFFSET 0x14
#define OPENSAUCE_DEFINITIONS_OFFSET_OFFSET 0x18
#define OPENSAUCE_DEFINITIONS_BUILD_OFFSET 0x20
#define OPENSAUCE_MOD_NAME_OFFSET 0x40
#define OPENSAUCE_BUILD_STAGE_OFFSET 0x62
#define OPENSAUCE_BUILD_REVISION_OFFSET 0x64
#define OPENSAUCE_BUILD_TIMESTAMP_OFFSET 0x68
#define OPENSAUCE_BUILD_STRING_OFFSET 0x70
#define OPENSAUCE_TOOLS_VERSION_OFFSET 0x90
#define OPENSAUCE_MINIMUM_VERSION_OFFSET 0xA4
#define OPENSAUCE_RESOURCE_OFFSETS_OFFSET 0xB8
#define OPENSAUCE_HEADER_BYTES 0xC8
/* s_cache_header_yelo::k_version and k_version_minimum_build */
#define OPENSAUCE_HEADER_VERSION 1
#define OPENSAUCE_HEADER_VERSION_WITH_MINIMUM_BUILD 2
/* project_yellow::k_version and project_yellow_globals::k_version */
#define OPENSAUCE_PROJECT_YELLOW_VERSION 2
#define OPENSAUCE_PROJECT_YELLOW_GLOBALS_VERSION 2
/* K_MEMORY_UPGRADE_INCREASE_AMOUNT */
#define OPENSAUCE_MEMORY_UPGRADE_AMOUNT 1.5f

/* the tag index at the start of the tag data (OpenSauce
cache_files_structures.hpp, s_cache_tag_header and s_cache_tag_instance) */
#define TAG_INDEX_BYTES 0x28
#define TAG_INDEX_INSTANCES_OFFSET 0x00
#define TAG_INDEX_SCENARIO_OFFSET 0x04
#define TAG_INDEX_COUNT_OFFSET 0x0C
#define TAG_INDEX_VERTEX_DATA_OFFSET_OFFSET 0x14
#define TAG_INDEX_INDEX_DATA_OFFSET_OFFSET 0x1C
#define TAG_INDEX_MODEL_DATA_SIZE_OFFSET 0x20
#define TAG_INDEX_SIGNATURE_OFFSET 0x24
#define TAG_INSTANCE_BYTES 0x20
#define TAG_INSTANCE_GROUP_OFFSET 0x00
#define TAG_INSTANCE_HANDLE_OFFSET 0x0C
#define TAG_INSTANCE_NAME_OFFSET 0x10
#define TAG_INSTANCE_ADDRESS_OFFSET 0x14
#define TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET 0x18
/* the longest tag path a name may hold, terminator excluded */
#define TAG_NAME_MAXIMUM_LENGTH 255

/* tag blocks and tag data, as in this build (tag_files/tag_groups.h) */
#define TAG_BLOCK_BYTES 12
#define TAG_BLOCK_COUNT_OFFSET 0
#define TAG_BLOCK_ADDRESS_OFFSET 4
#define TAG_BLOCK_DEFINITION_OFFSET 8
#define TAG_DATA_BYTES 20
#define TAG_DATA_SIZE_OFFSET 0
#define TAG_DATA_FLAGS_OFFSET 4
#define TAG_DATA_FILE_OFFSET_OFFSET 8
#define TAG_DATA_ADDRESS_OFFSET 12
#define TAG_DATA_DEFINITION_OFFSET 16

/* the scenario's structure BSP block (OpenSauce scenario_definitions.hpp;
the offset holds in every map examined) */
#define SCENARIO_STRUCTURE_BSPS_OFFSET 0x5A4
#define SCENARIO_BYTES 0x5B0

/* The scenario's script syntax data (OpenSauce scenario_definitions.hpp,
hs_syntax_data): a data array (this build's memory/data.h) of 20-byte
syntax nodes. This build has room for 19001 nodes (hs.c,
MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO), as stock Custom Edition does;
OpenSauce's memory upgrades make room for 1.5 times as many (OpenSauce
blam_memory_upgrades.hpp, k_maximum_hs_syntax_nodes_per_scenario_upgrade). */
#define SCENARIO_HS_SYNTAX_DATA_OFFSET 0x474
#define DATA_ARRAY_HEADER_BYTES 0x38
#define DATA_ARRAY_MAXIMUM_COUNT_OFFSET 0x20
#define DATA_ARRAY_ELEMENT_BYTES_OFFSET 0x22
#define DATA_ARRAY_FIRST_FREE_INDEX_OFFSET 0x2C
#define DATA_ARRAY_COUNT_OFFSET 0x2E
#define HS_SYNTAX_NODE_BYTES 20
#define MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO 19001
#define MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO_UPGRADED 28501
#define STRUCTURE_BSP_REFERENCE_BYTES 0x20
#define STRUCTURE_BSP_REFERENCE_FILE_OFFSET_OFFSET 0x00
#define STRUCTURE_BSP_REFERENCE_SIZE_OFFSET 0x04
#define STRUCTURE_BSP_REFERENCE_ADDRESS_OFFSET 0x08
#define STRUCTURE_BSP_REFERENCE_TAG_INDEX_OFFSET 0x1C
/* OpenSauce's "max count: 32" for scenario_structure_bsp_reference */
#define MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO 32
/* structure_bsp_header (OpenSauce structure_bsp_definitions.hpp): the bsp
pointer, two vertex buffer arrays (Xbox only) and the signature */
#define STRUCTURE_BSP_HEADER_BYTES 0x18
#define STRUCTURE_BSP_HEADER_BSP_OFFSET 0x00
#define STRUCTURE_BSP_HEADER_VERTEX_BUFFERS_OFFSET 0x04
#define STRUCTURE_BSP_HEADER_LIGHTMAP_VERTEX_BUFFERS_OFFSET 0x0C
#define STRUCTURE_BSP_HEADER_SIGNATURE_OFFSET 0x14
/* the structure BSP's lightmaps and their materials (OpenSauce
structure_bsp_definitions.hpp: structure_bsp 0x288, structure_bsp_lightmap
0x20, structure_bsp_material 0x100; this build's structure_bsp_definitions.h
has the same offsets). A material's vertices are uncompressed in Custom
Edition caches (environment vertices of 56 bytes, then lightmap vertices of
20), and it has lightmap vertices exactly when its lightmap has a bitmap
(every map examined). */
#define STRUCTURE_BSP_BYTES 0x288
#define STRUCTURE_BSP_LIGHTMAPS_OFFSET 0x104
#define STRUCTURE_BSP_LIGHTMAP_BYTES 0x20
#define STRUCTURE_BSP_LIGHTMAP_BITMAP_INDEX_OFFSET 0x00
#define STRUCTURE_BSP_LIGHTMAP_MATERIALS_OFFSET 0x14
#define STRUCTURE_BSP_MATERIAL_BYTES 0x100
#define STRUCTURE_BSP_MATERIAL_VERTEX_TYPE_OFFSET 0xB0
#define STRUCTURE_BSP_MATERIAL_VERTEX_COUNT_OFFSET 0xB4
#define STRUCTURE_BSP_MATERIAL_LIGHTMAP_VERTEX_COUNT_OFFSET 0xC8
#define STRUCTURE_BSP_MATERIAL_UNCOMPRESSED_VERTICES_OFFSET 0xD8
#define STRUCTURE_BSP_ENVIRONMENT_VERTEX_BYTES 56
#define STRUCTURE_BSP_LIGHTMAP_VERTEX_BYTES 20
/* the unit vectors of an environment vertex (normal, binormal and tangent,
after its position) and of a lightmap vertex (its incident radiosity) */
#define STRUCTURE_BSP_ENVIRONMENT_VERTEX_VECTORS_OFFSET 12
#define STRUCTURE_BSP_ENVIRONMENT_VERTEX_VECTOR_COUNT 3
#define STRUCTURE_BSP_LIGHTMAP_VERTEX_VECTORS_OFFSET 0
#define STRUCTURE_BSP_LIGHTMAP_VERTEX_VECTOR_COUNT 1
/* This build compresses those vectors, and its compressor asserts that each
component is within [-1, 1] give or take its rounding: this far past 1 is
safely within that. Every vector of the maps examined is within 1.0001. */
#define MAXIMUM_UNIT_VECTOR_COMPONENT 1.005f
/* the vertex and triangle buffer types of this build's
rasterizer_geometry.h */
#define VERTEX_TYPE_ENVIRONMENT_UNCOMPRESSED 0
#define VERTEX_TYPE_MODEL_UNCOMPRESSED 4
#define TRIANGLE_BUFFER_TYPE_PRECOMPILED_STRIP 1
/* strips index vertices with 16 bits */
#define MAXIMUM_VERTICES_PER_BUFFER 0xFFFF

/* gbxmodel (OpenSauce model_definitions.hpp: gbxmodel_definition 0xE8, its
markers 0x40 and their instances 0x20, nodes 0x9C, regions 0x4C,
permutations 0x58 and their markers 0x50, geometries 0x30, parts 0x84 and
shader references 0x20). A part's triangle and vertex buffer fields say
where its strip and its vertices lie in the model data (Reclaimer
Halo1/GbxmodelTag.cs, ReadPCMeshes): the strip, count + 2 16-bit indices, at
its offset from the index data, the vertices, uncompressed and 68 bytes each,
at theirs from the vertex data. */
#define GBXMODEL_BYTES 0xE8
#define GBXMODEL_GEOMETRY_BYTES 0x30
#define GBXMODEL_PART_BYTES 0x84
#define GBXMODEL_PART_TRIANGLE_BUFFER_TYPE_OFFSET 0x44
#define GBXMODEL_PART_STRIP_TRIANGLE_COUNT_OFFSET 0x48
#define GBXMODEL_PART_STRIP_OFFSET_OFFSET 0x4C
#define GBXMODEL_PART_VERTEX_BUFFER_TYPE_OFFSET 0x54
#define GBXMODEL_PART_VERTEX_COUNT_OFFSET 0x58
#define GBXMODEL_PART_VERTEX_OFFSET_OFFSET 0x64
#define GBXMODEL_PART_LOCAL_NODE_COUNT_OFFSET 0x6B
#define GBXMODEL_MAXIMUM_LOCAL_NODES 22
#define GBXMODEL_VERTEX_BYTES 0x44
#define STRIP_INDEX_BYTES 2

/* shaders (OpenSauce shader_definitions.hpp): every shader starts with the
0x28-byte base, whose type is at 0x24. A transparent chicago extended shader
('scex', 0x78 bytes) is a transparent chicago shader ('schi', 0x6C) up to its
maps, which it has twice, for four and for two texture stages, before its
extra flags. */
#define SHADER_BYTES 0x28
#define SHADER_TYPE_OFFSET 0x24
#define TRANSPARENT_CHICAGO_GROUP_TAG 'schi'
#define TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG 'scex'
#define TRANSPARENT_CHICAGO_BYTES 0x6C
#define TRANSPARENT_CHICAGO_EXTENDED_BYTES 0x78
#define TRANSPARENT_CHICAGO_EXTRA_LAYERS_OFFSET 0x48
#define TRANSPARENT_CHICAGO_MAPS_OFFSET 0x54
#define TRANSPARENT_CHICAGO_EXTRA_FLAGS_OFFSET 0x60
#define TRANSPARENT_CHICAGO_EXTENDED_TWO_STAGE_MAPS_OFFSET 0x60
#define TRANSPARENT_CHICAGO_EXTENDED_EXTRA_FLAGS_OFFSET 0x6C
#define TRANSPARENT_CHICAGO_MAP_BYTES 0xDC

/* animation graphs (OpenSauce model_animation_definitions.hpp; this
build's model_animation_definitions.h has the same offsets): the object
overlays, whose first field names an animation of the graph or none */
#define ANIMATION_GRAPH_GROUP_TAG 'antr'
#define ANIMATION_GRAPH_BYTES 0x80
#define ANIMATION_GRAPH_OBJECT_OVERLAYS_OFFSET 0x00
#define ANIMATION_GRAPH_ANIMATIONS_OFFSET 0x74
#define ANIMATION_GRAPH_OBJECT_OVERLAY_BYTES 0x14
#define ANIMATION_GRAPH_OBJECT_OVERLAY_ANIMATION_INDEX_OFFSET 0x00
#define NO_BLOCK_INDEX (-1)

/* HUD placements (this build's unit_hud_interface_definition.h and
hud_weapon.c; OpenSauce hud_definitions.hpp, s_hud_element, and
weapon_hud_interface_definition.hpp agree): an element's offset, scale and
scaling flags, of which Halo PC added the high resolution scale. Where they
lie in the unit, weapon and grenade HUD interfaces, in the elements of
their blocks and in the HUD globals' messages. */
#define HUD_PLACEMENT_WIDTH_SCALE_OFFSET 0x04
#define HUD_PLACEMENT_HEIGHT_SCALE_OFFSET 0x08
#define HUD_PLACEMENT_SCALING_FLAGS_OFFSET 0x0C
#define HUD_SCALING_USE_HIGH_RESOLUTION_SCALE_BIT 2
#define UNIT_HUD_INTERFACE_GROUP_TAG 'unhi'
#define UNIT_HUD_INTERFACE_BYTES 0x56C
#define UNIT_HUD_INTERFACE_AUXILIARY_OVERLAYS_OFFSET 0x3A4
#define UNIT_HUD_INTERFACE_AUXILIARY_OVERLAY_BYTES 0x84
#define UNIT_HUD_INTERFACE_AUXILIARY_METERS_OFFSET 0x3CC
#define UNIT_HUD_INTERFACE_AUXILIARY_METER_BYTES 0x144
#define UNIT_HUD_INTERFACE_AUXILIARY_METER_BACKGROUND_OFFSET 0x14
#define UNIT_HUD_INTERFACE_AUXILIARY_METER_METER_OFFSET 0x7C
#define WEAPON_HUD_INTERFACE_GROUP_TAG 'wphi'
#define WEAPON_HUD_INTERFACE_BYTES 0x17C
#define WEAPON_HUD_INTERFACE_STATICS_OFFSET 0x60
#define WEAPON_HUD_INTERFACE_METERS_OFFSET 0x6C
#define WEAPON_HUD_INTERFACE_NUMBERS_OFFSET 0x78
#define WEAPON_HUD_INTERFACE_CROSSHAIRS_OFFSET 0x84
#define WEAPON_HUD_INTERFACE_OVERLAYS_OFFSET 0x90
#define WEAPON_HUD_STATIC_OR_METER_BYTES 0xB4
#define WEAPON_HUD_NUMBER_BYTES 0xA0
/* statics, meters and numbers start with a 0x24-byte header */
#define WEAPON_HUD_ELEMENT_PLACEMENT_OFFSET 0x24
/* crosshairs and overlays: a bitmap and a block of items, each item
starting with its placement */
#define WEAPON_HUD_CROSSHAIRS_OR_OVERLAYS_BYTES 0x68
#define WEAPON_HUD_ITEMS_OFFSET 0x34
#define WEAPON_HUD_CROSSHAIR_ITEM_BYTES 0x6C
#define WEAPON_HUD_OVERLAY_ITEM_BYTES 0x88
#define GRENADE_HUD_INTERFACE_GROUP_TAG 'grhi'
#define GRENADE_HUD_INTERFACE_BYTES 0x1F8
#define GRENADE_HUD_INTERFACE_OVERLAY_ITEMS_OFFSET 0x15C
#define HUD_GLOBALS_GROUP_TAG 'hudg'
#define HUD_GLOBALS_BYTES 0x450
#define HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET 0x24

/* following_camera.c verifies the 0x30-byte track header and 0x3C-byte
   control points with compile-time size/offset assertions. */
#define CAMERA_TRACK_BYTES 0x30
#define CAMERA_TRACK_CONTROL_POINT_BYTES 0x3C

/* particle_system_definitions.h asserts these element sizes and the major
   nested block offsets for particle-system tags. */
#define PARTICLE_SYSTEM_BYTES 0x68
#define PARTICLE_SYSTEM_TYPE_BYTES 0x80
#define PARTICLE_SYSTEM_TYPE_STATE_BYTES 0xC0
#define PARTICLE_SYSTEM_PARTICLE_STATE_BYTES 0x178

/* the multiplayer messages: string 100 is the hint that the BACK button
shows the score, which this build copies verbatim
(game_engine.c, _game_engine_message_press_back_for_score) and Halo PC
formats with the name of its score key: 'Hold "%s" for score' */
#define MULTIPLAYER_GAME_TEXT_NAME "ui\\multiplayer_game_text"
#define MULTIPLAYER_GAME_TEXT_SCORE_HINT_INDEX 100
#define UNICODE_STRING_LIST_BYTES 0x0C
#define UNICODE_STRING_LIST_STRINGS_OFFSET 0x00
#define SCORE_KEY_PLACEHOLDER_CHARACTERS 4

/* resource maps (OpenSauce data_file_structures.hpp, s_data_file_header and
s_data_file_item) */
#define RESOURCE_MAP_HEADER_BYTES 0x10
#define RESOURCE_MAP_NAMES_OFFSET_OFFSET 0x04
#define RESOURCE_MAP_INDEX_OFFSET_OFFSET 0x08
#define RESOURCE_MAP_COUNT_OFFSET 0x0C
#define RESOURCE_MAP_ITEM_BYTES 0x0C
#define RESOURCE_MAP_ITEM_NAME_OFFSET 0x00
#define RESOURCE_MAP_ITEM_SIZE_OFFSET 0x04
#define RESOURCE_MAP_ITEM_DATA_OFFSET 0x08

/* bitmap group (OpenSauce bitmap_group.hpp: s_bitmap_group 0x6C,
s_bitmap_group_sequence 0x40, s_bitmap_group_sprite 0x20, s_bitmap_data 0x30) */
#define BITMAP_GROUP_BYTES 0x6C
#define BITMAP_GROUP_BITMAPS_OFFSET 0x60
#define BITMAP_DATA_BYTES 0x30
#define BITMAP_DATA_FLAGS_OFFSET 0x0E
#define BITMAP_DATA_PIXELS_OFFSET_OFFSET 0x18
#define BITMAP_DATA_PIXELS_SIZE_OFFSET 0x1C
/* the fields the game fills while it draws a bitmap (bitmap_group.h): the
tag the bitmap belongs to, its texture cache block, and its hardware
texture and pixels once they are loaded */
#define BITMAP_DATA_TAG_INDEX_OFFSET 0x20
#define BITMAP_DATA_CACHE_BLOCK_INDEX_OFFSET 0x24
#define BITMAP_DATA_HARDWARE_FORMAT_OFFSET 0x28
#define BITMAP_DATA_BASE_ADDRESS_OFFSET 0x2C
/* s_bitmap_data::_flags::in_data_file: the pixels are in bitmaps.map */
#define BITMAP_DATA_IN_RESOURCE_MAP_BIT 8

/* sound (OpenSauce sound_definitions.hpp: sound_definition 0xA4,
s_sound_pitch_range 0x48, s_sound_permutation 0x7C) */
#define SOUND_DEFINITION_BYTES 0xA4
#define SOUND_PITCH_RANGES_OFFSET 0x98
#define SOUND_PITCH_RANGE_BYTES 0x48
#define SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET 0x3C
#define SOUND_PERMUTATION_BYTES 0x7C
#define SOUND_PERMUTATION_SAMPLES_OFFSET 0x40
/* s_sound_permutation::_samples_in_data_file_bit */
#define SOUND_SAMPLES_IN_RESOURCE_MAP_BIT 0
/* The fields of a sound held by sounds.map that the map's copy of its
header leaves zero (every such sound of the maps examined), and that the
entry's copy holds as the Xbox maps of build 2276 do for the same sounds
(this build's sound_definitions.h names them) */
#define SOUND_SAMPLE_RATE_OFFSET 0x06
#define SOUND_ENCODING_OFFSET 0x6C
#define SOUND_COMPRESSION_OFFSET 0x6E
#define SOUND_LONGEST_PERMUTATION_LENGTH_OFFSET 0x84
/* a permutation's compression, and the fields the game fills while it plays
it (xbox_sound_cache.c: its cache block and base address, and the tag it
names in reads and messages) */
#define SOUND_PERMUTATION_COMPRESSION_OFFSET 0x28
#define SOUND_PERMUTATION_CACHE_BLOCK_INDEX_OFFSET 0x2C
#define SOUND_PERMUTATION_CACHE_BASE_ADDRESS_OFFSET 0x30
#define SOUND_PERMUTATION_CACHE_TAG_INDEX_OFFSET 0x34
#define SOUND_PERMUTATION_RUNTIME_TAG_INDEX_OFFSET 0x3C
/* the compressions this build plays or refuses cleanly (sound_manager.c:
none and Xbox ADPCM; Custom Edition also has Ogg Vorbis, 3) */
#define SOUND_COMPRESSION_NONE 0
#define SOUND_COMPRESSION_XBOX_ADPCM 1

/* font (BlamLib Misc.cs, font_group: 156 bytes, 36 bytes of padding after
the heights; the block offsets below hold for every font in loc.map) */
#define FONT_BYTES 0x9C
#define FONT_STYLE_REFERENCES_OFFSET 0x3C
#define FONT_STYLE_REFERENCE_COUNT 4
#define TAG_REFERENCE_BYTES 16
#define TAG_REFERENCE_INDEX_OFFSET 12

#define UNICODE_STRING_LIST_BYTES 0x0C
#define HUD_MESSAGE_TEXT_BYTES 0x80

/* CRC-32 (reflected polynomial 0x04C11DB7), updated from 0xFFFFFFFF with no
final inversion, as OpenSauce's Memory::CRC and CalculateChecksum do */
#define CRC32_POLYNOMIAL 0xEDB88320UL
#define CRC32_INITIAL 0xFFFFFFFFUL
#define CHECKSUM_READ_CHUNK_BYTES 0x10000

/* ---------- structures */

struct element_layout;

/* a tag block field and the layout of its elements */
struct block_layout
{
	uint32_t offset;
	uint32_t element_bytes;
	struct element_layout const *element;
};

struct load_state;

struct shader_group_type
{
	uint32_t group_tag;
	int16_t custom_edition_type;
	int16_t type;
};

typedef enum cache_file_status (*element_check_proc)(
	struct load_state *state,
	uint32_t element_offset);

/* the pointers an element holds: tag blocks and tag data fields */
struct element_layout
{
	uint32_t bytes;
	struct block_layout const *blocks;
	int block_count;
	uint32_t const *data_offsets;
	int data_count;
	element_check_proc check;
	/* Optional offsets of 0x10-byte tag_reference records. Their name
	   pointer is at +4 and may be NULL. Used by the native rebase pass. */
	uint32_t const *reference_offsets;
	int reference_count;
	/* A contiguous run of 0x10-byte references, used by dialogue tags. */
	uint32_t reference_span_offset;
	int reference_span_count;
};

struct load_state
{
	struct cache_file_source *map;
	struct resource_map *const *resource_maps;
	struct custom_edition_load_report *report;
	uint8_t *tag_cache;
	/* Source PC address during loading/conversion, destination address for
	   a cache passed through custom_edition_cache_rebase_known. */
	uint32_t address_base;
	/* the bytes of tag_cache in use, and the most that may be used: the
	lowest structure BSP address, relative to the tag cache */
	uint32_t used_bytes;
	uint32_t usable_bytes;
	uint32_t file_length;
	/* the model data in the file: vertices, then from `index_data_offset`
	the strips */
	uint32_t model_data_offset;
	uint32_t model_index_data_offset;
	uint32_t model_data_size;
	int32_t tag_index;
};

/* ---------- prototypes */

static enum cache_file_status bitmap_data_check(
	struct load_state *state,
	uint32_t element_offset);
static enum cache_file_status sound_permutation_check(
	struct load_state *state,
	uint32_t element_offset);
static enum cache_file_status gbxmodel_part_check(
	struct load_state *state,
	uint32_t element_offset);

/* ---------- globals */

static struct element_layout const plain_element_layout = { 0, NULL, 0, NULL, 0, NULL };

static struct block_layout const camera_track_blocks[] =
{
	{ 0x04, CAMERA_TRACK_CONTROL_POINT_BYTES, &plain_element_layout },
};
static struct element_layout const camera_track_layout =
{
	CAMERA_TRACK_BYTES, camera_track_blocks, 1, NULL, 0, NULL
};

/* the placements the unit and grenade HUD interfaces hold themselves */
static uint32_t const unit_hud_interface_placements[] =
{
	/* background, shield background and meter, health background and
	meter, motion sensor background and foreground, blips */
	0x24, 0x8C, 0xF4, 0x17C, 0x1E4, 0x26C, 0x2D4, 0x35C,
};
static uint32_t const grenade_hud_interface_placements[] =
{
	/* background, grenade count background and numbers */
	0x24, 0x8C, 0xF4,
};

/* the score key's name in Halo PC's hint, "%s" in quotes, and the Xbox
button that takes its place, in UTF-16 */
static uint16_t const score_key_placeholder[SCORE_KEY_PLACEHOLDER_CHARACTERS] = { '"', '%', 's', '"' };
static uint16_t const score_button_name[SCORE_KEY_PLACEHOLDER_CHARACTERS] = { 'B', 'A', 'C', 'K' };

static struct block_layout const bitmap_sequence_blocks[] =
{
	/* sprites */
	{ 0x34, 0x20, &plain_element_layout },
};
static struct element_layout const bitmap_sequence_layout =
{
	0x40, bitmap_sequence_blocks, 1, NULL, 0, NULL
};
static struct element_layout const bitmap_data_layout =
{
	0x30, NULL, 0, NULL, 0, bitmap_data_check
};
static struct block_layout const bitmap_group_blocks[] =
{
	/* sequences, bitmaps */
	{ 0x54, 0x40, &bitmap_sequence_layout },
	{ BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_DATA_BYTES, &bitmap_data_layout },
};
static uint32_t const bitmap_group_data[] =
{
	/* compressed color plate data, processed pixel data */
	0x1C,
	0x30,
};
static struct element_layout const bitmap_group_layout =
{
	BITMAP_GROUP_BYTES, bitmap_group_blocks, 2, bitmap_group_data, 2, NULL
};

static uint32_t const sound_permutation_data[] =
{
	/* mouth data, subtitle data; the samples are streamed from a file */
	0x54,
	0x68,
};
static struct element_layout const sound_permutation_layout =
{
	SOUND_PERMUTATION_BYTES, NULL, 0, sound_permutation_data, 2, sound_permutation_check
};
static struct block_layout const sound_pitch_range_blocks[] =
{
	/* permutations */
	{ SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET, SOUND_PERMUTATION_BYTES, &sound_permutation_layout },
};
static struct element_layout const sound_pitch_range_layout =
{
	SOUND_PITCH_RANGE_BYTES, sound_pitch_range_blocks, 1, NULL, 0, NULL
};
static struct block_layout const sound_definition_blocks[] =
{
	{ SOUND_PITCH_RANGES_OFFSET, SOUND_PITCH_RANGE_BYTES, &sound_pitch_range_layout },
};
/* sound_definitions.h: promotion_sound starts at 0x70, name pointer +4. */
static uint32_t const sound_definition_references[] = { 0x70 };
static struct element_layout const sound_definition_layout =
{
	SOUND_DEFINITION_BYTES, sound_definition_blocks, 1, NULL, 0, NULL,
	sound_definition_references, 1
};

static struct block_layout const font_character_table_blocks[] =
{
	/* character indices */
	{ 0x00, 2, &plain_element_layout },
};
static struct element_layout const font_character_table_layout =
{
	TAG_BLOCK_BYTES, font_character_table_blocks, 1, NULL, 0, NULL
};
static struct block_layout const font_blocks[] =
{
	/* character tables, characters */
	{ 0x30, TAG_BLOCK_BYTES, &font_character_table_layout },
	{ 0x7C, 20, &plain_element_layout },
};
static uint32_t const font_data[] =
{
	/* pixels */
	0x88,
};
static struct element_layout const font_layout =
{
	FONT_BYTES, font_blocks, 2, font_data, 1, NULL
};

static uint32_t const unicode_string_reference_data[] =
{
	0x00,
};
static struct element_layout const unicode_string_reference_layout =
{
	TAG_DATA_BYTES, NULL, 0, unicode_string_reference_data, 1, NULL
};
static struct block_layout const unicode_string_list_blocks[] =
{
	{ 0x00, TAG_DATA_BYTES, &unicode_string_reference_layout },
};
static struct element_layout const unicode_string_list_layout =
{
	UNICODE_STRING_LIST_BYTES, unicode_string_list_blocks, 1, NULL, 0, NULL
};

/* text_group.h uses the same block/data layout for ANSI and Unicode lists. */
static uint32_t const hud_number_references[] = { 0x00 };
static struct element_layout const hud_number_layout =
{
	0x64, NULL, 0, NULL, 0, NULL, hud_number_references, 1
};
/* sound_environment_definitions.h and wind_definitions.h contain scalars
   only, with compile-time size checks. */
static struct element_layout const sound_environment_layout =
{
	0x48, NULL, 0, NULL, 0, NULL
};
static struct element_layout const wind_layout =
{
	0x40, NULL, 0, NULL, 0, NULL
};

static struct block_layout const hud_message_text_blocks[] =
{
	/* message elements, messages */
	{ 0x14, 2, &plain_element_layout },
	{ 0x20, 0x40, &plain_element_layout },
};
static uint32_t const hud_message_text_data[] =
{
	/* text */
	0x00,
};
static struct element_layout const hud_message_text_layout =
{
	HUD_MESSAGE_TEXT_BYTES, hud_message_text_blocks, 2, hud_message_text_data, 1, NULL
};

static struct block_layout const model_marker_blocks[] =
{
	/* instances */
	{ 0x34, 0x20, &plain_element_layout },
};
static struct element_layout const model_marker_layout =
{
	0x40, model_marker_blocks, 1, NULL, 0, NULL
};
static struct block_layout const model_permutation_blocks[] =
{
	/* markers */
	{ 0x4C, 0x50, &plain_element_layout },
};
static struct element_layout const model_permutation_layout =
{
	0x58, model_permutation_blocks, 1, NULL, 0, NULL
};
static struct block_layout const model_region_blocks[] =
{
	/* permutations */
	{ 0x40, 0x58, &model_permutation_layout },
};
static struct element_layout const model_region_layout =
{
	0x4C, model_region_blocks, 1, NULL, 0, NULL
};
static struct block_layout const gbxmodel_part_blocks[] =
{
	/* uncompressed vertices, compressed vertices, triangles: empty in every
	map examined, the geometry being in the model data */
	{ 0x20, GBXMODEL_VERTEX_BYTES, &plain_element_layout },
	{ 0x2C, 0x20, &plain_element_layout },
	{ 0x38, 3 * STRIP_INDEX_BYTES, &plain_element_layout },
};
static struct element_layout const gbxmodel_part_layout =
{
	GBXMODEL_PART_BYTES, gbxmodel_part_blocks, 3, NULL, 0, gbxmodel_part_check
};
static struct block_layout const gbxmodel_geometry_blocks[] =
{
	/* parts */
	{ 0x24, GBXMODEL_PART_BYTES, &gbxmodel_part_layout },
};
static struct element_layout const gbxmodel_geometry_layout =
{
	GBXMODEL_GEOMETRY_BYTES, gbxmodel_geometry_blocks, 1, NULL, 0, NULL
};
/* models.c: each 0x20 model shader entry begins with a dependency. */
static uint32_t const model_shader_references[] = { 0x00 };
static struct element_layout const model_shader_reference_layout =
{
	0x20, NULL, 0, NULL, 0, NULL,
	model_shader_references, 1
};
static struct block_layout const gbxmodel_blocks[] =
{
	/* markers, nodes, regions, geometries, shaders */
	{ 0xAC, 0x40, &model_marker_layout },
	{ 0xB8, 0x9C, &plain_element_layout },
	{ 0xC4, 0x4C, &model_region_layout },
	{ 0xD0, GBXMODEL_GEOMETRY_BYTES, &gbxmodel_geometry_layout },
	{ 0xDC, 0x20, &model_shader_reference_layout },
};
static struct element_layout const gbxmodel_layout =
{
	GBXMODEL_BYTES, gbxmodel_blocks, 5, NULL, 0, NULL
};

/* effect_definitions.h and effects.c: effect 0x40, event 0x44, part
   0x68, particle 0xE8, location 0x20. The PC cache pass below checks
   these offsets against every available retail map before rebasing. */
static uint32_t const effect_part_references[] = { 0x18 };
static struct element_layout const effect_part_layout =
{
	0x68, NULL, 0, NULL, 0, NULL, effect_part_references, 1
};
static uint32_t const effect_particle_references[] = { 0x54 };
static struct element_layout const effect_particle_layout =
{
	0xE8, NULL, 0, NULL, 0, NULL, effect_particle_references, 1
};
static struct block_layout const effect_event_blocks[] =
{
	{ 0x2C, 0x68, &effect_part_layout },
	{ 0x38, 0xE8, &effect_particle_layout },
};
static struct element_layout const effect_event_layout =
{
	0x44, effect_event_blocks, 2, NULL, 0, NULL
};
static struct block_layout const effect_blocks[] =
{
	{ 0x28, 0x20, &plain_element_layout },
	{ 0x34, 0x44, &effect_event_layout },
};
static struct element_layout const effect_layout =
{
	0x40, effect_blocks, 2, NULL, 0, NULL
};

/* ui_widget.c's offset and size assertions cover these five arrays. */
static uint32_t const ui_event_references[] = { 0x08, 0x18 };
static struct element_layout const ui_event_layout =
{
	0x48, NULL, 0, NULL, 0, NULL, ui_event_references, 2
};
static uint32_t const ui_child_references[] = { 0x00 };
static struct element_layout const ui_child_layout =
{
	0x50, NULL, 0, NULL, 0, NULL, ui_child_references, 1
};
static struct block_layout const ui_widget_blocks[] =
{
	{ 0x48, 0x24, &plain_element_layout },
	{ 0x54, 0x48, &ui_event_layout },
	{ 0x60, 0x22, &plain_element_layout },
	{ 0x2D4, 0x50, &ui_child_layout },
	{ 0x3E0, 0x50, &ui_child_layout },
};
static uint32_t const ui_widget_references[] =
{
	0x38, 0xEC, 0xFC, 0x154, 0x164, 0x1A4
};
static struct element_layout const ui_widget_layout =
{
	0x3EC, ui_widget_blocks, 5, NULL, 0, NULL, ui_widget_references, 6
};

/* rasterizer_xbox_models.c asserts the four bitmap-reference index offsets;
   tag_reference.index is at +0xC, yielding these record starts. */
static uint32_t const shader_model_references[] =
{
	0xA4, 0xBC, 0xDC, 0x164
};
static struct element_layout const shader_model_layout =
{
	0x174, NULL, 0, NULL, 0, NULL, shader_model_references, 4
};

/* rasterizer_xbox_transparent_geometry.c fixes the glass's 0x16C detail
   map and meter's 0x7C gradient; the other bitmap records precede them. */
static uint32_t const transparent_glass_references[] =
{
	0x64, 0xAC, 0xC0, 0x158, 0x16C, 0x1A0, 0x1B4
};
static struct element_layout const transparent_glass_layout =
{
	0x1E0, NULL, 0, NULL, 0, NULL,
	transparent_glass_references, 7
};
static uint32_t const transparent_meter_references[] = { 0x4C };
static struct element_layout const transparent_meter_layout =
{
	0x104, NULL, 0, NULL, 0, NULL,
	transparent_meter_references, 1
};

/* Invader's shader_transparent_plasma.json defines a 0x28 Shader base and a
   0x124-byte extension. Its two noise-map dependencies start at 0xD4 and
   0x11C in the complete 0x14C-byte tag. */
static uint32_t const transparent_plasma_references[] = { 0xD4, 0x11C };
static struct element_layout const transparent_plasma_layout =
{
	0x14C, NULL, 0, NULL, 0, NULL,
	transparent_plasma_references, 2
};

/* particles.h fixes the 0x164 definition and shader offset 0xB0;
   shader_definitions.h fixes the shader's secondary map at +0x4C. */
static uint32_t const particle_references[] =
{
	0x04, 0x14, 0x24, 0x48, 0x58, 0xFC
};
static struct element_layout const particle_layout =
{
	0x164, NULL, 0, NULL, 0, NULL, particle_references, 6
};

static uint32_t const particle_system_particle_state_references[] =
{
	/* bitmap, point physics, shader effect secondary map */
	0x30, 0x84, 0xB8 + 0x4C
};
static struct element_layout const particle_system_particle_state_layout =
{
	PARTICLE_SYSTEM_PARTICLE_STATE_BYTES, NULL, 0, NULL, 0, NULL,
	particle_system_particle_state_references, 3
};
static struct block_layout const particle_system_type_state_blocks[] =
{
	{ 0xB4, 0x04, &plain_element_layout },
};
static struct element_layout const particle_system_type_state_layout =
{
	PARTICLE_SYSTEM_TYPE_STATE_BYTES, particle_system_type_state_blocks, 1,
	NULL, 0, NULL
};
static struct block_layout const particle_system_type_blocks[] =
{
	{ 0x5C, 0x04, &plain_element_layout },
	{ 0x68, PARTICLE_SYSTEM_TYPE_STATE_BYTES, &particle_system_type_state_layout },
	{ 0x74, PARTICLE_SYSTEM_PARTICLE_STATE_BYTES, &particle_system_particle_state_layout },
};
static struct element_layout const particle_system_type_layout =
{
	PARTICLE_SYSTEM_TYPE_BYTES, particle_system_type_blocks, 3, NULL, 0, NULL
};
static struct block_layout const particle_system_blocks[] =
{
	{ 0x50, 0x04, &plain_element_layout },
	{ 0x5C, PARTICLE_SYSTEM_TYPE_BYTES, &particle_system_type_layout },
};
static uint32_t const particle_system_references[] = { 0x38 };
static struct element_layout const particle_system_layout =
{
	PARTICLE_SYSTEM_BYTES, particle_system_blocks, 2, NULL, 0, NULL,
	particle_system_references, 1
};

/* material_effect_definitions.h fixes the 0x1C effect and 0x30 material;
   each material names an effect and sound. */
static uint32_t const material_effect_material_references[] = { 0x00, 0x10 };
static struct element_layout const material_effect_material_layout =
{
	0x30, NULL, 0, NULL, 0, NULL,
	material_effect_material_references, 2
};
static struct block_layout const material_effect_blocks[] =
{
	{ 0x00, 0x30, &material_effect_material_layout },
};
static struct element_layout const material_effect_layout =
{
	0x1C, material_effect_blocks, 1, NULL, 0, NULL
};
static struct block_layout const material_effects_blocks[] =
{
	{ 0x00, 0x1C, &material_effect_layout },
};
static struct element_layout const material_effects_layout =
{
	0x0C, material_effects_blocks, 1, NULL, 0, NULL
};

/* contrail_definitions.h fixes the 0x144 definition, 0x138 states block,
   and 0x68 state. The state holds one point-physics reference. */
static uint32_t const contrail_state_references[] = { 0x10 };
static struct element_layout const contrail_state_layout =
{
	0x68, NULL, 0, NULL, 0, NULL, contrail_state_references, 1
};
static struct block_layout const contrail_blocks[] =
{
	{ 0x138, 0x68, &contrail_state_layout },
};
static uint32_t const contrail_references[] = { 0x30 };
static struct element_layout const contrail_layout =
{
	0x144, contrail_blocks, 1, NULL, 0, NULL, contrail_references, 1
};

/* physics_definitions.h and physics.c fix these mass point sizes;
   the 3x3 inertia matrix is nine 32-bit reals. */
static struct block_layout const physics_blocks[] =
{
	{ 0x5C, 0x24, &plain_element_layout },
	{ 0x68, 0x80, &plain_element_layout },
	{ 0x74, 0x80, &plain_element_layout },
};
static struct element_layout const physics_layout =
{
	0x80, physics_blocks, 3, NULL, 0, NULL
};

/* light_volumes.h asserts the 0x14C definition, 0x120 frames block and
   0xB0 frame; its map reference starts at 0x5C. */
static struct block_layout const light_volume_blocks[] =
{
	{ 0x120, 0xB0, &plain_element_layout },
};
static uint32_t const light_volume_references[] = { 0x5C };
static struct element_layout const light_volume_layout =
{
	0x14C, light_volume_blocks, 1, NULL, 0, NULL,
	light_volume_references, 1
};

/* color_table_group.h fixes each color to 0x30 bytes. */
static struct block_layout const color_table_blocks[] =
{
	{ 0x00, 0x30, &plain_element_layout },
};
static struct element_layout const color_table_layout =
{
	0x0C, color_table_blocks, 1, NULL, 0, NULL
};

/* antenna.h fixes the 0xD0 definition and 0x80 vertex. */
static struct block_layout const antenna_blocks[] =
{
	{ 0xC4, 0x80, &plain_element_layout },
};
static uint32_t const antenna_references[] = { 0x20, 0x30 };
static struct element_layout const antenna_layout =
{
	0xD0, antenna_blocks, 1, NULL, 0, NULL, antenna_references, 2
};

/* flags.c places red shader, physics and blue shader references before
   its attachment-point block. */
static struct block_layout const flag_blocks[] =
{
	{ 0x54, 0x34, &plain_element_layout },
};
static uint32_t const flag_references[] = { 0x18, 0x28, 0x44 };
static struct element_layout const flag_layout =
{
	0x60, flag_blocks, 1, NULL, 0, NULL, flag_references, 3
};

/* damage_effect_definitions.h: the screen flash, vibration, impulse, and
   shake structures place the sound reference at 0x114. */
static uint32_t const damage_effect_references[] = { 0x114 };
static struct element_layout const damage_effect_layout =
{
	0x124, NULL, 0, NULL, 0, NULL, damage_effect_references, 1
};

/* The PC light cache keeps primary and secondary cube-map dependencies in
   the source's reserved 0x58..0xAC area; Invader's light schema fixes their
   starts at 0x64 and 0x7C. object_lights.c asserts lens flare at 0xAC. */
static uint32_t const light_references[] = { 0x64, 0x7C, 0xAC };
static struct element_layout const light_layout =
{
	0x104, NULL, 0, NULL, 0, NULL, light_references, 3
};

/* decals.c asserts size 0x10C and shader.map.index 0xE4. */
static uint32_t const decal_references[] = { 0x08, 0xD8 };
static struct element_layout const decal_layout =
{
	0x10C, NULL, 0, NULL, 0, NULL, decal_references, 2
};

/* rasterizer_lights.c verifies the 0xF0 lens flare definition and 0x80
   reflection element. */
static struct block_layout const lens_flare_blocks[] =
{
	{ 0xC4, 0x80, &plain_element_layout },
};
static uint32_t const lens_flare_references[] = { 0x20 };
static struct element_layout const lens_flare_layout =
{
	0xF0, lens_flare_blocks, 1, NULL, 0, NULL, lens_flare_references, 1
};

/* sound_definitions.h verifies all three sizes and the root tracks offset. */
static uint32_t const looping_sound_track_references[] =
{
	0x30, 0x40, 0x50, 0x80, 0x90
};
static struct element_layout const looping_sound_track_layout =
{
	0xA0, NULL, 0, NULL, 0, NULL, looping_sound_track_references, 5
};
static uint32_t const looping_sound_detail_references[] = { 0x00 };
static struct element_layout const looping_sound_detail_layout =
{
	0x68, NULL, 0, NULL, 0, NULL, looping_sound_detail_references, 1
};
static struct block_layout const looping_sound_blocks[] =
{
	{ 0x3C, 0xA0, &looping_sound_track_layout },
	{ 0x48, 0x68, &looping_sound_detail_layout },
};
static uint32_t const looping_sound_references[] = { 0x2C };
static struct element_layout const looping_sound_layout =
{
	0x54, looping_sound_blocks, 2, NULL, 0, NULL, looping_sound_references, 1
};

/* point_physics.h asserts a 0x40-byte scalar-only definition. */
static struct element_layout const point_physics_layout =
{
	0x40, NULL, 0, NULL, 0, NULL
};

/* collision_model_definitions.h, damage_resistances.h and
   collision_bsp_definitions.h fix the ARM offsets in collision_layout.c.
   The remaining region permutation (0x20) and modifier (0x34) sizes match
   Reclaimer's published Halo 1 coll descriptor:
   https://github.com/Sigmmma/reclaimer/blob/master/reclaimer/hek/defs/coll.py */
static struct block_layout const collision_bsp_blocks[] =
{
	{ 0x00, 0x0C, &plain_element_layout },
	{ 0x0C, 0x10, &plain_element_layout },
	{ 0x18, 0x08, &plain_element_layout },
	{ 0x24, 0x08, &plain_element_layout },
	{ 0x30, 0x14, &plain_element_layout },
	{ 0x3C, 0x0C, &plain_element_layout },
	{ 0x48, 0x18, &plain_element_layout },
	{ 0x54, 0x10, &plain_element_layout },
};
static struct element_layout const collision_bsp_layout =
{
	0x60, collision_bsp_blocks, 8, NULL, 0, NULL
};
static struct block_layout const collision_node_blocks[] =
{
	{ 0x34, 0x60, &collision_bsp_layout },
};
static struct element_layout const collision_node_layout =
{
	0x40, collision_node_blocks, 1, NULL, 0, NULL
};
static struct block_layout const collision_region_blocks[] =
{
	{ 0x48, 0x20, &plain_element_layout },
};
static uint32_t const collision_region_references[] = { 0x38 };
static struct element_layout const collision_region_layout =
{
	0x54, collision_region_blocks, 1, NULL, 0, NULL,
	collision_region_references, 1
};
static struct block_layout const collision_model_blocks[] =
{
	{ 0x234, 0x48, &plain_element_layout },
	{ 0x240, 0x54, &collision_region_layout },
	{ 0x24C, 0x34, &plain_element_layout },
	{ 0x280, 0x20, &plain_element_layout },
	{ 0x28C, 0x40, &collision_node_layout },
};
static uint32_t const collision_model_references[] =
{
	0x70, 0x84, 0x98, 0xA8, 0xBC, 0x188, 0x198, 0x1A8
};
static struct element_layout const collision_model_layout =
{
	0x298, collision_model_blocks, 5, NULL, 0, NULL,
	collision_model_references, 8
};

/* model_animation_definitions.h gives the nested ARM structures. The
   complete PC reflexive sizes also match Reclaimer's Halo 1 antr schema:
   https://github.com/Sigmmma/reclaimer/blob/master/reclaimer/hek/defs/antr.py */
static struct block_layout const animation_weapon_type_blocks[] =
{
	{ 0x30, 0x02, &plain_element_layout },
};
static struct element_layout const animation_weapon_type_layout =
{
	0x3C, animation_weapon_type_blocks, 1, NULL, 0, NULL
};
static struct block_layout const animation_weapon_class_blocks[] =
{
	{ 0x98, 0x02, &plain_element_layout },
	{ 0xA4, 0x40, &plain_element_layout },
	{ 0xB0, 0x3C, &animation_weapon_type_layout },
};
static struct element_layout const animation_weapon_class_layout =
{
	0xBC, animation_weapon_class_blocks, 3, NULL, 0, NULL
};
static struct block_layout const animation_unit_seat_blocks[] =
{
	{ 0x40, 0x02, &plain_element_layout },
	{ 0x4C, 0x40, &plain_element_layout },
	{ 0x58, 0xBC, &animation_weapon_class_layout },
};
static struct element_layout const animation_unit_seat_layout =
{
	0x64, animation_unit_seat_blocks, 3, NULL, 0, NULL
};
static struct block_layout const animation_weapon_blocks[] =
{
	{ 0x10, 0x02, &plain_element_layout },
};
static struct element_layout const animation_weapon_layout =
{
	0x1C, animation_weapon_blocks, 1, NULL, 0, NULL
};
static struct block_layout const animation_vehicle_blocks[] =
{
	{ 0x5C, 0x02, &plain_element_layout },
	{ 0x68, 0x14, &plain_element_layout },
};
static struct element_layout const animation_vehicle_layout =
{
	0x74, animation_vehicle_blocks, 2, NULL, 0, NULL
};
static struct block_layout const animation_device_blocks[] =
{
	{ 0x54, 0x02, &plain_element_layout },
};
static struct element_layout const animation_device_layout =
{
	0x60, animation_device_blocks, 1, NULL, 0, NULL
};
static uint32_t const animation_sound_references[] = { 0x00 };
static struct element_layout const animation_sound_layout =
{
	0x14, NULL, 0, NULL, 0, NULL, animation_sound_references, 1
};
static uint32_t const animation_data_offsets[] =
{
	0x48, 0x8C, 0xA0
};
static struct element_layout const animation_record_layout =
{
	0xB4, NULL, 0, animation_data_offsets, 3, NULL
};
static struct block_layout const animation_graph_blocks[] =
{
	{ 0x00, 0x14, &plain_element_layout },
	{ 0x0C, 0x64, &animation_unit_seat_layout },
	{ 0x18, 0x1C, &animation_weapon_layout },
	{ 0x24, 0x74, &animation_vehicle_layout },
	{ 0x30, 0x60, &animation_device_layout },
	{ 0x3C, 0x02, &plain_element_layout },
	{ 0x48, 0x1C, &animation_weapon_layout },
	{ 0x54, 0x14, &animation_sound_layout },
	{ 0x68, 0x40, &plain_element_layout },
	{ 0x74, 0xB4, &animation_record_layout },
};
static struct element_layout const animation_graph_layout =
{
	0x80, animation_graph_blocks, 10, NULL, 0, NULL
};

/* rasterizer_xbox_environment.c lays out every bitmap reference; its
   offsets and the 0x344 total match the PC senv definition in Reclaimer. */
static uint32_t const shader_environment_references[] =
{
	0x30, 0x88, 0xB8, 0xCC, 0xFC, 0x128, 0x254, 0x324
};
static struct element_layout const shader_environment_layout =
{
	0x344, NULL, 0, NULL, 0, NULL, shader_environment_references, 8
};

/* Reclaimer's Halo 1 input_device_defaults descriptor: two rawdata_ref
   fields after a 4-byte type/flags prefix. */
static uint32_t const input_device_data_offsets[] = { 0x04, 0x18 };
static struct element_layout const input_device_layout =
{
	0x2C, NULL, 0, input_device_data_offsets, 2, NULL
};

/* ARM offsetof evidence: port/n3ds/tests/object_layout.c. The object base
   (0x17C) is also the prefix of projectile, weapon, vehicle and other object
   tags. Only the projectile suffix is covered here; no other derived group
   is claimed until its own fields have been checked. */
static uint32_t const object_attachment_references[] = { 0x00 };
static struct element_layout const object_attachment_layout =
{
	0x48, NULL, 0, NULL, 0, NULL, object_attachment_references, 1
};
static uint32_t const object_widget_references[] = { 0x00 };
static struct element_layout const object_widget_layout =
{
	0x20, NULL, 0, NULL, 0, NULL, object_widget_references, 1
};
static struct block_layout const object_change_color_blocks[] =
{
	{ 0x20, 0x1C, &plain_element_layout },
};
static struct element_layout const object_change_color_layout =
{
	0x2C, object_change_color_blocks, 1, NULL, 0, NULL
};
static uint32_t const projectile_material_references[] = { 0x04, 0x3C, 0x68 };
static struct element_layout const projectile_material_layout =
{
	0xA0, NULL, 0, NULL, 0, NULL, projectile_material_references, 3
};
static struct block_layout const projectile_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
	{ 0x240, 0xA0, &projectile_material_layout },
};
static uint32_t const projectile_references[] =
{
	/* _object_definition */
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	/* _projectile_definition at 0x17C */
	0x18C, 0x1AC, 0x1F4, 0x204, 0x214, 0x224,
};
static struct element_layout const projectile_layout =
{
	0x24C, projectile_blocks, 6, NULL, 0, NULL,
	projectile_references, 12
};

/* port/n3ds/tests/weapon_layout.c records the ARM offsets, including the
   TU-local firing effect in weapons.c (three effect/damage pairs). */
static uint32_t const weapon_ammunition_references[] = { 0x0C };
static struct element_layout const weapon_ammunition_layout =
{
	0x1C, NULL, 0, NULL, 0, NULL, weapon_ammunition_references, 1
};
static struct block_layout const weapon_magazine_blocks[] =
{
	{ 0x64, 0x1C, &weapon_ammunition_layout },
};
static uint32_t const weapon_magazine_references[] = { 0x38, 0x48 };
static struct element_layout const weapon_magazine_layout =
{
	0x70, weapon_magazine_blocks, 1, NULL, 0, NULL,
	weapon_magazine_references, 2
};
static uint32_t const weapon_firing_references[] =
{
	0x24, 0x34, 0x44, 0x54, 0x64, 0x74
};
static struct element_layout const weapon_firing_layout =
{
	0x84, NULL, 0, NULL, 0, NULL, weapon_firing_references, 6
};
static struct block_layout const weapon_trigger_blocks[] =
{
	{ 0x108, 0x84, &weapon_firing_layout },
};
static uint32_t const weapon_trigger_references[] = { 0x5C, 0x94 };
static struct element_layout const weapon_trigger_layout =
{
	0x114, weapon_trigger_blocks, 1, NULL, 0, NULL,
	weapon_trigger_references, 2
};
static struct block_layout const weapon_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
	{ 0x4C0, 0x01, &plain_element_layout },
	{ 0x4E4, 0x08, &plain_element_layout },
	{ 0x4F0, 0x70, &weapon_magazine_layout },
	{ 0x4FC, 0x114, &weapon_trigger_layout },
};
static uint32_t const weapon_references[] =
{
	/* object */ 0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	/* item at 0x17C */ 0x248, 0x258, 0x2E8, 0x2F8,
	/* weapon at 0x308 */
	0x33C, 0x374, 0x384, 0x394, 0x3A4, 0x3BC, 0x420, 0x430,
	/* first person interface and sounds */
	0x45C, 0x46C, 0x480, 0x490, 0x4A0, 0x4B0,
};
static struct element_layout const weapon_layout =
{
	0x508, weapon_blocks, 9, NULL, 0, NULL,
	weapon_references, 24
};

/* unit_definitions.h plus the vehicle_definition in vehicles.c; the ARM
   offsets of their common prefix are recorded by tests/unit_layout.c. */
static uint32_t const unit_camera_track_references[] = { 0x00 };
static struct element_layout const unit_camera_track_layout =
{
	0x1C, NULL, 0, NULL, 0, NULL, unit_camera_track_references, 1
};
static uint32_t const unit_hud_references[] = { 0x00 };
static struct element_layout const unit_hud_layout =
{
	0x30, NULL, 0, NULL, 0, NULL, unit_hud_references, 1
};
static uint32_t const unit_initial_weapon_references[] = { 0x00 };
static struct element_layout const unit_initial_weapon_layout =
{
	0x24, NULL, 0, NULL, 0, NULL, unit_initial_weapon_references, 1
};
static struct block_layout const unit_seat_blocks[] =
{
	{ 0xD0, 0x1C, &unit_camera_track_layout },
	{ 0xDC, 0x30, &unit_hud_layout },
};
static uint32_t const unit_seat_references[] = { 0xF8 };
static struct element_layout const unit_seat_layout =
{
	0x11C, unit_seat_blocks, 2, NULL, 0, NULL,
	unit_seat_references, 1
};
/* unit_definitions.h's 0x18 dialogue variant keeps a serialized udlg
   reference starting at +8 (its tag index is at +0x14). */
static uint32_t const unit_dialogue_variant_references[] = { 0x08 };
static struct element_layout const unit_dialogue_variant_layout =
{
	0x18, NULL, 0, NULL, 0, NULL,
	unit_dialogue_variant_references, 1
};
static struct block_layout const vehicle_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
	{ 0x1F4, 0x1C, &unit_camera_track_layout },
	{ 0x2A8, 0x30, &unit_hud_layout },
	{ 0x2B4, 0x18, &unit_dialogue_variant_layout },
	{ 0x2CC, 0x44, &plain_element_layout },
	{ 0x2D8, 0x24, &unit_initial_weapon_layout },
	{ 0x2E4, 0x11C, &unit_seat_layout },
};
static uint32_t const vehicle_references[] =
{
	/* object and unit prefixes */
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x188, 0x24C, 0x288,
	/* four vehicle-specific references */
	0x3B0, 0x3C0, 0x3D0, 0x3E0,
};
static struct element_layout const vehicle_layout =
{
	0x3F0, vehicle_blocks, 11, NULL, 0, NULL,
	vehicle_references, 13
};

/* The biped suffix follows the same 0x2F0-byte object/unit prefix. */
static struct block_layout const biped_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
	{ 0x1F4, 0x1C, &unit_camera_track_layout },
	{ 0x2A8, 0x30, &unit_hud_layout },
	{ 0x2B4, 0x18, &unit_dialogue_variant_layout },
	{ 0x2CC, 0x44, &plain_element_layout },
	{ 0x2D8, 0x24, &unit_initial_weapon_layout },
	{ 0x2E4, 0x11C, &unit_seat_layout },
	{ 0x4E8, 0x40, &plain_element_layout },
};
static uint32_t const biped_references[] =
{
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x188, 0x24C, 0x288,
	0x314, 0x38C,
};
static struct element_layout const biped_layout =
{
	0x4F4, biped_blocks, 12, NULL, 0, NULL,
	biped_references, 11
};

/* The shared scenery tag is the object definition alone. The equipment
   suffix and item collection offsets come from object_derived_layout.c,
   compiled with the same ARM engine headers as the game units. */
static struct block_layout const scenery_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
};
static uint32_t const scenery_references[] =
{
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
};
static struct element_layout const scenery_layout =
{
	0x17C, scenery_blocks, 5, NULL, 0, NULL,
	scenery_references, 6
};
/* Halo 1's sound_scenery inherits BasicObject: the 0x17C object prefix
   followed by 0x80 bytes of flags and padding, with no further pointers. */
static struct element_layout const sound_scenery_layout =
{
	0x1FC, scenery_blocks, 5, NULL, 0, NULL,
	scenery_references, 6
};

/* Devices share the object prefix. device_layout.c checks the seven device
   effects and control suffix on ARM; Invader's Halo 1 light-fixture schema
   confirms its remaining 0x40-byte suffix contains no pointers. */
static uint32_t const device_references[] =
{
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x1A0, 0x1B0, 0x1C0, 0x1D0, 0x1E0, 0x1F0, 0x20C
};
static struct element_layout const device_layout =
{
	0x290, scenery_blocks, 5, NULL, 0, NULL,
	device_references, 13
};
static struct element_layout const machine_layout =
{
	0x324, scenery_blocks, 5, NULL, 0, NULL,
	device_references, 13
};
static struct element_layout const light_fixture_layout =
{
	0x2D0, scenery_blocks, 5, NULL, 0, NULL,
	device_references, 13
};
static uint32_t const control_references[] =
{
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x1A0, 0x1B0, 0x1C0, 0x1D0, 0x1E0, 0x1F0, 0x20C,
	0x2E8, 0x2F8, 0x308
};
static struct element_layout const control_layout =
{
	0x324, scenery_blocks, 5, NULL, 0, NULL,
	control_references, 16
};
static struct block_layout const equipment_blocks[] =
{
	{ 0x140, 0x48, &object_attachment_layout },
	{ 0x14C, 0x20, &object_widget_layout },
	{ 0x158, 0x168, &plain_element_layout },
	{ 0x164, 0x2C, &object_change_color_layout },
	{ 0x170, 0x08, &plain_element_layout },
};
static uint32_t const equipment_references[] =
{
	/* object, item, and equipment pickup sound */
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x248, 0x258, 0x2E8, 0x2F8, 0x310,
};
static struct element_layout const equipment_layout =
{
	0x320, equipment_blocks, 5, NULL, 0, NULL,
	equipment_references, 11
};
/* Garbage inherits Item: the suffix is cache padding with no new pointers. */
static uint32_t const garbage_references[] =
{
	0x28, 0x38, 0x70, 0x80, 0x90, 0xA0,
	0x248, 0x258, 0x2E8, 0x2F8
};
static struct element_layout const garbage_layout =
{
	0x3B0, scenery_blocks, 5, NULL, 0, NULL,
	garbage_references, 10
};
static uint32_t const item_permutation_references[] = { 0x24 };
static struct element_layout const item_permutation_layout =
{
	0x54, NULL, 0, NULL, 0, NULL,
	item_permutation_references, 1
};
static struct block_layout const item_collection_blocks[] =
{
	{ 0x00, 0x54, &item_permutation_layout },
};
static struct element_layout const item_collection_layout =
{
	0x5C, item_collection_blocks, 1, NULL, 0, NULL
};

/* actor_definitions.h fixes the five dependencies and final change-colors
   block; actors.c asserts that each color entry is 0x20 bytes without
   nested pointers. ARM offsets are emitted by tests/actor_variant_layout.c. */
static struct block_layout const actor_variant_blocks[] =
{
	{ 0x22C, 0x20, &plain_element_layout },
};
static uint32_t const actor_variant_references[] =
{
	0x04, 0x14, 0x24, 0x64, 0x1C0
};
static struct element_layout const actor_variant_layout =
{
	0x238, actor_variant_blocks, 1, NULL, 0, NULL,
	actor_variant_references, 5
};

/* Source actor_definition has three unused but still serialized tag
   references. tests/actor_layout.c confirms these ARM offsets. */
static uint32_t const actor_references[] = { 0x13C, 0x258, 0x4B8 };
static struct element_layout const actor_layout =
{
	0x4F8, NULL, 0, NULL, 0, NULL, actor_references, 3
};

/* The following PC retail groups have direct source definitions. Fog and
   glow hold one dependency; detail collections, virtual keyboards, and the
   multiplayer description hold checked nested blocks. */
static uint32_t const fog_references[] = { 0xBC, 0xF4, 0x104 };
static struct element_layout const fog_layout =
{
	0x114, NULL, 0, NULL, 0, NULL, fog_references, 3
};
static uint32_t const glow_references[] = { 0x144 };
static struct element_layout const glow_layout =
{
	0x154, NULL, 0, NULL, 0, NULL, glow_references, 1
};
static struct block_layout const detail_object_collection_blocks[] =
{
	{ 0x44, 0x60, &plain_element_layout },
};
static uint32_t const detail_object_collection_references[] = { 0x34 };
static struct element_layout const detail_object_collection_layout =
{
	0x80, detail_object_collection_blocks, 1, NULL, 0, NULL,
	detail_object_collection_references, 1
};
static uint32_t const virtual_keyboard_key_references[] =
{
	0x10, 0x20, 0x30, 0x40
};
static struct element_layout const virtual_keyboard_key_layout =
{
	0x50, NULL, 0, NULL, 0, NULL,
	virtual_keyboard_key_references, 4
};
static struct block_layout const virtual_keyboard_blocks[] =
{
	{ 0x30, 0x50, &virtual_keyboard_key_layout },
};
static uint32_t const virtual_keyboard_references[] =
{
	0x00, 0x10, 0x20
};
static struct element_layout const virtual_keyboard_layout =
{
	0x3C, virtual_keyboard_blocks, 1, NULL, 0, NULL,
	virtual_keyboard_references, 3
};
static uint32_t const multiplayer_description_item_references[] =
{
	0x00, 0x10
};
static struct element_layout const multiplayer_description_item_layout =
{
	0x44, NULL, 0, NULL, 0, NULL,
	multiplayer_description_item_references, 2
};
static struct block_layout const multiplayer_description_blocks[] =
{
	{ 0x00, 0x44, &multiplayer_description_item_layout },
};
static struct element_layout const multiplayer_description_layout =
{
	0x0C, multiplayer_description_blocks, 1, NULL, 0, NULL
};

/* Invader's Halo 1 lightning, transparent-water, and weather-particle
   schemas provide these nested records. Bounded float/Angle fields in the
   weather type are 8-byte pairs; its cache size is 0x25C. */
static uint32_t const lightning_shader_references[] = { 0x4C };
static struct element_layout const lightning_shader_layout =
{
	0xB4, NULL, 0, NULL, 0, NULL,
	lightning_shader_references, 1
};
static struct block_layout const lightning_blocks[] =
{
	{ 0x98, 0xE4, &plain_element_layout },
	{ 0xA4, 0xB4, &lightning_shader_layout },
};
static uint32_t const lightning_references[] = { 0x34 };
static struct element_layout const lightning_layout =
{
	0x108, lightning_blocks, 2, NULL, 0, NULL,
	lightning_references, 1
};
static struct block_layout const transparent_water_blocks[] =
{
	{ 0x124, 0x4C, &plain_element_layout },
};
static uint32_t const transparent_water_references[] =
{
	0x4C, 0x9C, 0xC8
};
static struct element_layout const transparent_water_layout =
{
	0x140, transparent_water_blocks, 1, NULL, 0, NULL,
	transparent_water_references, 3
};
static uint32_t const weather_particle_type_references[] =
{
	0xAC, 0x194, 0x1F4
};
static struct element_layout const weather_particle_type_layout =
{
	0x25C, NULL, 0, NULL, 0, NULL,
	weather_particle_type_references, 3
};
static struct block_layout const weather_particle_blocks[] =
{
	{ 0x24, 0x25C, &weather_particle_type_layout },
};
static struct element_layout const weather_particle_layout =
{
	0x30, weather_particle_blocks, 1, NULL, 0, NULL
};

/* unit_dialogue.c defines a 0x10-byte prefix and 209 active plus 47 unused
   tag_reference records. The inactive records still hold cache pointers. */
static struct element_layout const dialogue_layout =
{
	0x1010, NULL, 0, NULL, 0, NULL,
	NULL, 0, 0x10, 256
};

/* game_globals.h fixes the root and most nested sizes/offsets; Invader's
   globals.json supplies cache-only falling damage and material layouts.
   tests/globals_layout.c emits the source-defined ARM offsets. */
static uint32_t const globals_ref_references[] = { 0x00 };
static struct element_layout const globals_ref_layout =
{
	0x10, NULL, 0, NULL, 0, NULL, globals_ref_references, 1
};
static struct block_layout const globals_player_control_blocks[] =
{
	{ 0x74, 0x04, &plain_element_layout },
};
static struct element_layout const globals_player_control_layout =
{
	0x80, globals_player_control_blocks, 1, NULL, 0, NULL
};
static uint32_t const globals_grenade_references[] =
{
	0x04, 0x14, 0x24, 0x34
};
static struct element_layout const globals_grenade_layout =
{
	0x44, NULL, 0, NULL, 0, NULL, globals_grenade_references, 4
};
static uint32_t const globals_rasterizer_references[] =
{
	0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
	0xAC, 0xBC, 0xCC, 0xDC, 0xEC, 0xFC, 0x10C, 0x11C, 0x12C,
	0x19C
};
static struct element_layout const globals_rasterizer_layout =
{
	0x1AC, NULL, 0, NULL, 0, NULL, globals_rasterizer_references, 17
};
static struct element_layout const globals_interface_layout =
{
	0x130, NULL, 0, NULL, 0, NULL, NULL, 0, 0x00, 16
};
static struct block_layout const globals_multiplayer_blocks[] =
{
	{ 0x20, 0x10, &globals_ref_layout }, /* vehicles */
	{ 0x5C, 0x10, &globals_ref_layout }, /* sounds */
};
static uint32_t const globals_multiplayer_references[] =
{
	0x00, 0x10, 0x2C, 0x3C, 0x4C
};
static struct element_layout const globals_multiplayer_layout =
{
	0xA0, globals_multiplayer_blocks, 2, NULL, 0, NULL,
	globals_multiplayer_references, 5
};
static uint32_t const globals_player_references[] = { 0x00, 0xB8 };
static struct element_layout const globals_player_layout =
{
	0xF4, NULL, 0, NULL, 0, NULL, globals_player_references, 2
};
static uint32_t const globals_first_person_references[] =
{
	0x00, 0x10, 0x20, 0x34, 0x48, 0x58
};
static struct element_layout const globals_first_person_layout =
{
	0xC0, NULL, 0, NULL, 0, NULL, globals_first_person_references, 6
};
static uint32_t const globals_falling_damage_references[] =
{
	0x10, 0x2C, 0x3C, 0x4C, 0x5C, 0x6C
};
static struct element_layout const globals_falling_damage_layout =
{
	0x98, NULL, 0, NULL, 0, NULL, globals_falling_damage_references, 6
};
static struct element_layout const globals_breakable_particle_layout =
{
	0x80, NULL, 0, NULL, 0, NULL, globals_ref_references, 1
};
static struct block_layout const globals_material_blocks[] =
{
	{ 0x31C, 0x80, &globals_breakable_particle_layout },
};
static uint32_t const globals_material_references[] =
{
	0x2E4, 0x2F4, 0x364
};
static struct element_layout const globals_material_layout =
{
	0x374, globals_material_blocks, 1, NULL, 0, NULL,
	globals_material_references, 3
};
static struct block_layout const globals_blocks[] =
{
	{ 0x0F8, 0x010, &globals_ref_layout },             /* sounds */
	{ 0x104, 0x010, &globals_ref_layout },             /* camera */
	{ 0x110, 0x080, &globals_player_control_layout }, /* player control */
	{ 0x11C, 0x284, &plain_element_layout },           /* difficulty */
	{ 0x128, 0x044, &globals_grenade_layout },        /* grenades */
	{ 0x134, 0x1AC, &globals_rasterizer_layout },     /* rasterizer */
	{ 0x140, 0x130, &globals_interface_layout },      /* interface bitmaps */
	{ 0x14C, 0x010, &globals_ref_layout },             /* weapons */
	{ 0x158, 0x010, &globals_ref_layout },             /* cheat powerups */
	{ 0x164, 0x0A0, &globals_multiplayer_layout },    /* multiplayer */
	{ 0x170, 0x0F4, &globals_player_layout },         /* player */
	{ 0x17C, 0x0C0, &globals_first_person_layout },   /* first person */
	{ 0x188, 0x098, &globals_falling_damage_layout }, /* falling damage */
	{ 0x194, 0x374, &globals_material_layout },       /* materials */
	{ 0x1A0, 0x094, &plain_element_layout },           /* playlist */
};
static struct element_layout const globals_layout =
{
	0x1AC, globals_blocks, 15, NULL, 0, NULL
};

/* Invader's Halo 1 tag_collection schema (src/tag/hek/definition/
   tag_collection.json): a 12-byte root with a block of 16-byte tag
   dependencies. Their name fields are cache pointers. */
static uint32_t const tag_collection_entry_references[] = { 0x00 };
static struct element_layout const tag_collection_entry_layout =
{
	0x10, NULL, 0, NULL, 0, NULL,
	tag_collection_entry_references, 1
};
static struct block_layout const tag_collection_blocks[] =
{
	{ 0x00, 0x10, &tag_collection_entry_layout },
};
static struct element_layout const tag_collection_layout =
{
	0x0C, tag_collection_blocks, 1, NULL, 0, NULL
};

/* Invader's legacy Halo 1 meter definition has two bitmap dependencies
   and a cache-only 20-byte encoded stencil data field at 0x98. */
static uint32_t const meter_references[] = { 0x04, 0x14 };
static uint32_t const meter_data[] = { 0x98 };
static struct element_layout const meter_layout =
{
	0xAC, NULL, 0, meter_data, 1, NULL,
	meter_references, 2
};

/* Halo 1 sky: source/scenario/sky_definitions.h fixes the fog and screen
   offsets; Invader's sky.json supplies the three reflexive block layouts. */
static uint32_t const sky_light_references[] = { 0x00 };
static struct element_layout const sky_light_layout =
{
	0x74, NULL, 0, NULL, 0, NULL,
	sky_light_references, 1
};
static struct block_layout const sky_blocks[] =
{
	{ 0xAC, 0x24, &plain_element_layout },
	{ 0xB8, 0x24, &plain_element_layout },
	{ 0xC4, 0x74, &sky_light_layout },
};
static uint32_t const sky_references[] = { 0x00, 0x10, 0x98 };
static struct element_layout const sky_layout =
{
	0xD0, sky_blocks, 3, NULL, 0, NULL,
	sky_references, 3
};

/* The 0x5B0 scenario header and these element sizes were emitted by the
   ARM build of source/scenario/scenario_definitions.h (scenario_layout.c).
   Its BSP reference block needs separate handling: each entry also holds
   an unloaded BSP's reserved destination address, outside the used tags. */
static uint32_t const scenario_tag_reference_offsets[] = { 0x00 };
static struct element_layout const scenario_tag_reference_layout =
{
	0x10, NULL, 0, NULL, 0, NULL,
	scenario_tag_reference_offsets, 1
};
/* The compiled retail cache uses 0x20-byte scenario-reference records,
   with the dependency first; the editor tag's 0x28-byte form is different. */
static uint32_t const scenario_reference_offsets[] = { 0x00 };
static struct element_layout const scenario_reference_layout =
{
	0x20, NULL, 0, NULL, 0, NULL,
	scenario_reference_offsets, 1
};
static struct element_layout const scenario_palette_layout =
{
	0x30, NULL, 0, NULL, 0, NULL,
	scenario_tag_reference_offsets, 1
};
static uint32_t const scenario_starting_profile_references[] = { 0x28, 0x3C };
static struct element_layout const scenario_starting_profile_layout =
{
	0x68, NULL, 0, NULL, 0, NULL,
	scenario_starting_profile_references, 2
};
static uint32_t const scenario_netgame_equipment_references[] = { 0x50 };
static struct element_layout const scenario_netgame_equipment_layout =
{
	0x90, NULL, 0, NULL, 0, NULL,
	scenario_netgame_equipment_references, 1
};
static uint32_t const scenario_starting_equipment_references[] =
{
	0x3C, 0x4C, 0x5C, 0x6C, 0x7C, 0x8C
};
static struct element_layout const scenario_starting_equipment_layout =
{
	0xCC, NULL, 0, NULL, 0, NULL,
	scenario_starting_equipment_references, 6
};
static uint32_t const scenario_hs_reference_offsets[] = { 0x18 };
static struct element_layout const scenario_hs_reference_layout =
{
	0x28, NULL, 0, NULL, 0, NULL,
	scenario_hs_reference_offsets, 1
};
static uint32_t const scenario_hs_source_data_offsets[] = { 0x20 };
static struct element_layout const scenario_hs_source_layout =
{
	0x34, NULL, 0, scenario_hs_source_data_offsets, 1, NULL
};
static uint32_t const scenario_ai_animation_references[] = { 0x20 };
static struct element_layout const scenario_ai_animation_layout =
{
	0x3C, NULL, 0, NULL, 0, NULL,
	scenario_ai_animation_references, 1
};
/* ARM layout values are emitted by port/n3ds/tests/scenario_layout.c. PC
   campaign scripts resolve encounter and squad names through these blocks. */
static struct block_layout const scenario_ai_squad_blocks[] =
{
	{ 0xC4, 0x50, &plain_element_layout }, /* move positions */
	{ 0xD0, 0x1C, &plain_element_layout }, /* actor starts */
};
static struct element_layout const scenario_ai_squad_layout =
{
	0xE8, scenario_ai_squad_blocks, 2, NULL, 0, NULL
};
static struct block_layout const scenario_ai_encounter_blocks[] =
{
	{ 0x80, 0xE8, &scenario_ai_squad_layout },
	{ 0x8C, 0xAC, &plain_element_layout }, /* platoons */
	{ 0x98, 0x18, &plain_element_layout }, /* firing positions */
	{ 0xA4, 0x34, &plain_element_layout }, /* player starts */
};
static struct element_layout const scenario_ai_encounter_layout =
{
	0xB0, scenario_ai_encounter_blocks, 4, NULL, 0, NULL
};
static struct block_layout const scenario_ai_command_list_blocks[] =
{
	{ 0x30, 0x20, &plain_element_layout },
	{ 0x3C, 0x14, &plain_element_layout },
};
static struct element_layout const scenario_ai_command_list_layout =
{
	0x60, scenario_ai_command_list_blocks, 2, NULL, 0, NULL
};
/* source/ai/ai_scenario_definitions.h fixes the conversation header at 0x74
   with participants at 0x50 and lines at 0x5C. Invader's scenario schema
   defines each 0x7C line's six sound dependencies after its 0x1C prefix. */
static uint32_t const scenario_conversation_line_references[] =
{
	0x1C, 0x2C, 0x3C, 0x4C, 0x5C, 0x6C
};
static struct element_layout const scenario_conversation_line_layout =
{
	0x7C, NULL, 0, NULL, 0, NULL,
	scenario_conversation_line_references, 6
};
static struct block_layout const scenario_conversation_blocks[] =
{
	{ 0x50, 0x54, &plain_element_layout },
	{ 0x5C, 0x7C, &scenario_conversation_line_layout },
};
static struct element_layout const scenario_conversation_layout =
{
	0x74, scenario_conversation_blocks, 2, NULL, 0, NULL
};
static struct block_layout const scenario_blocks[] =
{
	{ 0x030, 0x10, &scenario_tag_reference_layout }, /* sky */
	{ 0x040, 0x20, &scenario_reference_layout }, /* scenario refs */
	{ 0x0EC, 0x08, &plain_element_layout }, /* predicted UI resources */
	{ 0x204, 0x24, &plain_element_layout }, /* object names */
	{ 0x210, 0x28, &plain_element_layout }, /* scenery placements */
	{ 0x21C, 0x30, &scenario_palette_layout }, /* scenery */
	{ 0x228, 0x78, &plain_element_layout }, /* biped placements */
	{ 0x234, 0x30, &scenario_palette_layout }, /* bipeds */
	{ 0x240, 0x78, &plain_element_layout }, /* vehicle placements */
	{ 0x24C, 0x30, &scenario_palette_layout }, /* vehicles */
	{ 0x258, 0x28, &plain_element_layout }, /* equipment placements */
	{ 0x264, 0x30, &scenario_palette_layout }, /* equipment */
	{ 0x270, 0x5C, &plain_element_layout }, /* weapon placements */
	{ 0x27C, 0x30, &scenario_palette_layout }, /* weapons */
	{ 0x288, 0x34, &plain_element_layout }, /* device groups */
	{ 0x294, 0x40, &plain_element_layout }, /* machine placements */
	{ 0x2A0, 0x30, &scenario_palette_layout }, /* machines */
	{ 0x2AC, 0x40, &plain_element_layout }, /* control placements */
	{ 0x2B8, 0x30, &scenario_palette_layout }, /* controls */
	{ 0x2C4, 0x58, &plain_element_layout }, /* light fixture placements */
	{ 0x2D0, 0x30, &scenario_palette_layout }, /* light fixtures */
	{ 0x2DC, 0x28, &plain_element_layout }, /* sound scenery placements */
	{ 0x2E8, 0x30, &scenario_palette_layout }, /* sound scenery */
	{ 0x348, 0x68, &scenario_starting_profile_layout },
	{ 0x354, 0x34, &plain_element_layout }, /* player starts */
	{ 0x360, 0x60, &plain_element_layout }, /* trigger volumes */
	{ 0x36C, 0x40, &plain_element_layout }, /* recorded animations */
	{ 0x378, 0x94, &plain_element_layout }, /* netgame flags */
	{ 0x384, 0x90, &scenario_netgame_equipment_layout },
	{ 0x390, 0xCC, &scenario_starting_equipment_layout },
	{ 0x39C, 0x08, &plain_element_layout }, /* BSP switch trigger volumes */
	{ 0x3B4, 0x10, &scenario_tag_reference_layout }, /* decals */
	{ 0x3C0, 0x30, &scenario_palette_layout }, /* detail objects */
	{ 0x420, 0x10, &scenario_tag_reference_layout }, /* AI actors */
	{ 0x42C, 0xB0, &scenario_ai_encounter_layout },
	{ 0x438, 0x60, &scenario_ai_command_list_layout },
	{ 0x444, 0x3C, &scenario_ai_animation_layout }, /* AI animations */
	{ 0x450, 0x28, &plain_element_layout }, /* AI script names */
	{ 0x45C, 0x28, &plain_element_layout }, /* AI recording names */
	{ 0x468, 0x74, &scenario_conversation_layout }, /* AI conversations */
	{ 0x49C, 0x5C, &plain_element_layout }, /* scripts */
	{ 0x4A8, 0x5C, &plain_element_layout }, /* script globals */
	{ 0x4B4, 0x28, &scenario_hs_reference_layout },
	{ 0x4C0, 0x34, &scenario_hs_source_layout },
	{ 0x4E4, 0x5C, &plain_element_layout }, /* cutscene flags */
	{ 0x4F0, 0x68, &plain_element_layout }, /* cutscene cameras */
	{ 0x4FC, 0x60, &plain_element_layout }, /* cutscene titles */
};
static uint32_t const scenario_data_offsets[] = { 0x104, 0x474, 0x488 };
static uint32_t const scenario_references[] =
{
	0x000, 0x010, 0x020,
	0x574, 0x584, 0x594
};
static struct element_layout const scenario_layout =
{
	0x5B0, scenario_blocks, 47,
	scenario_data_offsets, 3, NULL,
	scenario_references, 6
};

/* Weapon HUD offsets are emitted from the shared ARM headers by
   tests/hud_layout.c. The private element structs and their size asserts
   are in source/interface/hud_weapon.c and hud_draw.c. */
static uint32_t const hud_multitexture_references[] = { 0x64, 0x74, 0x84 };
static struct block_layout const hud_multitexture_blocks[] =
{
	{ 0x154, 0xDC, &plain_element_layout },
};
static struct element_layout const hud_multitexture_layout =
{
	0x1E0, hud_multitexture_blocks, 1, NULL, 0, NULL,
	hud_multitexture_references, 3
};
static struct block_layout const weapon_hud_static_meter_blocks[] =
{
	{ 0x7C, 0x1E0, &hud_multitexture_layout },
};
static uint32_t const weapon_hud_static_meter_references[] = { 0x48 };
static struct element_layout const weapon_hud_static_meter_layout =
{
	0xB4, weapon_hud_static_meter_blocks, 1, NULL, 0, NULL,
	weapon_hud_static_meter_references, 1
};
static struct block_layout const weapon_hud_crosshair_blocks[] =
{
	{ 0x34, 0x6C, &plain_element_layout },
};
static struct block_layout const weapon_hud_overlay_blocks[] =
{
	{ 0x34, 0x88, &plain_element_layout },
};
static uint32_t const weapon_hud_crosshair_overlay_references[] = { 0x24 };
static struct element_layout const weapon_hud_crosshair_layout =
{
	0x68, weapon_hud_crosshair_blocks, 1, NULL, 0, NULL,
	weapon_hud_crosshair_overlay_references, 1
};
static struct element_layout const weapon_hud_overlay_layout =
{
	0x68, weapon_hud_overlay_blocks, 1, NULL, 0, NULL,
	weapon_hud_crosshair_overlay_references, 1
};
static struct element_layout const hud_warning_sound_layout =
{
	0x38, NULL, 0, NULL, 0, NULL,
	scenario_tag_reference_offsets, 1
};
static uint32_t const hud_screen_effect_references[] = { 0x18, 0x28 };
static struct element_layout const hud_screen_effect_layout =
{
	0xB8, NULL, 0, NULL, 0, NULL,
	hud_screen_effect_references, 2
};
static struct block_layout const weapon_hud_blocks[] =
{
	{ 0x060, 0xB4, &weapon_hud_static_meter_layout },
	{ 0x06C, 0xB4, &weapon_hud_static_meter_layout },
	{ 0x078, 0xA0, &plain_element_layout },
	{ 0x084, 0x68, &weapon_hud_crosshair_layout },
	{ 0x090, 0x68, &weapon_hud_overlay_layout },
	{ 0x0A0, 0x38, &hud_warning_sound_layout },
	{ 0x0AC, 0xB8, &hud_screen_effect_layout },
};
static uint32_t const weapon_hud_references[] = { 0x00 };
static struct element_layout const weapon_hud_layout =
{
	0x17C, weapon_hud_blocks, 7, NULL, 0, NULL,
	weapon_hud_references, 1
};
static struct block_layout const unit_hud_aux_overlay_blocks[] =
{
	{ 0x58, 0x1E0, &hud_multitexture_layout },
};
static uint32_t const unit_hud_aux_overlay_references[] = { 0x24 };
static struct element_layout const unit_hud_aux_overlay_layout =
{
	0x84, unit_hud_aux_overlay_blocks, 1, NULL, 0, NULL,
	unit_hud_aux_overlay_references, 1
};
static struct block_layout const unit_hud_aux_meter_blocks[] =
{
	{ 0x06C, 0x1E0, &hud_multitexture_layout },
	{ 0x0D4, 0x1E0, &hud_multitexture_layout },
};
static uint32_t const unit_hud_aux_meter_references[] = { 0x38, 0xA0 };
static struct element_layout const unit_hud_aux_meter_layout =
{
	0x144, unit_hud_aux_meter_blocks, 2, NULL, 0, NULL,
	unit_hud_aux_meter_references, 2
};
static struct block_layout const unit_hud_blocks[] =
{
	/* background; shield background/meter; health background/meter;
	   motion sensor background/foreground */
	{ 0x07C, 0x1E0, &hud_multitexture_layout },
	{ 0x0E4, 0x1E0, &hud_multitexture_layout },
	{ 0x14C, 0x1E0, &hud_multitexture_layout },
	{ 0x1D4, 0x1E0, &hud_multitexture_layout },
	{ 0x23C, 0x1E0, &hud_multitexture_layout },
	{ 0x2C4, 0x1E0, &hud_multitexture_layout },
	{ 0x32C, 0x1E0, &hud_multitexture_layout },
	{ 0x3A4, 0x84, &unit_hud_aux_overlay_layout },
	{ 0x3C0, 0x38, &hud_warning_sound_layout },
	{ 0x3CC, 0x144, &unit_hud_aux_meter_layout },
};
static uint32_t const unit_interface_hud_references[] =
{
	0x048, 0x0B0, 0x118, 0x1A0, 0x208, 0x290, 0x2F8
};
static struct element_layout const unit_interface_hud_layout =
{
	0x56C, unit_hud_blocks, 10, NULL, 0, NULL,
	unit_interface_hud_references, 7
};
/* grenade_hud_interface_definition is in hud_weapon.c; its 0x1F8 size
   and 0x15C overlay item offset are asserted/used by the shared code. */
static struct block_layout const grenade_hud_blocks[] =
{
	{ 0x07C, 0x1E0, &hud_multitexture_layout },
	{ 0x0E4, 0x1E0, &hud_multitexture_layout },
	{ 0x15C, 0x88, &plain_element_layout },
	{ 0x168, 0x38, &hud_warning_sound_layout },
};
static uint32_t const grenade_hud_references[] =
{
	/* PC retail caches leave the 0x1B8 messaging icon in a different
	   representation (its name word is not a tag-cache address). */
	0x048, 0x0B0, 0x14C
};
static struct element_layout const grenade_hud_layout =
{
	0x1F8, grenade_hud_blocks, 4, NULL, 0, NULL,
	grenade_hud_references, 3
};
static struct block_layout const hud_globals_blocks[] =
{
	{ 0x0C4, 0x10, &plain_element_layout }, /* button icons */
	{ 0x160, 0x68, &plain_element_layout }, /* waypoint arrows */
};
static uint32_t const hud_globals_references[] =
{
	0x048, 0x058, 0x094, 0x0A4, 0x0B4, 0x0F0,
	0x150, 0x2C0, 0x338, 0x3C8, 0x3E0
};
static struct element_layout const hud_globals_layout =
{
	0x450, hud_globals_blocks, 2, NULL, 0, NULL,
	hud_globals_references, 11
};

/* transparent_geometry.c: 0xDC map record, bitmap dependency at 0x6C. */
static uint32_t const transparent_chicago_map_references[] = { 0x6C };
static struct element_layout const transparent_chicago_map_layout =
{
	TRANSPARENT_CHICAGO_MAP_BYTES, NULL, 0, NULL, 0, NULL,
	transparent_chicago_map_references, 1
};
static uint32_t const transparent_chicago_references[] = { 0x38 };
static struct block_layout const transparent_chicago_blocks[] =
{
	/* extra layers (shader references), maps */
	{ TRANSPARENT_CHICAGO_EXTRA_LAYERS_OFFSET, TAG_REFERENCE_BYTES, &tag_collection_entry_layout },
	{ TRANSPARENT_CHICAGO_MAPS_OFFSET, TRANSPARENT_CHICAGO_MAP_BYTES, &transparent_chicago_map_layout },
};
static struct element_layout const transparent_chicago_layout =
{
	TRANSPARENT_CHICAGO_BYTES, transparent_chicago_blocks, 2, NULL, 0, NULL,
	transparent_chicago_references, 1
};
static struct block_layout const transparent_chicago_extended_blocks[] =
{
	/* extra layers, four-stage maps, two-stage maps */
	{ TRANSPARENT_CHICAGO_EXTRA_LAYERS_OFFSET, TAG_REFERENCE_BYTES, &tag_collection_entry_layout },
	{ TRANSPARENT_CHICAGO_MAPS_OFFSET, TRANSPARENT_CHICAGO_MAP_BYTES, &transparent_chicago_map_layout },
	{ TRANSPARENT_CHICAGO_EXTENDED_TWO_STAGE_MAPS_OFFSET, TRANSPARENT_CHICAGO_MAP_BYTES, &transparent_chicago_map_layout },
};
static struct element_layout const transparent_chicago_extended_layout =
{
	TRANSPARENT_CHICAGO_EXTENDED_BYTES, transparent_chicago_extended_blocks, 3, NULL, 0, NULL,
	transparent_chicago_references, 1
};

/* The value of the type field each shader group has in Custom Edition, which
inserted transparent chicago extended at 7 (OpenSauce
shader_definitions.hpp, e_shader_type), and in this build (shaders.c); a
chicago extended shader becomes a chicago shader. The Xbox maps of build 2276
have this build's values for the groups they hold. */
static struct shader_group_type const shader_group_types[] =
{
	{ 'senv', 3, 3 },
	{ 'soso', 4, 4 },
	{ 'sotr', 5, 5 },
	{ TRANSPARENT_CHICAGO_GROUP_TAG, 6, 6 },
	{ TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG, 7, 6 },
	{ 'swat', 8, 7 },
	{ 'sgla', 9, 8 },
	{ 'smet', 10, 9 },
	{ 'spla', 11, 10 },
};

static char const *const cache_file_status_descriptions[NUMBER_OF_CACHE_FILE_STATUSES] =
{
	"ok",
	"the file could not be read",
	"out of memory",
	"the file is too small to hold a header",
	"the file is larger than any map can be",
	"the header or footer signature is wrong",
	"the cache version is not one this build knows",
	"a name or build string in the header is not terminated",
	"the file length in the header does not fit the file or the size limit",
	"the cache is compressed, which Custom Edition caches never are",
	"the tag data range in the header does not fit the file or the tag cache",
	"the OpenSauce header is not valid (version, tag versions or memory upgrade)",
	"the OpenSauce header sets flags OpenSauce does not define",
	"the OpenSauce tag definitions lie outside the file",
	"the tag index signature is not 'tags'",
	"the tag instances do not fit in the tag data",
	"a tag handle does not match its position in the index",
	"a tag name lies outside the tag data or is not terminated",
	"a tag's address lies outside the tag data",
	"the scenario tag is missing, misplaced or not a scenario",
	"the model vertex and index data do not fit in the file",
	"a tag of a group that resource maps never hold is marked as held by one",
	"a tag block or tag data field lies outside the loaded tags",
	"the scenario's structure BSP block is not valid",
	"a structure BSP does not fit in the file or in the tag cache",
	"a structure BSP header is not valid",
	"a structure BSP's lightmaps or their materials do not have the documented layout",
	"a model part's strip or vertices lie outside the model data or are not of the kind Custom Edition writes",
	"a shader's type is not the one Custom Edition gives its group",
	"the scenario's scripts use more syntax nodes than this build has room for, or are not a syntax node array",
	"a resource map header is not valid",
	"a resource map is not of the type needed",
	"a resource map entry lies outside the file or its name is not terminated",
	"a map needs a resource map that was not supplied",
	"a tag's entry is missing from its resource map",
	"a tag held by a resource map does not have the documented layout",
	"bitmap pixels or sound samples lie outside their file",
	"the tag data and the tags held by resource maps do not fit below the structure BSP",
};

static char const *const cache_file_format_descriptions[NUMBER_OF_CACHE_FILE_FORMATS] =
{
	"not a Halo map file",
	"Xbox cache",
	"cache of an unknown version",
	"Custom Edition cache",
	"Halo PC retail cache",
	"Custom Edition resource map",
};

static char const *const resource_map_type_descriptions[NUMBER_OF_RESOURCE_MAP_TYPES] =
{
	"none",
	"bitmaps",
	"sounds",
	"loc",
};

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

/* ---------- private code */

static uint16_t read_u16(
	uint8_t const *bytes)
{
	return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static uint32_t read_u32(
	uint8_t const *bytes)
{
	return (uint32_t)bytes[0] |
		((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) |
		((uint32_t)bytes[3] << 24);
}

static int32_t read_s32(
	uint8_t const *bytes)
{
	uint32_t value = read_u32(bytes);

	/* two's complement without relying on an out-of-range conversion */
	return value <= INT32_MAX ? (int32_t)value : -(int32_t)(~value) - 1;
}

static int16_t read_s16(
	uint8_t const *bytes)
{
	uint16_t value = read_u16(bytes);

	return value <= INT16_MAX ? (int16_t)value : (int16_t)(-(int32_t)(uint16_t)~value - 1);
}

static float read_f32(
	uint8_t const *bytes)
{
	uint32_t bits = read_u32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));

	return value;
}

static void write_u16(
	uint8_t *bytes,
	uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);

	return;
}

static void write_u32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);

	return;
}

static void write_f32(
	uint8_t *bytes,
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	write_u32(bytes, bits);

	return;
}

static int flag_is_set(
	uint32_t flags,
	int bit)
{
	return (flags >> bit) & 1;
}

/* nonzero when [offset, offset + size) lies within [0, limit) */
static int range_fits(
	uint32_t offset,
	uint32_t size,
	uint32_t limit)
{
	return offset <= limit && size <= limit - offset;
}

/* copies a fixed 32-byte string field, returning nonzero when it holds a
terminator (a name of at most 31 characters) */
static int copy_string_field(
	char *destination,
	uint8_t const *source)
{
	int terminated = memchr(source, 0, CACHE_FILE_STRING_BYTES) != NULL;

	memcpy(destination, source, CACHE_FILE_STRING_BYTES);
	destination[CACHE_FILE_STRING_BYTES - 1] = 0;

	return terminated;
}

static void crc32_table_initialize(
	void)
{
	uint32_t byte_value;

	for (byte_value = 0; byte_value < 256; byte_value++)
	{
		uint32_t crc = byte_value;
		int bit;

		for (bit = 0; bit < 8; bit++)
		{
			crc = (crc & 1) ? (crc >> 1) ^ CRC32_POLYNOMIAL : crc >> 1;
		}
		crc32_table[byte_value] = crc;
	}
	crc32_table_initialized = 1;

	return;
}

static uint32_t crc32_update(
	uint32_t crc,
	uint8_t const *bytes,
	uint32_t size)
{
	uint32_t index;

	if (!crc32_table_initialized)
	{
		crc32_table_initialize();
	}
	for (index = 0; index < size; index++)
	{
		crc = (crc >> 8) ^ crc32_table[(crc ^ bytes[index]) & 0xFF];
	}

	return crc;
}

static enum cache_file_status crc32_update_from_file(
	struct cache_file_source *source,
	uint32_t *crc,
	uint32_t offset,
	uint32_t size)
{
	uint8_t *buffer;
	enum cache_file_status status = _cache_file_status_ok;

	if (!range_fits(offset, size, source->size))
	{
		return _cache_file_status_read_failed;
	}
	buffer = malloc(CHECKSUM_READ_CHUNK_BYTES);
	if (!buffer)
	{
		return _cache_file_status_out_of_memory;
	}
	while (size > 0)
	{
		uint32_t chunk_bytes = size < CHECKSUM_READ_CHUNK_BYTES ? size : CHECKSUM_READ_CHUNK_BYTES;

		if (!source->read(source->context, offset, chunk_bytes, buffer))
		{
			status = _cache_file_status_read_failed;
			break;
		}
		*crc = crc32_update(*crc, buffer, chunk_bytes);
		offset += chunk_bytes;
		size -= chunk_bytes;
	}
	free(buffer);

	return status;
}

static void opensauce_header_read(
	uint8_t const *bytes,
	struct opensauce_cache_header *header)
{
	int resource_index;

	header->version = read_s16(bytes + OPENSAUCE_VERSION_OFFSET);
	header->flags = read_u16(bytes + OPENSAUCE_FLAGS_OFFSET);
	header->project_yellow_version = bytes[OPENSAUCE_PROJECT_YELLOW_VERSION_OFFSET];
	header->project_yellow_globals_version = bytes[OPENSAUCE_PROJECT_YELLOW_GLOBALS_VERSION_OFFSET];
	header->memory_upgrade_amount = read_f32(bytes + OPENSAUCE_MEMORY_UPGRADE_AMOUNT_OFFSET);
	header->definitions_size = read_u32(bytes + OPENSAUCE_DEFINITIONS_SIZE_OFFSET);
	header->definitions_decompressed_size = read_u32(bytes + OPENSAUCE_DEFINITIONS_DECOMPRESSED_SIZE_OFFSET);
	header->definitions_offset = read_u32(bytes + OPENSAUCE_DEFINITIONS_OFFSET_OFFSET);
	header->build_stage = read_s16(bytes + OPENSAUCE_BUILD_STAGE_OFFSET);
	header->build_revision = read_u32(bytes + OPENSAUCE_BUILD_REVISION_OFFSET);
	header->build_timestamp = (int64_t)read_s32(bytes + OPENSAUCE_BUILD_TIMESTAMP_OFFSET + 4) * 0x100000000LL +
		(int64_t)read_u32(bytes + OPENSAUCE_BUILD_TIMESTAMP_OFFSET);
	header->tools_version_major = bytes[OPENSAUCE_TOOLS_VERSION_OFFSET];
	header->tools_version_minor = bytes[OPENSAUCE_TOOLS_VERSION_OFFSET + 1];
	header->tools_version_build = read_u16(bytes + OPENSAUCE_TOOLS_VERSION_OFFSET + 2);
	header->minimum_version_major = bytes[OPENSAUCE_MINIMUM_VERSION_OFFSET];
	header->minimum_version_minor = bytes[OPENSAUCE_MINIMUM_VERSION_OFFSET + 1];
	header->minimum_version_build = read_u16(bytes + OPENSAUCE_MINIMUM_VERSION_OFFSET + 2);
	for (resource_index = 0; resource_index < 4; resource_index++)
	{
		header->resource_offsets[resource_index] =
			read_u32(bytes + OPENSAUCE_RESOURCE_OFFSETS_OFFSET + resource_index * 4);
	}

	return;
}

/* the checks OpenSauce's s_cache_header_yelo::IsValid makes, except the
minimum OpenSauce version, which only concerns OpenSauce itself */
static enum cache_file_status opensauce_header_verify(
	uint8_t const *bytes,
	struct cache_file_identity *identity)
{
	struct opensauce_cache_header *header = &identity->opensauce;
	float amount;

	if (!copy_string_field(header->definitions_build, bytes + OPENSAUCE_DEFINITIONS_BUILD_OFFSET) ||
		!copy_string_field(header->mod_name, bytes + OPENSAUCE_MOD_NAME_OFFSET) ||
		!copy_string_field(header->build_string, bytes + OPENSAUCE_BUILD_STRING_OFFSET))
	{
		return _cache_file_status_unterminated_string;
	}
	opensauce_header_read(bytes, header);
	amount = header->memory_upgrade_amount;
	if ((header->version != OPENSAUCE_HEADER_VERSION &&
		header->version != OPENSAUCE_HEADER_VERSION_WITH_MINIMUM_BUILD) ||
		header->project_yellow_version != OPENSAUCE_PROJECT_YELLOW_VERSION ||
		header->project_yellow_globals_version != OPENSAUCE_PROJECT_YELLOW_GLOBALS_VERSION ||
		!(amount >= 0.0f && amount <= OPENSAUCE_MEMORY_UPGRADE_AMOUNT))
	{
		return _cache_file_status_bad_opensauce_header;
	}
	if (header->flags >> NUMBER_OF_OPENSAUCE_CACHE_FLAGS)
	{
		return _cache_file_status_unknown_opensauce_flags;
	}
	if (flag_is_set(header->flags, _opensauce_cache_uses_mod_data_files_bit) && !header->mod_name[0])
	{
		return _cache_file_status_bad_opensauce_header;
	}
	if (header->definitions_size &&
		(header->definitions_offset < identity->file_length ||
		!range_fits(header->definitions_offset, header->definitions_size, identity->file_size) ||
		!header->definitions_decompressed_size))
	{
		return _cache_file_status_bad_opensauce_definitions_range;
	}

	return _cache_file_status_ok;
}

static enum cache_file_status custom_edition_header_verify(
	uint8_t const *bytes,
	struct cache_file_identity *identity)
{
	enum cache_file_status status;
	uint32_t maximum_file_length;

	identity->has_opensauce_header =
		read_u32(bytes + CACHE_HEADER_OPENSAUCE_OFFSET) == OPENSAUCE_HEADER_SIGNATURE;
	if (identity->has_opensauce_header)
	{
		status = opensauce_header_verify(bytes + CACHE_HEADER_OPENSAUCE_OFFSET, identity);
		if (status != _cache_file_status_ok)
		{
			return status;
		}
	}
	if (identity->compressed_file_length)
	{
		return _cache_file_status_compressed_cache;
	}
	maximum_file_length = identity->has_opensauce_header &&
		flag_is_set(identity->opensauce.flags, _opensauce_cache_uses_memory_upgrades_bit) ?
		CUSTOM_EDITION_CACHE_FILE_MAXIMUM_BYTES_UPGRADED :
		CUSTOM_EDITION_CACHE_FILE_MAXIMUM_BYTES;
	if (identity->file_length < CACHE_FILE_HEADER_BYTES ||
		identity->file_length > maximum_file_length ||
		identity->file_length > identity->file_size)
	{
		return _cache_file_status_bad_file_length;
	}
	if (identity->tag_data_offset < CACHE_FILE_HEADER_BYTES ||
		identity->tag_data_size < TAG_INDEX_BYTES ||
		identity->tag_data_size > custom_edition_tag_cache_bytes(identity) ||
		!range_fits(identity->tag_data_offset, identity->tag_data_size, identity->file_length))
	{
		return _cache_file_status_bad_tag_data_range;
	}

	return _cache_file_status_ok;
}

static enum cache_file_status cache_header_identify(
	uint8_t const *bytes,
	uint32_t byte_count,
	struct cache_file_identity *identity)
{
	if (byte_count < CACHE_FILE_HEADER_BYTES)
	{
		return _cache_file_status_file_too_small;
	}
	if (read_u32(bytes + CACHE_HEADER_FOOTER_OFFSET) != CACHE_FOOTER_SIGNATURE)
	{
		return _cache_file_status_bad_signatures;
	}
	identity->version = read_s32(bytes + CACHE_HEADER_VERSION_OFFSET);
	identity->file_length = read_u32(bytes + CACHE_HEADER_FILE_LENGTH_OFFSET);
	identity->compressed_file_length = read_u32(bytes + CACHE_HEADER_COMPRESSED_LENGTH_OFFSET);
	identity->tag_data_offset = read_u32(bytes + CACHE_HEADER_TAG_DATA_OFFSET_OFFSET);
	identity->tag_data_size = read_u32(bytes + CACHE_HEADER_TAG_DATA_SIZE_OFFSET);
	identity->scenario_type = read_s16(bytes + CACHE_HEADER_SCENARIO_TYPE_OFFSET);
	identity->checksum = read_u32(bytes + CACHE_HEADER_CHECKSUM_OFFSET);
	switch (identity->version)
	{
	case CACHE_FILE_VERSION_XBOX:
		identity->format = _cache_file_format_xbox_cache;
		break;
	case CACHE_FILE_VERSION_CUSTOM_EDITION:
		identity->format = _cache_file_format_custom_edition_cache;
		break;
	case CACHE_FILE_VERSION_PC_RETAIL:
		identity->format = _cache_file_format_pc_retail_cache;
		break;
	default:
		identity->format = _cache_file_format_other_cache;
		break;
	}
	if (!copy_string_field(identity->name, bytes + CACHE_HEADER_NAME_OFFSET) ||
		!copy_string_field(identity->build, bytes + CACHE_HEADER_BUILD_OFFSET))
	{
		return _cache_file_status_unterminated_string;
	}
	switch (identity->format)
	{
	case _cache_file_format_xbox_cache:
		/* the Xbox header is this build's own; cache_file_header_verify
		judges it */
		return _cache_file_status_ok;
	case _cache_file_format_custom_edition_cache:
	case _cache_file_format_pc_retail_cache:
		return custom_edition_header_verify(bytes, identity);
	default:
		return _cache_file_status_unsupported_version;
	}
}

static enum cache_file_status resource_map_header_identify(
	uint8_t const *bytes,
	struct cache_file_identity *identity)
{
	uint32_t type = read_u32(bytes);
	uint32_t names_offset = read_u32(bytes + RESOURCE_MAP_NAMES_OFFSET_OFFSET);
	uint32_t index_offset = read_u32(bytes + RESOURCE_MAP_INDEX_OFFSET_OFFSET);
	int32_t item_count = read_s32(bytes + RESOURCE_MAP_COUNT_OFFSET);

	if (type <= _resource_map_none || type >= NUMBER_OF_RESOURCE_MAP_TYPES)
	{
		identity->format = _cache_file_format_unrecognized;
		return _cache_file_status_ok;
	}
	identity->format = _cache_file_format_resource_map;
	identity->resource_map_type = (enum resource_map_type)type;
	identity->resource_item_count = item_count;
	/* the item data, then the names, then the index table */
	if (names_offset < RESOURCE_MAP_HEADER_BYTES ||
		index_offset < names_offset ||
		item_count < 0 ||
		index_offset > identity->file_size ||
		(uint32_t)item_count > (identity->file_size - index_offset) / RESOURCE_MAP_ITEM_BYTES)
	{
		return _cache_file_status_bad_resource_map_header;
	}

	return _cache_file_status_ok;
}

static enum cache_file_status load_fail(
	struct load_state *state,
	enum cache_file_status status,
	uint32_t location)
{
	state->report->status = status;
	state->report->problem_tag_index = state->tag_index;
	state->report->problem_location = location;

	return status;
}

/* the offset in the tag cache of the runtime address `address`, when
`size` bytes there lie within the bytes in use */
static int tag_cache_offset(
	struct load_state const *state,
	uint32_t address,
	uint32_t size,
	uint32_t *offset)
{
	if (address < state->address_base ||
		!range_fits(address - state->address_base, size, state->used_bytes))
	{
		return 0;
	}
	*offset = address - state->address_base;

	return 1;
}

/* the name of a tag instance, terminated within the tag data (checked at
every use, since loading writes into the tag data) */
static char const *tag_name_get(
	struct load_state const *state,
	uint8_t const *instance)
{
	uint32_t name_address = read_u32(instance + TAG_INSTANCE_NAME_OFFSET);
	uint32_t name_offset;
	uint32_t search_bytes;

	if (!tag_cache_offset(state, name_address, 1, &name_offset) ||
		name_offset >= state->report->tag_data_bytes)
	{
		return NULL;
	}
	search_bytes = state->report->tag_data_bytes - name_offset;
	if (search_bytes > TAG_NAME_MAXIMUM_LENGTH + 1)
	{
		search_bytes = TAG_NAME_MAXIMUM_LENGTH + 1;
	}

	return memchr(state->tag_cache + name_offset, 0, search_bytes) ?
		(char const *)state->tag_cache + name_offset :
		NULL;
}

static enum cache_file_status bitmap_data_check(
	struct load_state *state,
	uint32_t element_offset)
{
	uint8_t const *bitmap = state->tag_cache + element_offset;
	uint32_t pixels_offset = read_u32(bitmap + BITMAP_DATA_PIXELS_OFFSET_OFFSET);
	uint32_t pixels_size = read_u32(bitmap + BITMAP_DATA_PIXELS_SIZE_OFFSET);
	uint32_t limit;

	/* Reclaimer (Halo1/BitmapTag.cs): the pixels are in bitmaps.map when
	the bitmap says so, else in the map itself */
	if (flag_is_set(read_u16(bitmap + BITMAP_DATA_FLAGS_OFFSET), BITMAP_DATA_IN_RESOURCE_MAP_BIT))
	{
		struct resource_map const *bitmaps = state->resource_maps[_resource_map_bitmaps];

		if (!bitmaps)
		{
			return load_fail(state, _cache_file_status_missing_resource_map, element_offset);
		}
		limit = bitmaps->source->size;
	}
	else
	{
		limit = state->file_length;
	}
	if (!range_fits(pixels_offset, pixels_size, limit))
	{
		return load_fail(state, _cache_file_status_bad_resource_data_range, pixels_offset);
	}
	state->report->bitmap_data_ranges_checked++;

	return _cache_file_status_ok;
}

static enum cache_file_status sound_permutation_check(
	struct load_state *state,
	uint32_t element_offset)
{
	uint8_t const *samples = state->tag_cache + element_offset + SOUND_PERMUTATION_SAMPLES_OFFSET;
	int32_t size = read_s32(samples + TAG_DATA_SIZE_OFFSET);
	uint32_t file_offset = read_u32(samples + TAG_DATA_FILE_OFFSET_OFFSET);
	uint32_t limit;

	if (size < 0)
	{
		return load_fail(state, _cache_file_status_bad_resource_data_range, element_offset);
	}
	if (flag_is_set(read_u32(samples + TAG_DATA_FLAGS_OFFSET), SOUND_SAMPLES_IN_RESOURCE_MAP_BIT))
	{
		struct resource_map const *sounds = state->resource_maps[_resource_map_sounds];

		if (!sounds)
		{
			return load_fail(state, _cache_file_status_missing_resource_map, element_offset);
		}
		limit = sounds->source->size;
	}
	else
	{
		limit = state->file_length;
	}
	if (!range_fits(file_offset, (uint32_t)size, limit))
	{
		return load_fail(state, _cache_file_status_bad_resource_data_range, file_offset);
	}
	state->report->sound_sample_ranges_checked++;

	return _cache_file_status_ok;
}

/* A gbxmodel part's strip and vertices: of the kinds Custom Edition writes,
within the index and vertex parts of the model data, aligned for reading as
16-bit indices and floats, and few enough vertices for 16-bit indices. What
the strip and the vertices hold is checked when they are converted
(custom_edition_cache.h). */
static enum cache_file_status gbxmodel_part_check(
	struct load_state *state,
	uint32_t element_offset)
{
	uint8_t const *part = state->tag_cache + element_offset;
	int32_t strip_triangle_count = read_s32(part + GBXMODEL_PART_STRIP_TRIANGLE_COUNT_OFFSET);
	uint32_t strip_offset = read_u32(part + GBXMODEL_PART_STRIP_OFFSET_OFFSET);
	int32_t vertex_count = read_s32(part + GBXMODEL_PART_VERTEX_COUNT_OFFSET);
	uint32_t vertex_offset = read_u32(part + GBXMODEL_PART_VERTEX_OFFSET_OFFSET);
	uint32_t index_data_bytes = state->model_data_size - state->model_index_data_offset;

	if (read_s16(part + GBXMODEL_PART_TRIANGLE_BUFFER_TYPE_OFFSET) != TRIANGLE_BUFFER_TYPE_PRECOMPILED_STRIP ||
		read_s16(part + GBXMODEL_PART_VERTEX_BUFFER_TYPE_OFFSET) != VERTEX_TYPE_MODEL_UNCOMPRESSED ||
		part[GBXMODEL_PART_LOCAL_NODE_COUNT_OFFSET] > GBXMODEL_MAXIMUM_LOCAL_NODES ||
		strip_triangle_count < 1 ||
		(uint32_t)strip_triangle_count + 2 > index_data_bytes / STRIP_INDEX_BYTES ||
		!range_fits(strip_offset, ((uint32_t)strip_triangle_count + 2) * STRIP_INDEX_BYTES, index_data_bytes) ||
		((state->model_index_data_offset + strip_offset) & (STRIP_INDEX_BYTES - 1)) ||
		vertex_count < 1 ||
		vertex_count > MAXIMUM_VERTICES_PER_BUFFER ||
		!range_fits(vertex_offset, (uint32_t)vertex_count * GBXMODEL_VERTEX_BYTES, state->model_index_data_offset) ||
		(vertex_offset & 3))
	{
		return load_fail(state, _cache_file_status_bad_model_part, element_offset);
	}

	return _cache_file_status_ok;
}

/* Walks the element at `element_offset` of the tag cache and everything its
blocks hold, requiring every block and every present tag data field to lie
within the bytes in use. Tag data with a size but no address is data the
cache build left out (bitmaps' compressed color plates), which the game
never reads. */
static enum cache_file_status element_verify(
	struct load_state *state,
	uint32_t element_offset,
	struct element_layout const *layout,
	int depth)
{
	enum cache_file_status status = _cache_file_status_ok;
	int field_index;

	/* the layouts nest at most three deep; this also stops runaway input */
	if (depth > 4)
	{
		return load_fail(state, _cache_file_status_bad_tag_layout, element_offset);
	}
	for (field_index = 0; field_index < layout->data_count; field_index++)
	{
		uint8_t const *data = state->tag_cache + element_offset + layout->data_offsets[field_index];
		int32_t size = read_s32(data + TAG_DATA_SIZE_OFFSET);
		uint32_t address = read_u32(data + TAG_DATA_ADDRESS_OFFSET);
		uint32_t offset;

		if (size < 0 || (address && !tag_cache_offset(state, address, (uint32_t)size, &offset)))
		{
			return load_fail(state, _cache_file_status_bad_tag_layout, element_offset);
		}
	}
	for (field_index = 0; field_index < layout->block_count && status == _cache_file_status_ok; field_index++)
	{
		struct block_layout const *block = &layout->blocks[field_index];
		uint8_t const *field = state->tag_cache + element_offset + block->offset;
		int32_t count = read_s32(field + TAG_BLOCK_COUNT_OFFSET);
		uint32_t address = read_u32(field + TAG_BLOCK_ADDRESS_OFFSET);
		uint32_t offset;
		int32_t element_index;

		if (count == 0)
		{
			continue;
		}
		if (count < 0 ||
			(uint32_t)count > UINT32_MAX / block->element_bytes ||
			!tag_cache_offset(state, address, (uint32_t)count * block->element_bytes, &offset))
		{
			return load_fail(state, _cache_file_status_bad_tag_layout, element_offset);
		}
		for (element_index = 0; element_index < count && status == _cache_file_status_ok; element_index++)
		{
			status = element_verify(
				state,
				offset + (uint32_t)element_index * block->element_bytes,
				block->element,
				depth + 1);
		}
	}
	if (status == _cache_file_status_ok && layout->check)
	{
		status = layout->check(state, element_offset);
	}

	return status;
}

/* Turns the addresses in an element read from a resource map, which count
from `item_base` within the `item_bytes` of the item, into runtime addresses
of the item placed at `item_address`. The editing kit's definition pointers
are cleared: they refer to nothing in this process. */
static enum cache_file_status element_relocate(
	struct load_state *state,
	uint8_t *item,
	uint32_t item_bytes,
	uint32_t element_offset,
	struct element_layout const *layout,
	uint32_t item_address,
	int depth)
{
	enum cache_file_status status = _cache_file_status_ok;
	int field_index;

	if (depth > 4 || !range_fits(element_offset, layout->bytes, item_bytes))
	{
		return load_fail(state, _cache_file_status_bad_resource_layout, element_offset);
	}
	for (field_index = 0; field_index < layout->data_count; field_index++)
	{
		uint8_t *data = item + element_offset + layout->data_offsets[field_index];
		int32_t size = read_s32(data + TAG_DATA_SIZE_OFFSET);
		uint32_t address = read_u32(data + TAG_DATA_ADDRESS_OFFSET);

		if (size < 0 || (address && !range_fits(address, (uint32_t)size, item_bytes)))
		{
			return load_fail(state, _cache_file_status_bad_resource_layout, element_offset);
		}
		if (address)
		{
			write_u32(data + TAG_DATA_ADDRESS_OFFSET, item_address + address);
			state->report->relocated_pointer_count++;
		}
		write_u32(data + TAG_DATA_DEFINITION_OFFSET, 0);
	}
	for (field_index = 0; field_index < layout->block_count && status == _cache_file_status_ok; field_index++)
	{
		struct block_layout const *block = &layout->blocks[field_index];
		uint8_t *field = item + element_offset + block->offset;
		int32_t count = read_s32(field + TAG_BLOCK_COUNT_OFFSET);
		uint32_t address = read_u32(field + TAG_BLOCK_ADDRESS_OFFSET);
		int32_t element_index;

		write_u32(field + TAG_BLOCK_DEFINITION_OFFSET, 0);
		if (count == 0)
		{
			continue;
		}
		if (count < 0 ||
			(uint32_t)count > UINT32_MAX / block->element_bytes ||
			!range_fits(address, (uint32_t)count * block->element_bytes, item_bytes))
		{
			return load_fail(state, _cache_file_status_bad_resource_layout, element_offset);
		}
		write_u32(field + TAG_BLOCK_ADDRESS_OFFSET, item_address + address);
		state->report->relocated_pointer_count++;
		for (element_index = 0; element_index < count && status == _cache_file_status_ok; element_index++)
		{
			status = element_relocate(
				state,
				item,
				item_bytes,
				address + (uint32_t)element_index * block->element_bytes,
				block->element,
				item_address,
				depth + 1);
		}
	}

	return status;
}

/* room for `size` more bytes of resource-held tags after what is in use */
static enum cache_file_status tag_cache_allocate(
	struct load_state *state,
	uint32_t size,
	uint32_t *offset)
{
	uint32_t aligned = (state->used_bytes + 3) & ~(uint32_t)3;

	if (aligned < state->used_bytes || !range_fits(aligned, size, state->usable_bytes))
	{
		return load_fail(state, _cache_file_status_tag_cache_overflow, state->used_bytes);
	}
	*offset = aligned;
	state->report->resource_tag_bytes += size + (aligned - state->used_bytes);
	state->used_bytes = aligned + size;

	return _cache_file_status_ok;
}

static struct resource_map_item const *resource_map_find(
	struct resource_map const *map,
	char const *name)
{
	int32_t item_index;

	for (item_index = 0; item_index < map->item_count; item_index++)
	{
		if (!strcmp(map->items[item_index].name, name))
		{
			return &map->items[item_index];
		}
	}

	return NULL;
}

/* Bitmaps, fonts, unicode string lists and HUD message text held by a
resource map: the tag's address field is the index of its entry, whose name
is the tag's path (observed for every such tag of the stock maps; Reclaimer
reads bitmaps the same way). The entry is the whole tag, its addresses
counting from the start of the entry. */
static enum cache_file_status resource_tag_load(
	struct load_state *state,
	uint8_t *instance,
	char const *name,
	enum resource_map_type type,
	struct element_layout const *layout)
{
	struct resource_map const *map = state->resource_maps[type];
	uint32_t item_index = read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
	struct resource_map_item const *item;
	enum cache_file_status status;
	uint32_t offset;

	if (!map)
	{
		return load_fail(state, _cache_file_status_missing_resource_map, item_index);
	}
	if (item_index >= (uint32_t)map->item_count || strcmp(map->items[item_index].name, name))
	{
		return load_fail(state, _cache_file_status_missing_resource_item, item_index);
	}
	item = &map->items[item_index];
	status = tag_cache_allocate(state, item->size, &offset);
	if (status != _cache_file_status_ok)
	{
		return status;
	}
	if (!map->source->read(map->source->context, item->data_offset, item->size, state->tag_cache + offset))
	{
		return load_fail(state, _cache_file_status_read_failed, item->data_offset);
	}
	status = element_relocate(
		state,
		state->tag_cache + offset,
		item->size,
		0,
		layout,
		CUSTOM_EDITION_TAG_CACHE_ADDRESS + offset,
		0);
	if (status == _cache_file_status_ok && layout == &font_layout)
	{
		int reference_index;

		/* a style reference would name a tag index of whatever map the
		resource map was built with; every stock font has none */
		for (reference_index = 0; reference_index < FONT_STYLE_REFERENCE_COUNT; reference_index++)
		{
			uint8_t const *reference = state->tag_cache + offset + FONT_STYLE_REFERENCES_OFFSET +
				reference_index * TAG_REFERENCE_BYTES;

			if (read_s32(reference + TAG_REFERENCE_INDEX_OFFSET) != NO_TAG_INDEX)
			{
				return load_fail(state, _cache_file_status_bad_resource_layout, item->data_offset);
			}
		}
	}
	if (status == _cache_file_status_ok)
	{
		write_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET, CUSTOM_EDITION_TAG_CACHE_ADDRESS + offset);
		state->report->resource_tag_counts[type]++;
	}

	return status;
}

/* Sounds held by sounds.map: the map keeps the tag's 0xA4-byte header, its
pitch range block with a count and no address, and the tag's address field
points at that header. The entry named by the tag's path repeats the header
and then holds the pitch ranges and their permutations, whose addresses
count from the first pitch range. (Observed in every stock map: OpenSauce
leaves this path of cache_file_data_load unimplemented and hooks the game's
own data_file_read instead.) The entry's copy of the header is used for the
fields the map's copy leaves zero, its sample rate, encoding, compression
and longest permutation length, which it holds as the Xbox maps of build
2276 do. Its pointers and tag references belong to the editing kit and to
the map that sounds.map was built with (port/n3ds/pc/FORMAT-REFERENCE.md). */
static enum cache_file_status resource_sound_load(
	struct load_state *state,
	uint8_t *instance,
	char const *name)
{
	struct resource_map const *map = state->resource_maps[_resource_map_sounds];
	uint32_t header_address = read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
	struct resource_map_item const *item;
	uint8_t entry_header[SOUND_DEFINITION_BYTES];
	uint8_t *header;
	uint8_t *pitch_ranges_field;
	uint32_t header_offset;
	uint32_t data_bytes;
	uint32_t offset;
	int32_t pitch_range_count;
	int32_t pitch_range_index;
	enum cache_file_status status;

	if (!map)
	{
		return load_fail(state, _cache_file_status_missing_resource_map, header_address);
	}
	if (!tag_cache_offset(state, header_address, SOUND_DEFINITION_BYTES, &header_offset))
	{
		return load_fail(state, _cache_file_status_bad_tag_address, header_address);
	}
	item = resource_map_find(map, name);
	if (!item)
	{
		return load_fail(state, _cache_file_status_missing_resource_item, header_address);
	}
	pitch_ranges_field = state->tag_cache + header_offset + SOUND_PITCH_RANGES_OFFSET;
	pitch_range_count = read_s32(pitch_ranges_field + TAG_BLOCK_COUNT_OFFSET);
	if (item->size < SOUND_DEFINITION_BYTES ||
		pitch_range_count < 0 ||
		(uint32_t)pitch_range_count > (item->size - SOUND_DEFINITION_BYTES) / SOUND_PITCH_RANGE_BYTES)
	{
		return load_fail(state, _cache_file_status_bad_resource_layout, item->data_offset);
	}
	data_bytes = item->size - SOUND_DEFINITION_BYTES;
	status = tag_cache_allocate(state, data_bytes, &offset);
	if (status != _cache_file_status_ok)
	{
		return status;
	}
	if (!map->source->read(map->source->context, item->data_offset, SOUND_DEFINITION_BYTES, entry_header) ||
		!map->source->read(
			map->source->context,
			item->data_offset + SOUND_DEFINITION_BYTES,
			data_bytes,
			state->tag_cache + offset))
	{
		return load_fail(state, _cache_file_status_read_failed, item->data_offset);
	}
	header = state->tag_cache + header_offset;
	memcpy(header + SOUND_SAMPLE_RATE_OFFSET, entry_header + SOUND_SAMPLE_RATE_OFFSET, 2);
	memcpy(header + SOUND_ENCODING_OFFSET, entry_header + SOUND_ENCODING_OFFSET, 2);
	memcpy(header + SOUND_COMPRESSION_OFFSET, entry_header + SOUND_COMPRESSION_OFFSET, 2);
	memcpy(header + SOUND_LONGEST_PERMUTATION_LENGTH_OFFSET, entry_header + SOUND_LONGEST_PERMUTATION_LENGTH_OFFSET, 4);
	for (pitch_range_index = 0; pitch_range_index < pitch_range_count; pitch_range_index++)
	{
		status = element_relocate(
			state,
			state->tag_cache + offset,
			data_bytes,
			(uint32_t)pitch_range_index * SOUND_PITCH_RANGE_BYTES,
			&sound_pitch_range_layout,
			CUSTOM_EDITION_TAG_CACHE_ADDRESS + offset,
			1);
		if (status != _cache_file_status_ok)
		{
			return status;
		}
	}
	if (pitch_range_count)
	{
		write_u32(pitch_ranges_field + TAG_BLOCK_ADDRESS_OFFSET, CUSTOM_EDITION_TAG_CACHE_ADDRESS + offset);
		state->report->relocated_pointer_count++;
	}
	state->report->resource_tag_counts[_resource_map_sounds]++;

	return _cache_file_status_ok;
}

/* the layout of a group whose tags resource maps may hold, or NULL: only
these groups ever are (OpenSauce cache_files.cpp, cache_file_data_load) */
static struct element_layout const *resource_group_layout(
	uint32_t group_tag)
{
	switch (group_tag)
	{
	case BITMAP_GROUP_TAG:
		return &bitmap_group_layout;
	case SOUND_GROUP_TAG:
		return &sound_definition_layout;
	case FONT_GROUP_TAG:
		return &font_layout;
	case UNICODE_STRING_LIST_GROUP_TAG:
		return &unicode_string_list_layout;
	case HUD_MESSAGE_TEXT_GROUP_TAG:
		return &hud_message_text_layout;
	default:
		return NULL;
	}
}

/* the layout of a group whose tags are checked after loading, or NULL: the
groups resource maps hold, and those converted for this build */
static struct element_layout const *checked_group_layout(
	uint32_t group_tag)
{
	switch (group_tag)
	{
	case INPUT_DEVICE_DEFAULTS_GROUP_TAG:
		return &input_device_layout;
	case CAMERA_TRACK_GROUP_TAG:
		return &camera_track_layout;
	case ACTOR_VARIANT_GROUP_TAG:
		return &actor_variant_layout;
	case ACTOR_GROUP_TAG:
		return &actor_layout;
	case FOG_GROUP_TAG:
		return &fog_layout;
	case GLOW_GROUP_TAG:
		return &glow_layout;
	case DETAIL_OBJECT_COLLECTION_GROUP_TAG:
		return &detail_object_collection_layout;
	case VIRTUAL_KEYBOARD_GROUP_TAG:
		return &virtual_keyboard_layout;
	case MULTIPLAYER_SCENARIO_DESCRIPTION_GROUP_TAG:
		return &multiplayer_description_layout;
	case LIGHTNING_GROUP_TAG:
		return &lightning_layout;
	case TRANSPARENT_WATER_GROUP_TAG:
		return &transparent_water_layout;
	case WEATHER_PARTICLE_SYSTEM_GROUP_TAG:
		return &weather_particle_layout;
	case DIALOGUE_GROUP_TAG:
		return &dialogue_layout;
	case GAME_GLOBALS_GROUP_TAG:
		return &globals_layout;
	case PROJECTILE_GROUP_TAG:
		return &projectile_layout;
	case WEAPON_GROUP_TAG:
		return &weapon_layout;
	case VEHICLE_GROUP_TAG:
		return &vehicle_layout;
	case BIPED_GROUP_TAG:
		return &biped_layout;
	case SCENERY_GROUP_TAG:
		return &scenery_layout;
	case SOUND_SCENERY_GROUP_TAG:
		return &sound_scenery_layout;
	case DEVICE_GROUP_TAG:
		return &device_layout;
	case MACHINE_GROUP_TAG:
		return &machine_layout;
	case CONTROL_GROUP_TAG:
		return &control_layout;
	case LIGHT_FIXTURE_GROUP_TAG:
		return &light_fixture_layout;
	case GARBAGE_GROUP_TAG:
		return &garbage_layout;
	case EQUIPMENT_GROUP_TAG:
		return &equipment_layout;
	case ITEM_COLLECTION_GROUP_TAG:
		return &item_collection_layout;
	case TAG_COLLECTION_GROUP_TAG:
		return &tag_collection_layout;
	case UI_WIDGET_COLLECTION_GROUP_TAG:
		/* PC retail Soul caches have the same 0x0C root and 0x10-byte
		   dependency entries as tagc. The entries reference DeLa widgets. */
		return &tag_collection_layout;
	case METER_GROUP_TAG:
		return &meter_layout;
	case SKY_GROUP_TAG:
		return &sky_layout;
	case SCENARIO_GROUP_TAG:
		return &scenario_layout;
	case WEAPON_HUD_INTERFACE_GROUP_TAG:
		return &weapon_hud_layout;
	case UNIT_HUD_INTERFACE_GROUP_TAG:
		return &unit_interface_hud_layout;
	case GRENADE_HUD_INTERFACE_GROUP_TAG:
		return &grenade_hud_layout;
	case HUD_GLOBALS_GROUP_TAG:
		return &hud_globals_layout;
	case SHADER_ENVIRONMENT_GROUP_TAG:
		return &shader_environment_layout;
	case ANIMATION_GRAPH_GROUP_TAG:
		return &animation_graph_layout;
	case COLLISION_MODEL_GROUP_TAG:
		return &collision_model_layout;
	case POINT_PHYSICS_GROUP_TAG:
		return &point_physics_layout;
	case LOOPING_SOUND_GROUP_TAG:
		return &looping_sound_layout;
	case LENS_FLARE_GROUP_TAG:
		return &lens_flare_layout;
	case DECAL_GROUP_TAG:
		return &decal_layout;
	case LIGHT_GROUP_TAG:
		return &light_layout;
	case DAMAGE_EFFECT_GROUP_TAG:
		return &damage_effect_layout;
	case PARTICLE_GROUP_TAG:
		return &particle_layout;
	case PARTICLE_SYSTEM_GROUP_TAG:
		return &particle_system_layout;
	case MATERIAL_EFFECTS_GROUP_TAG:
		return &material_effects_layout;
	case CONTRAIL_GROUP_TAG:
		return &contrail_layout;
	case PHYSICS_GROUP_TAG:
		return &physics_layout;
	case LIGHT_VOLUME_GROUP_TAG:
		return &light_volume_layout;
	case COLOR_TABLE_GROUP_TAG:
		return &color_table_layout;
	case ANTENNA_GROUP_TAG:
		return &antenna_layout;
	case STRING_LIST_GROUP_TAG:
		return &unicode_string_list_layout;
	case HUD_NUMBER_GROUP_TAG:
		return &hud_number_layout;
	case SOUND_ENVIRONMENT_GROUP_TAG:
		return &sound_environment_layout;
	case WIND_GROUP_TAG:
		return &wind_layout;
	case FLAG_GROUP_TAG:
		return &flag_layout;
	case SHADER_MODEL_GROUP_TAG:
		return &shader_model_layout;
	case TRANSPARENT_GLASS_GROUP_TAG:
		return &transparent_glass_layout;
	case TRANSPARENT_METER_GROUP_TAG:
		return &transparent_meter_layout;
	case TRANSPARENT_PLASMA_GROUP_TAG:
		return &transparent_plasma_layout;
	case UI_WIDGET_GROUP_TAG:
		return &ui_widget_layout;
	case EFFECT_GROUP_TAG:
		return &effect_layout;
	case GBXMODEL_GROUP_TAG:
		return &gbxmodel_layout;
	case TRANSPARENT_CHICAGO_GROUP_TAG:
		return &transparent_chicago_layout;
	case TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG:
		return &transparent_chicago_extended_layout;
	default:
		return resource_group_layout(group_tag);
	}
}

/* The `count` elements of `element_bytes` a tag block points to, in memory
of `size` bytes loaded at `address` (a structure BSP, or the tag cache):
nonzero when they lie within it, aligned for the game to read as
structures, with `*offset` their offset in it. */
static int loaded_block_get(
	uint8_t const *block,
	uint32_t address,
	uint32_t size,
	uint32_t element_bytes,
	int32_t *count,
	uint32_t *offset)
{
	uint32_t block_address = read_u32(block + TAG_BLOCK_ADDRESS_OFFSET);

	*count = read_s32(block + TAG_BLOCK_COUNT_OFFSET);
	*offset = block_address - address;

	return *count == 0 ||
		(*count > 0 &&
		(uint32_t)*count <= size / element_bytes &&
		block_address >= address &&
		range_fits(*offset, (uint32_t)*count * element_bytes, size) &&
		!(*offset & 3));
}

/* nonzero when the `vector_count` vectors at `vectors_offset` of each of
the `vertex_count` vertices of `vertex_bytes` at `vertices` are unit-range */
static int unit_vectors_valid(
	uint8_t const *vertices,
	int32_t vertex_count,
	uint32_t vertex_bytes,
	uint32_t vectors_offset,
	int vector_count)
{
	int32_t vertex_index;

	for (vertex_index = 0; vertex_index < vertex_count; vertex_index++)
	{
		uint8_t const *vectors = vertices + (uint32_t)vertex_index * vertex_bytes + vectors_offset;
		int component_index;

		for (component_index = 0; component_index < 3 * vector_count; component_index++)
		{
			float component = read_f32(vectors + component_index * 4);

			/* a NaN fails both comparisons */
			if (!(component >= -MAXIMUM_UNIT_VECTOR_COMPONENT && component <= MAXIMUM_UNIT_VECTOR_COMPONENT))
			{
				return 0;
			}
		}
	}

	return 1;
}

/* The lightmaps of a structure BSP (`size` bytes read from the file into
`bsp`, which load at `address`) and their materials, whose vertices are
converted for this build when the BSP is loaded (custom_edition_cache.h):
each material's uncompressed vertices within the BSP, of the size its vertex
counts give, and lightmap vertices exactly when its lightmap has a bitmap. */
static enum cache_file_status structure_bsp_geometry_verify(
	struct load_state *state,
	uint8_t const *bsp,
	uint32_t size,
	uint32_t address)
{
	uint32_t structure_offset = read_u32(bsp + STRUCTURE_BSP_HEADER_BSP_OFFSET) - address;
	uint32_t lightmaps_offset;
	int32_t lightmap_count;
	int32_t lightmap_index;

	if (!range_fits(structure_offset, STRUCTURE_BSP_BYTES, size) ||
		!loaded_block_get(
			bsp + structure_offset + STRUCTURE_BSP_LIGHTMAPS_OFFSET,
			address,
			size,
			STRUCTURE_BSP_LIGHTMAP_BYTES,
			&lightmap_count,
			&lightmaps_offset))
	{
		return load_fail(state, _cache_file_status_bad_structure_bsp_geometry, address);
	}
	for (lightmap_index = 0; lightmap_index < lightmap_count; lightmap_index++)
	{
		uint8_t const *lightmap = bsp + lightmaps_offset + (uint32_t)lightmap_index * STRUCTURE_BSP_LIGHTMAP_BYTES;
		int has_bitmap = read_s16(lightmap + STRUCTURE_BSP_LIGHTMAP_BITMAP_INDEX_OFFSET) != -1;
		uint32_t materials_offset;
		int32_t material_count;
		int32_t material_index;

		if (!loaded_block_get(
			lightmap + STRUCTURE_BSP_LIGHTMAP_MATERIALS_OFFSET,
			address,
			size,
			STRUCTURE_BSP_MATERIAL_BYTES,
			&material_count,
			&materials_offset))
		{
			return load_fail(state, _cache_file_status_bad_structure_bsp_geometry, address + lightmaps_offset);
		}
		for (material_index = 0; material_index < material_count; material_index++)
		{
			uint32_t material_offset = materials_offset + (uint32_t)material_index * STRUCTURE_BSP_MATERIAL_BYTES;
			uint8_t const *material = bsp + material_offset;
			uint8_t const *vertices = material + STRUCTURE_BSP_MATERIAL_UNCOMPRESSED_VERTICES_OFFSET;
			int32_t vertex_count = read_s32(material + STRUCTURE_BSP_MATERIAL_VERTEX_COUNT_OFFSET);
			int32_t lightmap_vertex_count = read_s32(material + STRUCTURE_BSP_MATERIAL_LIGHTMAP_VERTEX_COUNT_OFFSET);
			int32_t vertices_size = read_s32(vertices + TAG_DATA_SIZE_OFFSET);
			uint32_t vertices_address = read_u32(vertices + TAG_DATA_ADDRESS_OFFSET);

			/* the counts bound the size, which bounds the counts */
			if (read_s16(material + STRUCTURE_BSP_MATERIAL_VERTEX_TYPE_OFFSET) != VERTEX_TYPE_ENVIRONMENT_UNCOMPRESSED ||
				vertex_count < 0 ||
				vertex_count > MAXIMUM_VERTICES_PER_BUFFER ||
				lightmap_vertex_count != (has_bitmap ? vertex_count : 0) ||
				vertices_size != vertex_count * STRUCTURE_BSP_ENVIRONMENT_VERTEX_BYTES +
					lightmap_vertex_count * STRUCTURE_BSP_LIGHTMAP_VERTEX_BYTES ||
				(vertices_size &&
				(vertices_address < address ||
				!range_fits(vertices_address - address, (uint32_t)vertices_size, size) ||
				(vertices_address & 3))))
			{
				return load_fail(state, _cache_file_status_bad_structure_bsp_geometry, address + material_offset);
			}
			if (vertices_size &&
				(!unit_vectors_valid(
					bsp + (vertices_address - address),
					vertex_count,
					STRUCTURE_BSP_ENVIRONMENT_VERTEX_BYTES,
					STRUCTURE_BSP_ENVIRONMENT_VERTEX_VECTORS_OFFSET,
					STRUCTURE_BSP_ENVIRONMENT_VERTEX_VECTOR_COUNT) ||
				!unit_vectors_valid(
					bsp + (vertices_address - address) + (uint32_t)vertex_count * STRUCTURE_BSP_ENVIRONMENT_VERTEX_BYTES,
					lightmap_vertex_count,
					STRUCTURE_BSP_LIGHTMAP_VERTEX_BYTES,
					STRUCTURE_BSP_LIGHTMAP_VERTEX_VECTORS_OFFSET,
					STRUCTURE_BSP_LIGHTMAP_VERTEX_VECTOR_COUNT)))
			{
				return load_fail(state, _cache_file_status_bad_structure_bsp_geometry, vertices_address);
			}
			state->report->structure_bsp_materials_checked++;
		}
	}

	return _cache_file_status_ok;
}

static enum cache_file_status structure_bsps_verify(
	struct load_state *state,
	uint8_t const *tag_instances,
	int32_t tag_count,
	uint32_t scenario_offset,
	uint32_t *checksum)
{
	uint8_t const *block = state->tag_cache + scenario_offset + SCENARIO_STRUCTURE_BSPS_OFFSET;
	int32_t count = read_s32(block + TAG_BLOCK_COUNT_OFFSET);
	uint32_t address = read_u32(block + TAG_BLOCK_ADDRESS_OFFSET);
	uint32_t tag_cache_bytes = state->report->tag_cache_bytes;
	uint32_t checksum_offset = CACHE_FILE_HEADER_BYTES;
	uint32_t block_offset;
	int32_t bsp_index;

	state->usable_bytes = tag_cache_bytes;
	if (count < 0 ||
		count > MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO ||
		(count && !tag_cache_offset(state, address, (uint32_t)count * STRUCTURE_BSP_REFERENCE_BYTES, &block_offset)))
	{
		return load_fail(state, _cache_file_status_bad_structure_bsp_block, address);
	}
	state->report->structure_bsp_count = count;
	for (bsp_index = 0; bsp_index < count; bsp_index++)
	{
		uint8_t const *reference = state->tag_cache + block_offset + (uint32_t)bsp_index * STRUCTURE_BSP_REFERENCE_BYTES;
		int32_t file_offset = read_s32(reference + STRUCTURE_BSP_REFERENCE_FILE_OFFSET_OFFSET);
		int32_t size = read_s32(reference + STRUCTURE_BSP_REFERENCE_SIZE_OFFSET);
		uint32_t bsp_address = read_u32(reference + STRUCTURE_BSP_REFERENCE_ADDRESS_OFFSET);
		uint32_t tag_handle = read_u32(reference + STRUCTURE_BSP_REFERENCE_TAG_INDEX_OFFSET);
		uint32_t tag_index = tag_handle & ABSOLUTE_INDEX_MASK;
		uint8_t *bsp;
		uint32_t bsp_pointer;
		enum cache_file_status status;

		/* the tag the reference names: a structure BSP, not yet loaded */
		if (tag_index >= (uint32_t)tag_count ||
			read_u32(tag_instances + tag_index * TAG_INSTANCE_BYTES + TAG_INSTANCE_HANDLE_OFFSET) != tag_handle ||
			read_u32(tag_instances + tag_index * TAG_INSTANCE_BYTES + TAG_INSTANCE_GROUP_OFFSET) != STRUCTURE_BSP_GROUP_TAG ||
			read_u32(tag_instances + tag_index * TAG_INSTANCE_BYTES + TAG_INSTANCE_ADDRESS_OFFSET))
		{
			return load_fail(state, _cache_file_status_bad_structure_bsp_block, tag_handle);
		}
		/* in the file, and at the top of the tag cache above the tag data,
		where it is loaded when the game switches to it */
		if (file_offset < CACHE_FILE_HEADER_BYTES ||
			size < STRUCTURE_BSP_HEADER_BYTES ||
			!range_fits((uint32_t)file_offset, (uint32_t)size, state->file_length) ||
			bsp_address < CUSTOM_EDITION_TAG_CACHE_ADDRESS + state->used_bytes ||
			!range_fits(bsp_address - CUSTOM_EDITION_TAG_CACHE_ADDRESS, (uint32_t)size, tag_cache_bytes))
		{
			return load_fail(state, _cache_file_status_bad_structure_bsp_range, (uint32_t)file_offset);
		}
		bsp = malloc((size_t)size);
		if (!bsp)
		{
			return load_fail(state, _cache_file_status_out_of_memory, (uint32_t)size);
		}
		if (!state->map->read(state->map->context, (uint32_t)file_offset, (uint32_t)size, bsp))
		{
			free(bsp);
			return load_fail(state, _cache_file_status_read_failed, (uint32_t)file_offset);
		}
		bsp_pointer = read_u32(bsp + STRUCTURE_BSP_HEADER_BSP_OFFSET);
		/* PC caches have no Xbox vertex buffer arrays */
		if (read_u32(bsp + STRUCTURE_BSP_HEADER_SIGNATURE_OFFSET) != STRUCTURE_BSP_SIGNATURE ||
			read_u32(bsp + STRUCTURE_BSP_HEADER_VERTEX_BUFFERS_OFFSET) ||
			read_u32(bsp + STRUCTURE_BSP_HEADER_LIGHTMAP_VERTEX_BUFFERS_OFFSET) ||
			bsp_pointer < bsp_address + STRUCTURE_BSP_HEADER_BYTES ||
			bsp_pointer - bsp_address >= (uint32_t)size)
		{
			free(bsp);
			return load_fail(state, _cache_file_status_bad_structure_bsp_header, (uint32_t)file_offset);
		}
		status = structure_bsp_geometry_verify(state, bsp, (uint32_t)size, bsp_address);
		free(bsp);
		if (status != _cache_file_status_ok)
		{
			return status;
		}
		if ((uint32_t)size > state->report->largest_structure_bsp_bytes)
		{
			state->report->largest_structure_bsp_bytes = (uint32_t)size;
		}
		if (bsp_address - CUSTOM_EDITION_TAG_CACHE_ADDRESS < state->usable_bytes)
		{
			state->usable_bytes = bsp_address - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
			state->report->lowest_structure_bsp_address = bsp_address;
		}
		/* OpenSauce's CalculateChecksum takes the structure BSPs as packed
		one after another from the end of the header */
		if (!range_fits(checksum_offset, (uint32_t)size, state->map->size))
		{
			return load_fail(state, _cache_file_status_bad_structure_bsp_range, checksum_offset);
		}
		status = crc32_update_from_file(state->map, checksum, checksum_offset, (uint32_t)size);
		if (status != _cache_file_status_ok)
		{
			return load_fail(state, status, checksum_offset);
		}
		checksum_offset += (uint32_t)size;
	}

	return _cache_file_status_ok;
}

/* ---------- public code */

char const *cache_file_status_describe(
	enum cache_file_status status)
{
	return status >= 0 && status < NUMBER_OF_CACHE_FILE_STATUSES ?
		cache_file_status_descriptions[status] :
		"unknown status";
}

char const *cache_file_format_describe(
	enum cache_file_format format)
{
	return format >= 0 && format < NUMBER_OF_CACHE_FILE_FORMATS ?
		cache_file_format_descriptions[format] :
		"unknown format";
}

char const *resource_map_type_describe(
	enum resource_map_type type)
{
	return type >= 0 && type < NUMBER_OF_RESOURCE_MAP_TYPES ?
		resource_map_type_descriptions[type] :
		"unknown";
}

uint32_t custom_edition_tag_cache_bytes(
	struct cache_file_identity const *identity)
{
	return identity->has_opensauce_header &&
		flag_is_set(identity->opensauce.flags, _opensauce_cache_uses_memory_upgrades_bit) ?
		CUSTOM_EDITION_TAG_CACHE_BYTES_UPGRADED :
		CUSTOM_EDITION_TAG_CACHE_BYTES;
}

enum cache_file_status cache_file_identify(
	struct cache_file_source *source,
	struct cache_file_identity *identity)
{
	uint8_t header[CACHE_FILE_HEADER_BYTES];
	uint32_t header_bytes = source->size < CACHE_FILE_HEADER_BYTES ? source->size : CACHE_FILE_HEADER_BYTES;

	memset(identity, 0, sizeof(*identity));
	identity->file_size = source->size;
	if (header_bytes < RESOURCE_MAP_HEADER_BYTES)
	{
		return _cache_file_status_file_too_small;
	}
	if (!source->read(source->context, 0, header_bytes, header))
	{
		return _cache_file_status_read_failed;
	}
	if (read_u32(header) == CACHE_HEADER_SIGNATURE)
	{
		return cache_header_identify(header, header_bytes, identity);
	}

	return resource_map_header_identify(header, identity);
}

enum cache_file_format cache_file_header_format(
	void const *header,
	int *has_opensauce_header)
{
	uint8_t const *bytes = header;
	enum cache_file_format format;

	*has_opensauce_header = 0;
	if (read_u32(bytes) != CACHE_HEADER_SIGNATURE ||
		read_u32(bytes + CACHE_HEADER_FOOTER_OFFSET) != CACHE_FOOTER_SIGNATURE)
	{
		return _cache_file_format_unrecognized;
	}
	switch (read_s32(bytes + CACHE_HEADER_VERSION_OFFSET))
	{
	case CACHE_FILE_VERSION_XBOX:
		format = _cache_file_format_xbox_cache;
		break;
	case CACHE_FILE_VERSION_CUSTOM_EDITION:
		format = _cache_file_format_custom_edition_cache;
		*has_opensauce_header =
			read_u32(bytes + CACHE_HEADER_OPENSAUCE_OFFSET) == OPENSAUCE_HEADER_SIGNATURE;
		break;
	case CACHE_FILE_VERSION_PC_RETAIL:
		format = _cache_file_format_pc_retail_cache;
		break;
	default:
		format = _cache_file_format_other_cache;
		break;
	}

	return format;
}

enum cache_file_status resource_map_open(
	struct cache_file_source *source,
	enum resource_map_type expected_type,
	struct resource_map *map)
{
	struct cache_file_identity identity;
	uint8_t header[RESOURCE_MAP_HEADER_BYTES];
	uint8_t *index;
	uint32_t names_offset;
	uint32_t names_bytes;
	uint32_t index_offset;
	int32_t item_index;
	enum cache_file_status status;

	memset(map, 0, sizeof(*map));
	map->source = source;
	status = cache_file_identify(source, &identity);
	if (status != _cache_file_status_ok)
	{
		return status;
	}
	if (identity.format != _cache_file_format_resource_map)
	{
		return _cache_file_status_bad_resource_map_header;
	}
	if (identity.resource_map_type != expected_type)
	{
		return _cache_file_status_wrong_resource_map_type;
	}
	if (!source->read(source->context, 0, sizeof(header), header))
	{
		return _cache_file_status_read_failed;
	}
	names_offset = read_u32(header + RESOURCE_MAP_NAMES_OFFSET_OFFSET);
	index_offset = read_u32(header + RESOURCE_MAP_INDEX_OFFSET_OFFSET);
	names_bytes = index_offset - names_offset;
	map->type = expected_type;
	map->item_count = identity.resource_item_count;
	map->items = calloc((size_t)map->item_count + 1, sizeof(*map->items));
	map->names = malloc((size_t)names_bytes + 1);
	index = malloc((size_t)map->item_count * RESOURCE_MAP_ITEM_BYTES + 1);
	if (!map->items || !map->names || !index)
	{
		free(index);
		resource_map_close(map);
		return _cache_file_status_out_of_memory;
	}
	if (!source->read(source->context, names_offset, names_bytes, map->names) ||
		!source->read(source->context, index_offset, (uint32_t)map->item_count * RESOURCE_MAP_ITEM_BYTES, index))
	{
		free(index);
		resource_map_close(map);
		return _cache_file_status_read_failed;
	}
	map->names[names_bytes] = 0;
	for (item_index = 0; item_index < map->item_count; item_index++)
	{
		uint8_t const *entry = index + (uint32_t)item_index * RESOURCE_MAP_ITEM_BYTES;
		uint32_t name_offset = read_u32(entry + RESOURCE_MAP_ITEM_NAME_OFFSET);
		uint32_t size = read_u32(entry + RESOURCE_MAP_ITEM_SIZE_OFFSET);
		uint32_t data_offset = read_u32(entry + RESOURCE_MAP_ITEM_DATA_OFFSET);

		/* the item's data lies between the header and the names, and its
		name is terminated within the names */
		if (name_offset >= names_bytes ||
			!memchr(map->names + name_offset, 0, names_bytes - name_offset) ||
			data_offset < RESOURCE_MAP_HEADER_BYTES ||
			!range_fits(data_offset, size, names_offset))
		{
			free(index);
			resource_map_close(map);
			return _cache_file_status_bad_resource_item;
		}
		map->items[item_index].data_offset = data_offset;
		map->items[item_index].size = size;
		map->items[item_index].name = map->names + name_offset;
	}
	free(index);

	return _cache_file_status_ok;
}

void resource_map_close(
	struct resource_map *map)
{
	free(map->items);
	free(map->names);
	map->items = NULL;
	map->names = NULL;
	map->item_count = 0;

	return;
}

enum cache_file_status custom_edition_cache_load(
	struct cache_file_source *map,
	struct resource_map *const resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES],
	uint8_t *tag_cache,
	uint32_t tag_cache_bytes,
	struct custom_edition_load_report *report)
{
	struct cache_file_identity *identity = &report->identity;
	struct load_state state;
	uint8_t *tag_index;
	uint8_t *tag_instances;
	uint32_t instances_offset;
	uint32_t scenario_offset;
	uint32_t scenario_handle;
	uint32_t checksum = CRC32_INITIAL;
	uint32_t data_end;
	int32_t tag_count;
	int32_t tag_index_value;
	enum cache_file_status status;

	memset(report, 0, sizeof(*report));
	memset(&state, 0, sizeof(state));
	state.map = map;
	state.resource_maps = resource_maps;
	state.report = report;
	state.tag_cache = tag_cache;
	state.address_base = CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	state.tag_index = NO_TAG_INDEX;
	report->problem_tag_index = NO_TAG_INDEX;

	status = cache_file_identify(map, identity);
	if (status == _cache_file_status_ok &&
		identity->format != _cache_file_format_custom_edition_cache &&
		identity->format != _cache_file_format_pc_retail_cache)
	{
		status = _cache_file_status_unsupported_version;
	}
	if (status != _cache_file_status_ok)
	{
		return load_fail(&state, status, 0);
	}
	report->tag_cache_bytes = custom_edition_tag_cache_bytes(identity);
	if (tag_cache_bytes < report->tag_cache_bytes)
	{
		return load_fail(&state, _cache_file_status_out_of_memory, tag_cache_bytes);
	}
	state.file_length = identity->file_length;

	/* the tag data, at the start of the tag cache */
	if (!map->read(map->context, identity->tag_data_offset, identity->tag_data_size, tag_cache))
	{
		return load_fail(&state, _cache_file_status_read_failed, identity->tag_data_offset);
	}
	state.used_bytes = identity->tag_data_size;
	report->tag_data_bytes = identity->tag_data_size;

	/* the tag index */
	tag_index = tag_cache;
	if (read_u32(tag_index + TAG_INDEX_SIGNATURE_OFFSET) != TAG_INDEX_SIGNATURE)
	{
		return load_fail(&state, _cache_file_status_bad_tag_index_signature, identity->tag_data_offset);
	}
	tag_count = read_s32(tag_index + TAG_INDEX_COUNT_OFFSET);
	report->tag_count = tag_count;
	/* the game reads the instances as structures, which must not overlap
	the index or sit at an unaligned address */
	if (tag_count <= 0 ||
		(uint32_t)tag_count > ABSOLUTE_INDEX_MASK + 1 ||
		!tag_cache_offset(
			&state,
			read_u32(tag_index + TAG_INDEX_INSTANCES_OFFSET),
			(uint32_t)tag_count * TAG_INSTANCE_BYTES,
			&instances_offset) ||
		instances_offset < TAG_INDEX_BYTES ||
		(instances_offset & 3))
	{
		return load_fail(&state, _cache_file_status_bad_tag_instances_range, read_u32(tag_index + TAG_INDEX_INSTANCES_OFFSET));
	}
	tag_instances = tag_cache + instances_offset;

	/* the model vertex and index data (in the file; the index data offset
	counts from the vertex data) */
	state.model_data_offset = read_u32(tag_index + TAG_INDEX_VERTEX_DATA_OFFSET_OFFSET);
	state.model_index_data_offset = read_u32(tag_index + TAG_INDEX_INDEX_DATA_OFFSET_OFFSET);
	state.model_data_size = read_u32(tag_index + TAG_INDEX_MODEL_DATA_SIZE_OFFSET);
	if (state.model_data_offset < CACHE_FILE_HEADER_BYTES ||
		!range_fits(state.model_data_offset, state.model_data_size, identity->file_length) ||
		state.model_index_data_offset > state.model_data_size)
	{
		return load_fail(&state, _cache_file_status_bad_model_data_range, state.model_data_offset);
	}
	report->model_data_offset = state.model_data_offset;
	report->model_index_data_offset = state.model_index_data_offset;
	report->model_data_bytes = state.model_data_size;

	/* every tag instance, and the tags held by resource maps */
	for (tag_index_value = 0; tag_index_value < tag_count; tag_index_value++)
	{
		uint8_t const *instance = tag_instances + (uint32_t)tag_index_value * TAG_INSTANCE_BYTES;
		uint32_t group_tag = read_u32(instance + TAG_INSTANCE_GROUP_OFFSET);
		uint32_t handle = read_u32(instance + TAG_INSTANCE_HANDLE_OFFSET);
		uint32_t address = read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
		uint32_t offset;

		state.tag_index = tag_index_value;
		if ((handle & ABSOLUTE_INDEX_MASK) != (uint32_t)tag_index_value)
		{
			return load_fail(&state, _cache_file_status_bad_tag_handle, handle);
		}
		if (!tag_name_get(&state, instance))
		{
			return load_fail(&state, _cache_file_status_bad_tag_name, read_u32(instance + TAG_INSTANCE_NAME_OFFSET));
		}
		if (group_tag == PROJECT_YELLOW_GROUP_TAG || group_tag == PROJECT_YELLOW_GLOBALS_GROUP_TAG)
		{
			report->warnings |= 1UL << _custom_edition_warning_opensauce_tags_bit;
		}
		if (read_u32(instance + TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET))
		{
			if (!resource_group_layout(group_tag))
			{
				return load_fail(&state, _cache_file_status_unexpected_external_tag, group_tag);
			}
			if (group_tag == SOUND_GROUP_TAG &&
				!tag_cache_offset(&state, address, SOUND_DEFINITION_BYTES, &offset))
			{
				return load_fail(&state, _cache_file_status_bad_tag_address, address);
			}
		}
		else if (address ?
			!tag_cache_offset(&state, address, 1, &offset) :
			group_tag != STRUCTURE_BSP_GROUP_TAG)
		{
			/* structure BSPs alone have no address until they are loaded */
			return load_fail(&state, _cache_file_status_bad_tag_address, address);
		}
	}
	state.tag_index = NO_TAG_INDEX;

	/* the scenario and its structure BSPs */
	scenario_handle = read_u32(tag_index + TAG_INDEX_SCENARIO_OFFSET);
	report->scenario_tag_index = (int32_t)(scenario_handle & ABSOLUTE_INDEX_MASK);
	if ((scenario_handle & ABSOLUTE_INDEX_MASK) >= (uint32_t)tag_count ||
		read_u32(tag_instances + (scenario_handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES + TAG_INSTANCE_HANDLE_OFFSET) != scenario_handle ||
		read_u32(tag_instances + (scenario_handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES + TAG_INSTANCE_GROUP_OFFSET) != SCENARIO_GROUP_TAG ||
		read_u32(tag_instances + (scenario_handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES + TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET) ||
		!tag_cache_offset(
			&state,
			read_u32(tag_instances + (scenario_handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES + TAG_INSTANCE_ADDRESS_OFFSET),
			SCENARIO_BYTES,
			&scenario_offset))
	{
		return load_fail(&state, _cache_file_status_bad_scenario_tag, scenario_handle);
	}
	status = structure_bsps_verify(&state, tag_instances, tag_count, scenario_offset, &checksum);
	if (status != _cache_file_status_ok)
	{
		return status;
	}

	/* the header checksum covers the structure BSPs, the model data and the
	tag data as the map holds it, before anything below changes it */
	status = crc32_update_from_file(map, &checksum, state.model_data_offset, state.model_data_size);
	if (status != _cache_file_status_ok)
	{
		return load_fail(&state, status, state.model_data_offset);
	}
	checksum = crc32_update(checksum, tag_cache, identity->tag_data_size);
	report->computed_checksum = checksum;
	if (checksum != identity->checksum)
	{
		report->warnings |= 1UL << _custom_edition_warning_checksum_mismatch_bit;
	}

	/* the tags held by resource maps, placed after the tag data */
	for (tag_index_value = 0; tag_index_value < tag_count; tag_index_value++)
	{
		uint8_t *instance = tag_instances + (uint32_t)tag_index_value * TAG_INSTANCE_BYTES;
		uint32_t group_tag = read_u32(instance + TAG_INSTANCE_GROUP_OFFSET);
		char const *name;

		if (!read_u32(instance + TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET))
		{
			continue;
		}
		state.tag_index = tag_index_value;
		name = tag_name_get(&state, instance);
		if (!name)
		{
			return load_fail(&state, _cache_file_status_bad_tag_name, read_u32(instance + TAG_INSTANCE_NAME_OFFSET));
		}
		switch (group_tag)
		{
		case BITMAP_GROUP_TAG:
			status = resource_tag_load(&state, instance, name, _resource_map_bitmaps, &bitmap_group_layout);
			break;
		case SOUND_GROUP_TAG:
			status = resource_sound_load(&state, instance, name);
			break;
		case FONT_GROUP_TAG:
			status = resource_tag_load(&state, instance, name, _resource_map_locale, &font_layout);
			break;
		case UNICODE_STRING_LIST_GROUP_TAG:
			status = resource_tag_load(&state, instance, name, _resource_map_locale, &unicode_string_list_layout);
			break;
		default:
			status = resource_tag_load(&state, instance, name, _resource_map_locale, &hud_message_text_layout);
			break;
		}
		if (status != _cache_file_status_ok)
		{
			return status;
		}
	}

	/* every tag whose layout this module knows, wherever it came from: each
	address must now lie within the loaded tags, each bitmap's pixels and
	sound's samples within their file, and each model part's geometry
	within the model data */
	for (tag_index_value = 0; tag_index_value < tag_count; tag_index_value++)
	{
		uint8_t const *instance = tag_instances + (uint32_t)tag_index_value * TAG_INSTANCE_BYTES;
		struct element_layout const *layout = checked_group_layout(read_u32(instance + TAG_INSTANCE_GROUP_OFFSET));
		uint32_t offset;

		if (!layout)
		{
			continue;
		}
		state.tag_index = tag_index_value;
		if (!tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), layout->bytes, &offset))
		{
			return load_fail(&state, _cache_file_status_bad_tag_address, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET));
		}
		status = element_verify(&state, offset, layout, 0);
		if (status != _cache_file_status_ok)
		{
			return status;
		}
	}
	state.tag_index = NO_TAG_INDEX;

	/* anything after the cache data and the OpenSauce definitions */
	data_end = identity->file_length;
	if (identity->has_opensauce_header && identity->opensauce.definitions_size)
	{
		data_end = identity->opensauce.definitions_offset + identity->opensauce.definitions_size;
	}
	if (identity->file_size > data_end)
	{
		report->trailing_bytes = identity->file_size - data_end;
		report->warnings |= 1UL << _custom_edition_warning_trailing_data_bit;
	}
	report->status = _cache_file_status_ok;

	return _cache_file_status_ok;
}

/* Keep this separate from cache_load: validation/conversion operates on the
   file's original virtual addresses. The first walk checks every field; only
   then does the second walk alter the image. No arbitrary 32-bit word scan
   is safe here, because tag payloads also contain numbers in this range. */
static int rebase_address_offset(
	uint32_t address,
	uint32_t size,
	uint32_t limit,
	uint32_t *offset)
{
	if (address < CUSTOM_EDITION_TAG_CACHE_ADDRESS ||
		!range_fits(address - CUSTOM_EDITION_TAG_CACHE_ADDRESS, size, limit))
	{
		return 0;
	}
	*offset = address - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	return 1;
}

static int rebase_element_known(
	uint8_t *cache,
	uint32_t used,
	uint32_t base,
	uint32_t at,
	struct element_layout const *layout,
	int depth,
	int mutate,
	uint32_t *pointer_count)
{
	int i;
	if (depth > 4 || !range_fits(at, layout->bytes, used)) return 0;
	for (i = 0; i < layout->reference_count + layout->reference_span_count; ++i)
	{
		uint32_t field_offset = i < layout->reference_count ?
			layout->reference_offsets[i] :
			layout->reference_span_offset +
			(uint32_t)(i - layout->reference_count) * TAG_REFERENCE_BYTES;
		uint8_t *field;
		uint32_t address, offset;
		if (!range_fits(field_offset, TAG_REFERENCE_BYTES, layout->bytes)) return 0;
		field = cache + at + field_offset;
		address = read_u32(field + 4);
		if (!address) continue;
		if (!rebase_address_offset(address, 1, used, &offset) ||
			!memchr(cache + offset, 0, used - offset)) return 0;
		if (mutate) write_u32(field + 4, base + offset);
		else ++*pointer_count;
	}
	for (i = 0; i < layout->data_count; ++i)
	{
		uint8_t *field = cache + at + layout->data_offsets[i];
		int32_t size = read_s32(field + TAG_DATA_SIZE_OFFSET);
		uint32_t address = read_u32(field + TAG_DATA_ADDRESS_OFFSET), offset;
		if (size < 0 || (address &&
			!rebase_address_offset(address, (uint32_t)size, used, &offset))) return 0;
		if (address)
		{
			if (mutate) write_u32(field + TAG_DATA_ADDRESS_OFFSET, base + offset);
			else ++*pointer_count;
		}
	}
	for (i = 0; i < layout->block_count; ++i)
	{
		struct block_layout const *block = &layout->blocks[i];
		uint8_t *field = cache + at + block->offset;
		int32_t count = read_s32(field + TAG_BLOCK_COUNT_OFFSET);
		uint32_t address = read_u32(field + TAG_BLOCK_ADDRESS_OFFSET), offset;
		int32_t element;
		if (!count) continue;
		if (count < 0 || !block->element_bytes ||
			(uint32_t)count > used / block->element_bytes ||
			!rebase_address_offset(address, (uint32_t)count * block->element_bytes, used, &offset)) return 0;
		if (mutate) write_u32(field + TAG_BLOCK_ADDRESS_OFFSET, base + offset);
		else ++*pointer_count;
		for (element = 0; element < count; ++element)
		{
			if (!rebase_element_known(cache, used, base,
				offset + (uint32_t)element * block->element_bytes,
				block->element, depth + 1, mutate, pointer_count)) return 0;
		}
	}
	return 1;
}

/* The scenario's checked layout omits this special block: cache_load
   validated its entries and every reserved BSP destination separately. */
static int rebase_scenario_bsp_references(
	uint8_t *cache,
	uint32_t used,
	uint32_t reserved,
	uint32_t base,
	uint32_t scenario_offset,
	int mutate,
	uint32_t *pointer_count)
{
	uint8_t *block;
	int32_t count, i;
	uint32_t address, offset;
	if (!range_fits(scenario_offset, SCENARIO_BYTES, used)) return 0;
	block = cache + scenario_offset + SCENARIO_STRUCTURE_BSPS_OFFSET;
	count = read_s32(block + TAG_BLOCK_COUNT_OFFSET);
	address = read_u32(block + TAG_BLOCK_ADDRESS_OFFSET);
	if (count < 0 || count > MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO) return 0;
	if (!count) return 1;
	if (!rebase_address_offset(address, (uint32_t)count * STRUCTURE_BSP_REFERENCE_BYTES,
		used, &offset)) return 0;
	if (mutate) write_u32(block + TAG_BLOCK_ADDRESS_OFFSET, base + offset);
	else ++*pointer_count;
	for (i = 0; i < count; ++i)
	{
		uint8_t *reference = cache + offset + (uint32_t)i * STRUCTURE_BSP_REFERENCE_BYTES;
		int32_t size = read_s32(reference + STRUCTURE_BSP_REFERENCE_SIZE_OFFSET);
		uint32_t bsp_address = read_u32(reference + STRUCTURE_BSP_REFERENCE_ADDRESS_OFFSET);
		/* scenario_structure_bsp_reference ends with a tag_reference;
		   its name pointer is still inside the loaded tag cache. */
		uint32_t name_address = read_u32(reference + 0x14);
		uint32_t bsp_offset, name_offset;
		if (size <= 0 || !rebase_address_offset(bsp_address, (uint32_t)size,
			reserved, &bsp_offset)) return 0;
		if (name_address &&
			(!rebase_address_offset(name_address, 1, used, &name_offset) ||
			 !memchr(cache + name_offset, 0, used - name_offset))) return 0;
		if (mutate) write_u32(reference + STRUCTURE_BSP_REFERENCE_ADDRESS_OFFSET, base + bsp_offset);
		else ++*pointer_count;
		if (name_address)
		{
			if (mutate) write_u32(reference + 0x14, base + name_offset);
			else ++*pointer_count;
		}
	}
	return 1;
}

enum cache_file_status custom_edition_cache_rebase_known(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t reserved_bytes,
	uint32_t destination_base,
	struct custom_edition_rebase_report *report)
{
	uint32_t instances_offset;
	int32_t count, index;
	int pass;
	if (!report) return _cache_file_status_bad_tag_layout;
	memset(report, 0, sizeof(*report));
	report->problem_tag_index = NO_TAG_INDEX;
	if (!tag_cache || !loaded_bytes || loaded_bytes > reserved_bytes ||
		reserved_bytes > UINT32_MAX - destination_base ||
		loaded_bytes < TAG_INDEX_BYTES)
		return _cache_file_status_bad_tag_data_range;
	count = read_s32(tag_cache + TAG_INDEX_COUNT_OFFSET);
	if (count <= 0 || (uint32_t)count > ABSOLUTE_INDEX_MASK + 1 ||
		!rebase_address_offset(read_u32(tag_cache + TAG_INDEX_INSTANCES_OFFSET),
			(uint32_t)count * TAG_INSTANCE_BYTES, loaded_bytes, &instances_offset) ||
		instances_offset < TAG_INDEX_BYTES)
		return _cache_file_status_bad_tag_instances_range;
	for (pass = 0; pass < 2; ++pass)
	{
		for (index = 0; index < count; ++index)
		{
			uint8_t *instance = tag_cache + instances_offset + (uint32_t)index * TAG_INSTANCE_BYTES;
			uint32_t address = read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
			uint32_t name = read_u32(instance + TAG_INSTANCE_NAME_OFFSET);
			uint32_t group = read_u32(instance + TAG_INSTANCE_GROUP_OFFSET);
			uint32_t offset, name_offset;
			struct element_layout const *layout = checked_group_layout(group);
			if (!pass)
			{
				if (layout) ++report->known_tag_count;
				else if (group == STRUCTURE_BSP_GROUP_TAG && !address)
					++report->special_bsp_tag_count;
				else ++report->unknown_tag_count;
			}
			report->problem_tag_index = index;
			if (!rebase_address_offset(name, 1, loaded_bytes, &name_offset) ||
				!memchr(tag_cache + name_offset, 0, loaded_bytes - name_offset))
				return _cache_file_status_bad_tag_name;
			if (pass) write_u32(instance + TAG_INSTANCE_NAME_OFFSET, destination_base + name_offset);
			else ++report->pointer_count;
			if (!address)
			{
				if (group != STRUCTURE_BSP_GROUP_TAG) return _cache_file_status_bad_tag_address;
				continue;
			}
			if (!rebase_address_offset(address, layout ? layout->bytes : 1, loaded_bytes, &offset))
				return _cache_file_status_bad_tag_address;
			if (pass) write_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET, destination_base + offset);
			else ++report->pointer_count;
			if (layout && !rebase_element_known(tag_cache, loaded_bytes, destination_base,
				offset, layout, 0, pass, &report->pointer_count))
				return _cache_file_status_bad_tag_layout;
			if (group == SCENARIO_GROUP_TAG && !rebase_scenario_bsp_references(
				tag_cache, loaded_bytes, reserved_bytes, destination_base,
				offset, pass, &report->pointer_count))
				return _cache_file_status_bad_structure_bsp_block;
		}
		if (pass) write_u32(tag_cache + TAG_INDEX_INSTANCES_OFFSET, destination_base + instances_offset);
		else ++report->pointer_count;
	}
	report->problem_tag_index = NO_TAG_INDEX;
	return _cache_file_status_ok;
}

static int bsp_rebase_offset(
	uint32_t address,
	uint32_t bytes,
	uint32_t source_base,
	uint32_t bsp_bytes,
	uint32_t *offset)
{
	if (address < source_base ||
		!range_fits(address - source_base, bytes, bsp_bytes)) return 0;
	*offset = address - source_base;
	return 1;
}

/* Rebase a checked tag_block that lives within one PC BSP section. */
static int bsp_rebase_block(
	uint8_t *bsp,
	uint32_t bsp_bytes,
	uint32_t source_base,
	uint32_t destination_base,
	uint32_t block_offset,
	uint32_t element_bytes,
	int mutate,
	uint32_t *elements_offset,
	int32_t *element_count,
	uint32_t *pointer_count)
{
	uint8_t *block;
	uint32_t address;
	if (!element_bytes || !range_fits(block_offset, TAG_BLOCK_BYTES, bsp_bytes))
		return 0;
	block = bsp + block_offset;
	*element_count = read_s32(block + TAG_BLOCK_COUNT_OFFSET);
	*elements_offset = 0;
	if (*element_count < 0 || (uint32_t)*element_count > bsp_bytes / element_bytes)
		return 0;
	if (!*element_count)
		return 1;
	address = read_u32(block + TAG_BLOCK_ADDRESS_OFFSET);
	if (!bsp_rebase_offset(address, (uint32_t)*element_count * element_bytes,
		source_base, bsp_bytes, elements_offset))
		return 0;
	if (mutate)
		write_u32(block + TAG_BLOCK_ADDRESS_OFFSET,
			destination_base + *elements_offset);
	else
		++*pointer_count;
	return 1;
}

static int bsp_rebase_data(
	uint8_t *bsp,
	uint32_t bsp_bytes,
	uint32_t source_base,
	uint32_t destination_base,
	uint32_t data_offset,
	int mutate,
	uint32_t *pointer_count)
{
	int32_t size;
	uint32_t address, offset;
	if (!range_fits(data_offset, TAG_DATA_BYTES, bsp_bytes)) return 0;
	size = read_s32(bsp + data_offset + TAG_DATA_SIZE_OFFSET);
	if (size < 0 || (uint32_t)size > bsp_bytes) return 0;
	if (!size) return 1;
	address = read_u32(bsp + data_offset + TAG_DATA_ADDRESS_OFFSET);
	if (!bsp_rebase_offset(address, (uint32_t)size,
		source_base, bsp_bytes, &offset)) return 0;
	if (mutate)
		write_u32(bsp + data_offset + TAG_DATA_ADDRESS_OFFSET,
			destination_base + offset);
	else ++*pointer_count;
	return 1;
}

/* The cache loader validates the full source BSP before calling this. This
   independent pass repeats every bound it traverses, so a changed or
   untrusted buffer cannot turn a verified file offset into an ARM pointer. */
enum cache_file_status custom_edition_bsp_rebase_known(
	uint8_t *bsp,
	uint32_t bsp_bytes,
	uint32_t source_base,
	uint32_t destination_base,
	struct custom_edition_bsp_rebase_report *report)
{
	int pass;
	if (!report) return _cache_file_status_bad_structure_bsp_geometry;
	memset(report, 0, sizeof(*report));
	if (!bsp || bsp_bytes < STRUCTURE_BSP_HEADER_BYTES ||
		bsp_bytes > UINT32_MAX - source_base ||
		bsp_bytes > UINT32_MAX - destination_base ||
		read_u32(bsp + STRUCTURE_BSP_HEADER_SIGNATURE_OFFSET) != STRUCTURE_BSP_SIGNATURE ||
		read_u32(bsp + STRUCTURE_BSP_HEADER_VERTEX_BUFFERS_OFFSET) ||
		read_u32(bsp + STRUCTURE_BSP_HEADER_LIGHTMAP_VERTEX_BUFFERS_OFFSET))
		return _cache_file_status_bad_structure_bsp_header;
	for (pass = 0; pass < 2; ++pass)
	{
		uint32_t structure_offset;
		uint32_t lightmaps_offset = 0;
		uint8_t *lightmaps;
		int32_t lightmap_count;
		int32_t lightmap_index;
		if (!bsp_rebase_offset(read_u32(bsp + STRUCTURE_BSP_HEADER_BSP_OFFSET),
			STRUCTURE_BSP_BYTES, source_base, bsp_bytes, &structure_offset) ||
			structure_offset < STRUCTURE_BSP_HEADER_BYTES)
			return _cache_file_status_bad_structure_bsp_header;
		if (pass)
			write_u32(bsp + STRUCTURE_BSP_HEADER_BSP_OFFSET,
				destination_base + structure_offset);
		else ++report->pointer_count;
		/* Geometry, collision and object placement traverse these blocks
		   before any environment lightmap is drawn. */
		{
			uint32_t collision_materials_offset;
			uint32_t collision_offset;
			uint32_t leaves_offset;
			uint32_t runtime_decals_offset;
			int32_t collision_materials_count, collision_count, leaves_count;
			int32_t runtime_decals_count;
			int32_t collision_index;
			if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
				destination_base, structure_offset + 0xA4, 0x14, pass,
				&collision_materials_offset, &collision_materials_count,
				&report->pointer_count) ||
				!bsp_rebase_block(bsp, bsp_bytes, source_base,
				destination_base, structure_offset + 0xB0, 0x60, pass,
				&collision_offset, &collision_count, &report->pointer_count) ||
				!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, structure_offset + 0xE0, 0x10, pass,
					&leaves_offset, &leaves_count, &report->pointer_count) ||
				!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, structure_offset + 0x258, 0x10, pass,
					&runtime_decals_offset, &runtime_decals_count,
					&report->pointer_count))
				return _cache_file_status_bad_structure_bsp_geometry;
			/* Visibility reads surface references and the environment renderer
			   stages indices directly from the BSP surface triangle table. */
			{
				uint32_t ignored_offset;
				int32_t ignored_count;
				if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, structure_offset + 0xEC, 0x08, pass,
					&ignored_offset, &ignored_count, &report->pointer_count) ||
					!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, structure_offset + 0xF8, 0x06, pass,
						&ignored_offset, &ignored_count, &report->pointer_count))
					return _cache_file_status_bad_structure_bsp_geometry;
			}
			for (collision_index = 0; collision_index < collision_count; ++collision_index)
			{
				uint32_t at = collision_offset + (uint32_t)collision_index * 0x60;
				int block_index;
				for (block_index = 0; block_index < 8; ++block_index)
				{
					uint32_t ignored_offset;
					int32_t ignored_count;
					if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base,
						at + collision_bsp_blocks[block_index].offset,
						collision_bsp_blocks[block_index].element_bytes,
						pass, &ignored_offset, &ignored_count,
						&report->pointer_count))
						return _cache_file_status_bad_structure_bsp_geometry;
				}
			}
		}
		/* The PC BSP stores cluster and fog blocks outside the tag image.
		   These are read during the first game pulse, even in UI maps. */
		{
			static struct { uint32_t offset, stride; } const environment_blocks[] = {
				{ 0x134, 0x68 }, /* structure_cluster */
				{ 0x154, 0x40 }, /* structure_visibility_portal */
				{ 0x178, 0x20 }, /* structure_fog_plane */
				{ 0x184, 0x28 }, /* structure_fog_region */
				{ 0x190, 0x88 }, /* structure_fog_palette_entry */
				{ 0x1FC, 0x74 }, /* background sound palette */
				{ 0x208, 0x50 }, /* sound environment palette */
			};
			uint32_t block_index;
			uint32_t clusters_offset = 0;
			int32_t clusters_count = 0;
			uint32_t portals_offset = 0;
			int32_t portals_count = 0;
			uint32_t ignored_offset;
			int32_t ignored_count;
			/* Retail BSP lens flare references and their marker indices are
			   consumed by the first campaign render pass. */
			if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
				destination_base, structure_offset + 0x11C, 0x10,
				pass, &ignored_offset, &ignored_count, &report->pointer_count) ||
				!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, structure_offset + 0x128, 0x10,
					pass, &ignored_offset, &ignored_count, &report->pointer_count))
				return _cache_file_status_bad_structure_bsp_geometry;
			for (block_index = 0; block_index < sizeof(environment_blocks) / sizeof(environment_blocks[0]); ++block_index)
			{
				uint32_t elements_offset;
				int32_t elements_count;
				if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, structure_offset + environment_blocks[block_index].offset,
					environment_blocks[block_index].stride, pass, &elements_offset,
					&elements_count, &report->pointer_count))
					return _cache_file_status_bad_structure_bsp_geometry;
				if (!block_index)
				{
					clusters_offset = elements_offset;
					clusters_count = elements_count;
				}
				if (block_index == 1)
				{
					portals_offset = elements_offset;
					portals_count = elements_count;
				}
			}
			for (block_index = 0; block_index < (uint32_t)clusters_count; ++block_index)
			{
				uint32_t ignored_offset;
				int32_t ignored_count;
				uint32_t subclusters_offset;
				int32_t subclusters_count;
				uint32_t mirrors_offset;
				int32_t mirrors_count;
				if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, clusters_offset + block_index * 0x68 + 0x28,
					0x08, pass, &ignored_offset, &ignored_count,
					&report->pointer_count) ||
					!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, clusters_offset + block_index * 0x68 + 0x34,
						0x24, pass, &subclusters_offset, &subclusters_count,
						&report->pointer_count) ||
					!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, clusters_offset + block_index * 0x68 + 0x44,
						4, pass, &ignored_offset, &ignored_count,
						&report->pointer_count) ||
					!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, clusters_offset + block_index * 0x68 + 0x50,
						0x40, pass, &mirrors_offset, &mirrors_count,
						&report->pointer_count) ||
					!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, clusters_offset + block_index * 0x68 + 0x5C,
						2, pass, &ignored_offset, &ignored_count,
						&report->pointer_count))
					return _cache_file_status_bad_structure_bsp_geometry;
				{
				uint32_t child_index;
				for (child_index = 0; child_index < (uint32_t)subclusters_count; ++child_index)
					if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
							destination_base, subclusters_offset + child_index * 0x24 + 0x18,
							4, pass, &ignored_offset, &ignored_count,
							&report->pointer_count))
						return _cache_file_status_bad_structure_bsp_geometry;
				for (child_index = 0; child_index < (uint32_t)mirrors_count; ++child_index)
					if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
							destination_base, mirrors_offset + child_index * 0x40 + 0x34,
							0x0C, pass, &ignored_offset, &ignored_count,
							&report->pointer_count))
						return _cache_file_status_bad_structure_bsp_geometry;
			}
			}
			for (block_index = 0; block_index < (uint32_t)portals_count; ++block_index)
			{
				uint32_t ignored_offset;
				int32_t ignored_count;
				if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
					destination_base, portals_offset + block_index * 0x40 + 0x34,
					0x0C, pass, &ignored_offset, &ignored_count,
					&report->pointer_count))
					return _cache_file_status_bad_structure_bsp_geometry;
			}
		}
		if (!bsp_rebase_data(bsp, bsp_bytes, source_base,
			destination_base, structure_offset + 0x140, pass, &report->pointer_count) ||
			!bsp_rebase_data(bsp, bsp_bytes, source_base,
				destination_base, structure_offset + 0x214, pass, &report->pointer_count))
			return _cache_file_status_bad_structure_bsp_geometry;
		{
			uint32_t details_offset;
			int32_t details_count;
			int32_t detail_index;
			static struct { uint32_t offset, stride; } const detail_blocks[] = {
				{ 0x00, 0x20 }, { 0x0C, 0x06 },
				{ 0x18, 0x02 }, { 0x24, 0x10 }
			};
			if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
				destination_base, structure_offset + 0x24C, 0x40, pass,
				&details_offset, &details_count, &report->pointer_count))
				return _cache_file_status_bad_structure_bsp_geometry;
			for (detail_index = 0; detail_index < details_count; ++detail_index)
			{
				uint32_t block_index;
				for (block_index = 0; block_index < sizeof(detail_blocks) / sizeof(detail_blocks[0]); ++block_index)
				{
					uint32_t ignored_offset;
					int32_t ignored_count;
					if (!bsp_rebase_block(bsp, bsp_bytes, source_base,
						destination_base, details_offset + (uint32_t)detail_index * 0x40 +
							detail_blocks[block_index].offset, detail_blocks[block_index].stride,
						pass, &ignored_offset, &ignored_count, &report->pointer_count))
						return _cache_file_status_bad_structure_bsp_geometry;
				}
			}
		}
		lightmaps = bsp + structure_offset + STRUCTURE_BSP_LIGHTMAPS_OFFSET;
		lightmap_count = read_s32(lightmaps + TAG_BLOCK_COUNT_OFFSET);
		if (lightmap_count < 0 ||
			(uint32_t)lightmap_count > bsp_bytes / STRUCTURE_BSP_LIGHTMAP_BYTES ||
			(lightmap_count &&
			!bsp_rebase_offset(read_u32(lightmaps + TAG_BLOCK_ADDRESS_OFFSET),
				(uint32_t)lightmap_count * STRUCTURE_BSP_LIGHTMAP_BYTES,
				source_base, bsp_bytes, &lightmaps_offset)))
			return _cache_file_status_bad_structure_bsp_geometry;
		if (!pass) report->lightmap_count = (uint32_t)lightmap_count;
		if (lightmap_count)
		{
			if (pass)
				write_u32(lightmaps + TAG_BLOCK_ADDRESS_OFFSET,
					destination_base + lightmaps_offset);
			else ++report->pointer_count;
		}
		for (lightmap_index = 0; lightmap_index < lightmap_count; ++lightmap_index)
		{
			uint8_t *lightmap = bsp + lightmaps_offset +
				(uint32_t)lightmap_index * STRUCTURE_BSP_LIGHTMAP_BYTES;
			uint8_t *materials = lightmap + STRUCTURE_BSP_LIGHTMAP_MATERIALS_OFFSET;
			uint32_t materials_offset = 0;
			int32_t material_count = read_s32(materials + TAG_BLOCK_COUNT_OFFSET);
			int32_t material_index;
			int has_bitmap = read_s16(lightmap + STRUCTURE_BSP_LIGHTMAP_BITMAP_INDEX_OFFSET) != -1;
			if (material_count < 0 ||
				(uint32_t)material_count > bsp_bytes / STRUCTURE_BSP_MATERIAL_BYTES ||
				(material_count &&
				!bsp_rebase_offset(read_u32(materials + TAG_BLOCK_ADDRESS_OFFSET),
					(uint32_t)material_count * STRUCTURE_BSP_MATERIAL_BYTES,
					source_base, bsp_bytes, &materials_offset)))
				return _cache_file_status_bad_structure_bsp_geometry;
			if (!pass) report->material_count += (uint32_t)material_count;
			if (material_count)
			{
				if (pass)
					write_u32(materials + TAG_BLOCK_ADDRESS_OFFSET,
						destination_base + materials_offset);
				else ++report->pointer_count;
			}
			for (material_index = 0; material_index < material_count; ++material_index)
			{
				uint8_t *material = bsp + materials_offset +
					(uint32_t)material_index * STRUCTURE_BSP_MATERIAL_BYTES;
				uint8_t *vertices = material + STRUCTURE_BSP_MATERIAL_UNCOMPRESSED_VERTICES_OFFSET;
				int32_t vertex_count = read_s32(material + STRUCTURE_BSP_MATERIAL_VERTEX_COUNT_OFFSET);
				int32_t lightmap_vertex_count = read_s32(material + STRUCTURE_BSP_MATERIAL_LIGHTMAP_VERTEX_COUNT_OFFSET);
				int32_t vertex_bytes = read_s32(vertices + TAG_DATA_SIZE_OFFSET);
				uint32_t vertex_offset;
				if (read_s16(material + STRUCTURE_BSP_MATERIAL_VERTEX_TYPE_OFFSET) !=
					VERTEX_TYPE_ENVIRONMENT_UNCOMPRESSED ||
					vertex_count < 0 || vertex_count > MAXIMUM_VERTICES_PER_BUFFER ||
					lightmap_vertex_count != (has_bitmap ? vertex_count : 0) ||
					vertex_bytes != vertex_count * STRUCTURE_BSP_ENVIRONMENT_VERTEX_BYTES +
						lightmap_vertex_count * STRUCTURE_BSP_LIGHTMAP_VERTEX_BYTES ||
					(vertex_bytes &&
					!bsp_rebase_offset(read_u32(vertices + TAG_DATA_ADDRESS_OFFSET),
						(uint32_t)vertex_bytes, source_base, bsp_bytes, &vertex_offset)))
					return _cache_file_status_bad_structure_bsp_geometry;
				if (vertex_bytes)
				{
					if (pass)
						write_u32(vertices + TAG_DATA_ADDRESS_OFFSET,
							destination_base + vertex_offset);
					else
					{
						++report->pointer_count;
						report->vertex_bytes += (uint32_t)vertex_bytes;
					}
				}
			}
		}
	}
	return _cache_file_status_ok;
}

/* The tag instances of a tag cache custom_edition_cache_load filled (the
state's tag_cache and used_bytes), and their count, or NULL. The walks of the
public functions below take that image as checked, but still check every
block they follow. */
static uint8_t *loaded_tag_instances(
	struct load_state const *state,
	int32_t *tag_count)
{
	uint32_t instances_offset;

	if (state->used_bytes < TAG_INDEX_BYTES)
	{
		return NULL;
	}
	*tag_count = read_s32(state->tag_cache + TAG_INDEX_COUNT_OFFSET);
	if (*tag_count <= 0 ||
		(uint32_t)*tag_count > ABSOLUTE_INDEX_MASK + 1 ||
		!tag_cache_offset(
			state,
			read_u32(state->tag_cache + TAG_INDEX_INSTANCES_OFFSET),
			(uint32_t)*tag_count * TAG_INSTANCE_BYTES,
			&instances_offset))
	{
		return NULL;
	}

	return state->tag_cache + instances_offset;
}

static void loaded_state_initialize(
	struct load_state *state,
	uint8_t *tag_cache,
	uint32_t loaded_bytes)
{
	memset(state, 0, sizeof(*state));
	state->tag_cache = tag_cache;
	state->used_bytes = loaded_bytes;
	state->address_base = CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	if (loaded_bytes >= TAG_INDEX_BYTES)
	{
		uint32_t instances = read_u32(tag_cache + TAG_INDEX_INSTANCES_OFFSET);
		/* The tag instance array follows the 0x28-byte index header. The
		   original PC address is explicit; after the checked rebase the
		   same field names the destination base plus 0x28. */
		if (instances >= TAG_INDEX_BYTES &&
			(instances < CUSTOM_EDITION_TAG_CACHE_ADDRESS ||
			instances - CUSTOM_EDITION_TAG_CACHE_ADDRESS >= loaded_bytes))
			state->address_base = instances - TAG_INDEX_BYTES;
	}
	state->tag_index = NO_TAG_INDEX;

	return;
}

void *custom_edition_cache_tag_next(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t group_tag,
	uint32_t definition_bytes,
	int32_t *tag_index)
{
	struct load_state state;
	uint8_t const *instances;
	int32_t tag_count;
	int32_t index;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances || *tag_index >= tag_count)
	{
		return NULL;
	}
	for (index = *tag_index < 0 ? 0 : *tag_index + 1; index < tag_count; index++)
	{
		uint8_t const *instance = instances + (uint32_t)index * TAG_INSTANCE_BYTES;
		uint32_t offset;

		if (read_u32(instance + TAG_INSTANCE_GROUP_OFFSET) == group_tag &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), definition_bytes, &offset))
		{
			*tag_index = index;
			return tag_cache + offset;
		}
	}
	*tag_index = tag_count;

	return NULL;
}

void *custom_edition_cache_tag_get(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t handle,
	uint32_t group_tag,
	uint32_t definition_bytes)
{
	struct load_state state;
	uint8_t const *instances;
	uint8_t const *instance;
	uint32_t offset;
	int32_t tag_count;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances || (handle & ABSOLUTE_INDEX_MASK) >= (uint32_t)tag_count)
	{
		return NULL;
	}
	instance = instances + (handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES;

	return read_u32(instance + TAG_INSTANCE_HANDLE_OFFSET) == handle &&
		read_u32(instance + TAG_INSTANCE_GROUP_OFFSET) == group_tag &&
		tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), definition_bytes, &offset) ?
		tag_cache + offset :
		NULL;
}

void *custom_edition_cache_block_element(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	void const *block,
	int32_t element_index,
	uint32_t element_bytes)
{
	struct load_state state;
	int32_t element_count;
	uint32_t elements_offset;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	if ((uint8_t const *)block < tag_cache ||
		!range_fits((uint32_t)((uint8_t const *)block - tag_cache), TAG_BLOCK_BYTES, loaded_bytes) ||
		element_bytes == 0 ||
		!loaded_block_get(block, state.address_base, loaded_bytes, element_bytes, &element_count, &elements_offset) ||
		element_index < 0 ||
		element_index >= element_count)
	{
		return NULL;
	}

	return tag_cache + elements_offset + (uint32_t)element_index * element_bytes;
}

void *custom_edition_cache_data_get(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	void const *data,
	uint32_t *size)
{
	struct load_state state;
	uint8_t const *field = data;
	int32_t data_size;
	uint32_t data_offset;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	if (field < tag_cache ||
		!range_fits((uint32_t)(field - tag_cache), TAG_DATA_BYTES, loaded_bytes))
	{
		return NULL;
	}
	data_size = read_s32(field + TAG_DATA_SIZE_OFFSET);
	if (data_size <= 0 ||
		!tag_cache_offset(&state, read_u32(field + TAG_DATA_ADDRESS_OFFSET), (uint32_t)data_size, &data_offset))
	{
		return NULL;
	}
	*size = (uint32_t)data_size;

	return tag_cache + data_offset;
}

char const *custom_edition_cache_tag_name(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	int32_t tag_index)
{
	struct load_state state;
	uint8_t const *instances;
	uint32_t name_offset;
	int32_t tag_count;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances ||
		tag_index < 0 ||
		tag_index >= tag_count ||
		!tag_cache_offset(
			&state,
			read_u32(instances + (uint32_t)tag_index * TAG_INSTANCE_BYTES + TAG_INSTANCE_NAME_OFFSET),
			1,
			&name_offset) ||
		!memchr(tag_cache + name_offset, 0, loaded_bytes - name_offset))
	{
		return "<no name>";
	}

	return (char const *)tag_cache + name_offset;
}

int32_t custom_edition_cache_tags_regroup(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t group_tag,
	uint32_t new_group_tag)
{
	struct load_state state;
	uint8_t *instances;
	int32_t tag_count;
	int32_t tag_index;
	int32_t regrouped_count = 0;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances)
	{
		return 0;
	}
	for (tag_index = 0; tag_index < tag_count; tag_index++)
	{
		uint8_t *instance = instances + (uint32_t)tag_index * TAG_INSTANCE_BYTES;

		if (read_u32(instance + TAG_INSTANCE_GROUP_OFFSET) == group_tag)
		{
			write_u32(instance + TAG_INSTANCE_GROUP_OFFSET, new_group_tag);
			regrouped_count++;
		}
	}

	return regrouped_count;
}

static struct shader_group_type const *shader_group_type_get(
	uint32_t group_tag)
{
	size_t index;

	for (index = 0; index < sizeof(shader_group_types) / sizeof(shader_group_types[0]); index++)
	{
		if (shader_group_types[index].group_tag == group_tag)
		{
			return &shader_group_types[index];
		}
	}

	return NULL;
}

/* Makes the chicago extended shader `shader` a chicago shader: it keeps its
four-stage maps, or its two-stage maps when it has no four-stage ones (this
build's renderer draws four stages), and its extra flags move to where a
chicago shader has them. The two-stage maps are left unreferenced. */
static void transparent_chicago_extended_convert(
	uint8_t *shader)
{
	uint8_t two_stage_maps[TAG_BLOCK_BYTES];
	uint32_t extra_flags = read_u32(shader + TRANSPARENT_CHICAGO_EXTENDED_EXTRA_FLAGS_OFFSET);

	memcpy(two_stage_maps, shader + TRANSPARENT_CHICAGO_EXTENDED_TWO_STAGE_MAPS_OFFSET, TAG_BLOCK_BYTES);
	if (!read_s32(shader + TRANSPARENT_CHICAGO_MAPS_OFFSET + TAG_BLOCK_COUNT_OFFSET))
	{
		memcpy(shader + TRANSPARENT_CHICAGO_MAPS_OFFSET, two_stage_maps, TAG_BLOCK_BYTES);
	}
	write_u32(shader + TRANSPARENT_CHICAGO_EXTRA_FLAGS_OFFSET, extra_flags);
	/* the rest of a chicago shader is padding */
	memset(
		shader + TRANSPARENT_CHICAGO_EXTRA_FLAGS_OFFSET + 4,
		0,
		TRANSPARENT_CHICAGO_BYTES - (TRANSPARENT_CHICAGO_EXTRA_FLAGS_OFFSET + 4));

	return;
}

/* Gives every bitmap of the bitmap tag at `group_offset` (tag `handle`) the
state the game expects of a bitmap it has not drawn yet: its tag is its own
(bitmaps.map holds the handles of whatever map it was built with), and it
has no texture cache block, hardware texture or pixels. */
static void bitmaps_prepare(
	struct load_state const *state,
	uint32_t group_offset,
	uint32_t handle,
	struct custom_edition_conversion_report *report)
{
	int32_t bitmap_count;
	uint32_t bitmaps_offset;
	int32_t bitmap_index;

	if (!loaded_block_get(
		state->tag_cache + group_offset + BITMAP_GROUP_BITMAPS_OFFSET,
		CUSTOM_EDITION_TAG_CACHE_ADDRESS,
		state->used_bytes,
		BITMAP_DATA_BYTES,
		&bitmap_count,
		&bitmaps_offset))
	{
		return;
	}
	for (bitmap_index = 0; bitmap_index < bitmap_count; bitmap_index++)
	{
		uint8_t *bitmap = state->tag_cache + bitmaps_offset + (uint32_t)bitmap_index * BITMAP_DATA_BYTES;

		write_u32(bitmap + BITMAP_DATA_TAG_INDEX_OFFSET, handle);
		write_u32(bitmap + BITMAP_DATA_CACHE_BLOCK_INDEX_OFFSET, (uint32_t)NO_TAG_INDEX);
		write_u32(bitmap + BITMAP_DATA_HARDWARE_FORMAT_OFFSET, 0);
		write_u32(bitmap + BITMAP_DATA_BASE_ADDRESS_OFFSET, 0);
		report->bitmaps_prepared++;
	}

	return;
}

/* Gives every permutation of the sound at `sound_offset` (tag `handle`)
the state the game expects of one it has not played: its tag is its own
(sounds.map holds the handles of whatever map it was built with), with no
cache block or samples. A sound whose compression this build cannot decode
is made unplayable: this build plays Xbox ADPCM (and refuses uncompressed
sounds), and Custom Edition also has Ogg Vorbis, which would be decoded as
ADPCM noise. With no pitch ranges the game neither plays nor loads it
(sound_manager.c, sound_definition_is_playable). */
static void sound_prepare(
	struct load_state const *state,
	uint32_t sound_offset,
	uint32_t handle,
	struct custom_edition_conversion_report *report)
{
	uint8_t *sound = state->tag_cache + sound_offset;
	int16_t compression = read_s16(sound + SOUND_COMPRESSION_OFFSET);
	int decodable = compression == SOUND_COMPRESSION_NONE || compression == SOUND_COMPRESSION_XBOX_ADPCM;
	int32_t pitch_range_count;
	uint32_t pitch_ranges_offset;
	int32_t pitch_range_index;

	if (!loaded_block_get(
		sound + SOUND_PITCH_RANGES_OFFSET,
		CUSTOM_EDITION_TAG_CACHE_ADDRESS,
		state->used_bytes,
		SOUND_PITCH_RANGE_BYTES,
		&pitch_range_count,
		&pitch_ranges_offset))
	{
		return;
	}
	for (pitch_range_index = 0; pitch_range_index < pitch_range_count; pitch_range_index++)
	{
		uint8_t const *pitch_range = state->tag_cache + pitch_ranges_offset + (uint32_t)pitch_range_index * SOUND_PITCH_RANGE_BYTES;
		int32_t permutation_count;
		uint32_t permutations_offset;
		int32_t permutation_index;

		if (!loaded_block_get(
			pitch_range + SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET,
			CUSTOM_EDITION_TAG_CACHE_ADDRESS,
			state->used_bytes,
			SOUND_PERMUTATION_BYTES,
			&permutation_count,
			&permutations_offset))
		{
			continue;
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
		{
			uint8_t *permutation = state->tag_cache + permutations_offset + (uint32_t)permutation_index * SOUND_PERMUTATION_BYTES;
			int16_t permutation_compression = read_s16(permutation + SOUND_PERMUTATION_COMPRESSION_OFFSET);

			write_u32(permutation + SOUND_PERMUTATION_CACHE_BLOCK_INDEX_OFFSET, (uint32_t)NO_TAG_INDEX);
			write_u32(permutation + SOUND_PERMUTATION_CACHE_BASE_ADDRESS_OFFSET, 0);
			write_u32(permutation + SOUND_PERMUTATION_CACHE_TAG_INDEX_OFFSET, handle);
			write_u32(permutation + SOUND_PERMUTATION_RUNTIME_TAG_INDEX_OFFSET, handle);
			if (permutation_compression != SOUND_COMPRESSION_NONE && permutation_compression != SOUND_COMPRESSION_XBOX_ADPCM)
			{
				decodable = 0;
			}
		}
	}
	if (!decodable)
	{
		write_u32(sound + SOUND_PITCH_RANGES_OFFSET + TAG_BLOCK_COUNT_OFFSET, 0);
		report->sounds_undecodable++;
	}

	return;
}

/* Object overlays of the animation graph at `graph_offset` that name an
animation the graph does not have are made to name none, which the game
skips (objects.c, object_compute_node_matrices). Maps built with the editing
kit can have them (beavercreek_halo3.yelo has two); Custom Edition reads
past the graph's animations there, and this build asserts. */
static void animation_graph_overlays_repair(
	struct load_state const *state,
	uint32_t graph_offset,
	struct custom_edition_conversion_report *report)
{
	uint8_t const *graph = state->tag_cache + graph_offset;
	int32_t animation_count = read_s32(graph + ANIMATION_GRAPH_ANIMATIONS_OFFSET + TAG_BLOCK_COUNT_OFFSET);
	int32_t overlay_count;
	uint32_t overlays_offset;
	int32_t overlay_index;

	if (!loaded_block_get(
		graph + ANIMATION_GRAPH_OBJECT_OVERLAYS_OFFSET,
		CUSTOM_EDITION_TAG_CACHE_ADDRESS,
		state->used_bytes,
		ANIMATION_GRAPH_OBJECT_OVERLAY_BYTES,
		&overlay_count,
		&overlays_offset))
	{
		return;
	}
	for (overlay_index = 0; overlay_index < overlay_count; overlay_index++)
	{
		uint8_t *overlay = state->tag_cache + overlays_offset + (uint32_t)overlay_index * ANIMATION_GRAPH_OBJECT_OVERLAY_BYTES;
		int16_t animation_index = read_s16(overlay + ANIMATION_GRAPH_OBJECT_OVERLAY_ANIMATION_INDEX_OFFSET);

		if (animation_index != NO_BLOCK_INDEX && (animation_index < 0 || animation_index >= animation_count))
		{
			write_u16(overlay + ANIMATION_GRAPH_OBJECT_OVERLAY_ANIMATION_INDEX_OFFSET, (uint16_t)NO_BLOCK_INDEX);
			report->animation_overlays_disabled++;
		}
	}

	return;
}

/* This build's hs_allocate takes the scenario's syntax nodes only when they
are its own number, and otherwise frees them as if they had been allocated:
an upgraded array whose nodes in use fit in this build's number is made
that number. */
static enum cache_file_status scenario_script_nodes_convert(
	struct load_state const *state,
	uint8_t const *instances,
	int32_t tag_count,
	struct custom_edition_conversion_report *report)
{
	uint32_t scenario_handle = read_u32(state->tag_cache + TAG_INDEX_SCENARIO_OFFSET);
	uint32_t stock_bytes = DATA_ARRAY_HEADER_BYTES + MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO * HS_SYNTAX_NODE_BYTES;
	uint32_t upgraded_bytes = DATA_ARRAY_HEADER_BYTES + MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO_UPGRADED * HS_SYNTAX_NODE_BYTES;
	uint8_t *syntax_data;
	uint8_t *nodes;
	uint32_t scenario_offset;
	uint32_t nodes_offset;
	int32_t size;

	/* custom_edition_cache_load checked the scenario */
	if ((scenario_handle & ABSOLUTE_INDEX_MASK) >= (uint32_t)tag_count ||
		!tag_cache_offset(
			state,
			read_u32(instances + (scenario_handle & ABSOLUTE_INDEX_MASK) * TAG_INSTANCE_BYTES + TAG_INSTANCE_ADDRESS_OFFSET),
			SCENARIO_BYTES,
			&scenario_offset))
	{
		return _cache_file_status_bad_scenario_tag;
	}
	syntax_data = state->tag_cache + scenario_offset + SCENARIO_HS_SYNTAX_DATA_OFFSET;
	size = read_s32(syntax_data + TAG_DATA_SIZE_OFFSET);
	if (size < 0 || (uint32_t)size != upgraded_bytes)
	{
		return _cache_file_status_ok;
	}
	report->problem_tag_index = (int32_t)(scenario_handle & ABSOLUTE_INDEX_MASK);
	if (!tag_cache_offset(state, read_u32(syntax_data + TAG_DATA_ADDRESS_OFFSET), (uint32_t)size, &nodes_offset))
	{
		return _cache_file_status_bad_script_nodes;
	}
	nodes = state->tag_cache + nodes_offset;
	if (read_s16(nodes + DATA_ARRAY_MAXIMUM_COUNT_OFFSET) != MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO_UPGRADED ||
		read_s16(nodes + DATA_ARRAY_ELEMENT_BYTES_OFFSET) != HS_SYNTAX_NODE_BYTES ||
		read_s16(nodes + DATA_ARRAY_COUNT_OFFSET) > MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO ||
		read_s16(nodes + DATA_ARRAY_FIRST_FREE_INDEX_OFFSET) > MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO)
	{
		return _cache_file_status_bad_script_nodes;
	}
	report->problem_tag_index = NO_TAG_INDEX;
	write_u16(nodes + DATA_ARRAY_MAXIMUM_COUNT_OFFSET, MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO);
	write_u32(syntax_data + TAG_DATA_SIZE_OFFSET, stock_bytes);
	report->script_nodes_reduced = 1;

	return _cache_file_status_ok;
}

/* Halo PC draws a HUD element whose placement has the high resolution scale
flag at half the size of its bitmap, and Custom Edition's HUD bitmaps are
made for that: in Blood Gulch every flagged element draws a bitmap twice the
size of the one its Xbox counterpart draws, and every other element one of
the same size (port/n3ds/pc/FORMAT-REFERENCE.md). This build ignores the flag
(hud_draw.c draws a bitmap at its size times the placement's scale), so the
scale takes it in. */
static void hud_placement_convert(
	uint8_t *placement,
	struct custom_edition_conversion_report *report)
{
	uint16_t flags = read_u16(placement + HUD_PLACEMENT_SCALING_FLAGS_OFFSET);

	if (flag_is_set(flags, HUD_SCALING_USE_HIGH_RESOLUTION_SCALE_BIT))
	{
		write_f32(placement + HUD_PLACEMENT_WIDTH_SCALE_OFFSET, read_f32(placement + HUD_PLACEMENT_WIDTH_SCALE_OFFSET) * 0.5f);
		write_f32(placement + HUD_PLACEMENT_HEIGHT_SCALE_OFFSET, read_f32(placement + HUD_PLACEMENT_HEIGHT_SCALE_OFFSET) * 0.5f);
		write_u16(
			placement + HUD_PLACEMENT_SCALING_FLAGS_OFFSET,
			(uint16_t)(flags & ~(1U << HUD_SCALING_USE_HIGH_RESOLUTION_SCALE_BIT)));
		report->hud_placements_rescaled++;
	}

	return;
}

/* the placement at `placement_offset` in each element of the block at
`block`, when the block lies within the tag cache */
static void hud_placement_block_convert(
	struct load_state const *state,
	uint8_t const *block,
	uint32_t element_bytes,
	uint32_t placement_offset,
	struct custom_edition_conversion_report *report)
{
	int32_t element_count;
	uint32_t elements_offset;
	int32_t element_index;

	if (!loaded_block_get(block, CUSTOM_EDITION_TAG_CACHE_ADDRESS, state->used_bytes, element_bytes, &element_count, &elements_offset))
	{
		return;
	}
	for (element_index = 0; element_index < element_count; element_index++)
	{
		hud_placement_convert(
			state->tag_cache + elements_offset + (uint32_t)element_index * element_bytes + placement_offset,
			report);
	}

	return;
}

/* the placements of the items of each weapon HUD crosshair or overlay in
the block at `block` */
static void weapon_hud_items_convert(
	struct load_state const *state,
	uint8_t const *block,
	uint32_t item_bytes,
	struct custom_edition_conversion_report *report)
{
	int32_t element_count;
	uint32_t elements_offset;
	int32_t element_index;

	if (!loaded_block_get(
		block,
		CUSTOM_EDITION_TAG_CACHE_ADDRESS,
		state->used_bytes,
		WEAPON_HUD_CROSSHAIRS_OR_OVERLAYS_BYTES,
		&element_count,
		&elements_offset))
	{
		return;
	}
	for (element_index = 0; element_index < element_count; element_index++)
	{
		uint8_t const *element = state->tag_cache + elements_offset +
			(uint32_t)element_index * WEAPON_HUD_CROSSHAIRS_OR_OVERLAYS_BYTES;

		hud_placement_block_convert(state, element + WEAPON_HUD_ITEMS_OFFSET, item_bytes, 0, report);
	}

	return;
}

/* the size of a HUD definition whose placements hud_placements_convert
converts, or 0 for any other group */
static uint32_t hud_definition_bytes(
	uint32_t group_tag)
{
	switch (group_tag)
	{
	case UNIT_HUD_INTERFACE_GROUP_TAG:
		return UNIT_HUD_INTERFACE_BYTES;
	case WEAPON_HUD_INTERFACE_GROUP_TAG:
		return WEAPON_HUD_INTERFACE_BYTES;
	case GRENADE_HUD_INTERFACE_GROUP_TAG:
		return GRENADE_HUD_INTERFACE_BYTES;
	case HUD_GLOBALS_GROUP_TAG:
		return HUD_GLOBALS_BYTES;
	default:
		return 0;
	}
}

/* every placement of the HUD definition of group `group_tag` at `offset` */
static void hud_placements_convert(
	struct load_state const *state,
	uint32_t group_tag,
	uint32_t offset,
	struct custom_edition_conversion_report *report)
{
	uint8_t *definition = state->tag_cache + offset;
	size_t placement_index;

	switch (group_tag)
	{
	case UNIT_HUD_INTERFACE_GROUP_TAG:
		for (placement_index = 0;
			placement_index < sizeof(unit_hud_interface_placements) / sizeof(unit_hud_interface_placements[0]);
			placement_index++)
		{
			hud_placement_convert(definition + unit_hud_interface_placements[placement_index], report);
		}
		hud_placement_block_convert(
			state,
			definition + UNIT_HUD_INTERFACE_AUXILIARY_OVERLAYS_OFFSET,
			UNIT_HUD_INTERFACE_AUXILIARY_OVERLAY_BYTES,
			0,
			report);
		hud_placement_block_convert(
			state,
			definition + UNIT_HUD_INTERFACE_AUXILIARY_METERS_OFFSET,
			UNIT_HUD_INTERFACE_AUXILIARY_METER_BYTES,
			UNIT_HUD_INTERFACE_AUXILIARY_METER_BACKGROUND_OFFSET,
			report);
		hud_placement_block_convert(
			state,
			definition + UNIT_HUD_INTERFACE_AUXILIARY_METERS_OFFSET,
			UNIT_HUD_INTERFACE_AUXILIARY_METER_BYTES,
			UNIT_HUD_INTERFACE_AUXILIARY_METER_METER_OFFSET,
			report);
		break;
	case WEAPON_HUD_INTERFACE_GROUP_TAG:
		hud_placement_block_convert(
			state,
			definition + WEAPON_HUD_INTERFACE_STATICS_OFFSET,
			WEAPON_HUD_STATIC_OR_METER_BYTES,
			WEAPON_HUD_ELEMENT_PLACEMENT_OFFSET,
			report);
		hud_placement_block_convert(
			state,
			definition + WEAPON_HUD_INTERFACE_METERS_OFFSET,
			WEAPON_HUD_STATIC_OR_METER_BYTES,
			WEAPON_HUD_ELEMENT_PLACEMENT_OFFSET,
			report);
		hud_placement_block_convert(
			state,
			definition + WEAPON_HUD_INTERFACE_NUMBERS_OFFSET,
			WEAPON_HUD_NUMBER_BYTES,
			WEAPON_HUD_ELEMENT_PLACEMENT_OFFSET,
			report);
		weapon_hud_items_convert(state, definition + WEAPON_HUD_INTERFACE_CROSSHAIRS_OFFSET, WEAPON_HUD_CROSSHAIR_ITEM_BYTES, report);
		weapon_hud_items_convert(state, definition + WEAPON_HUD_INTERFACE_OVERLAYS_OFFSET, WEAPON_HUD_OVERLAY_ITEM_BYTES, report);
		break;
	case GRENADE_HUD_INTERFACE_GROUP_TAG:
		for (placement_index = 0;
			placement_index < sizeof(grenade_hud_interface_placements) / sizeof(grenade_hud_interface_placements[0]);
			placement_index++)
		{
			hud_placement_convert(definition + grenade_hud_interface_placements[placement_index], report);
		}
		hud_placement_block_convert(
			state,
			definition + GRENADE_HUD_INTERFACE_OVERLAY_ITEMS_OFFSET,
			WEAPON_HUD_OVERLAY_ITEM_BYTES,
			0,
			report);
		break;
	case HUD_GLOBALS_GROUP_TAG:
		hud_placement_convert(definition + HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET, report);
		break;
	default:
		break;
	}

	return;
}

/* Halo PC's hint that a key shows the score names the key where it reads
"%s" in quotes. This build copies the hint as it is, so the Xbox button it
reads for the score takes the place of the placeholder and its quotes: BACK,
which the native builds read from F1 and the controller's Back button
(port/linux/src/xinput_sdl.c). */
static void multiplayer_score_hint_convert(
	struct load_state const *state,
	uint32_t string_list_offset,
	struct custom_edition_conversion_report *report)
{
	int32_t string_count;
	uint32_t strings_offset;
	uint8_t const *string;
	int32_t string_bytes;
	uint32_t text_offset;
	uint32_t character_index;

	if (!loaded_block_get(
			state->tag_cache + string_list_offset + UNICODE_STRING_LIST_STRINGS_OFFSET,
			CUSTOM_EDITION_TAG_CACHE_ADDRESS,
			state->used_bytes,
			TAG_DATA_BYTES,
			&string_count,
			&strings_offset) ||
		string_count <= MULTIPLAYER_GAME_TEXT_SCORE_HINT_INDEX)
	{
		return;
	}
	string = state->tag_cache + strings_offset + MULTIPLAYER_GAME_TEXT_SCORE_HINT_INDEX * TAG_DATA_BYTES;
	string_bytes = read_s32(string + TAG_DATA_SIZE_OFFSET);
	if (string_bytes <= 0 ||
		!tag_cache_offset(state, read_u32(string + TAG_DATA_ADDRESS_OFFSET), (uint32_t)string_bytes, &text_offset))
	{
		return;
	}
	for (character_index = 0;
		character_index + SCORE_KEY_PLACEHOLDER_CHARACTERS <= (uint32_t)string_bytes / sizeof(uint16_t);
		character_index++)
	{
		uint8_t *text = state->tag_cache + text_offset + character_index * sizeof(uint16_t);
		int placeholder_index = 0;

		while (placeholder_index < SCORE_KEY_PLACEHOLDER_CHARACTERS &&
			read_u16(text + placeholder_index * sizeof(uint16_t)) == score_key_placeholder[placeholder_index])
		{
			placeholder_index++;
		}
		if (placeholder_index == SCORE_KEY_PLACEHOLDER_CHARACTERS)
		{
			for (placeholder_index = 0; placeholder_index < SCORE_KEY_PLACEHOLDER_CHARACTERS; placeholder_index++)
			{
				write_u16(text + placeholder_index * sizeof(uint16_t), score_button_name[placeholder_index]);
			}
			report->score_hint_converted = 1;
			return;
		}
	}

	return;
}

enum cache_file_status custom_edition_cache_convert(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	struct custom_edition_conversion_report *report)
{
	struct load_state state;
	uint8_t const *instances;
	int32_t tag_count;
	int32_t tag_index;

	memset(report, 0, sizeof(*report));
	report->problem_tag_index = NO_TAG_INDEX;
	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances)
	{
		return _cache_file_status_bad_tag_instances_range;
	}
	for (tag_index = 0; tag_index < tag_count; tag_index++)
	{
		uint8_t const *instance = instances + (uint32_t)tag_index * TAG_INSTANCE_BYTES;
		uint32_t group_tag = read_u32(instance + TAG_INSTANCE_GROUP_OFFSET);
		struct shader_group_type const *shader_type = shader_group_type_get(group_tag);
		uint32_t offset;

		if (group_tag == BITMAP_GROUP_TAG &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), BITMAP_GROUP_BYTES, &offset))
		{
			bitmaps_prepare(&state, offset, read_u32(instance + TAG_INSTANCE_HANDLE_OFFSET), report);
		}
		if (group_tag == ANIMATION_GRAPH_GROUP_TAG &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), ANIMATION_GRAPH_BYTES, &offset))
		{
			animation_graph_overlays_repair(&state, offset, report);
		}
		if (group_tag == SOUND_GROUP_TAG &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), SOUND_DEFINITION_BYTES, &offset))
		{
			sound_prepare(&state, offset, read_u32(instance + TAG_INSTANCE_HANDLE_OFFSET), report);
		}
		if (hud_definition_bytes(group_tag) &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), hud_definition_bytes(group_tag), &offset))
		{
			hud_placements_convert(&state, group_tag, offset, report);
		}
		if (group_tag == UNICODE_STRING_LIST_GROUP_TAG &&
			!strcmp(custom_edition_cache_tag_name(tag_cache, loaded_bytes, tag_index), MULTIPLAYER_GAME_TEXT_NAME) &&
			tag_cache_offset(&state, read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET), UNICODE_STRING_LIST_BYTES, &offset))
		{
			multiplayer_score_hint_convert(&state, offset, report);
		}
		if (!shader_type)
		{
			continue;
		}
		if (!tag_cache_offset(
				&state,
				read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET),
				group_tag == TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG ? TRANSPARENT_CHICAGO_EXTENDED_BYTES : SHADER_BYTES,
				&offset) ||
			read_s16(tag_cache + offset + SHADER_TYPE_OFFSET) != shader_type->custom_edition_type)
		{
			report->problem_tag_index = tag_index;
			return _cache_file_status_bad_shader_type;
		}
		write_u16(tag_cache + offset + SHADER_TYPE_OFFSET, (uint16_t)shader_type->type);
		if (shader_type->type != shader_type->custom_edition_type)
		{
			report->shaders_retyped++;
		}
		if (group_tag == TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG)
		{
			transparent_chicago_extended_convert(tag_cache + offset);
		}
	}
	report->chicago_extended_shaders = custom_edition_cache_tags_regroup(
		tag_cache,
		loaded_bytes,
		TRANSPARENT_CHICAGO_EXTENDED_GROUP_TAG,
		TRANSPARENT_CHICAGO_GROUP_TAG);

	return scenario_script_nodes_convert(&state, instances, tag_count, report);
}

void custom_edition_cache_combine_resource_offsets(
	uint8_t *tag_cache,
	uint32_t loaded_bytes,
	uint32_t bitmaps_offset,
	uint32_t sounds_offset)
{
	struct load_state state;
	uint8_t const *instances;
	int32_t tag_count;
	int32_t tag_index;

	loaded_state_initialize(&state, tag_cache, loaded_bytes);
	instances = loaded_tag_instances(&state, &tag_count);
	if (!instances)
	{
		return;
	}
	for (tag_index = 0; tag_index < tag_count; tag_index++)
	{
		uint8_t const *instance = instances + (uint32_t)tag_index * TAG_INSTANCE_BYTES;
		uint32_t group_tag = read_u32(instance + TAG_INSTANCE_GROUP_OFFSET);
		uint32_t address = read_u32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
		uint32_t tag_offset;

		if (group_tag == BITMAP_GROUP_TAG && tag_cache_offset(&state, address, BITMAP_GROUP_BYTES, &tag_offset))
		{
			uint8_t const *bitmaps = tag_cache + tag_offset + BITMAP_GROUP_BITMAPS_OFFSET;
			int32_t bitmap_count = read_s32(bitmaps + TAG_BLOCK_COUNT_OFFSET);
			uint32_t bitmaps_block_offset;
			int32_t bitmap_index;

			if (bitmap_count <= 0 ||
				(uint32_t)bitmap_count > UINT32_MAX / BITMAP_DATA_BYTES ||
				!tag_cache_offset(
					&state,
					read_u32(bitmaps + TAG_BLOCK_ADDRESS_OFFSET),
					(uint32_t)bitmap_count * BITMAP_DATA_BYTES,
					&bitmaps_block_offset))
			{
				continue;
			}
			for (bitmap_index = 0; bitmap_index < bitmap_count; bitmap_index++)
			{
				uint8_t *bitmap = tag_cache + bitmaps_block_offset + (uint32_t)bitmap_index * BITMAP_DATA_BYTES;
				uint16_t flags = read_u16(bitmap + BITMAP_DATA_FLAGS_OFFSET);

				if (flag_is_set(flags, BITMAP_DATA_IN_RESOURCE_MAP_BIT))
				{
					write_u32(
						bitmap + BITMAP_DATA_PIXELS_OFFSET_OFFSET,
						read_u32(bitmap + BITMAP_DATA_PIXELS_OFFSET_OFFSET) + bitmaps_offset);
					write_u16(bitmap + BITMAP_DATA_FLAGS_OFFSET, (uint16_t)(flags & ~(1U << BITMAP_DATA_IN_RESOURCE_MAP_BIT)));
				}
			}
		}
		else if (group_tag == SOUND_GROUP_TAG && tag_cache_offset(&state, address, SOUND_DEFINITION_BYTES, &tag_offset))
		{
			uint8_t const *pitch_ranges = tag_cache + tag_offset + SOUND_PITCH_RANGES_OFFSET;
			int32_t pitch_range_count = read_s32(pitch_ranges + TAG_BLOCK_COUNT_OFFSET);
			uint32_t pitch_ranges_offset;
			int32_t pitch_range_index;

			if (pitch_range_count <= 0 ||
				(uint32_t)pitch_range_count > UINT32_MAX / SOUND_PITCH_RANGE_BYTES ||
				!tag_cache_offset(
					&state,
					read_u32(pitch_ranges + TAG_BLOCK_ADDRESS_OFFSET),
					(uint32_t)pitch_range_count * SOUND_PITCH_RANGE_BYTES,
					&pitch_ranges_offset))
			{
				continue;
			}
			for (pitch_range_index = 0; pitch_range_index < pitch_range_count; pitch_range_index++)
			{
				uint8_t const *permutations = tag_cache + pitch_ranges_offset +
					(uint32_t)pitch_range_index * SOUND_PITCH_RANGE_BYTES + SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET;
				int32_t permutation_count = read_s32(permutations + TAG_BLOCK_COUNT_OFFSET);
				uint32_t permutations_offset;
				int32_t permutation_index;

				if (permutation_count <= 0 ||
					(uint32_t)permutation_count > UINT32_MAX / SOUND_PERMUTATION_BYTES ||
					!tag_cache_offset(
						&state,
						read_u32(permutations + TAG_BLOCK_ADDRESS_OFFSET),
						(uint32_t)permutation_count * SOUND_PERMUTATION_BYTES,
						&permutations_offset))
				{
					continue;
				}
				for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
				{
					uint8_t *samples = tag_cache + permutations_offset +
						(uint32_t)permutation_index * SOUND_PERMUTATION_BYTES + SOUND_PERMUTATION_SAMPLES_OFFSET;
					uint32_t flags = read_u32(samples + TAG_DATA_FLAGS_OFFSET);

					if (flag_is_set(flags, SOUND_SAMPLES_IN_RESOURCE_MAP_BIT))
					{
						write_u32(
							samples + TAG_DATA_FILE_OFFSET_OFFSET,
							read_u32(samples + TAG_DATA_FILE_OFFSET_OFFSET) + sounds_offset);
						write_u32(samples + TAG_DATA_FLAGS_OFFSET, flags & ~(1UL << SOUND_SAMPLES_IN_RESOURCE_MAP_BIT));
					}
				}
			}
		}
	}

	return;
}
