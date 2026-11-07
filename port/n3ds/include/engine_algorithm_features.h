#ifndef HALO_N3DS_ENGINE_ALGORITHM_FEATURES_H
#define HALO_N3DS_ENGINE_ALGORITHM_FEATURES_H

/* Stable independent comparison IDs, not promoted optimizations. Candidate
 * and verification paths are reserved until their implementation is admitted
 * by measurements. Reference means the frozen current WIP, not proven parity. */
#define N3DS_ALGORITHM_FEATURE_SCHEMA 1
#define N3DS_ALGORITHM_REFERENCE 0
#define N3DS_ALGORITHM_CANDIDATE 1
#define N3DS_ALGORITHM_VERIFY 2

#ifndef HALO_N3DS_ALGO_ROOT_POSE
#define HALO_N3DS_ALGO_ROOT_POSE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_ROOT_POSE != N3DS_ALGORITHM_REFERENCE
#error "root_pose candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_root_pose_supported[(HALO_N3DS_ALGO_ROOT_POSE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_VFP_SCHEDULE
#define HALO_N3DS_ALGO_VFP_SCHEDULE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_VFP_SCHEDULE != N3DS_ALGORITHM_REFERENCE
#error "vfp_schedule candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_vfp_schedule_supported[(HALO_N3DS_ALGO_VFP_SCHEDULE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_SUPPORT_CORNERS
#define HALO_N3DS_ALGO_SUPPORT_CORNERS N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_SUPPORT_CORNERS != N3DS_ALGORITHM_REFERENCE
#error "support_corners candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_support_corners_supported[(HALO_N3DS_ALGO_SUPPORT_CORNERS == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_WATER_BASE
#define HALO_N3DS_ALGO_WATER_BASE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_WATER_BASE != N3DS_ALGORITHM_REFERENCE
#error "water_base candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_water_base_supported[(HALO_N3DS_ALGO_WATER_BASE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_ADPCM_TABLE
#define HALO_N3DS_ALGO_ADPCM_TABLE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_ADPCM_TABLE != N3DS_ALGORITHM_REFERENCE
#error "adpcm_table candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_adpcm_table_supported[(HALO_N3DS_ALGO_ADPCM_TABLE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_ADPCM_LOOPS
#define HALO_N3DS_ALGO_ADPCM_LOOPS N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_ADPCM_LOOPS != N3DS_ALGORITHM_REFERENCE
#error "adpcm_loops candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_adpcm_loops_supported[(HALO_N3DS_ALGO_ADPCM_LOOPS == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_ROOT_DECODE
#define HALO_N3DS_ALGO_ROOT_DECODE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_ROOT_DECODE != N3DS_ALGORITHM_REFERENCE
#error "root_decode candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_root_decode_supported[(HALO_N3DS_ALGO_ROOT_DECODE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_COLLISION_INVERSE
#define HALO_N3DS_ALGO_COLLISION_INVERSE N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_COLLISION_INVERSE != N3DS_ALGORITHM_REFERENCE
#error "collision_inverse candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_collision_inverse_supported[(HALO_N3DS_ALGO_COLLISION_INVERSE == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_COLLISION_TOPOLOGY
#define HALO_N3DS_ALGO_COLLISION_TOPOLOGY N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_COLLISION_TOPOLOGY != N3DS_ALGORITHM_REFERENCE
#error "collision_topology candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_collision_topology_supported[(HALO_N3DS_ALGO_COLLISION_TOPOLOGY == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_MATERIAL_CANDIDATES
#define HALO_N3DS_ALGO_MATERIAL_CANDIDATES N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_MATERIAL_CANDIDATES != N3DS_ALGORITHM_REFERENCE
#error "material_candidates candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_material_candidates_supported[(HALO_N3DS_ALGO_MATERIAL_CANDIDATES == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_CPU_SKIN_STREAM
#define HALO_N3DS_ALGO_CPU_SKIN_STREAM N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_CPU_SKIN_STREAM != N3DS_ALGORITHM_REFERENCE
#error "cpu_skin_stream candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_cpu_skin_stream_supported[(HALO_N3DS_ALGO_CPU_SKIN_STREAM == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_POSE_EVICTION
#define HALO_N3DS_ALGO_POSE_EVICTION N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_POSE_EVICTION != N3DS_ALGORITHM_REFERENCE
#error "pose_eviction candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_pose_eviction_supported[(HALO_N3DS_ALGO_POSE_EVICTION == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_BSP_RESOLVED_BATCH
#define HALO_N3DS_ALGO_BSP_RESOLVED_BATCH N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_BSP_RESOLVED_BATCH != N3DS_ALGORITHM_REFERENCE
#error "bsp_resolved_batch candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_bsp_resolved_batch_supported[(HALO_N3DS_ALGO_BSP_RESOLVED_BATCH == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_ALGO_AUDIO_SPLIT
#define HALO_N3DS_ALGO_AUDIO_SPLIT N3DS_ALGORITHM_REFERENCE
#endif
#if HALO_N3DS_ALGO_AUDIO_SPLIT != N3DS_ALGORITHM_REFERENCE
#error "audio_split candidate/verify not implemented; select reference (0)"
#endif
typedef char n3ds_algo_audio_split_supported[(HALO_N3DS_ALGO_AUDIO_SPLIT == N3DS_ALGORITHM_REFERENCE) ? 1 : -1];

#ifndef HALO_N3DS_PROFILE_FRAMES
#define HALO_N3DS_PROFILE_FRAMES 0
#endif
#if HALO_N3DS_PROFILE_FRAMES < 0 || HALO_N3DS_PROFILE_FRAMES > 18000
#error "Profile frame limit must be 0..18000 frontend loops"
#endif
typedef char n3ds_profile_frame_limit_valid[(HALO_N3DS_PROFILE_FRAMES >= 0 &&
    HALO_N3DS_PROFILE_FRAMES <= 18000) ? 1 : -1];
#if (HALO_N3DS_PROFILE_FRAMES || defined(HALO_N3DS_ALGORITHM_PROFILE)) && !defined(HALO_N3DS_PERFORMANCE_PROFILE)
#error "Profile frame limit and algorithm instrumentation require performance profiling"
#endif
#if HALO_N3DS_PROFILE_FRAMES && (!defined(HALO_N3DS_MISSION_TICKS) || HALO_N3DS_MISSION_TICKS != 0)
#error "Profile frame limit requires explicit MISSION_TICKS=0; readback is a separate run"
#endif
#ifdef HALO_N3DS_ALGORITHM_PROFILE
#define N3DS_ALGORITHM_PROFILE_CONFIGURED 1
#else
#define N3DS_ALGORITHM_PROFILE_CONFIGURED 0
#endif

#endif

