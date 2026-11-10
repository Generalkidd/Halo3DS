/* New3DS integration additions by Kevin Boggs. */
#ifndef N3DS_ENGINE_RENDER_COMMANDS_H
#define N3DS_ENGINE_RENDER_COMMANDS_H

/* CPU command ownership, separate from the GPU's existing FrameBegin fence.
 * Callbacks consume owned payloads and frame/map resources whose owners fence
 * before mutation. They may call the renderer, but must not access live game
 * objects. Serial replay is the reference; the optional New-model worker
 * overlaps owned backend spans with preparation, not simulation ticks. */
typedef int (*n3ds_render_command_function)(const void *payload);
enum n3ds_render_fence {
    N3DS_RENDER_FENCE_CAPACITY,
    N3DS_RENDER_FENCE_STATE,
    N3DS_RENDER_FENCE_VIEW,
    N3DS_RENDER_FENCE_TEXTURE,
    N3DS_RENDER_FENCE_GEOMETRY,
    N3DS_RENDER_FENCE_IMMEDIATE,
    N3DS_RENDER_FENCE_PRESENT,
    N3DS_RENDER_FENCE_LIFETIME,
    N3DS_RENDER_FENCE_HANDOFF,
    N3DS_RENDER_FENCE_COUNT
};
void n3ds_render_commands_begin(unsigned long long frame);
void n3ds_render_commands_end(void);
void n3ds_render_commands_release(void);
int n3ds_render_commands_recording(void);
int n3ds_render_commands_replaying(void);
void n3ds_render_commands_kick(void);
void *n3ds_render_command_allocate(n3ds_render_command_function,unsigned int bytes);
void n3ds_render_commands_flush(enum n3ds_render_fence);
void n3ds_render_commands_wait(void);
int n3ds_render_commands_submit(void);
void n3ds_render_commands_report(void);
int n3ds_render_commands_tests(void);
int n3ds_render_worker_tests(void);
int n3ds_model_command_tests(void);
int n3ds_environment_command_tests(void);
int n3ds_chicago_command_tests(void);
void n3ds_chicago_commands_report(void);

#ifdef HALO_N3DS_RENDER_COMMANDS
#define N3DS_RENDER_FENCE(reason) n3ds_render_commands_flush(reason)
#else
#define N3DS_RENDER_FENCE(reason) ((void)0)
#endif
#ifdef HALO_N3DS_RENDER_WORKER
#define N3DS_RENDER_WAIT() n3ds_render_commands_wait()
#else
#define N3DS_RENDER_WAIT() ((void)0)
#endif
#endif
