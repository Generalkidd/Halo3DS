#include "engine_parallel.h"
#include "engine_algorithm_profile.h"
/* Original model packets -> bounded native vertex/index streams. Opaque base
 * materials only; effect, decal, local-node and viewspace paths remain explicit
 * unsupported states until their semantics are implemented. */
#include "cseries.h"
#include "rasterizer/rasterizer_geometry.h"
#include "shaders/shader_definitions.h"
#include "tag_files/tag_groups.h"
#include "engine_models.h"
#include "engine_model_palette.h"
#include "engine_chicago.h"
#include "engine_dynamic_geometry.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "engine_textures.h"
#include "engine_presentation.h"
#include "engine_transparent.h"
#include "engine_text.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_transparent_geometry.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "rasterizer/rasterizer_frame_statistics.h"
#include "math/periodic_functions.h"
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
#include "crc.h"
/* Optional map geometry plans use the native SD path. */
#undef fopen
#endif

#define N3DS_MODEL_SKIN_VARIANT (HALO_N3DS_ALGO_SHARED_SKINNING != N3DS_ALGORITHM_REFERENCE || \
    HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE)

static int models_active, model_active, first_person_active;
static int scenery_batch_allowed;
static void scenery_batch_flush(void),scenery_batch_release(void);
void n3ds_scenery_report(void);
static struct native_model_counters counters;
#ifdef HALO_N3DS_MODEL_STAGE_PROFILE
long long n3ds_engine_ticks(void);
static long long model_profile[3];
#define MODEL_PROFILE_START() const long long model_profile_start = n3ds_engine_ticks()
#define MODEL_PROFILE_END(stage) (model_profile[stage] += n3ds_engine_ticks() - model_profile_start)
void n3ds_model_profile_read(long long out[3])
{
    memcpy(out,model_profile,sizeof(model_profile));
    memset(model_profile,0,sizeof(model_profile));
#ifdef HALO_N3DS_CROWD_PROFILE
    extern void n3ds_animation_profile(void);
    n3ds_animation_profile();
#endif
}
#else
#define MODEL_PROFILE_START() ((void)0)
#define MODEL_PROFILE_END(stage) ((void)0)
#endif
#ifdef HALO_N3DS_RENDERER_TESTS
static long long material_profile[2][6];
void n3ds_model_material_profile_read(long long out[12])
{memcpy(out,material_profile,sizeof(material_profile));memset(material_profile,0,sizeof(material_profile));}
#endif
void n3ds_engine_model_counters(struct native_model_counters *result) { assert(result); *result=counters; }
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
/* Count only immediately emitted ordinary colour packets. A scene batch
 * flush may emit another object's deferred geometry during this draw. */
static unsigned int presentation_model_draw_serial;
unsigned int n3ds_presentation_model_draw_serial(void)
{
    return presentation_model_draw_serial;
}
#endif
static struct rasterizer_model_begin_parameters parameters;
static struct native_model_lighting native_lighting;
static float lighting_channel(float value) {return isfinite(value)?PIN(value,0.f,1.f):0;}
static void model_lighting_prepare(void)
{
    static struct render_lighting previous;
    static float previous_debug;
    static int valid;
    float debug=rasterizer_debug_options.model_lighting_ambient;
    /* Compare values: deferred packets and reused packet storage are safe. */
    if(valid && previous_debug==debug && !memcmp(&previous,&parameters.lighting,sizeof(previous))) return;
    previous=parameters.lighting;previous_debug=debug;valid=1;
    memset(&native_lighting,0,sizeof(native_lighting));
    for(int c=0;c<3;++c) native_lighting.ambient[c]=lighting_channel(parameters.lighting.ambient_color.n[c]);
    native_lighting.count=PIN(parameters.lighting.distant_light_count,0,2);
    if(rasterizer_debug_options.model_lighting_ambient>0) {
        native_lighting.count=0;
        for(int c=0;c<3;++c) native_lighting.ambient[c]=lighting_channel(rasterizer_debug_options.model_lighting_ambient);
    }
    for(unsigned int i=0;i<native_lighting.count;++i) for(int c=0;c<3;++c) {
        native_lighting.distant[i].color[c]=lighting_channel(parameters.lighting.distant_lights[i].color.n[c]);
        float d=parameters.lighting.distant_lights[i].direction.n[c];
        /* Xbox model microcode DP3 uses -c[-73] and -c[-71]:
         * authored direction is light travel; PICA expects toward the light. */
        native_lighting.distant[i].direction[c]=isfinite(d)?-d:0;
    }
}
static real_matrix4x3 matrices[64];
/* A model packet can contain many material parts referencing the same bones.
 * Prepare scaled GPU rows once per bone in that packet, not once per part. */
static float prepared_bone_rows[64][12];
static byte prepared_bone_valid[64];
static unsigned int prepared_bone_prefix;
static const real_matrix4x3 *prepared_bone_source;
static void prepared_bones_invalidate(void)
{
    memset(prepared_bone_valid,0,sizeof(prepared_bone_valid));prepared_bone_prefix=0;
}
static void prepare_bone_source(void)
{
    if(prepared_bone_source!=parameters.skinning.node_matrices) {
        prepared_bone_source=parameters.skinning.node_matrices;
        prepared_bones_invalidate();
    }
}
static const float *prepare_bone_rows(unsigned int node)
{
    assert(node<64);
    if(!prepared_bone_valid[node]) {
        const real_matrix4x3 *m=parameters.skinning.node_matrices+node;float *rows=prepared_bone_rows[node];
        for(int axis=0;axis<3;++axis) {
            rows[axis*4]=m->forward.n[axis]*m->scale;
            rows[axis*4+1]=m->left.n[axis]*m->scale;
            rows[axis*4+2]=m->up.n[axis]*m->scale;
            rows[axis*4+3]=m->position.n[axis];
            for(int c=0;c<4;++c) assert(isfinite(rows[axis*4+c]));
        }
        prepared_bone_valid[node]=1;
        while(prepared_bone_prefix<NUMBEROF(prepared_bone_valid) && prepared_bone_valid[prepared_bone_prefix])
            ++prepared_bone_prefix;
    }
    return prepared_bone_rows[node];
}

static struct rasterizer_model_begin_parameters *queued_parameters;
extern struct rasterizer_window_begin_parameters global_window_parameters;
extern struct rasterizer_frame_begin_parameters global_frame_parameters;
extern boolean rasterizer_model_cortana_hack;
void n3ds_log(const char *message);
static unsigned short index_scratch[65535*3];
boolean rasterizer_set_texture_direct(short stage, long group_index, short index);
real uncompress_int16_to_real(short value);
struct native_model_definition {
    byte header[0x28];word flags;short type;byte reserved04[0xc];real translucency;
    byte reserved14[0x10];short change_color_source;byte reserved26[0x1e];
    word illumination_flags;short pad46,illumination_source,illumination_function;
    real illumination_period;real_rgb_color illumination_lower,illumination_upper;
    byte reserved68[0xc];real u,v;struct tag_reference base_map;
    byte reserved8c[8];struct tag_reference multipurpose_map;
    byte reserved_cc[8];short detail_function,detail_mask;real detail_scale;
    struct tag_reference detail_map;real detail_v_scale;
    byte reserved_f0[0x4c];real reflection_falloff,reflection_cutoff;
    real perpendicular_brightness;real_rgb_color perpendicular_tint;
    real parallel_brightness;real_rgb_color parallel_tint;struct tag_reference reflection_map;
};
_Static_assert(offsetof(struct native_model_definition,base_map)==0xa4,"Xbox model base map ABI");
_Static_assert(offsetof(struct native_model_definition,multipurpose_map)==0xbc,"Xbox multipurpose ABI");
_Static_assert(offsetof(struct native_model_definition,illumination_period)==0x74,"Xbox model illumination ABI");
_Static_assert(offsetof(struct native_model_definition,reflection_falloff)==0x13c,"Xbox model reflection falloff ABI");
_Static_assert(offsetof(struct native_model_definition,reflection_map)==0x164,"Xbox model reflection bitmap ABI");
_Static_assert(offsetof(struct native_model_definition,detail_function)==0xd4,"Xbox model detail function ABI");
_Static_assert(offsetof(struct native_model_definition,detail_map)==0xdc,"Xbox model detail bitmap ABI");
static unsigned int model_color(const real_rgb_color *c)
{
    unsigned int result=0xff000000;
    for(unsigned int i=0;i<3;++i) {
        real channel=c->n[i];if(!isfinite(channel)) channel=0;
        result|=(unsigned int)(PIN(channel,0.f,1.f)*255.f+.5f)<<(i*8);
    }
    return result;
}
enum { MATERIAL_PACKET_CACHE=16 };
static struct { const struct native_model_definition *shader;short permutation;int reflection;struct native_model_material material; } packet_materials[MATERIAL_PACKET_CACHE];
static unsigned int packet_material_count;
static struct native_model_material model_material(const struct native_model_definition *s,short permutation,int allow_reflection)
{
    /* Original gameplay trades decorative reflections/detail for fewer texture
     * fetches and simpler vertex programs. Preserve base, cutout, team color,
     * self illumination and native lighting; menus and New models keep both. */
    extern int n3ds_input_platform_original_model(void);
    if(n3ds_input_platform_original_model() && n3ds_gpu_world_pressure())allow_reflection=0;
    for(unsigned int i=0;i<packet_material_count;++i)
        if(packet_materials[i].shader==s && packet_materials[i].permutation==permutation && packet_materials[i].reflection==allow_reflection)
            return packet_materials[i].material;
    struct native_model_material result={NULL,0xff000000,0xffffffff,0};
    real_rgb_color emission=s->illumination_lower,tint={1,1,1};
    if(memcmp(&s->illumination_lower,&s->illumination_upper,sizeof(emission))) {
        real phase=0,fraction=0;unsigned long seed=parameters.unique_identifier;
        if(!(s->illumination_flags&1)) phase=real_seed_random(&seed);
        if(isfinite(s->illumination_period) && s->illumination_period!=0 &&
           s->illumination_function>=0 && s->illumination_function<12) /* original periodic_functions.c enum */
            fraction=periodic_function_evaluate(s->illumination_function,global_frame_parameters.game_time_sec/s->illumination_period+phase);
        for(int i=0;i<3;++i) emission.n[i]+=(s->illumination_upper.n[i]-emission.n[i])*fraction;
    }
    if(parameters.animation.colors) {
        if(s->illumination_source>0 && s->illumination_source<5)
            for(int i=0;i<3;++i) emission.n[i]*=parameters.animation.colors[s->illumination_source-1].n[i];
        if(s->change_color_source>0 && s->change_color_source<5) tint=parameters.animation.colors[s->change_color_source-1];
    }
    result.emission=model_color(&emission);result.tint=model_color(&tint);
    if(allow_reflection && s->reflection_map.index!=NONE) {
        real fraction=1;
        if(s->reflection_cutoff!=0) {
            real distance=0;for(int c=0;c<3;++c) distance+=(parameters.centroid.n[c]-global_window_parameters.camera.position.n[c])*global_window_parameters.camera.forward.n[c];
            real range=s->reflection_falloff-s->reflection_cutoff;
            fraction=isfinite(range) && range!=0?lighting_channel((distance-s->reflection_cutoff)/range):0;
        }
        real alpha=fraction*lighting_channel(parameters.lighting.reflection_tint_color.alpha);
        if(alpha>0 && (s->perpendicular_brightness>0 || s->parallel_brightness>0))
            result.reflection=n3ds_engine_bitmap_resource(s->reflection_map.index,permutation);
        if(result.reflection) {
            for(int c=0;c<3;++c) {
                real tint=lighting_channel(parameters.lighting.reflection_tint_color.rgb.n[c]);
                result.reflection_parallel[c]=lighting_channel(s->parallel_tint.n[c])*tint;
                result.reflection_delta[c]=lighting_channel(s->perpendicular_tint.n[c])*tint-result.reflection_parallel[c];
            }
            result.reflection_parallel[3]=lighting_channel(s->parallel_brightness)*alpha;
            result.reflection_delta[3]=lighting_channel(s->perpendicular_brightness)*alpha-result.reflection_parallel[3];
        }
    }
    /* Reflection already uses all three PICA texture units. Restore authored
     * multiply detail only when unit0 is free; retain the reflection path. */
    if(allow_reflection && !result.reflection && s->detail_map.index!=NONE &&
       s->detail_function>=0 && s->detail_function<=1 && s->detail_mask>=0 && s->detail_mask<=8 &&
       isfinite(s->detail_scale) && isfinite(s->detail_scale*s->detail_v_scale)) {
        result.detail=n3ds_engine_bitmap_resource(s->detail_map.index,permutation);
        result.detail_scale[0]=s->detail_scale;result.detail_scale[1]=s->detail_scale*s->detail_v_scale;
        result.detail_function=s->detail_function;result.detail_mask=s->detail_mask;result.detail_after=!!(s->flags&1);
    }
    if(result.emission!=0xff000000 || result.tint!=0xffffffff || result.reflection || (result.detail && result.detail_mask))
        result.multipurpose=n3ds_engine_bitmap_resource(s->multipurpose_map.index,permutation);
    if(result.detail && result.detail_mask && !result.multipurpose) result.detail=NULL;
    result.alpha_test=!(s->flags&(1<<2));result.translucency=lighting_channel(s->translucency);
    if(packet_material_count<MATERIAL_PACKET_CACHE) {
        unsigned int i=packet_material_count++;
        packet_materials[i].shader=s;packet_materials[i].permutation=permutation;
        packet_materials[i].reflection=allow_reflection;packet_materials[i].material=result;
    }
    return result;
}
void n3ds_chicago_model_draw(const struct shader *shader,short permutation,
    const struct rasterizer_model_begin_parameters *parameters,const void *data,
    unsigned int count,short type,const unsigned short *indices,unsigned int index_count);

int n3ds_engine_models_active(void) { return models_active; }
/* Exact original script setter. Reflection shading is a separate renderer path
 * and is not added by storing this game-state value. */
void rasterizer_model_ambient_reflection_tint(real alpha,real red,real green,real blue)
{
    if(global_rasterizer_model_ambient_reflection_tint) {
        global_rasterizer_model_ambient_reflection_tint->alpha=alpha;
        global_rasterizer_model_ambient_reflection_tint->red=red;
        global_rasterizer_model_ambient_reflection_tint->green=green;
        global_rasterizer_model_ambient_reflection_tint->blue=blue;
    }
}
void _rasterizer_models_begin(boolean skip_obscurer_test)
{
    (void)skip_obscurer_test;
    assert(n3ds_engine_window_active() && !models_active && !model_active);
    models_active=1;scenery_batch_allowed=1;
    n3ds_gpu_model_batch_begin();
    memset(&counters,0,sizeof(counters));
}
void _rasterizer_models_end(void)
{
    assert(models_active && !model_active);
    scenery_batch_flush();scenery_batch_allowed=0;
    n3ds_gpu_model_batch_end();models_active=0;
}
void _rasterizer_model_begin(const struct rasterizer_model_begin_parameters *input, boolean preserve_z)
{
    assert(models_active && !model_active && input);
    if((input->effect.type!=0 && input->effect.type!=1 && (input->effect.type!=2 || input->effect.modifier_shader)) || (input->geometry_flags & ((1u<<5)|(1u<<8)))) {
        char message[128];
        snprintf(message,sizeof(message),"NATIVE MODEL UNSUPPORTED: effect=%d geometry=%08lx nodes=%d",input->effect.type,input->geometry_flags,input->skinning.node_matrix_count);
        n3ds_log(message);
    }
    assert((input->effect.type==0 || input->effect.type==1 || (input->effect.type==2 && !input->effect.modifier_shader)) && !(input->geometry_flags & ((1u<<5)|(1u<<8))));
    /* Original immediate sky packets set both sky and no-Z, never one alone. */
    assert(!!(input->geometry_flags&(1u<<3))==!!(input->geometry_flags&(1u<<4)));
    assert(input->skinning.node_matrix_count>0 && input->skinning.node_matrix_count<=64 && input->skinning.node_matrices);
    parameters=*input;model_lighting_prepare();
    first_person_active=(input->geometry_flags&(1u<<7)) && !preserve_z;
    if(first_person_active) {
        assert(n3ds_gpu_first_person_begin(rasterizer_globals.first_person_weapon_near_clip_distance,rasterizer_globals.first_person_weapon_far_clip_distance));
        static int reported;
        if(!reported) {n3ds_log("NATIVE FIRST PERSON: original weapon clip planes; stencil write/reject protects held weapon from world geometry");reported=1;}
    }
    ++counters.models;
    counters.max_nodes=MAX(counters.max_nodes,(unsigned int)input->skinning.node_matrix_count);
    memcpy(matrices,input->skinning.node_matrices,input->skinning.node_matrix_count*sizeof(*matrices));
    parameters.skinning.node_matrices=matrices;
    prepared_bones_invalidate();packet_material_count=0;
    queued_parameters=NULL;
    model_active=1;
}
void _rasterizer_model_end(void) {
    assert(models_active && model_active);
    if(first_person_active) n3ds_gpu_first_person_end();
    first_person_active=0;model_active=0;
}

int n3ds_model_decode_vertex(const struct model_vertex_compressed *input, struct model_vertex_uncompressed *output)
{
    struct model_vertex_uncompressed result={0};
    int i;
    if (!input || !output || input->node_weight<0) return 0;
    /* Cached Xbox GPU vertices use a signed normalized 16-bit weight and 0xFD
     * for NONE*3. The original generic CPU decompressor reads an 8-bit weight
     * and asserts on that sentinel, so it cannot decode these retail buffers.
     * The original rasterizer_debug_model_vertices confirms /32767 weighting. */
    result.position=input->position;
    result.normal=uncompress_int32_to_real_vector3d(input->normal);
    result.texcoord.x=uncompress_int16_to_real(input->texcoord.x);
    result.texcoord.y=uncompress_int16_to_real(input->texcoord.y);
    for (i=0;i<2;++i) {
        if (input->nodes[i]==253) result.nodes[i]=NONE;
        else { if (input->nodes[i]%3 || input->nodes[i]/3>=64) return 0; result.nodes[i]=input->nodes[i]/3; }
    }
    result.node_weights[0]=input->node_weight/32767.f;
    result.node_weights[1]=1.f-result.node_weights[0];
    *output=result; return 1;
}

/* Bound decoded and posed storage independently. Only mutable inputs need a
 * compressed source shadow. Local GPU inputs use the classified stride. */
enum { DECODE_STREAMS=256, DECODE_BYTES=6*1024*1024, POSE_BYTES=2*1024*1024, STREAM_POSES=8 };
struct cached_pose {
    struct native_render_vertex *posed;
    real_matrix4x3 *bones;
    real_vector2d uv_scale;
    unsigned int bytes,bone_count,stamp,owner;
    int valid;
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
    real_vector3d *raw_normals;
    unsigned int raw_bytes;
#endif
#if N3DS_MODEL_SKIN_VARIANT
    int has_lighting;
#endif
};
static struct decoded_stream {
    const struct model_vertex_compressed *address;
    struct model_vertex_compressed *source;
    struct model_vertex_uncompressed *vertices;
    struct native_render_vertex *local;
    struct native_render_vertex *uploaded;
    unsigned long long uploaded_frame,revision;
    int rigid_node;
    unsigned int gpu_nodes,required_nodes;
    unsigned int used_nodes[2],used_node_count;
    int used_nodes_valid;
    float uv_bounds[4];
    int identity_palette;
    unsigned int single_influence;
    byte palette[NATIVE_GPU_MODEL_BONES];
    int immutable;
    unsigned int count,bytes,local_bytes;
    int valid;
    struct cached_pose poses[STREAM_POSES];
} decoded_streams[DECODE_STREAMS];
static struct decoded_stream *decoded_hints[512],*last_decoded;
static unsigned int stream_hint(const void *p) {unsigned int n=(unsigned int)p;return ((n>>4)^(n>>13))&511;}
static struct decoded_stream *find_decoded_vertices(const struct model_vertex_uncompressed *v,unsigned int count)
{
    if(last_decoded && last_decoded->valid && last_decoded->vertices==v && last_decoded->count==count) return last_decoded;
    for(unsigned int i=0;i<DECODE_STREAMS;++i) if(decoded_streams[i].valid && decoded_streams[i].vertices==v && decoded_streams[i].count==count) return last_decoded=decoded_streams+i;
    return NULL;
}
int n3ds_model_uv_bounds(const struct model_vertex_uncompressed *v,unsigned int count,float bounds[4])
{
    if(!v || !count || !bounds)return 0;
    const struct decoded_stream *stream=find_decoded_vertices(v,count);
    if(!stream)return 0;
    memcpy(bounds,stream->uv_bounds,sizeof(stream->uv_bounds));return 1;
}
static int model_pose_matrices_equal(const struct decoded_stream *stream,const real_matrix4x3 *a,
    const real_matrix4x3 *b,unsigned int nodes,int normals)
{
    /* A decoded part can reference a small subset of its object's skeleton.
     * Unused bones cannot affect this part; vector posing additionally reads
     * scale/orientation only. Keep those bytes exact, including signed zero. */
    if(!stream->used_nodes_valid || !nodes || nodes>64)return 0;
    if(nodes<32) {
        if(stream->used_nodes[1] || (stream->used_nodes[0]>>nodes))return 0;
    } else if(nodes<64 && (stream->used_nodes[1]>>(nodes-32)))return 0;
    if(!normals && stream->used_node_count*2>=nodes)
        return !memcmp(a,b,nodes*sizeof(*a));
    unsigned int bytes=normals?offsetof(real_matrix4x3,position):sizeof(*a);
    for(unsigned int word=0;word<2;++word) {
        unsigned int mask=stream->used_nodes[word];
        while(mask) {
            unsigned int node=word*32+__builtin_ctz(mask);mask&=mask-1;
            if(node>=nodes || memcmp(a+node,b+node,bytes))return 0;
        }
    }
    return 1;
}
static unsigned int decoded_bytes,decoded_hits,decoded_misses,decoded_next;
static unsigned long long decoded_revision;
static unsigned int pose_bytes,pose_hits,pose_misses,pose_clock,pose_evictions;
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
static unsigned int retained_normal_bytes;
#endif
#if N3DS_MODEL_SKIN_VARIANT
static void midpoint_skin_clear(void);
#endif
static unsigned int upload_hits,upload_misses;
static unsigned long long upload_saved_bytes;
/* Separate CPU ownership prevents an optional normal lookup from invalidating
 * a posed stream that its caller has already borrowed. Protect every returned
 * normal array for the rest of the frame, including subsequent material/eye
 * draws with another pose of the same object. */
enum { NORMAL_POSE_SLOTS=32 };
static struct cached_normals {
    const struct decoded_stream *stream;
    unsigned long long revision,frame;
    real_vector3d *normals;
    real_matrix4x3 *bones;
    unsigned int count,bone_count,owner,bytes,stamp;
    int valid;
} normal_poses[NORMAL_POSE_SLOTS];
static unsigned int normal_pose_bytes,normal_pose_clock;
static unsigned long long normal_pose_failure_frame;
static int normal_pose_failed;
static void normal_pose_release(struct cached_normals *p)
{
    free(p->normals);normal_pose_bytes-=p->bytes;memset(p,0,sizeof(*p));
}
static void normal_poses_release(void)
{
    for(unsigned int i=0;i<NORMAL_POSE_SLOTS;++i)normal_pose_release(normal_poses+i);
    normal_pose_clock=0;normal_pose_failure_frame=0;normal_pose_failed=0;
}
static void index_cache_flush(void);
static void merged_models_release(void);
static void scenery_mesh_release(void);
static void model_compact_release(void);
static void pose_release(struct cached_pose *p)
{
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
    free(p->raw_normals);
    retained_normal_bytes-=p->raw_bytes;
#endif
    free(p->posed);free(p->bones);pose_bytes-=p->bytes;memset(p,0,sizeof(*p));
}
static void decode_stream_release(struct decoded_stream *s)
{
    for(unsigned int i=0;i<STREAM_POSES;++i) pose_release(s->poses+i);
    free(s->source);free(s->vertices);free(s->local);decoded_bytes-=s->bytes;memset(s,0,sizeof(*s));
}
static int decode_stream_reserve(struct decoded_stream *stream,unsigned int bytes)
{
    if(bytes>DECODE_BYTES-stream->bytes) return 0;
    while(bytes>DECODE_BYTES-decoded_bytes) {
        struct decoded_stream *victim=decoded_streams+(decoded_next++%DECODE_STREAMS);
        if(victim!=stream) decode_stream_release(victim);
    }
    return 1;
}
static void decode_stream_drop_shadow(struct decoded_stream *stream)
{
    if(!stream->source) return;
    unsigned int bytes=stream->count*sizeof(*stream->source);
    free(stream->source);stream->source=NULL;
    stream->bytes-=bytes;decoded_bytes-=bytes;
}
int n3ds_animation_map_owned(const void *p,unsigned int bytes)
{const struct n3ds_cache_view *v=n3ds_engine_cache_view(0);return p && v && n3ds_cache_resolve(v,p,bytes)==p;}
void n3ds_model_decode_cache_flush(void)
{
    extern void n3ds_animation_cache_reset(void);n3ds_animation_cache_reset();
    index_cache_flush();
    if(upload_hits || upload_misses) {
        char message[160];snprintf(message,sizeof(message),"MODEL SHARED UPLOADS: reused=%u uploaded=%u bytes_saved=%llu",upload_hits,upload_misses,upload_saved_bytes);n3ds_log(message);
        upload_hits=upload_misses=0;upload_saved_bytes=0;
    }
    if(pose_hits || pose_misses) {
        char message[160];snprintf(message,sizeof(message),"MODEL POSE CACHE: hits=%u misses=%u bytes=%u",pose_hits,pose_misses,pose_bytes);n3ds_log(message);
        snprintf(message,sizeof(message),"MODEL MULTIPOSE: slots=%u budget=%u evictions=%u",STREAM_POSES,POSE_BYTES,pose_evictions);n3ds_log(message);
    }
    if(decoded_hits || decoded_misses) {
        char message[160];snprintf(message,sizeof(message),"MODEL STREAM CACHE: hits=%u misses=%u bytes=%u",decoded_hits,decoded_misses,decoded_bytes);n3ds_log(message);
    }
    normal_poses_release();
    for(unsigned int i=0;i<DECODE_STREAMS;++i) decode_stream_release(decoded_streams+i);
#if N3DS_MODEL_SKIN_VARIANT
    midpoint_skin_clear();
#endif
    scenery_batch_release();model_compact_release();merged_models_release();scenery_mesh_release();
    decoded_hits=decoded_misses=decoded_next=0;memset(decoded_hints,0,sizeof(decoded_hints));last_decoded=NULL;
    pose_hits=pose_misses=pose_clock=pose_evictions=0;
}
/* Returned storage is only valid until the next stream lookup or map unload.
 * Callers consume it immediately. Mutable sources require an exact byte match;
 * allocation/budget failure leaves the checked per-vertex path available. */
static int decode_vertex_range(void *context,unsigned int begin,unsigned int end)
{
    struct n3ds_model_decode_range *r=context;
    for(unsigned int i=begin;i<end;++i) if(!n3ds_model_decode_vertex(r->input+i,r->output+i)) return 0;
    return 1;
}
static const struct model_vertex_uncompressed *decode_stream(const void *data,unsigned int count,int immutable)
{
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
    unsigned int first_worker_vertex=0, generation=0;
#endif
    if(!data || !count || count>65535) return NULL;
    unsigned int source_bytes=count*sizeof(struct model_vertex_compressed);
    unsigned int vertex_bytes=count*sizeof(struct model_vertex_uncompressed);
    if(vertex_bytes+(immutable?0:source_bytes)>DECODE_BYTES) return NULL;
    unsigned int hint=stream_hint(data);
    struct decoded_stream *s=decoded_hints[hint];
    if(!s || s->address!=data || s->count!=count) {
        s=NULL;
        for(unsigned int i=0;i<DECODE_STREAMS;++i) if(decoded_streams[i].address==data && decoded_streams[i].count==count) {s=decoded_streams+i;break;}
    }
    if(s) {decoded_hints[hint]=s;last_decoded=s;}
    if(s && s->valid && (s->immutable || (s->source && !memcmp(s->source,data,source_bytes)))) {
        if(immutable && !s->immutable) {
            decode_stream_drop_shadow(s);s->immutable=1;
        }
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_DECODE_HIT,1);
        ++decoded_hits;return s->vertices;
    }
    ++decoded_misses;
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_DECODE_MISS,1);
    if(!s) {
        s=decoded_streams+(decoded_next++%DECODE_STREAMS);decode_stream_release(s);
        s->count=count;s->address=data;
    }
    if(immutable) decode_stream_drop_shadow(s);
    if(!s->vertices) {
        if(!decode_stream_reserve(s,vertex_bytes)) goto failed;
        s->vertices=(malloc)(vertex_bytes);if(!s->vertices) goto failed;
        s->bytes+=vertex_bytes;decoded_bytes+=vertex_bytes;
    }
    if(!immutable && !s->source) {
        if(!decode_stream_reserve(s,source_bytes)) goto failed;
        s->source=(malloc)(source_bytes);if(!s->source) goto failed;
        s->bytes+=source_bytes;decoded_bytes+=source_bytes;
    }
    decoded_hints[hint]=s;last_decoded=s;
    s->valid=0;
    s->uploaded=NULL;
    s->immutable=immutable;
    for(unsigned int i=0;i<STREAM_POSES;++i) s->poses[i].valid=0;
    struct n3ds_model_decode_range decode={data,s->vertices};
    /* Record on the submitting owner, never inside the shared range worker. */
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_DECODE_VERTICES,count);
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
    /* The main owner cannot release this source or cache slot before the
     * join. The worker only writes the unpublished tail of the vertex array. */
    generation=n3ds_engine_cache_generation();
    first_worker_vertex=n3ds_parallel_decode_begin(decode_vertex_range,&decode,count,256);
    if(first_worker_vertex) {
        if(!decode_vertex_range(&decode,0,first_worker_vertex)) goto failed;
    } else
#endif
    if(!n3ds_parallel_range(decode_vertex_range,&decode,count,256,1)) goto failed;
    /* Accept only exactly rigid streams, including two weights on one bone.
     * Mutable source changes rebuild both the classification and local stream. */
    s->rigid_node=-2;s->gpu_nodes=s->required_nodes=0;s->identity_palette=0;s->single_influence=0;
    s->used_nodes[0]=s->used_nodes[1]=s->used_node_count=0;s->used_nodes_valid=1;
    s->uv_bounds[0]=s->uv_bounds[1]=1e30f;s->uv_bounds[2]=s->uv_bounds[3]=-1e30f;
    byte remap[64];memset(remap,255,sizeof(remap));int gpu_valid=1;
    for(unsigned int i=0;i<count;++i) {
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
        if(first_worker_vertex && i==first_worker_vertex) {
            first_worker_vertex=0;
            if(!n3ds_parallel_decode_end(0) || generation!=n3ds_engine_cache_generation()) goto failed;
        }
#endif
        const struct model_vertex_uncompressed *v=s->vertices+i;
#if HALO_N3DS_ALGO_DEFERRED_DECODE == N3DS_ALGORITHM_VERIFY
        struct model_vertex_uncompressed reference;
        assert(n3ds_model_decode_vertex(decode.input+i,&reference) && !memcmp(v,&reference,sizeof(reference)));
#endif
        for(int axis=0;axis<3;++axis) if(!isfinite(v->position.n[axis]) || !isfinite(v->normal.n[axis])) goto failed;
        for(int axis=0;axis<2;++axis) {
            float uv=v->texcoord.n[axis];
            s->uv_bounds[axis]=MIN(s->uv_bounds[axis],uv);
            s->uv_bounds[axis+2]=MAX(s->uv_bounds[axis+2],uv);
        }
        if(v->node_weights[1]==0.f)++s->single_influence;
        int node=v->node_weights[0]?v->nodes[0]:v->nodes[1];
        if(node<0 || (v->node_weights[0] && v->node_weights[1] && v->nodes[0]!=v->nodes[1])) node=-1;
        if(s->rigid_node==-2) s->rigid_node=node;
        else if(s->rigid_node!=node) s->rigid_node=-1;
        for(int k=0;k<2;++k) if(v->node_weights[k]) {
            int n=v->nodes[k];
            if(n<0 || n>=64) {gpu_valid=0;s->used_nodes_valid=0;continue;}
            unsigned int bit=1u<<(n%32);
            if(!(s->used_nodes[n/32]&bit)) {s->used_nodes[n/32]|=bit;++s->used_node_count;}
            if(remap[n]==255) {
                if(s->gpu_nodes==NATIVE_GPU_MODEL_BONES) {gpu_valid=0;continue;}
                remap[n]=s->gpu_nodes;s->palette[s->gpu_nodes++]=n;
            }
        }
    }
    if(!gpu_valid) s->gpu_nodes=0;
    /* A stable identity palette lets neighboring parts share GPU bone rows.
     * High-index bones still require a compact palette. */
    if(gpu_valid && s->rigid_node<0 && s->gpu_nodes) {
        unsigned int highest=0;for(unsigned int n=0;n<s->gpu_nodes;++n)highest=MAX(highest,s->palette[n]);
        if(highest<NATIVE_GPU_MODEL_BONES) {
            s->gpu_nodes=highest+1;
            s->identity_palette=1;
            for(unsigned int n=0;n<s->gpu_nodes;++n) {s->palette[n]=n;remap[n]=n;}
        }
#if HALO_N3DS_ALGO_STABLE_PALETTE != N3DS_ALGORITHM_REFERENCE
        else {
            unsigned int changed=n3ds_model_order_compact_palette(s->palette,remap);
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_COMPACT_PALETTES_ORDERED,1);
            n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_COMPACT_PALETTE_SLOTS_CHANGED,changed);
            (void)changed;
        }
#endif
    }
    for(unsigned int n=0;n<s->gpu_nodes;++n)
        s->required_nodes=MAX(s->required_nodes,(unsigned int)s->palette[n]+1);
    unsigned int local_bytes=s->gpu_nodes?count*(s->rigid_node>=0?sizeof(*s->local):sizeof(struct native_skin_vertex)):0;
    if(local_bytes!=s->local_bytes) {
        free(s->local);s->local=NULL;
        s->bytes-=s->local_bytes;decoded_bytes-=s->local_bytes;s->local_bytes=0;
        if(local_bytes) {
            if(!decode_stream_reserve(s,local_bytes)) goto failed;
            s->local=(malloc)(local_bytes);if(!s->local) goto failed;
            s->local_bytes=local_bytes;s->bytes+=local_bytes;decoded_bytes+=local_bytes;
        }
    }
    if(s->gpu_nodes && s->rigid_node>=0) for(unsigned int i=0;i<count;++i) {
        const struct model_vertex_uncompressed *v=s->vertices+i;
        memcpy(s->local[i].position,&v->position,12);memcpy(s->local[i].color,&v->normal,12);memcpy(s->local[i].uv,&v->texcoord,8);
    }
    else if(s->gpu_nodes) for(unsigned int i=0;i<count;++i) {
        const struct model_vertex_uncompressed *v=s->vertices+i;
        struct native_skin_vertex *out=(struct native_skin_vertex *)s->local+i;
        memcpy(out->position,&v->position,12);memcpy(out->normal,&v->normal,12);memcpy(out->uv,&v->texcoord,8);
        n3ds_skin_vertex_set_bones(out,v->node_weights[0]?remap[v->nodes[0]]*3:0,
            v->node_weights[1]?remap[v->nodes[1]]*3:0,v->node_weights[0],v->node_weights[1]);
#if HALO_N3DS_ALGO_STABLE_PALETTE == N3DS_ALGORITHM_VERIFY
        float influences[4];
        n3ds_skin_vertex_read_bones(out,influences);
        assert(!memcmp(influences+2,v->node_weights,2*sizeof(float)));
        for(int k=0;k<2;++k) {
            unsigned int row=(unsigned int)influences[k];
            assert(row%3==0 && row/3<s->gpu_nodes);
            assert(v->node_weights[k]?s->palette[row/3]==v->nodes[k]:!row);
        }
#endif
    }
    if(s->gpu_nodes && s->rigid_node<0) {
        n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_WEIGHTED_VERTICES,count);
        n3ds_algorithm_profile_add(N3DS_ALGORITHM_GPU_WEIGHTED_STREAM_BYTES,local_bytes);
    }
    if(s->source) {
        memcpy(s->source,data,source_bytes);
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_DECODE_COPY,source_bytes);
    }
    s->revision=++decoded_revision;s->valid=1;return s->vertices;
failed:
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
    if(first_worker_vertex) n3ds_parallel_decode_end(1);
#endif
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_DECODE_FAILURE,1);
    decode_stream_release(s);
    return NULL;
}
const struct model_vertex_uncompressed *n3ds_model_decode_stream(const void *data,unsigned int count)
{
    MODEL_PROFILE_START();
    const struct model_vertex_uncompressed *result=decode_stream(data,count,0);
    MODEL_PROFILE_END(0);
    return result;
}
int n3ds_model_decode_cache_tests(void)
{
    struct model_vertex_compressed v[16]={0};struct model_vertex_uncompressed expected;
    unsigned int state=0x18342u;
    for(unsigned int iteration=0;iteration<64;++iteration) {
        for(unsigned int i=0;i<16;++i) {
            state=state*1664525u+1013904223u;v[i].normal=state;
            v[i].position=(real_point3d){i*.25f,-(real)iteration,i*.0625f};
            v[i].texcoord.x=(short)state;v[i].texcoord.y=(short)(state>>16);
            v[i].nodes[0]=(i%64)*3;v[i].nodes[1]=253;v[i].node_weight=32767;
        }
        const struct model_vertex_uncompressed *decoded=n3ds_model_decode_stream(v,16);
        if(!decoded) return 0;
        for(unsigned int i=0;i<16;++i) if(!n3ds_model_decode_vertex(v+i,&expected) || memcmp(decoded+i,&expected,sizeof(expected))) return 0;
        unsigned int hits=decoded_hits;
        if(n3ds_model_decode_stream(v,16)!=decoded || decoded_hits!=hits+1) return 0;
        v[15].nodes[1]=1;if(n3ds_model_decode_stream(v,16)) return 0;
    }
    /* Exercise bounded replacement and revisit an evicted source. */
    struct model_vertex_compressed evict[DECODE_STREAMS+1]={0};
    for(unsigned int i=0;i<=DECODE_STREAMS;++i) {
        evict[i].position.x=i;evict[i].nodes[1]=253;evict[i].node_weight=32767;
        const struct model_vertex_uncompressed *decoded=n3ds_model_decode_stream(evict+i,1);
        if(!decoded || decoded->position.x!=(real)i) return 0;
    }
    const struct model_vertex_uncompressed *again=n3ds_model_decode_stream(evict,1);
    if(!again || again->position.x!=0 || decoded_bytes>DECODE_BYTES) return 0;
#if HALO_N3DS_ALGO_DEFERRED_DECODE != N3DS_ALGORITHM_REFERENCE
    /* Reject both an owner-prefix failure and a worker-tail failure, then
     * immediately reuse the slot. A leaked in-flight job cannot pass this. */
    struct model_vertex_compressed split[257]={0};
    const unsigned int failure_indices[]={0,256};
    for(unsigned int j=0;j<NUMBEROF(failure_indices);++j) {
        unsigned int invalid=failure_indices[j];
        split[invalid].nodes[0]=1;
        if(n3ds_model_decode_stream(split,NUMBEROF(split))) return 0;
        split[invalid].nodes[0]=0;
        const struct model_vertex_uncompressed *valid=n3ds_model_decode_stream(split,NUMBEROF(split));
        if(!valid || !n3ds_model_decode_vertex(split+256,&expected) ||
           memcmp(valid+256,&expected,sizeof(expected))) return 0;
    }
#endif
    n3ds_model_decode_cache_flush();return 1;
}

static int skin_attributes(const struct model_vertex_uncompressed *v, const real_vector2d *scale,
    real_vector3d normal, int lighting, struct native_render_vertex *result)
{
    if (lighting) {
        real length=sqrtf(normal.i*normal.i+normal.j*normal.j+normal.k*normal.k);
        real light=.3f;
        if (length>.00001f) light+=.7f*fabsf((normal.i*.3f+normal.j*.4f+normal.k*.8660254f)/length);
        result->color[0]=light*.72f; result->color[1]=light*.82f; result->color[2]=light*.9f;
    } else memset(result->color,0,sizeof(result->color));
    result->uv[0]=v->texcoord.x*scale->i; result->uv[1]=v->texcoord.y*scale->j;
    return isfinite(result->uv[0]) && isfinite(result->uv[1]);
}

static int skin_vertex(const struct model_vertex_uncompressed *v,
    const struct rasterizer_model_skinning *skin, const real_vector2d *scale,
    const real_vector3d *shared_normal, int lighting, struct native_render_vertex *output,
    real_vector3d *raw_normal)
{
    real_point3d point={0}; real_vector3d normal={0};
    struct native_render_vertex result;
    int i,j;
    if (!v || !skin || !skin->node_matrices || skin->node_matrix_count<=0 || skin->node_matrix_count>64 || !scale || !output) return 0;
    if (!isfinite(v->node_weights[0]) || !isfinite(v->node_weights[1]) ||
        v->node_weights[0]<0 || v->node_weights[1]<0 ||
        fabsf(v->node_weights[0]+v->node_weights[1]-1.f)>.0001f) return 0;
    for (i=0;i<2;++i) {
        real_point3d p; real_vector3d n;
        if (!v->node_weights[i]) continue; /* An unused bone may carry a sentinel. */
        if (v->nodes[i]<0 || v->nodes[i]>=skin->node_matrix_count) return 0;
        matrix4x3_transform_point(skin->node_matrices+v->nodes[i],&v->position,&p);
        if (!shared_normal) matrix4x3_transform_vector(skin->node_matrices+v->nodes[i],&v->normal,&n);
        for (j=0;j<3;++j) {
            point.n[j]+=p.n[j]*v->node_weights[i];
            if (!shared_normal) normal.n[j]+=n.n[j]*v->node_weights[i];
        }
    }
    if (shared_normal) normal=*shared_normal;
    for (j=0;j<3;++j) if (!isfinite(point.n[j]) || !isfinite(normal.n[j])) return 0;
    memcpy(result.position,&point,12);
    if (!skin_attributes(v,scale,normal,lighting,&result)) return 0;
    if (raw_normal) *raw_normal=normal;
    *output=result; return 1;
}

int n3ds_model_skin_vertex(const struct model_vertex_uncompressed *v,
    const struct rasterizer_model_skinning *skin, const real_vector2d *scale,
    struct native_render_vertex *output)
{
    return skin_vertex(v,skin,scale,NULL,1,output,NULL);
}
/* Only the result of the pure skin/lighting/UV conversion is retained. Owners
 * still apply sky colors, bind current materials, and submit every draw. */
struct model_skin_range {
    const struct model_vertex_uncompressed *input;
    const struct rasterizer_model_skinning *skin;
    const real_vector2d *scale;
    struct native_render_vertex *output;
#if N3DS_MODEL_SKIN_VARIANT
    const real_vector3d *normals;
    int lighting;
#endif
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
    real_vector3d *raw_output;
#endif
};
static int skin_vertex_range(void *context,unsigned int begin,unsigned int end)
{
    struct model_skin_range *r=context;
    for(unsigned int i=begin;i<end;++i) {
#if N3DS_MODEL_SKIN_VARIANT
        const real_vector3d *normal=r->normals?r->normals+i:NULL;
        real_vector3d *raw_output=NULL;
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
        if(r->raw_output)raw_output=r->raw_output+i;
#endif
        int ok=skin_vertex(r->input+i,r->skin,r->scale,normal,r->lighting,r->output+i,raw_output);
#if HALO_N3DS_ALGO_SHARED_SKINNING == N3DS_ALGORITHM_VERIFY || HALO_N3DS_ALGO_RETAINED_SKIN == N3DS_ALGORITHM_VERIFY
        struct native_render_vertex expected;
        real_vector3d expected_normal;
        int reference=skin_vertex(r->input+i,r->skin,r->scale,NULL,1,&expected,&expected_normal);
        assert(ok==reference);
        if(ok) {
            assert(!memcmp(expected.position,r->output[i].position,sizeof(expected.position)));
            assert(!memcmp(expected.uv,r->output[i].uv,sizeof(expected.uv)));
            if(r->lighting) assert(!memcmp(expected.color,r->output[i].color,sizeof(expected.color)));
            if(normal) assert(!memcmp(normal,&expected_normal,sizeof(*normal)));
            if(raw_output) assert(!memcmp(raw_output,&expected_normal,sizeof(*raw_output)));
        }
#endif
        if(!ok)return 0;
#else
        if(!n3ds_model_skin_vertex(r->input+i,r->skin,r->scale,r->output+i))return 0;
#endif
    }
    return 1;
}

#if N3DS_MODEL_SKIN_VARIANT
#include "engine_model_midpoint_skin.inl"
#endif

static const struct native_render_vertex *skin_stream(const struct model_vertex_uncompressed *v,
    unsigned int count,const struct rasterizer_model_skinning *skin,const real_vector2d *scale,unsigned int owner
#if N3DS_MODEL_SKIN_VARIANT
    ,const real_vector3d *normals,int lighting,int midpoint
#endif
    )
{
    if(!v || !skin || !scale || !skin->node_matrices || skin->node_matrix_count<=0 || skin->node_matrix_count>64) return NULL;
    struct decoded_stream *s=find_decoded_vertices(v,count);
    if(!s) return NULL;
    unsigned int nodes=skin->node_matrix_count,matrices=nodes*sizeof(real_matrix4x3);
    /* Shared geometry may occur at several world transforms in one frame.
     * Retain the latest exact pose per original object identifier, so animated
     * objects do not fill the cache with their own obsolete poses.
     * Unsigned age subtraction keeps replacement ordered across clock wrap. */
    ++pose_clock;
    struct cached_pose *p=NULL;
    for(unsigned int i=0;i<STREAM_POSES;++i) {
        struct cached_pose *candidate=s->poses+i;
        if(candidate->bytes && candidate->owner==owner) {
            if(candidate->valid && candidate->bone_count==nodes && !memcmp(&candidate->uv_scale,scale,sizeof(*scale)) &&
#if N3DS_MODEL_SKIN_VARIANT
               (!lighting || candidate->has_lighting) &&
#endif
               model_pose_matrices_equal(s,candidate->bones,skin->node_matrices,nodes,0)) {
                n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_HIT,1);
                ++pose_hits;candidate->stamp=pose_clock;return candidate->posed;
            }
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
            /* Positions and raw normals depend on the consumed pose. The
             * current material can derive new attributes without reposing. */
            if(!midpoint && candidate->raw_normals && candidate->valid && candidate->bone_count==nodes &&
               model_pose_matrices_equal(s,candidate->bones,skin->node_matrices,nodes,0)) {
                candidate->valid=0;
                for(unsigned int j=0;j<count;++j) {
                    struct native_render_vertex *out=candidate->posed+j;
                    if(!skin_attributes(v+j,scale,candidate->raw_normals[j],lighting,out))return NULL;
#if HALO_N3DS_ALGO_RETAINED_SKIN == N3DS_ALGORITHM_VERIFY
                    struct native_render_vertex expected;
                    assert(skin_vertex(v+j,skin,scale,NULL,lighting,&expected,NULL) &&
                           !memcmp(out,&expected,sizeof(expected)));
#endif
                }
                candidate->uv_scale=*scale;
                candidate->has_lighting=lighting;
                candidate->stamp=pose_clock;
                candidate->valid=1;
                ++pose_hits;
                n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_HIT,1);
                n3ds_algorithm_profile_add(N3DS_ALG_CPU_RETAINED_SKIN_REUSE,count);
                return candidate->posed;
            }
#endif
            p=candidate;break;
        }
        if(!p || (!candidate->valid && p->valid) || (candidate->valid==p->valid && pose_clock-candidate->stamp>pose_clock-p->stamp)) p=candidate;
    }
    ++pose_misses;
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_MISS,1);
#if N3DS_MODEL_SKIN_VARIANT
    if(midpoint) {
        struct model_skin_range range={v,skin,scale,NULL,normals,lighting};
        return skin_midpoint_stream(s,&range,owner);
    }
#endif
    p->valid=0;
    if(!p->posed || p->bone_count!=nodes) {
        pose_release(p);
        unsigned int bytes=count*sizeof(*p->posed)+matrices;
        if(bytes>POSE_BYTES) return NULL;
        while(bytes>POSE_BYTES-pose_bytes) {
            struct cached_pose *oldest=NULL;
            for(unsigned int i=0;i<DECODE_STREAMS;++i) for(unsigned int j=0;j<STREAM_POSES;++j) {
                struct cached_pose *candidate=decoded_streams[i].poses+j;
                if(candidate!=p && candidate->bytes && (!oldest || pose_clock-candidate->stamp>pose_clock-oldest->stamp)) oldest=candidate;
            }
            if(!oldest) return NULL;
            pose_release(oldest);++pose_evictions;
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_EVICT,1);
        }
        /* This is optional acceleration storage. Use fallible libc allocation
         * so the existing scalar fallback runs instead of debug_malloc abort. */
        p->posed=(malloc)(count*sizeof(*p->posed));p->bones=(malloc)(matrices);
        if(!p->posed || !p->bones) {pose_release(p);return NULL;}
        p->bytes=bytes;pose_bytes+=bytes;p->bone_count=nodes;
    }
    p->stamp=pose_clock;p->owner=owner;
    struct model_skin_range range={v,skin,scale,p->posed
#if N3DS_MODEL_SKIN_VARIANT
        ,normals,lighting
#endif
    };
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
    if(!p->raw_normals) {
        extern int n3ds_input_platform_original_model(void);
        unsigned int budget=n3ds_input_platform_original_model()?128*1024:512*1024;
        unsigned int bytes=count*sizeof(*p->raw_normals);
        if(retained_normal_bytes<=budget && bytes<=budget-retained_normal_bytes)
            p->raw_normals=(malloc)(bytes);
        if(p->raw_normals) {
            p->raw_bytes=bytes;
            retained_normal_bytes+=bytes;
        } else n3ds_algorithm_profile_add(N3DS_ALG_CPU_RETAINED_SKIN_BYPASS,1);
    }
    range.raw_output=p->raw_normals;
    n3ds_algorithm_profile_max(N3DS_ALG_CPU_RETAINED_SKIN_PEAK,retained_normal_bytes);
#endif
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_VERTICES,count);
    if(!n3ds_parallel_range(skin_vertex_range,&range,count,256,2)) return NULL;
#if N3DS_MODEL_SKIN_VARIANT
    p->has_lighting=lighting;
    if(normals)n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_NORMAL_REUSE,count);
    if(!lighting)n3ds_algorithm_profile_add(N3DS_ALG_CPU_SKIN_LIGHT_AVOIDED,count);
#endif
    memcpy(p->bones,skin->node_matrices,matrices);p->uv_scale=*scale;p->valid=1;
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_COPY,matrices);
    return p->posed;
}
const struct native_render_vertex *n3ds_model_skin_stream(const struct model_vertex_uncompressed *v,
    unsigned int count,const struct rasterizer_model_skinning *skin,const real_vector2d *scale,unsigned int owner)
{
    MODEL_PROFILE_START();
    const struct native_render_vertex *result=skin_stream(v,count,skin,scale,owner
#if N3DS_MODEL_SKIN_VARIANT
        ,NULL,1,n3ds_presentation_midpoint_active()
#endif
    );
    MODEL_PROFILE_END(1);
    if(!result)n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_FALLBACK,1);
    return result;
}

#if HALO_N3DS_ALGO_SHARED_SKINNING != N3DS_ALGORITHM_REFERENCE
const struct native_render_vertex *n3ds_model_unlit_skin_stream(const struct model_vertex_uncompressed *v,
    unsigned int count,const struct rasterizer_model_skinning *skin,const real_vector2d *scale,
    unsigned int owner,const real_vector3d **normals)
{
    *normals=n3ds_model_normal_stream(v,count,skin,owner);
    MODEL_PROFILE_START();
    const struct native_render_vertex *pose=skin_stream(v,count,skin,scale,owner,*normals,0,n3ds_presentation_midpoint_active());
    MODEL_PROFILE_END(1);
    if(!pose)n3ds_algorithm_profile_add(N3DS_ALG_CPU_POSE_FALLBACK,1);
    return pose;
}
#endif
const real_vector3d *n3ds_model_normal_stream(const struct model_vertex_uncompressed *v,
    unsigned int count,const struct rasterizer_model_skinning *skin,unsigned int owner)
{
    if(!v || !count || count>65535 || !skin || !skin->node_matrices ||
       skin->node_matrix_count<=0 || skin->node_matrix_count>64 || !n3ds_gpu_frame_active())return NULL;
    struct decoded_stream *stream=find_decoded_vertices(v,count);
    if(!stream)return NULL;
    const unsigned int nodes=skin->node_matrix_count,matrices=nodes*sizeof(real_matrix4x3);
    const unsigned long long frame=n3ds_gpu_model_frame_serial();
    ++normal_pose_clock;
    struct cached_normals *slot=NULL;
    for(unsigned int i=0;i<NORMAL_POSE_SLOTS;++i) {
        struct cached_normals *p=normal_poses+i;
        if(p->valid && p->stream==stream && p->revision==stream->revision &&
           p->owner==owner && p->count==count && p->bone_count==nodes &&
           model_pose_matrices_equal(stream,p->bones,skin->node_matrices,nodes,1)) {
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_NORMAL_HIT,1);
            p->stamp=normal_pose_clock;p->frame=frame;return p->normals;
        }
        if(p->valid && p->frame==frame)continue;
        /* Prefer replacing this owner's obsolete pose, then unused storage,
         * then the least recently borrowed array. Unsigned ages wrap safely. */
        int same=p->stream==stream && p->owner==owner;
        int slot_same=slot && slot->stream==stream && slot->owner==owner;
        if(!slot || same>slot_same || (same==slot_same &&
           ((!p->valid && slot->valid) || (p->valid==slot->valid &&
           normal_pose_clock-p->stamp>normal_pose_clock-slot->stamp))))slot=p;
    }
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_NORMAL_MISS,1);
#ifdef HALO_N3DS_PRESENT_INTERPOLATION
    if(n3ds_presentation_midpoint_active())return NULL;
#endif
    if(!slot)return NULL;
    extern int n3ds_input_platform_original_model(void);
    const unsigned int budget=n3ds_input_platform_original_model()?128*1024:512*1024;
    const unsigned int normal_bytes=count*sizeof(real_vector3d),bytes=normal_bytes+matrices;
    if(bytes>budget)return NULL;
    if(slot->bytes<bytes) {
        if(normal_pose_failed && normal_pose_failure_frame==frame)return NULL;
        normal_pose_release(slot);
        while(bytes>budget-normal_pose_bytes) {
            struct cached_normals *oldest=NULL;
            for(unsigned int i=0;i<NORMAL_POSE_SLOTS;++i) {
                struct cached_normals *p=normal_poses+i;
                if(p!=slot && p->bytes && (!p->valid || p->frame!=frame) &&
                   (!oldest || normal_pose_clock-p->stamp>normal_pose_clock-oldest->stamp))oldest=p;
            }
            if(!oldest)return NULL;
            normal_pose_release(oldest);
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_NORMAL_EVICT,1);
        }
        slot->normals=(malloc)(bytes);
        if(!slot->normals) {
            normal_pose_failed=1;normal_pose_failure_frame=frame;return NULL;
        }
        slot->bytes=bytes;normal_pose_bytes+=bytes;
    }
    slot->valid=0;
    slot->bones=(void *)((byte *)slot->normals+normal_bytes);
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_REFERENCE
    const real_vector3d *retained=NULL;
    for(unsigned int i=0;i<STREAM_POSES;++i) {
        const struct cached_pose *pose=stream->poses+i;
        if(pose->valid && pose->owner==owner && pose->bone_count==nodes && pose->raw_normals &&
           model_pose_matrices_equal(stream,pose->bones,skin->node_matrices,nodes,1)) {
            retained=pose->raw_normals;
            break;
        }
    }
    if(retained) {
        /* The normal-stream consumer retains a frame-owned copy. A later
         * pose eviction cannot invalidate that consumer's borrowed pointer. */
        memcpy(slot->normals,retained,normal_bytes);
        n3ds_algorithm_profile_add(N3DS_ALG_CPU_RETAINED_NORMAL_COPY,normal_bytes);
    }
#if HALO_N3DS_ALGO_RETAINED_SKIN != N3DS_ALGORITHM_VERIFY
    else
#endif
#endif
    {
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_NORMAL_VERTICES,count);
    for(unsigned int i=0;i<count;++i) {
        real_vector3d normal={0};
        /* Preserve the two-influence/component accumulation used by glass,
         * water and generic shaders. Normalization stays in each consumer. */
        for(int k=0;k<2;++k)if(v[i].node_weights[k]) {
            if(v[i].nodes[k]<0 || v[i].nodes[k]>=skin->node_matrix_count)return NULL;
            real_vector3d n;
            matrix4x3_transform_vector(skin->node_matrices+v[i].nodes[k],&v[i].normal,&n);
            for(int c=0;c<3;++c)normal.n[c]+=n.n[c]*v[i].node_weights[k];
        }
        for(int c=0;c<3;++c)if(!isfinite(normal.n[c]))return NULL;
#if HALO_N3DS_ALGO_RETAINED_SKIN == N3DS_ALGORITHM_VERIFY
        if(retained)assert(!memcmp(&normal,retained+i,sizeof(normal)));
#endif
        slot->normals[i]=normal;
    }
    }
    memcpy(slot->bones,skin->node_matrices,matrices);
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_NORMAL_COPY,matrices);
    slot->stream=stream;slot->revision=stream->revision;slot->count=count;
    slot->bone_count=nodes;slot->owner=owner;slot->stamp=normal_pose_clock;
    slot->frame=frame;slot->valid=1;
    return slot->normals;
}
static int multipose_tests(void)
{
    struct model_vertex_compressed raw[16]={0};
    real_matrix4x3 bone={0};real_vector2d uv={1,1};
    struct rasterizer_model_skinning skin={0};skin.node_matrices=&bone;skin.node_matrix_count=1;
    bone.scale=1;bone.forward.i=bone.left.j=bone.up.k=1;
    for(int i=0;i<16;++i) {raw[i].nodes[1]=253;raw[i].node_weight=32767;raw[i].normal=0x3ff;raw[i].position.x=i*.25f;}
    const struct model_vertex_uncompressed *v=n3ds_model_decode_stream(raw,16);if(!v) return 0;
    for(int cycle=0;cycle<4;++cycle) for(int instance=0;instance<STREAM_POSES;++instance) {
        bone.position.x=instance*2.f;
        const struct native_render_vertex *posed=n3ds_model_skin_stream(v,16,&skin,&uv,instance);if(!posed) return 0;
        for(int i=0;i<16;++i) {
            struct native_render_vertex expected;
            if(!n3ds_model_skin_vertex(v+i,&skin,&uv,&expected) || memcmp(posed+i,&expected,sizeof(expected))) return 0;
        }
    }
    if(pose_hits!=24 || pose_misses!=8) return 0;
    /* A mutation at the same address must invalidate ALL retained poses. */
    raw[0].position.y=7;
    v=n3ds_model_decode_stream(raw,16);if(!v) return 0;
    for(int instance=0;instance<STREAM_POSES;++instance) {
        bone.position.x=instance*2.f;
        const struct native_render_vertex *posed=n3ds_model_skin_stream(v,16,&skin,&uv,instance);if(!posed) return 0;
        for(int i=0;i<16;++i) {
            struct native_render_vertex expected;
            if(!n3ds_model_skin_vertex(v+i,&skin,&uv,&expected) || memcmp(posed+i,&expected,sizeof(expected))) return 0;
        }
    }
    if(pose_hits!=24 || pose_misses!=16) return 0;
    /* Ninth pose replaces the oldest; the most recently touched pose survives. */
    bone.position.x=16;if(!n3ds_model_skin_stream(v,16,&skin,&uv,8)) return 0;
    bone.position.x=14;if(!n3ds_model_skin_stream(v,16,&skin,&uv,7) || pose_hits!=25) return 0;
    bone.position.x=0;if(!n3ds_model_skin_stream(v,16,&skin,&uv,0) || pose_misses!=18) return 0;
    /* Fill enough output storage to force global byte-budget replacement. */
    struct model_vertex_compressed *large=calloc(8192,sizeof(*large));if(!large) return 0;
    for(int i=0;i<8192;++i) {large[i]=raw[i%16];large[i].position.z=i*.001f;}
    /* Two streams with eight poses each exceed the new byte budget while
     * their decoded sources still fit. Test actual budget eviction, not only
     * replacement of an old pose in a full per-stream slot array. */
    for(int stream=0;stream<2;++stream) {
      v=n3ds_model_decode_stream(large+stream*4096,4096);if(!v) {free(large);return 0;}
      for(int instance=0;instance<STREAM_POSES;++instance) {
        bone.position.x=instance*3.f;
        const struct native_render_vertex *posed=n3ds_model_skin_stream(v,4096,&skin,&uv,instance);
        if(!posed || pose_bytes>POSE_BYTES) {free(large);return 0;}
        for(int i=0;i<4096;++i) {
            struct native_render_vertex expected;
            if(!n3ds_model_skin_vertex(v+i,&skin,&uv,&expected) || memcmp(posed+i,&expected,sizeof(expected))) {free(large);return 0;}
        }
      }
    }
    if(!pose_evictions) {free(large);return 0;}
    unsigned int hits=pose_hits;
    if(!n3ds_model_skin_stream(v,4096,&skin,&uv,STREAM_POSES-1) || pose_hits!=hits+1) {free(large);return 0;}
    n3ds_model_decode_cache_flush();free(large);
    if(pose_bytes || decoded_bytes) return 0;
    n3ds_log("PASS: model multipose cache: scalar equivalence, shared instances, full source invalidation, slot and 2 MiB byte-budget eviction, disposal");
    return 1;
}
int n3ds_model_skin_cache_tests(void)
{
    struct model_vertex_compressed raw[16]={0};
    real_matrix4x3 bones[2]={0};real_vector2d uv={1,1};
    struct rasterizer_model_skinning skin={0};skin.node_matrices=bones;skin.node_matrix_count=2;
    for(int i=0;i<2;++i) {bones[i].scale=1;bones[i].forward.i=bones[i].left.j=bones[i].up.k=1;}
    for(int i=0;i<16;++i) {raw[i].nodes[1]=3;raw[i].node_weight=16384;raw[i].normal=0x3ff;raw[i].texcoord.x=8192;raw[i].position.x=i*.25f;}
    for(int test=0;test<64;++test) {
        /* Change each dependency independently as well as together. */
        if(test%4==0) raw[0].position.y=test*.125f;
        if(test%4==1) bones[0].position.x=test*.25f;
        if(test%4==2) bones[1].scale=1+test*.01f;
        if(test%4==3) uv.i=1+test*.02f;
        const struct model_vertex_uncompressed *v=n3ds_model_decode_stream(raw,16);if(!v) return 0;
        const struct native_render_vertex *posed=n3ds_model_skin_stream(v,16,&skin,&uv,0);if(!posed) return 0;
        for(int i=0;i<16;++i) {
            struct native_render_vertex expected;
            if(!n3ds_model_skin_vertex(v+i,&skin,&uv,&expected) || memcmp(posed+i,&expected,sizeof(expected))) return 0;
        }
        unsigned int hits=pose_hits;
        if(n3ds_model_skin_stream(v,16,&skin,&uv,0)!=posed || pose_hits!=hits+1) return 0;
    }
    const struct model_vertex_uncompressed *v=n3ds_model_decode_stream(raw,16);
    skin.node_matrix_count=1;if(n3ds_model_skin_stream(v,16,&skin,&uv,0)) return 0;
    skin.node_matrix_count=2;
    const struct native_render_vertex *regular=n3ds_model_skin_stream(v,16,&skin,&uv,0);
    if(!regular)return 0;
#if N3DS_MODEL_SKIN_VARIANT
    struct native_render_vertex saved_regular[16];
    memcpy(saved_regular,regular,sizeof(saved_regular));
    unsigned int regular_bytes=pose_bytes;
    float x=bones[0].position.x;
    bones[0].position.x=x+1.f;
    for(int lighting=0;lighting<=1;++lighting) {
        const struct native_render_vertex *scratch=skin_stream(v,16,&skin,&uv,0,NULL,lighting,1);
        if(!scratch || scratch==regular || memcmp(saved_regular,regular,sizeof(saved_regular)) ||
           pose_bytes!=regular_bytes || skin_stream(v,16,&skin,&uv,0,NULL,lighting,1)!=scratch)return 0;
        for(unsigned int i=0;i<NUMBEROF(saved_regular);++i) {
            struct native_render_vertex expected;
            if(!n3ds_model_skin_vertex(v+i,&skin,&uv,&expected) ||
               memcmp(expected.position,scratch[i].position,sizeof(expected.position)) ||
               memcmp(expected.uv,scratch[i].uv,sizeof(expected.uv)) ||
               (lighting && memcmp(expected.color,scratch[i].color,sizeof(expected.color))))return 0;
        }
    }
    bones[0].position.x=x;
    if(n3ds_model_skin_stream(v,16,&skin,&uv,0)!=regular)return 0;
    n3ds_log("PASS: lit and unlit midpoint skinning preserve regular pose storage");
#endif
    raw[0].node_weight=-1;if(n3ds_model_decode_stream(raw,16)) return 0;
    if(n3ds_model_skin_stream(v,16,&skin,&uv,0)) return 0;
    n3ds_model_decode_cache_flush();return multipose_tests();
}
void n3ds_glass_model_draw(const struct shader *,short,const struct rasterizer_model_begin_parameters *,
    const void *,unsigned int,short,const unsigned short *,unsigned int);
void n3ds_meter_model_draw(const struct shader *,short,const struct rasterizer_model_begin_parameters *,
    const void *,unsigned int,short,const unsigned short *,unsigned int);

static long triangle_list(short type, const unsigned short *source,
    unsigned int triangles, unsigned int vertices, unsigned short *output, unsigned int capacity)
{
    unsigned int i,count=0,indices;
    if ((type!=0 && type!=1) || triangles>65535 || !vertices || vertices>65535 ||
        !source || !output || triangles>capacity/3) return NONE;
    indices=type ? triangles+2 : triangles*3;
    for (i=0;i<indices;++i) if (source[i]>=vertices) return NONE;
    for (i=0;i<triangles;++i) {
        unsigned short a=source[type ? i : i*3], b=source[type ? i+1 : i*3+1], c=source[type ? i+2 : i*3+2];
        if (a==b || b==c || a==c) continue;
        /* Parity belongs to the source strip, including degenerate connectors. */
        if (type && (i&1)) { unsigned short swap=a; a=b; b=swap; }
        output[count++]=a; output[count++]=b; output[count++]=c;
    }
    return count;
}

long n3ds_model_triangle_list(short type,const unsigned short *source,unsigned int triangles,
    unsigned int vertices,unsigned short *output,unsigned int capacity)
{
    MODEL_PROFILE_START();
    long result=triangle_list(type,source,triangles,vertices,output,capacity);
    MODEL_PROFILE_END(2);
    return result;
}
static int depth_only;
/* Only map-owned static strips use identity keys. The loader relocates these
 * buffers once; runtime deformation uses separate dynamic buffers. Map/BSP
 * lifetime barriers flush this cache alongside decoded vertices. */
enum { INDEX_SLOTS=128, INDEX_CACHE_BYTES=512*1024 };
static struct cached_indices {
    const void *source;unsigned short *list;unsigned int triangles,vertices,count;short type;
    const unsigned short *uploaded,*resident;
    unsigned long long uploaded_frame,resident_epoch;
} index_cache[INDEX_SLOTS];
static unsigned int index_cache_bytes,index_cache_next;
static struct cached_indices *index_hints[512];
static void index_release(struct cached_indices *p) {index_cache_bytes-=p->count*2;free(p->list);memset(p,0,sizeof(*p));}
static void index_cache_flush(void) {for(unsigned int i=0;i<INDEX_SLOTS;++i) index_release(index_cache+i);index_cache_next=0;memset(index_hints,0,sizeof(index_hints));}
static const unsigned short *index_upload(struct cached_indices *p,int allow_residency)
{
    if(!n3ds_gpu_frame_active()) return p->list;
    unsigned long long frame=n3ds_gpu_model_frame_serial();
    if(!p->uploaded || p->uploaded_frame!=frame) {
        p->uploaded=NULL;
        unsigned long long epoch=allow_residency?n3ds_gpu_index_residency_epoch():0;
        if(epoch) {
            /* Admit repeated visible lists, leaving one-off draws and large
             * lists in the existing transient arena. */
            if(p->resident_epoch!=epoch && p->uploaded_frame && frame-p->uploaded_frame==1 &&
                    p->count<=32*1024/sizeof(*p->list)) {
                p->resident=n3ds_gpu_indices_upload_resident(p->list,p->count);
                p->resident_epoch=epoch;
            }
            if(p->resident_epoch==epoch)
                p->uploaded=p->resident;
        }
        if(!p->uploaded)
            p->uploaded=n3ds_gpu_indices_upload(p->list,p->count);
        p->uploaded_frame=frame;
    }
    return p->uploaded?p->uploaded:p->list;
}
static const unsigned short *static_indices_prepare(short type,const unsigned short *source,unsigned int triangles,
    unsigned int vertices,long *count,struct cached_indices **owner)
{
    *owner=NULL;
    unsigned int hint=stream_hint(source);
    struct cached_indices *cached=index_hints[hint];
    if(cached && cached->source==source && cached->triangles==triangles && cached->vertices==vertices && cached->type==type) {*count=cached->count;*owner=cached;return cached->list;}
    for(unsigned int i=0;i<INDEX_SLOTS;++i) {
        struct cached_indices *p=index_cache+i;
        if(p->source==source && p->triangles==triangles && p->vertices==vertices && p->type==type) {index_hints[hint]=p;*count=p->count;*owner=p;return p->list;}
    }
    *count=n3ds_model_triangle_list(type,source,triangles,vertices,index_scratch,65535*3);
    if(*count<=0) return index_scratch;
    unsigned int bytes=*count*2;if(bytes>INDEX_CACHE_BYTES) return index_scratch;
    struct cached_indices *p=index_cache+(index_cache_next++%INDEX_SLOTS);index_release(p);
    while(index_cache_bytes+bytes>INDEX_CACHE_BYTES) index_release(index_cache+(index_cache_next++%INDEX_SLOTS));
    unsigned short *list=(malloc)(bytes);if(!list) return index_scratch;
    memcpy(list,index_scratch,bytes);
    *p=(struct cached_indices){.source=source,.list=list,.triangles=triangles,.vertices=vertices,.count=*count,.type=type};
    index_cache_bytes+=bytes;
    index_hints[hint]=p;
    *owner=p;return p->list;
}
static const unsigned short *static_indices(short type,const unsigned short *source,unsigned int triangles,
    unsigned int vertices,long *count,int allow_residency)
{
    struct cached_indices *owner;
    const unsigned short *list=static_indices_prepare(type,source,triangles,vertices,count,&owner);
    return owner?index_upload(owner,allow_residency):list;
}
static const unsigned short *model_indices_submit(const unsigned short *list,unsigned int count,
    struct cached_indices *owner,int allow_residency)
{
    if(owner && owner->list==list && owner->count==count) return index_upload(owner,allow_residency);
    const unsigned short *uploaded=n3ds_gpu_indices_prepare(list,count);
    return uploaded?uploaded:list;
}
static struct decoded_stream *gpu_stream(const struct model_vertex_uncompressed *v,unsigned int count,unsigned int nodes)
{
    struct decoded_stream *s=find_decoded_vertices(v,count);
    /* The exact validated palette is immutable until this stream is decoded
     * again. Its highest original bone replaces the per-draw palette scan. */
    if(s && s->gpu_nodes && s->required_nodes<=nodes) return s;
    return NULL;
}
/* Rigid and identity palettes already match the prepared packet rows. Only
 * compact high-node palettes need a private reordered copy for submission. */
static const float *gpu_stream_rows(const struct decoded_stream *stream,float compact[NATIVE_GPU_MODEL_BONES*12])
{
    prepare_bone_source();
    if(stream->rigid_node>=0) return prepare_bone_rows(stream->rigid_node);
    if(stream->identity_palette) {
        while(prepared_bone_prefix<stream->gpu_nodes) prepare_bone_rows(prepared_bone_prefix);
        return &prepared_bone_rows[0][0];
    }
    for(unsigned int n=0;n<stream->gpu_nodes;++n)
        memcpy(compact+n*12,prepare_bone_rows(stream->palette[n]),12*sizeof(float));
    return compact;
}

static unsigned int skin_shortcut_vertices,skin_stream_vertices;
static struct native_render_vertex *gpu_stream_upload(struct decoded_stream *s)
{
    /* Local positions/weights are shared, while pose and UV scale are draw
     * uniforms. Reuse one upload across identical enemies, scenery and stereo
     * eyes. Immutable map streams can persist across frames; mutable streams
     * and memory-pressure fallbacks share only the current frame's upload. */
    if(s->rigid_node<0){skin_shortcut_vertices+=s->single_influence;skin_stream_vertices+=s->count;}
    unsigned long long frame=n3ds_gpu_model_frame_serial();
    unsigned int bytes=s->count*(s->rigid_node>=0?sizeof(struct native_render_vertex):sizeof(struct native_skin_vertex));
    if(s->immutable) {
        struct native_render_vertex *resident=n3ds_gpu_model_resident_upload((unsigned int)(s-decoded_streams),s->revision,s->local,bytes);
        if(resident) return resident;
    }
    if(s->uploaded && s->uploaded_frame==frame) {++upload_hits;upload_saved_bytes+=bytes;return s->uploaded;}
    struct native_render_vertex *gpu=n3ds_gpu_model_vertices_allocate((bytes+sizeof(*gpu)-1)/sizeof(*gpu));
    if(!gpu) return NULL;
    ++upload_misses;
    memcpy(gpu,s->local,bytes);n3ds_gpu_geometry_flush(gpu,bytes);
    s->uploaded=gpu;s->uploaded_frame=frame;return gpu;
}
static unsigned int gpu_depth_parts,gpu_cloak_parts,gpu_chicago_parts;
static int special_gpu_stream(const struct model_vertex_uncompressed *cached,unsigned int count,
    const unsigned short *indices,unsigned int index_count,int depth,const float *uv,float fade,int textured,int two_sided)
{
    struct decoded_stream *stream=gpu_stream(cached,count,parameters.skinning.node_matrix_count);
    if(!stream) return 0;
    const void *gpu=gpu_stream_upload(stream);if(!gpu) return 0;
    unsigned int nodes=stream->rigid_node>=0?1:stream->gpu_nodes;
    float compact_rows[NATIVE_GPU_MODEL_BONES*12];
    const float *rows=gpu_stream_rows(stream,compact_rows);
    int result=depth?n3ds_gpu_model_depth_transform_draw(gpu,indices,index_count,rows,nodes,stream->rigid_node<0):
        n3ds_gpu_model_cloak_transform_draw(gpu,indices,index_count,rows,nodes,stream->rigid_node<0,uv,fade,textured,two_sided);
    if(result) {if(depth) ++gpu_depth_parts;else ++gpu_cloak_parts;}
    return result;
}
int n3ds_model_chicago_gpu_draw(const struct model_vertex_uncompressed *cached,unsigned int count,
    const unsigned short *indices,unsigned int index_count,const struct native_chicago_material *m,const float *uv)
{
    struct decoded_stream *stream=gpu_stream(cached,count,parameters.skinning.node_matrix_count);
    if(!stream || stream->gpu_nodes>28) return 0;
    const void *gpu=gpu_stream_upload(stream);if(!gpu) return 0;
    unsigned int nodes=stream->rigid_node>=0?1:stream->gpu_nodes;
    float compact_rows[NATIVE_GPU_MODEL_BONES*12];
    const float *rows=gpu_stream_rows(stream,compact_rows);
    int result=n3ds_gpu_chicago_transform_draw(m,gpu,count,indices,index_count,rows,nodes,stream->rigid_node<0,uv);
    if(result) ++gpu_chicago_parts;return result;
}
void n3ds_model_offload_report(void)
{n3ds_scenery_report();char skin[128];snprintf(skin,sizeof(skin),"GPU SKIN WORK: single_influence=%u stream_vertices=%u",skin_shortcut_vertices,skin_stream_vertices);n3ds_log(skin);skin_shortcut_vertices=skin_stream_vertices=0;char message[128];snprintf(message,sizeof(message),"MODEL OFFLOAD: depth_parts=%u cloak_parts=%u chicago_parts=%u",gpu_depth_parts,gpu_cloak_parts,gpu_chicago_parts);n3ds_log(message);}
int n3ds_model_gpu_upload_tests(void)
{
    struct model_vertex_compressed raw[4]={0};
    for(unsigned int i=0;i<4;++i) {raw[i].node_weight=32767;raw[i].nodes[1]=253;raw[i].position.x=i;raw[i].normal=0x3ff;}
    if(!n3ds_gpu_frame_begin()) return 0;
    const struct model_vertex_uncompressed *v=n3ds_model_decode_stream(raw,4);
    struct decoded_stream *s=gpu_stream(v,4,1);
    struct native_render_vertex *a=s?gpu_stream_upload(s):NULL;
    int ok=a && gpu_stream_upload(s)==a && a[3].position[0]==3;
    real_matrix4x3 matrix={.scale=-2,.forward={0,1,0},.left={-1,0,0},.up={0,0,1},.position={3,-4,5}};
    struct rasterizer_model_skinning skin={.node_matrix_count=1,.node_matrices=&matrix};
    float rows[12];const real_vector2d uv={1,1};
    const struct native_render_vertex *rigid=n3ds_model_rigid_stream(v,4,&skin,rows);
    ok=ok && rigid==a;
    for(int i=0;ok && i<4;++i) {
        struct native_render_vertex expected;
        ok=n3ds_model_skin_vertex(v+i,&skin,&uv,&expected);
        for(int axis=0;axis<3;++axis) {
            float actual=rows[axis*4+3];
            for(int k=0;k<3;++k) actual+=rows[axis*4+k]*rigid[i].position[k];
            if(fabsf(actual-expected.position[axis])>1e-5f) ok=0;
        }
    }
    matrix.scale=NAN;ok=ok && !n3ds_model_rigid_stream(v,4,&skin,rows);matrix.scale=-2;
    skin.node_matrix_count=0;ok=ok && !n3ds_model_rigid_stream(v,4,&skin,rows);skin.node_matrix_count=1;
    ok=ok && !n3ds_model_rigid_stream(NULL,4,&skin,rows) && !n3ds_model_rigid_stream(v,3,&skin,rows);
    raw[3].position.x=17;v=n3ds_model_decode_stream(raw,4);s=gpu_stream(v,4,1);
    struct native_render_vertex *b=s?gpu_stream_upload(s):NULL;
    ok=ok && b && b!=a && b[3].position[0]==17 && a[3].position[0]==3;
    unsigned short strips[INDEX_SLOTS+1][3];long index_count=0;
    for(unsigned int i=0;i<=INDEX_SLOTS;++i) {strips[i][0]=0;strips[i][1]=1;strips[i][2]=2;}
    const unsigned short *first=static_indices(0,strips[0],1,4,&index_count,0);
    ok=ok && index_count==3 && first!=strips[0] && static_indices(0,strips[0],1,4,&index_count,0)==first;
    /* Evict the CPU index owner while its queued GPU copy stays immutable. */
    for(unsigned int i=1;i<=INDEX_SLOTS;++i) static_indices(0,strips[i],1,4,&index_count,0);
    ok=ok && first[0]==0 && first[1]==1 && first[2]==2;
    strips[0][2]=3;
    const unsigned short *replacement=static_indices(0,strips[0],1,4,&index_count,0);
    ok=ok && replacement!=first && replacement[2]==3 && first[2]==2;
    unsigned long long previous=n3ds_gpu_model_frame_serial();
    n3ds_gpu_present();
    if(!n3ds_gpu_frame_begin()) return 0;
    ok=ok && n3ds_gpu_model_frame_serial()!=previous;
    struct native_render_vertex *c=s?gpu_stream_upload(s):NULL;
    ok=ok && c && s->uploaded_frame==n3ds_gpu_model_frame_serial() && c[3].position[0]==17;
    const unsigned short *next=static_indices(0,strips[0],1,4,&index_count,0);
    ok=ok && next && index_count==3 && next[2]==3 && static_indices(0,strips[0],1,4,&index_count,0)==next;
    raw[3].nodes[0]=3;v=n3ds_model_decode_stream(raw,4);skin.node_matrix_count=2;
    ok=ok && !n3ds_model_rigid_stream(v,4,&skin,rows);
    n3ds_gpu_present();n3ds_model_decode_cache_flush();n3ds_gpu_model_cache_release();
    if(ok) n3ds_log("PASS: rigid glass stream shares cache; signed pose matches CPU; invalid matrices/counts and mixed-node streams fall back");
    if(ok) n3ds_log("PASS: shared GPU model uploads reuse within frame, preserve queued old contents after mutation and refresh after GPU wait");
    if(ok) n3ds_log("PASS: shared model index uploads survive CPU cache eviction, same-address replacement and GPU frame retirement");
    return ok;
}
const struct native_render_vertex *n3ds_model_rigid_stream(
    const struct model_vertex_uncompressed *v,unsigned int count,
    const struct rasterizer_model_skinning *skin,float rows[12])
{
    if(!v || !skin || !skin->node_matrices || !rows || skin->node_matrix_count<=0) return NULL;
    struct decoded_stream *s=gpu_stream(v,count,skin->node_matrix_count);
    if(!s || s->rigid_node<0 || s->rigid_node>=skin->node_matrix_count) return NULL;
    const real_matrix4x3 *m=skin->node_matrices+s->rigid_node;
    for(int axis=0;axis<3;++axis) {
        rows[axis*4]=m->forward.n[axis]*m->scale;
        rows[axis*4+1]=m->left.n[axis]*m->scale;
        rows[axis*4+2]=m->up.n[axis]*m->scale;
        rows[axis*4+3]=m->position.n[axis];
        for(int c=0;c<4;++c) if(!isfinite(rows[axis*4+c])) return NULL;
    }
    return gpu_stream_upload(s);
}
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
static int model_compact_remap_tests(void);
#endif
int n3ds_model_gpu_cache_tests(void)
{
#if HALO_N3DS_ALGO_MODEL_COMPACT != N3DS_ALGORITHM_REFERENCE
    if(!model_compact_remap_tests())return 0;
#endif
    /* Transparent sorting changes the pose source independently of opaque
     * packet begin/end. Returning to the opaque packet must restore its rows. */
    struct rasterizer_model_skinning saved_skin=parameters.skinning;
    real_matrix4x3 poses[2];matrix4x3_identity(&poses[0]);poses[1]=poses[0];
    poses[0].position.x=7;poses[1].position.x=-11;poses[1].scale=-2;
    for(int i=0;i<3;++i) {
        int n=i&1;parameters.skinning.node_matrices=&poses[n];
        prepare_bone_source();const float *rows=prepare_bone_rows(0);
        if(rows[3]!=poses[n].position.x || rows[0]!=poses[n].scale) return 0;
    }
    parameters.skinning=saved_skin;prepared_bone_source=NULL;prepared_bones_invalidate();
    struct model_vertex_compressed raw[NATIVE_GPU_MODEL_BONES+1]={0};
    for(unsigned int i=0;i<NATIVE_GPU_MODEL_BONES+1;++i) {raw[i].node_weight=32767;raw[i].nodes[1]=253;raw[i].position.x=i;raw[i].normal=0x3ff;}
    const struct model_vertex_uncompressed *v=n3ds_model_decode_stream(raw,NATIVE_GPU_MODEL_BONES+1);
    struct decoded_stream *s=gpu_stream(v,NATIVE_GPU_MODEL_BONES+1,64);
    if(!s || s->rigid_node!=0 || s->gpu_nodes!=1 || s->local[25].position[0]!=25) return 0;
    /* A shared stream switches from rigid to weighted, preserving both bones
     * even when their original model indices are near the 64-node limit. */
    raw[25].node_weight=16384;raw[25].nodes[0]=62*3;raw[25].nodes[1]=63*3;
    v=n3ds_model_decode_stream(raw,NATIVE_GPU_MODEL_BONES+1);s=gpu_stream(v,NATIVE_GPU_MODEL_BONES+1,64);
    if(!s || s->rigid_node!=-1 || s->gpu_nodes!=3 || s->palette[1]!=62 || s->palette[2]!=63 || gpu_stream(v,NATIVE_GPU_MODEL_BONES+1,63)) return 0;
    const struct native_skin_vertex *weighted=(const void *)s->local;
    float influences[4];
    n3ds_skin_vertex_read_bones(weighted+25,influences);
    if(influences[0]!=3 || influences[1]!=6 || influences[2]!=v[25].node_weights[0]) return 0;
    n3ds_skin_vertex_read_bones(weighted,influences);
    if(influences[1]!=0) return 0;
    for(int i=0;i<NATIVE_GPU_MODEL_BONES+1;++i) {raw[i].nodes[0]=i*3;raw[i].nodes[1]=253;raw[i].node_weight=32767;}
    v=n3ds_model_decode_stream(raw,NATIVE_GPU_MODEL_BONES);s=gpu_stream(v,NATIVE_GPU_MODEL_BONES,64);if(!s || s->gpu_nodes!=NATIVE_GPU_MODEL_BONES) return 0;
    v=n3ds_model_decode_stream(raw,NATIVE_GPU_MODEL_BONES+1);if(gpu_stream(v,NATIVE_GPU_MODEL_BONES+1,64)) return 0;
    raw[0].position.x=NAN;if(n3ds_model_decode_stream(raw,NATIVE_GPU_MODEL_BONES+1)) return 0;raw[0].position.x=9;
    /* Only the explicit static-map publication opts out of repeated compares.
     * A map-lifetime flush must observe new contents at a reused address. */
    v=decode_stream(raw,24,1);if(!v || v[0].position.x!=9) return 0;
    n3ds_model_decode_cache_flush();raw[0].position.x=11;
    v=decode_stream(raw,24,1);if(!v || v[0].position.x!=11) return 0;
    unsigned short strips[INDEX_SLOTS+1][6];long count;
    for(unsigned int i=0;i<=INDEX_SLOTS;++i) {
        const unsigned short strip[6]={0,1,2,2,3,4};memcpy(strips[i],strip,sizeof(strip));
        const unsigned short *list=static_indices(1,strips[i],4,5,&count,0);
        const unsigned short expected[6]={0,1,2,3,2,4};
        if(count!=6 || memcmp(list,expected,sizeof(expected))) return 0;
        if(static_indices(1,strips[i],4,5,&count,0)!=list) return 0;
    }
    unsigned short *large=malloc(60012*2);if(!large) return 0;
    for(int i=0;i<60012;++i) large[i]=i%100;
    for(unsigned int triangles=20000;triangles<20005;++triangles) {
        static_indices(0,large,triangles,100,&count,0);
        if(count!=triangles*3 || index_cache_bytes>INDEX_CACHE_BYTES) {free(large);return 0;}
    }
    n3ds_model_decode_cache_flush();free(large);
    if(index_cache_bytes || decoded_bytes || pose_bytes) return 0;
    n3ds_log("PASS: GPU model cache: rigid/weighted mutation, compact high bones, 29-node boundary, invalid positions, static lifetime, strip parity, index slot/byte eviction");return 1;
}
void _rasterizer_model_draw(struct shader *shader, short permutation,
    const struct triangle_buffer *triangles, long dynamic_triangles, long triangle_count,
    const struct vertex_buffer *vertices, long dynamic_vertices);
#include "engine_model_merge.inl"
#include "engine_scenery_mesh.inl"
#include "engine_scenery_batch.inl"
#include "engine_model_compact.inl"
void _rasterizer_model_draw(struct shader *shader, short permutation,
    const struct triangle_buffer *triangles, long dynamic_triangles, long triangle_count,
    const struct vertex_buffer *vertices, long dynamic_vertices)
{
    const struct n3ds_cache_view *tags=n3ds_engine_cache_view(0);
    const void *data; const unsigned short *indices;
    unsigned int count,stride,i; short type,triangle_type;
    struct native_render_vertex *gpu;
    unsigned int material_flags=0;
    const struct native_model_definition *model_definition=NULL;
    struct native_model_material native_material={NULL,0xff000000,0xffffffff,0};
    real_vector2d uv_scale=parameters.base_map_scale;
    long bitmap, index_count;
    assert(models_active && model_active && triangle_count>=0 && triangle_count<=65535);
    if (!triangle_count || n3ds_presentation_midpoint_rejected()) return;
    if((parameters.geometry_flags&((1u<<4)|(1u<<7))) || depth_only || parameters.effect.type ||
       (shader->base.type!=3 && shader->base.type!=4))scenery_batch_flush();
    if (dynamic_vertices!=NONE) {
        const struct native_vertex_view *v=n3ds_dynamic_vertices_get(dynamic_vertices);
        assert(!vertices && v); data=v->data; count=v->count; type=v->type;
    } else {
        assert(vertices && ((active_merged && vertices==&active_merged->vertices) || n3ds_cache_resolve(tags,vertices,sizeof(*vertices))==vertices));
        data=vertices->base_address; count=vertices->count; type=vertices->type;
    }
    assert(count>0 && count<=65535 && (type==4 || type==5));
    stride=type==4 ? 68 : 32;
    if (dynamic_vertices==NONE && !active_merged) assert(data && n3ds_cache_resolve(tags,data,count*stride)==data);
    if (dynamic_triangles!=NONE) {
        const struct native_triangle_view *t=n3ds_dynamic_triangles_get(dynamic_triangles);
        assert(!triangles && t && (unsigned long)triangle_count<=t->count);
        indices=t->indices; triangle_type=0;
    } else {
        assert(triangles && ((active_merged && triangles==&active_merged->triangles) || n3ds_cache_resolve(tags,triangles,sizeof(*triangles))==triangles));
        assert(triangle_count<=triangles->count && (triangles->type==0 || triangles->type==1));
        indices=triangles->base_address; triangle_type=triangles->type;
        i=triangle_type ? (triangle_count+2)*2 : triangle_count*6;
        assert(indices && ((active_merged && indices==active_merged->triangles.base_address) || n3ds_cache_resolve(tags,indices,i)==indices));
    }
    const unsigned short *draw_indices;
    struct cached_indices *index_owner=NULL;
    if(dynamic_triangles==NONE)
        draw_indices=static_indices_prepare(triangle_type,indices,triangle_count,count,&index_count,&index_owner);
    else {index_count=n3ds_model_triangle_list(triangle_type,indices,triangle_count,count,index_scratch,65535*3);draw_indices=index_scratch;}
    assert(index_count!=NONE);
    if (!index_count) return;
    /* Distant named foliage only. Whole connected islands retain exact UVs,
     * normals and bone weights; solid connected geometry is never thinned. */
    if((parameters.geometry_flags&(1u<<17)) && (parameters.geometry_flags&(1u<<18)) &&
       !depth_only && !parameters.effect.type && dynamic_vertices==NONE && dynamic_triangles==NONE && type==5 && (shader->base.type==6 || shader->base.type==4)) {
        extern int n3ds_input_platform_original_model(void);
        int original=n3ds_input_platform_original_model();
        unsigned int quality=(parameters.geometry_flags&(1u<<19))?(original?6:4):(original?4:2);
        struct scenery_mesh *s=scenery_mesh_get(data,indices,count,draw_indices,index_count,quality);
        if(s){scenery_saved_triangles+=(index_count-s->index_count)/3;scenery_saved_vertices+=count-s->count;data=s->vertices;count=s->count;draw_indices=s->indices;index_count=s->index_count;}
    }
    const void *scenery_index_key=draw_indices==index_scratch?NULL:indices;
    /* Special paths keep the original upload timing. Ordinary opaque parts
     * defer it until resident scenery/compaction have declined the draw. */
    if(depth_only || parameters.effect.type || (shader->base.type!=3 && shader->base.type!=4))
        draw_indices=model_indices_submit(draw_indices,index_count,index_owner,dynamic_vertices==NONE);
    const struct model_vertex_uncompressed *known_decoded=NULL;
    if(dynamic_vertices==NONE && type==5) {
        /* Explicitly publish a checked map-owned immutable vertex buffer.
         * Dynamic staging and standalone tests keep full byte comparisons. */
        MODEL_PROFILE_START();
        known_decoded=decode_stream(data,count,1);
        MODEL_PROFILE_END(0);
    }
    assert(shader && n3ds_cache_resolve(tags,shader,sizeof(*shader))==shader);
    if(depth_only || (shader->base.type!=3 && shader->base.type!=4) || parameters.effect.type==1)
        n3ds_gpu_model_batch_flush();
    if(depth_only) {
        const real_vector2d unit_scale={1,1};
        const struct model_vertex_uncompressed *cached=known_decoded?known_decoded:type==5?n3ds_model_decode_stream(data,count):NULL;
        /* Generic hologram colour uses the cached CPU pose. Its self-occlusion
         * prepass must use those exact same positions: independently skinning
         * on PICA introduces depth-rounding disagreement and flickering facets.
         * The colour pass reuses this pose, so skinning still happens once. */
        if(shader->base.type!=5 && special_gpu_stream(cached,count,draw_indices,index_count,1,NULL,1,0,1)) return;
        const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&parameters.skinning,&unit_scale,parameters.unique_identifier);
        gpu=n3ds_gpu_model_vertices_allocate(count);assert(gpu);
        if(posed) memcpy(gpu,posed,count*sizeof(*gpu));
        else for(unsigned int v=0;v<count;++v) {
            struct model_vertex_uncompressed decoded;
            const struct model_vertex_uncompressed *vertex=cached?cached+v:&decoded;
            if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+v*stride),&decoded));}
            else if(!cached) memcpy(&decoded,(const byte *)data+v*stride,sizeof(decoded));
            assert(n3ds_model_skin_vertex(vertex,&parameters.skinning,&unit_scale,gpu+v));
        }
        n3ds_gpu_model_rebase_vertices(gpu,count,0);
        n3ds_gpu_geometry_flush(gpu,count*sizeof(*gpu));
        assert(n3ds_gpu_model_depth_relative_draw(gpu,draw_indices,index_count));return;
    }
    if(shader->base.type==10) {
        n3ds_plasma_model_draw(shader,permutation,&parameters,data,count,type,draw_indices,index_count);
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    if(shader->base.type==7) {
        n3ds_water_model_draw(shader,permutation,&parameters,data,count,type,draw_indices,index_count);
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    if(shader->base.type==8) {
        n3ds_glass_model_draw(shader,permutation,&parameters,data,count,type,draw_indices,index_count);
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    if(shader->base.type==9) {
        n3ds_meter_model_draw(shader,permutation,&parameters,data,count,type,draw_indices,index_count);
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    if(shader->base.type==5 || shader->base.type==6) {
        n3ds_chicago_model_draw(shader,permutation,&parameters,data,count,type,draw_indices,index_count);
        ++counters.parts; counters.triangles+=(unsigned int)index_count/3;
        return;
    }
    if (shader->base.type==4) {
        const struct native_model_definition *material=(const void *)shader;
        assert(n3ds_cache_resolve(tags,material,sizeof(*material))==material);
        material_flags=material->flags;
        uv_scale.i*=material->u; uv_scale.j*=material->v; bitmap=material->base_map.index;
        model_definition=material;
    } else {
        struct environment_prefix { byte header[0x88]; struct tag_reference base_map; };
        const struct environment_prefix *material=(const void *)shader;
        assert(shader->base.type==3 && n3ds_cache_resolve(tags,material,sizeof(*material))==material);
        bitmap=material->base_map.index;
    }
    const struct model_vertex_uncompressed *cached=known_decoded?known_decoded:type==5?n3ds_model_decode_stream(data,count):NULL;
    if(parameters.effect.type==1) {
        /* PICA profile: translucent base material, driven by the real cloak
         * intensity. Refraction of the previous framebuffer is not available. */
        struct native_chicago_material cloak={.maps=1,.blend=0,.two_sided=!!(material_flags&2),
            .fade=1.f-.92f*PIN(parameters.effect.intensity,0.f,1.f)};
        boolean bound=rasterizer_set_texture_direct(0,bitmap,permutation);
        if(n3ds_presentation_midpoint_rejected()) return;
        cloak.vertex_color=!bound;
        if(special_gpu_stream(cached,count,draw_indices,index_count,0,uv_scale.n,cloak.fade,bound,cloak.two_sided)) {
            ++counters.parts;counters.triangles+=index_count/3;return;
        }
        struct native_chicago_vertex *out=n3ds_gpu_chicago_allocate(count);assert(out);
        const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&parameters.skinning,&uv_scale,parameters.unique_identifier);
        float relative_origin[3];n3ds_gpu_camera_origin(relative_origin);
        for(unsigned int v=0;v<count;++v) {
            struct model_vertex_uncompressed decoded;struct native_render_vertex temporary;
            const struct model_vertex_uncompressed *vertex=cached?cached+v:&decoded;
            const struct native_render_vertex *skin=posed?posed+v:&temporary;
            if(!posed) {
                if(!cached && type==5) {assert(n3ds_model_decode_vertex((const void *)((const byte *)data+v*stride),&decoded));}
                else if(!cached) memcpy(&decoded,(const byte *)data+v*stride,sizeof(decoded));
                assert(n3ds_model_skin_vertex(vertex,&parameters.skinning,&uv_scale,&temporary));
            }
            memset(out+v,0,sizeof(*out));memcpy(out[v].position,skin->position,sizeof(skin->position));
            for(int c=0;c<3;++c) out[v].position[c]-=relative_origin[c];
            memcpy(out[v].uv[0],skin->uv,sizeof(skin->uv));
            memcpy(out[v].color,skin->color,sizeof(skin->color));out[v].color[3]=1;
        }
        assert(n3ds_gpu_chicago_relative_draw(&cloak,out,count,draw_indices,index_count));
        static int reported;if(!reported) {n3ds_log("NATIVE CAMOUFLAGE: translucent model profile; original cloak intensity, pose and gameplay retained");reported=1;}
        ++counters.parts;counters.triangles+=index_count/3;return;
    }
    struct decoded_stream *rigid=gpu_stream(cached,count,parameters.skinning.node_matrix_count);
#ifdef HALO_N3DS_RENDERER_TESTS
    static unsigned int material_sequence;
    int sampled=rigid && !(++material_sequence&31);
    long long material_stamps[5];if(sampled) material_stamps[0]=n3ds_engine_ticks();
#endif
    if(model_definition) native_material=model_material(model_definition,permutation,rigid && !(parameters.geometry_flags&(1u<<4)));
    if(n3ds_presentation_midpoint_rejected()) return;
    if(rigid && !(parameters.geometry_flags&(1u<<4))) {
        native_material.lighting=&native_lighting;
    }
    if(rigid) {
#ifdef HALO_N3DS_RENDERER_TESTS
        if(sampled) material_stamps[1]=n3ds_engine_ticks();
#endif
        unsigned int nodes=rigid->rigid_node>=0?1:rigid->gpu_nodes;
        float compact_rows[NATIVE_GPU_MODEL_BONES*12];
        const float *rows=gpu_stream_rows(rigid,compact_rows);
        gpu=NULL;
#ifdef HALO_N3DS_RENDERER_TESTS
        if(sampled) material_stamps[2]=n3ds_engine_ticks();
#endif
        if(native_material.lighting) native_material.base=n3ds_engine_bitmap_resource(bitmap,permutation);
        else if(rasterizer_set_texture_direct(0,bitmap,permutation)) native_material.base=n3ds_gpu_texture_bound(0);
        if(n3ds_presentation_midpoint_rejected()) return;
        boolean bound=native_material.base!=NULL;
        if(material_flags&(1u<<3)) assert(bound);
        if(!bound) n3ds_gpu_texture_bind(0,NULL);
        if(!bound) native_material.alpha_test=0;
        int sky=!!(parameters.geometry_flags&(1u<<4)),decal=!!(material_flags&(1u<<3)),two_sided=!!(material_flags&((1u<<1)|(1u<<5)));
#ifdef HALO_N3DS_RENDERER_TESTS
        if(sampled) material_stamps[3]=n3ds_engine_ticks();
#endif
        if((parameters.geometry_flags&(1u<<16)) && dynamic_vertices==NONE && dynamic_triangles==NONE && rigid->immutable && rigid->rigid_node>=0 && scenery_index_key && !sky && !decal && !parameters.effect.type &&
           scenery_batch_add(data,scenery_index_key,(const void *)rigid->local,count,draw_indices,index_count,rows,uv_scale.n,two_sided,&native_material)) {
            ++counters.parts;counters.triangles+=index_count/3;return;
        }
        const struct model_compact_mesh *compact=NULL;
        if(dynamic_vertices==NONE && dynamic_triangles==NONE && rigid->immutable && !sky && !decal &&
           !first_person_active && !(parameters.geometry_flags&((1u<<7)|(1u<<18))) && !parameters.effect.type)
            compact=model_compact_get(data,indices,triangle_count,triangle_type,rigid,draw_indices,index_count);
        /* Opaque depth-writing packets can pass queued scenery. Preserve
         * ordered boundaries for weapon overlays, skies and decals. */
        if(sky || decal || first_person_active || parameters.effect.type)scenery_batch_flush();
        int drew;
        if(compact) {
            drew=n3ds_gpu_model_compact_draw_resident(compact->vertices,compact->indices,index_count,
                rows,nodes,uv_scale.n,two_sided,rigid->rigid_node<0,&native_material);
        } else {
            draw_indices=model_indices_submit(draw_indices,index_count,index_owner,dynamic_vertices==NONE);
            gpu=gpu_stream_upload(rigid);assert(gpu);
            if(rigid->rigid_node>=0) drew=n3ds_gpu_model_rigid_draw(gpu,draw_indices,index_count,rows,uv_scale.n,sky,decal,two_sided,&native_material);
            else drew=n3ds_gpu_model_skin_draw((const void *)gpu,draw_indices,index_count,rows,nodes,uv_scale.n,sky,decal,two_sided,&native_material);
        }
        assert(drew);
#ifdef HALO_N3DS_RENDERER_TESTS
        unsigned int reflective=native_material.reflection && native_material.multipurpose;
        ++material_profile[reflective][0];
        if(sampled) {
            material_stamps[4]=n3ds_engine_ticks();++material_profile[reflective][1];
            for(int k=0;k<4;++k) material_profile[reflective][k+2]+=material_stamps[k+1]-material_stamps[k];
        }
#endif
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
        if(drew && !decal && n3ds_presentation_midpoint_active()) ++presentation_model_draw_serial;
#endif
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    n3ds_gpu_model_batch_flush();
    draw_indices=model_indices_submit(draw_indices,index_count,index_owner,dynamic_vertices==NONE);
    gpu=n3ds_gpu_model_vertices_allocate(count); assert(gpu);
#ifdef HALO_N3DS_RENDERER_TESTS
    static int first_person_cpu_reported;
    if(first_person_active && !first_person_cpu_reported) {
        char line[160];snprintf(line,sizeof(line),"MODEL PRECISION: first-person CPU fallback rebased; nodes=%d vertices=%u",parameters.skinning.node_matrix_count,count);
        n3ds_log(line);first_person_cpu_reported=1;
    }
#endif
    const struct native_render_vertex *posed=n3ds_model_skin_stream(cached,count,&parameters.skinning,&uv_scale,parameters.unique_identifier);
    if(posed) memcpy(gpu,posed,count*sizeof(*gpu));
    else for (i=0;i<count;++i) {
        struct model_vertex_uncompressed decoded;
        const struct model_vertex_uncompressed *vertex=cached?cached+i:&decoded;
        if (!cached && type==5) { assert(n3ds_model_decode_vertex((const void *)((const byte *)data+i*stride),&decoded)); }
        else if(!cached) memcpy(&decoded,(const byte *)data+i*stride,sizeof(decoded));
        assert(n3ds_model_skin_vertex(vertex,&parameters.skinning,&uv_scale,gpu+i));
    }
    if(parameters.geometry_flags&(1u<<4)) for(i=0;i<count;++i) gpu[i].color[0]=gpu[i].color[1]=gpu[i].color[2]=1.f;
    n3ds_gpu_model_rebase_vertices(gpu,count,!!(parameters.geometry_flags&(1u<<4)));
    n3ds_gpu_geometry_flush(gpu,count*sizeof(*gpu));
    boolean texture_bound=rasterizer_set_texture_direct(0,bitmap,permutation);
    if(n3ds_presentation_midpoint_rejected()) return;
    if(material_flags&(1u<<3)) {
        scenery_batch_flush();
        if(!texture_bound) {
            for(long t=0;t<tags->tag_header->tag_count;++t) if(tags->instances[t].base_address==shader) {
                char message[240];snprintf(message,sizeof(message),"MODEL DECAL BITMAP: tag=%08lx bitmap=%08lx permutation=%d textures=%u bytes=%u name=%.120s",tags->instances[t].tag_index,bitmap,permutation,n3ds_engine_texture_count(),n3ds_engine_texture_bytes(),tags->instances[t].name);n3ds_log(message);break;
            }
        }
        assert(texture_bound);
        assert(n3ds_gpu_model_cpu_relative_draw(gpu,draw_indices,index_count,!!(parameters.geometry_flags&(1u<<4)),1,!!(material_flags&((1u<<1)|(1u<<5))),&native_material));
        static const void *seen[32];static unsigned int seen_count;
        for(i=0;i<seen_count && seen[i]!=shader;++i) {}
        if(i==seen_count && seen_count<32) {
            seen[seen_count++]=shader;
            for(long t=0;t<tags->tag_header->tag_count;++t) if(tags->instances[t].base_address==shader) {
                char message[224];snprintf(message,sizeof(message),"NATIVE MODEL DECAL: tag=%08lx flags=%04x triangles=%u name=%.140s",tags->instances[t].tag_index,material_flags,(unsigned int)index_count/3,tags->instances[t].name);n3ds_log(message);break;
            }
        }
        ++counters.parts;counters.triangles+=(unsigned int)index_count/3;return;
    }
    if(!texture_bound) n3ds_gpu_texture_bind(0,NULL);
    if(!texture_bound) native_material.alpha_test=0;
    int drew=n3ds_gpu_model_cpu_relative_draw(gpu,draw_indices,index_count,!!(parameters.geometry_flags&(1u<<4)),0,!!(material_flags&((1u<<1)|(1u<<5))),&native_material);
    assert(drew);
#if HALO_N3DS_ALGO_MIDPOINT_PARTICLES != N3DS_ALGORITHM_REFERENCE
    if(drew && n3ds_presentation_midpoint_active()) ++presentation_model_draw_serial;
#endif
    ++counters.parts; counters.triangles+=(unsigned int)index_count/3;
}

struct transparent_geometry_group *_rasterizer_model_transparent_geometry_submit(
    struct shader *shader, short permutation, const struct triangle_buffer *triangles,
    long dynamic_triangles, long count, const struct vertex_buffer *vertices,
    long dynamic_vertices, const real_point3d *centroid, struct render_sort_filth *sort)
{
    struct transparent_geometry_group immediate, *group;
    const struct rasterizer_model_begin_parameters *packet;
    unsigned long flags;
    real_vector3d relative;
    boolean no_queue;
    assert(models_active && model_active);
    if(sort) { sort->group_index=NONE; sort->previous_group_presorted_index_reference=NULL; sort->next_group_presorted_index_reference=NULL; }
    if(!rasterizer_debug_options.models || !rasterizer_debug_options.model_transparents) return NULL;
    assert(shader && centroid && shader_type_is_valid_for_model(shader->base.type));
    assert(count>=0 && count<=65535);
    assert(parameters.effect.type==0 || parameters.effect.type==1 || (parameters.effect.type==2 && !parameters.effect.modifier_shader));
    /* The original model pass submits alpha-blended decal layers separately. */
    if(shader->base.type==4 && (*(const word *)((const byte *)shader+0x28)&(1<<3))) return NULL;
    flags=parameters.geometry_flags;
    if(shader_is_decal(shader)) flags|=3;
    no_queue=(flags&2)!=0;
    if(no_queue) { group=&immediate; packet=&parameters; }
    else {
        if(!queued_parameters) {
            unsigned long bytes=sizeof(parameters)+parameters.skinning.node_matrix_count*sizeof(*matrices);
            queued_parameters=rasterizer_memory_alloc(NULL,bytes);
            if(!queued_parameters) { n3ds_log("Native transparent pose pool exhausted; model submission rejected"); return NULL; }
            *queued_parameters=parameters;
            queued_parameters->skinning.node_matrices=(const real_matrix4x3 *)(queued_parameters+1);
            memcpy(queued_parameters+1,matrices,bytes-sizeof(parameters));
        }
        packet=queued_parameters;
        group=rasterizer_transparent_geometry_new_group();
        if(!group) { n3ds_log("Native transparent group budget exhausted; model submission rejected"); return NULL; }
    }
    /* Allocation's sorted_index survives zero-initialization of all reserved
     * fields. This also prevents a previous view's pending data leaking in. */
    { long sorted=no_queue?NONE:group->sorted_index; memset(group,0,sizeof(*group)); group->sorted_index=sorted; }
    group->geometry_flags=flags; group->object_index=packet->unique_identifier;
    group->shader=shader; group->shader_permutation_index=permutation;
    group->effect=packet->effect; group->centroid=*centroid;
    if(packet->effect.type) {group->source_object_index=packet->effect.source_object_index;group->centroid=packet->effect.source_object_centroid;}
    group->dynamic_triangle_buffer_index=dynamic_triangles; group->triangle_buffer=triangles;
    group->triangle_count=count; group->dynamic_vertex_buffer_index=dynamic_vertices; group->vertex_buffer=vertices;
    group->model_base_map_scale=packet->base_map_scale;
    group->node_matrices=packet->skinning.node_matrices; group->node_matrix_count=packet->skinning.node_matrix_count;
    group->lighting=&packet->lighting; group->animation=&packet->animation;
    vector_from_points3d(&global_window_parameters.camera.position,&group->centroid,&relative);
    group->z_sort=-dot_product3d(&global_window_parameters.camera.forward,&relative);
    group->previous_group_presorted_index=group->next_group_presorted_index=NONE;
    group->cortana_hack=rasterizer_model_cortana_hack;
    if(!no_queue && sort) {
        sort->group_index=rasterizer_transparent_geometry_get_group_presorted_index(group);
        sort->previous_group_presorted_index_reference=&group->previous_group_presorted_index;
        sort->next_group_presorted_index_reference=&group->next_group_presorted_index;
    }
    if(rasterizer_debug_options.stats==2) {
        ++rasterizer_frame_statistics.transparent_model_submit_count;
        rasterizer_frame_statistics.transparent_model_triangle_count+=count;
        rasterizer_frame_statistics.transparent_model_maximum_triangle_count=MAX(count,rasterizer_frame_statistics.transparent_model_maximum_triangle_count);
        rasterizer_frame_statistics.transparent_model_vertex_count+=rasterizer_frame_statistics_count_static_vertices(triangles,vertices);
    }
    if(no_queue) {
        rasterizer_transparent_geometry_groups_begin();
        rasterizer_transparent_geometry_group_draw(group,FALSE);
        rasterizer_transparent_geometry_groups_end();
        return NULL;
    }
    return group;
}

void n3ds_model_draw_group(const struct transparent_geometry_group *group)
{
    scenery_batch_flush();
    int saved_batch=scenery_batch_allowed;scenery_batch_allowed=0;
    struct rasterizer_model_begin_parameters saved=parameters;
    int saved_models=models_active, saved_model=model_active, own_first_person=0;
    assert(n3ds_engine_window_active() && group && group->shader);
    assert((group->effect.type==0 || group->effect.type==1 || (group->effect.type==2 && !group->effect.modifier_shader)) && !group->first_triangle_index);
    assert(!(group->geometry_flags&((1u<<5)|(1u<<8))));
    assert(!!(group->geometry_flags&(1u<<3))==!!(group->geometry_flags&(1u<<4)));
    if(group->shader->base.type<3 || group->shader->base.type>10) {
        char message[192];
        snprintf(message,sizeof(message),"NATIVE MODEL MATERIAL: unsupported shader_type=%d geometry_flags=%08lx triangles=%ld nodes=%d",
            group->shader->base.type,group->geometry_flags,group->triangle_count,group->node_matrix_count);
        n3ds_log(message);
        const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
        for(long t=0;t<view->tag_header->tag_count;++t) if(view->instances[t].base_address==group->shader) {
            snprintf(message,sizeof(message),"NATIVE MODEL MATERIAL TAG: index=%08lx name=%.130s",view->instances[t].tag_index,view->instances[t].name);
            n3ds_log(message);
        }
    }
    assert(group->shader->base.type>=3 && group->shader->base.type<=10);
    assert(group->node_matrices && group->node_matrix_count>0 && group->node_matrix_count<=64);
    assert(group->lighting && group->animation);
    /* Restore the submitting object's identity as well as its pose. Otherwise
     * sorted transparent draws compete for the last opaque object's cache slot. */
    parameters.unique_identifier=group->object_index;
    parameters.geometry_flags=group->geometry_flags;
    parameters.skinning.node_matrices=group->node_matrices;
    parameters.skinning.node_matrix_count=group->node_matrix_count;
    parameters.base_map_scale=group->model_base_map_scale;
    parameters.lighting=*group->lighting; parameters.animation=*group->animation;model_lighting_prepare();
    parameters.effect=group->effect;
    parameters.centroid=group->centroid;
    /* Material results include this object's animated colors, reflection
     * tint and centroid. Sorted groups are separate packets even when their
     * shader/permutation matches the last opaque model. */
    packet_material_count=0;
    if((group->geometry_flags&(1u<<7)) && !first_person_active) {
        assert(n3ds_gpu_first_person_begin(rasterizer_globals.first_person_weapon_near_clip_distance,rasterizer_globals.first_person_weapon_far_clip_distance));
        own_first_person=1;
    }
    models_active=model_active=1;
    _rasterizer_model_draw(group->shader,group->shader_permutation_index,group->triangle_buffer,
        group->dynamic_triangle_buffer_index,group->triangle_count,group->vertex_buffer,group->dynamic_vertex_buffer_index);
    if(own_first_person) n3ds_gpu_first_person_end();
    parameters=saved;model_lighting_prepare();packet_material_count=0;
    models_active=saved_models;model_active=saved_model;scenery_batch_allowed=saved_batch;
}
void n3ds_model_depth_group(const struct transparent_geometry_group *group)
{
    assert(!depth_only && group->effect.type==2);
    depth_only=1;n3ds_model_draw_group(group);depth_only=0;
}

#include "engine_parallel_model_tests.inl"
