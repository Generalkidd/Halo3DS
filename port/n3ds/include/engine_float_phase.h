/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_FLOAT_PHASE_H
#define HALO_N3DS_FLOAT_PHASE_H
#include <math.h>
#include <stdint.h>

/* Float inputs already have no fractional bits at magnitude 2^23. Keep
 * negative zero and the exceptional-value behavior of the original libm
 * calls; do not change quaternion/trigonometric accuracy or animation rate. */
/* Kevin Boggs: n3ds_float_fraction */
static inline float n3ds_float_fraction(float value)
{
    if (value > -8388608.f && value < 8388608.f) {
        float fraction = value - (float)(int32_t)value;
        return fraction == 0.f ? copysignf(0.f, value) : fraction;
    }
    return isfinite(value) ? copysignf(0.f, value) : (float)fmod((double)value, 1.0);
}

/* Kevin Boggs: n3ds_float_floor */
static inline float n3ds_float_floor(float value)
{
    if (value > -8388608.f && value < 8388608.f) {
        float whole = (float)(int32_t)value;
        return value == 0.f ? value : whole - (whole > value ? 1.f : 0.f);
    }
    return (float)floor((double)value);
}
#endif
