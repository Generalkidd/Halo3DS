#ifndef HALO_N3DS_PLATFORM_H
#define HALO_N3DS_PLATFORM_H
/* Read libc before adapting MSVC's header-inline linkage. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define __inline static __inline__
#define _inline static __inline__
#define __forceinline static __inline__ __attribute__((always_inline))
/* Fail immediately if the game is accidentally built with a desktop ABI. */
_Static_assert(sizeof(void *) == 4, "Halo requires 32-bit pointers");
_Static_assert(sizeof(long) == 4, "Halo requires 32-bit long");
_Static_assert(sizeof(int) == 4, "Halo requires 32-bit int");
_Static_assert(sizeof(short) == 2, "Halo requires 16-bit short");
_Static_assert(sizeof(float) == 4, "Halo requires 32-bit float");
_Static_assert(sizeof(__WCHAR_TYPE__) == 2, "Halo requires 16-bit game wchar_t");
#if !defined(__ARMEL__) || !defined(__ARM_PCS_VFP)
#error Halo N3DS requires little-endian ARM with the hard-float ABI
#endif
#endif
