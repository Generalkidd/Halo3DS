#ifndef HALO_N3DS_ENGINE_PRESENTATION_H
#define HALO_N3DS_ENGINE_PRESENTATION_H

#ifdef HALO_N3DS_PRESENT_INTERPOLATION
struct render_camera;

int n3ds_presentation_midpoint_active(void);
/* Mark the optional frame unusable before doing any non-resident work. */
int n3ds_presentation_reject_texture(void);
int n3ds_presentation_check_midpoint(void);
enum { NATIVE_MIDPOINT_TEXTURE = 1, NATIVE_MIDPOINT_DEADLINE };
extern int n3ds_presentation_reject_reason;
static inline int n3ds_presentation_midpoint_rejected(void)
{
    return n3ds_presentation_reject_reason != 0;
}
void n3ds_presentation_reset(void);
void n3ds_presentation_poses_capture(void);
void n3ds_presentation_poses_reset(void);
void n3ds_presentation_weapon_reset(void);
void n3ds_presentation_weapon_capture(void);
int n3ds_presentation_weapon_midpoint(const struct render_camera *camera);
#else
static inline int n3ds_presentation_reject_texture(void) { return 0; }
static inline int n3ds_presentation_midpoint_rejected(void) { return 0; }
static inline int n3ds_presentation_check_midpoint(void) { return 0; }
#endif

#endif
