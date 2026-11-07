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
#if HALO_N3DS_GEOMETRY_SOURCE != N3DS_GEOMETRY_SOURCE_RUNTIME
#error "Cooked geometry is not implemented; select runtime geometry (0)"
#endif
#if HALO_N3DS_VERTEX_LAYOUT != N3DS_VERTEX_LAYOUT_FLOAT
#error "Packed vertex layouts are not implemented; select float vertex layout (0)"
#endif
/* A misspelled symbolic -D value is zero in #if expressions. Require real
 * language constant expressions as well, so it cannot masquerade as legacy.
 * Typedef guards work in the C99 engine and C++11 platform translation units. */
typedef char n3ds_texture_storage_supported[(HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_LEGACY ||
    HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE)?1:-1];
typedef char n3ds_geometry_source_supported[(HALO_N3DS_GEOMETRY_SOURCE == N3DS_GEOMETRY_SOURCE_RUNTIME)?1:-1];
typedef char n3ds_vertex_layout_supported[(HALO_N3DS_VERTEX_LAYOUT == N3DS_VERTEX_LAYOUT_FLOAT)?1:-1];
#if HALO_N3DS_TEXTURE_STORAGE == N3DS_TEXTURE_STORAGE_CANDIDATE_ARCHIVE
#define N3DS_TEXTURE_STORAGE_NAME "candidate-archive"
#else
#define N3DS_TEXTURE_STORAGE_NAME "legacy"
#endif
#define N3DS_GEOMETRY_SOURCE_NAME "runtime"
#define N3DS_VERTEX_LAYOUT_NAME "float"

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

/* Frame limits capture loading/final images even without renderer diagnostics.
 * Classify all current developer fixtures separately from uncaptured profile
 * candidates. New readback/validation fixtures must also be listed here. */
#if N3DS_RENDERER_CONFIGURED_MISSION_TICKS || N3DS_RENDERER_DIAGNOSTICS_CONFIGURED || \
    defined(HALO_N3DS_PLASMA_SHIELD_TEST) || defined(HALO_N3DS_CHECKPOINT_TEST)
#define N3DS_RENDERER_READBACK_CONFIGURED 1
#else
#define N3DS_RENDERER_READBACK_CONFIGURED 0
#endif
#if N3DS_RENDERER_READBACK_CONFIGURED || defined(HALO_N3DS_LOW_MEMORY_TEST) || \
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
#define N3DS_RENDERER_UNCAPTURED_PROFILE_ELIGIBLE \
    (N3DS_RENDERER_PROFILE_CONFIGURED && !N3DS_RENDERER_VALIDATION_CONFIGURED)
#if N3DS_RENDERER_READBACK_CONFIGURED
#define N3DS_RENDERER_RUN_PURPOSE "validation-readback"
#elif N3DS_RENDERER_VALIDATION_CONFIGURED
#define N3DS_RENDERER_RUN_PURPOSE "validation-fixture"
#elif N3DS_RENDERER_UNCAPTURED_PROFILE_ELIGIBLE
#define N3DS_RENDERER_RUN_PURPOSE "performance-uncaptured-candidate"
#else
#define N3DS_RENDERER_RUN_PURPOSE "gameplay"
#endif

#endif
