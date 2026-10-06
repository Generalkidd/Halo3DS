/*
RASTERIZER_XBOX_DEBUG.H
*/

#ifndef __RASTERIZER_XBOX_DEBUG_H
#define __RASTERIZER_XBOX_DEBUG_H
#pragma once

/* ---------- headers */

#include "cseries.h"

/* ---------- prototypes/RASTERIZER_XBOX_DEBUG.C */

/* The PAL implementation reads only opaque; depth bias comes from debug options. */
void rasterizer_debug_drawing_begin(
	boolean opaque);
void rasterizer_debug_drawing_end(
	void);

#endif // __RASTERIZER_XBOX_DEBUG_H
