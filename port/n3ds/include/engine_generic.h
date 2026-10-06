#ifndef N3DS_ENGINE_GENERIC_H
#define N3DS_ENGINE_GENERIC_H
#define NATIVE_GENERIC_BAKE_SIZE 8
#define NATIVE_GENERIC_SAMPLE_SIZE 4
/* Evaluated animation constants are RGBA. Operations retain original tag enums.
 * This packet crosses the Clang/GCC boundary; no engine long/bool fields. */
struct native_generic_stage { int flags,op[26]; float constant[2][4]; };
struct native_generic_material {
    int maps,stages,point[4],clamp_u[4],clamp_v[4];
    void *textures[4];
    float transform[4][2][4];
    struct native_generic_stage stage[8];
};
int n3ds_generic_validate(const struct native_generic_material *m);
/* Exact two-map alpha interpolation, vertex0 lighting and inverse map1 blue
 * alpha packet. Recognize operations, never tag names; other packets fall back. */
int n3ds_generic_lit_lerp(const struct native_generic_material *m);
/* Exact authored combiner recognition: 1 hair, 2 body, 0 fallback. */
int n3ds_generic_hologram(const struct native_generic_material *m);
void *n3ds_gpu_hologram_stream_bake(const struct native_generic_material *m,const float bounds[4]);
/* Four-map seven-stage shield packet. Recognize once per draw/bake; the
 * reduced CPU expression retains all sampled channels and animated constants.
 * This is a fallback optimization, not a native GPU material path. */
int n3ds_generic_shield(const struct native_generic_material *m);
void n3ds_generic_shield_evaluate(const struct native_generic_material *m,
    const float samples[4][4],float result[4]);
float n3ds_generic_shield_intensity(const struct native_generic_material *m,const float alpha[4]);
/* Same bounded animation grid, alpha only; native mask/tint composition. */
void *n3ds_gpu_shield_bake(const struct native_generic_material *m,const float bounds[4]);
/* Register/component masks: bit0 vertex0, bit1 vertex1; RGBA output bits1,2,4,8.
 * Missing vertex/scratch inputs remain rejected. Disabled Xbox texture
 * stages supply RGB zero and alpha one. */
int n3ds_generic_validate_inputs(const struct native_generic_material *m,unsigned int vertex_mask,unsigned int output_mask);
void n3ds_generic_evaluate_inputs(const struct native_generic_material *m,
    const float samples[4][4],const float vertex[2][4],float result[4]);
void n3ds_gpu_generic_sample(const struct native_generic_material *m,int map,float u,float v,float result[4]);
void n3ds_generic_evaluate(const struct native_generic_material *m,
    const float samples[4][4],float result[4]);
int n3ds_generic_tests(void);
int n3ds_chicago_program(struct native_generic_material *,int maps,const int color[3],const int alpha[3],const int replicate[3]);
/* Low-detail UV-domain baking; returned handles are safe through this frame.
 * Immutable source/packet matches can reuse resident outputs across frames;
 * callers must still reacquire a handle after each waited FrameBegin.
 * Vertex-dependent combiner inputs are deliberately rejected by validation. */
void *n3ds_gpu_generic_bake(const struct native_generic_material *m,const float bounds[4]);
void *n3ds_gpu_meter_bake(const struct native_generic_material *m,const float bounds[4]);
/* RGBA: min(value), max(flash-width), flash(extension), background, tint. */
void n3ds_meter_program(struct native_generic_material *m,const float constants[5][4],int negative_flash);
void n3ds_hud_meter_program(struct native_generic_material *m,const float constants[5][4],int negative_flash);
#endif
