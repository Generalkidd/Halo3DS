#ifndef HALO_N3DS_ENGINE_RENDERER_FEATURES_H
#define HALO_N3DS_ENGINE_RENDERER_FEATURES_H

#include "engine_algorithm_features.h"

/* Independent mode IDs are stable build/report identities, not enable bits.
 * Keep them in sync with build.py. Legacy includes the existing texture cache
 * and archives; the candidate RGBA8 archive is a separate opt-in format.
 * Runtime geometry/float layout retain all existing rendering optimisations.
 * These selectors do not change simulation, saved state or checkpoint layout. */
#define N3DS_RENDERER_FEATURE_SCHEMA 1
#define N3DS_TEXTURE_STORAGE_LEGACY 0
#define N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE 1
#define N3DS_GEOMETRY_SOURCE_RUNTIME 0
#define N3DS_GEOMETRY_SOURCE_COOKED 1
#define N3DS_VERTEX_LAYOUT_FLOAT 0
#define N3DS_VERTEX_LAYOUT_PACKED 1
#define N3DS_VERTEX_LAYOUT_PACKED_VERIFY 2

#ifndef HALO_N3DS_TEXTURE_STORAGE
#define HALO_N3DS_TEXTURE_STORAGE N3DS_TEXTURE_STORAGE_LEGACY
#endif
#ifndef HALO_N3DS_GEOMETRY_SOURCE
#define HALO_N3DS_GEOMETRY_SOURCE N3DS_GEOMETRY_SOURCE_RUNTIME
#endif
#ifndef HALO_N3DS_VERTEX_LAYOUT
#define HALO_N3DS_VERTEX_LAYOUT N3DS_VERTEX_LAYOUT_FLOAT
#endif

/* Fail closed for direct compiler builds too. Do not turn these checks into
 * opt-in overrides: each candidate needs its implementation and review first. */
#if HALO_N3DS_TEXTURE_STORAGE != N3DS_TEXTURE_STORAGE_LEGACY && \
    HALO_N3DS_TEXTURE_STORAGE != N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
#error "Unsupported texture storage; select legacy (0) or candidate archive (1)"
#endif
/* A misspelled symbolic -D value is zero in #if expressions. Require real
 * language constant expressions as well, so it cannot masquerade as legacy.
 * Typedef guards work in the C99 engine and C++11 platform translation units. */
typedef char n3ds_texture_storage_supported[(HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_LEGACY ||
    HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE)?1:-1];
typedef char n3ds_geometry_source_supported[(HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_RUNTIME ||
    HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED)?1:-1];
typedef char n3ds_vertex_layout_supported[(HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_FLOAT ||
    HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_PACKED ||
    HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_PACKED_VERIFY)?1:-1];
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
#define N3DS_TEXTURE_STORAGE_NAME "candidate-archive"
#else
#define N3DS_TEXTURE_STORAGE_NAME "legacy"
#endif
#if HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_COOKED
#define N3DS_GEOMETRY_SOURCE_NAME "cooked"
#else
#define N3DS_GEOMETRY_SOURCE_NAME "runtime"
#endif
#define N3DS_VERTEX_LAYOUT_PACKED_ENABLED (HALO_N3DS_VERTEX_LAYOUT != N3DS_VERTEX_LAYOUT_FLOAT)
#if HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_PACKED_VERIFY
#define N3DS_VERTEX_LAYOUT_NAME "packed-verify"
#elif N3DS_VERTEX_LAYOUT_PACKED_ENABLED
#define N3DS_VERTEX_LAYOUT_NAME "packed"
#else
#define N3DS_VERTEX_LAYOUT_NAME "float"
#endif
#define N3DS_RENDERER_ALGORITHM_VERIFICATION \
    (N3DS_ALGORITHM_VERIFICATION || HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_PACKED_VERIFY)

/* engine_frontend_boot.c historically defaults to 180 ticks for direct
 * builds. Reflect that value without defining/changing its gameplay macro. */
#ifdef HALO_N3DS_MISSION_TICKS
#define N3DS_RENDERER_CONFIGURED_MISSION_TICKS HALO_N3DS_MISSION_TICKS
#else
#define N3DS_RENDERER_CONFIGURED_MISSION_TICKS 180
#endif
#ifdef HALO_N3DS_RENDERER_TESTS
#define N3DS_RENDERER_DIAGNOSTICS_CONFIGURED 1
#else
#define N3DS_RENDERER_DIAGNOSTICS_CONFIGURED 0
#endif
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
#define N3DS_RENDERER_PROFILE_CONFIGURED 1
#else
#define N3DS_RENDERER_PROFILE_CONFIGURED 0
#endif
#ifndef HALO_N3DS_MIDPOINT_CAPTURE_TICK
#define HALO_N3DS_MIDPOINT_CAPTURE_TICK 0
#endif
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK < 0 || HALO_N3DS_MIDPOINT_CAPTURE_TICK > 2147483647
#error "Midpoint capture start tick must be 0..2147483647"
#endif
typedef char n3ds_midpoint_capture_tick_valid[(HALO_N3DS_MIDPOINT_CAPTURE_TICK >= 0 &&
    HALO_N3DS_MIDPOINT_CAPTURE_TICK <= 2147483647) ? 1 : -1];
#if HALO_N3DS_MIDPOINT_CAPTURE_TICK && (!defined(HALO_N3DS_PRESENT_INTERPOLATION) || N3DS_RENDERER_PROFILE_CONFIGURED || \
    N3DS_RENDERER_CONFIGURED_MISSION_TICKS || N3DS_RENDERER_DIAGNOSTICS_CONFIGURED || defined(HALO_N3DS_LOW_MEMORY_TEST))
#error "Midpoint capture requires a separate midpoint correctness build"
#endif

/* Frame limits capture loading/final images even without renderer diagnostics.
 * Classify all current developer fixtures separately from uncaptured profile
 * candidates. New readback/validation fixtures must also be listed here. */
#if N3DS_RENDERER_CONFIGURED_MISSION_TICKS || N3DS_RENDERER_DIAGNOSTICS_CONFIGURED || HALO_N3DS_MIDPOINT_CAPTURE_TICK || \
    defined(HALO_N3DS_PLASMA_SHIELD_TEST) || defined(HALO_N3DS_CHECKPOINT_TEST)
#define N3DS_RENDERER_READBACK_CONFIGURED 1
#else
#define N3DS_RENDERER_READBACK_CONFIGURED 0
#endif
#if N3DS_RENDERER_READBACK_CONFIGURED || HALO_N3DS_ACTION_REPLAY || defined(HALO_N3DS_LOW_MEMORY_TEST) || \
    defined(HALO_N3DS_CHECKPOINT_TEST) || defined(HALO_N3DS_MAP_CYCLE_TEST) || \
    defined(HALO_N3DS_MAW_CUTSCENE_TEST) || defined(HALO_N3DS_SNIPER_INVENTORY_TEST) || \
    defined(HALO_N3DS_GRENADE_INVENTORY_TEST) || defined(HALO_N3DS_CAMO_TEST) || \
    defined(HALO_N3DS_FATAL_EXIT_TEST) || defined(HALO_N3DS_HIDDEN_HUD_AIM_TEST) || \
    defined(HALO_N3DS_BSP_PROBE) || defined(HALO_N3DS_MEMORY_CHECKS) || \
    defined(HALO_N3DS_WORLD_SCALE_TEST) || defined(HALO_N3DS_TEXTURE_PRESSURE_TEST) || \
    defined(HALO_N3DS_ARCHIVE_VERIFY) || defined(HALO_N3DS_CROWD_VERIFY)
#define N3DS_RENDERER_VALIDATION_CONFIGURED 1
#else
#define N3DS_RENDERER_VALIDATION_CONFIGURED 0
#endif
#if HALO_N3DS_PROFILE_FRAMES && N3DS_RENDERER_VALIDATION_CONFIGURED
#error "Non-readback profile frame limit cannot be mixed with validation fixtures"
#endif
#if defined(HALO_N3DS_DEPENDENCY_TRACE) && N3DS_RENDERER_VALIDATION_CONFIGURED
#error "Dependency trace cannot be mixed with readback or validation fixtures"
#endif
#ifdef HALO_N3DS_ALLOCATION_PROFILE
#define N3DS_RENDERER_ALLOCATION_OBSERVATION 1
#else
#define N3DS_RENDERER_ALLOCATION_OBSERVATION 0
#endif
#if (defined(HALO_N3DS_GPU_PROBE) || defined(HALO_N3DS_FRAMEBUFFER_REUSE) || defined(HALO_N3DS_OPAQUE_COLOR_READS) || \
     defined(HALO_N3DS_UNUSED_TEXTURE_UNITS) || defined(HALO_N3DS_TEXTURE_VRAM_MIRRORS)) && \
    (N3DS_RENDERER_ALGORITHM_VERIFICATION || N3DS_RENDERER_ALLOCATION_OBSERVATION || \
     HALO_N3DS_TEXTURE_COMPARE_FIXTURE || HALO_N3DS_TEXTURE_LOD_FIXTURE || \
     HALO_N3DS_GEOMETRY_SOURCE != N3DS_GEOMETRY_SOURCE_RUNTIME || HALO_N3DS_VERTEX_LAYOUT != N3DS_VERTEX_LAYOUT_FLOAT)
#error "GPU comparisons require an independent regular-renderer build"
#endif
#if defined(HALO_N3DS_GPU_PROBE) && (N3DS_RENDERER_VALIDATION_CONFIGURED || \
    N3DS_RENDERER_PROFILE_CONFIGURED || defined(HALO_N3DS_DEPENDENCY_TRACE) || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || HALO_N3DS_FIXED_WORLD_WIDTH != 400)
#error "GPU probe requires regular native 400x240 gameplay without other diagnostics"
#endif
#if defined(HALO_N3DS_FRAMEBUFFER_REUSE) && (N3DS_RENDERER_VALIDATION_CONFIGURED || \
    N3DS_RENDERER_PROFILE_CONFIGURED || defined(HALO_N3DS_DEPENDENCY_TRACE) || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || HALO_N3DS_FIXED_WORLD_WIDTH != 400)
#error "Framebuffer reuse requires regular native 400x240 gameplay"
#endif
#if defined(HALO_N3DS_OPAQUE_COLOR_READS) && (N3DS_RENDERER_VALIDATION_CONFIGURED || \
    N3DS_RENDERER_PROFILE_CONFIGURED || defined(HALO_N3DS_DEPENDENCY_TRACE) || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || defined(HALO_N3DS_FRAMEBUFFER_REUSE) || \
    HALO_N3DS_FIXED_WORLD_WIDTH != 400)
#error "Opaque color read comparison requires regular native 400x240 gameplay without framebuffer reuse"
#endif
#if defined(HALO_N3DS_UNUSED_TEXTURE_UNITS) && (N3DS_RENDERER_VALIDATION_CONFIGURED || \
    N3DS_RENDERER_PROFILE_CONFIGURED || defined(HALO_N3DS_DEPENDENCY_TRACE) || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || HALO_N3DS_FIXED_WORLD_WIDTH != 400)
#error "Unused texture units require regular native 400x240 gameplay"
#endif
#if defined(HALO_N3DS_TEXTURE_VRAM_MIRRORS) && (N3DS_RENDERER_VALIDATION_CONFIGURED || \
    N3DS_RENDERER_PROFILE_CONFIGURED || defined(HALO_N3DS_DEPENDENCY_TRACE) || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || HALO_N3DS_FIXED_WORLD_WIDTH != 400)
#error "Texture VRAM mirrors require regular native 400x240 gameplay"
#endif
#if defined(HALO_N3DS_GPU_PROBE) && (defined(HALO_N3DS_UNUSED_TEXTURE_UNITS) + \
    defined(HALO_N3DS_OPAQUE_COLOR_READS) + defined(HALO_N3DS_FRAMEBUFFER_REUSE) + \
    defined(HALO_N3DS_TEXTURE_VRAM_MIRRORS)) > 1
#error "GPU probe compares each candidate independently"
#endif
#define N3DS_RENDERER_UNCAPTURED_PROFILE_ELIGIBLE \
    (N3DS_RENDERER_PROFILE_CONFIGURED && !N3DS_RENDERER_VALIDATION_CONFIGURED && \
     !N3DS_RENDERER_ALGORITHM_VERIFICATION && !N3DS_RENDERER_ALLOCATION_OBSERVATION)
#if N3DS_RENDERER_READBACK_CONFIGURED
#define N3DS_RENDERER_RUN_PURPOSE "validation-readback"
#elif N3DS_RENDERER_VALIDATION_CONFIGURED
#define N3DS_RENDERER_RUN_PURPOSE "validation-fixture"
#elif N3DS_RENDERER_ALGORITHM_VERIFICATION
#define N3DS_RENDERER_RUN_PURPOSE "algorithm-verification"
#elif N3DS_RENDERER_ALLOCATION_OBSERVATION
#define N3DS_RENDERER_RUN_PURPOSE "allocation-observation"
#elif defined(HALO_N3DS_GPU_PROBE) && defined(HALO_N3DS_TEXTURE_VRAM_MIRRORS)
#define N3DS_RENDERER_RUN_PURPOSE "texture-vram-mirrors-probe"
#elif defined(HALO_N3DS_GPU_PROBE) && defined(HALO_N3DS_UNUSED_TEXTURE_UNITS)
#define N3DS_RENDERER_RUN_PURPOSE "unused-texture-units-probe"
#elif defined(HALO_N3DS_GPU_PROBE) && defined(HALO_N3DS_OPAQUE_COLOR_READS)
#define N3DS_RENDERER_RUN_PURPOSE "opaque-color-read-probe"
#elif defined(HALO_N3DS_GPU_PROBE) && defined(HALO_N3DS_FRAMEBUFFER_REUSE)
#define N3DS_RENDERER_RUN_PURPOSE "framebuffer-reuse-probe"
#elif defined(HALO_N3DS_GPU_PROBE)
#define N3DS_RENDERER_RUN_PURPOSE "gpu-workload-probe"
#elif defined(HALO_N3DS_DEPENDENCY_TRACE)
#define N3DS_RENDERER_RUN_PURPOSE "dependency-trace-uncaptured"
#elif N3DS_RENDERER_UNCAPTURED_PROFILE_ELIGIBLE
#define N3DS_RENDERER_RUN_PURPOSE "performance-uncaptured-candidate"
#else
#define N3DS_RENDERER_RUN_PURPOSE "gameplay"
#endif

#endif
