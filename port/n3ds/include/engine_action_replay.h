#ifndef HALO_N3DS_ENGINE_ACTION_REPLAY_H
#define HALO_N3DS_ENGINE_ACTION_REPLAY_H

#ifndef HALO_N3DS_ACTION_REPLAY
#define HALO_N3DS_ACTION_REPLAY 0
#endif
#ifndef HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS
#define HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS 0
#endif
#if HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS < 0 || HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS > 3600 || \
    (HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS && (!HALO_N3DS_ACTION_REPLAY || \
    defined(HALO_N3DS_PRESENT_INTERPOLATION) || HALO_N3DS_FIXED_WORLD_WIDTH != 400))
#error "Action replay fixture requires action record/replay and a regular native-400 build"
#endif
typedef char n3ds_action_replay_mode_supported[
    (HALO_N3DS_ACTION_REPLAY == 0 || HALO_N3DS_ACTION_REPLAY == 1 || HALO_N3DS_ACTION_REPLAY == 2) ? 1 : -1];

#if HALO_N3DS_ACTION_REPLAY
#if defined(HALO_N3DS_PERFORMANCE_PROFILE) || defined(HALO_N3DS_ALGORITHM_PROFILE) || \
    defined(HALO_N3DS_DEPENDENCY_TRACE) || defined(HALO_N3DS_RENDERER_TESTS) || \
    defined(HALO_N3DS_LOW_MEMORY_TEST) || !defined(HALO_N3DS_MISSION_TICKS) || HALO_N3DS_MISSION_TICKS || \
    HALO_N3DS_PROFILE_FRAMES || HALO_N3DS_MIDPOINT_CAPTURE_TICK || HALO_N3DS_TEXTURE_COMPARE_FIXTURE
#error "Action replay requires a separate correctness run"
#endif
#include <stdint.h>
#define N3DS_ACTION_REPLAY_HASH_INIT UINT64_C(14695981039346656037)
static inline uint64_t n3ds_action_replay_mix(uint64_t hash,uint32_t value)
{
    return (hash^value)*UINT64_C(1099511628211);
}
struct player_action;
int n3ds_action_replay_begin_tick(long tick);
int n3ds_action_replay_apply(struct player_action *actions, unsigned int count, int live);
void n3ds_action_replay_end_tick(void);
int n3ds_action_replay_failed(void);
void n3ds_action_replay_restore(void);
int n3ds_action_replay_should_exit(void);
int n3ds_action_replay_owns_input(void);
int n3ds_action_replay_finish(int clean_exit);
uint64_t n3ds_action_replay_script_digest(void);
#else
#define n3ds_action_replay_begin_tick(tick) 1
#define n3ds_action_replay_end_tick() ((void)0)
#define n3ds_action_replay_failed() 0
#define n3ds_action_replay_restore() ((void)0)
#define n3ds_action_replay_should_exit() 0
#define n3ds_action_replay_owns_input() 0
#define n3ds_action_replay_finish(clean_exit) 1
#endif

#endif
