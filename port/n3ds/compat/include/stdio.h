/*
STDIO.H

Host <stdio.h> plus MSVC-named functions.
*/

#ifndef __HALO_LINUX_STDIO_H
#define __HALO_LINUX_STDIO_H

#include <stdarg.h>
#include_next <stdio.h>

int snprintf(char *buffer, size_t count, const char *format, ...);
int vsnprintf(char *buffer, size_t count, const char *format, va_list arguments);

/* MSVC's _snprintf does not terminate a truncated string; always
terminating is strictly safer for callers written against it. */
#define _snprintf snprintf
#define _vsnprintf vsnprintf

FILE *_fdopen(int handle, const char *mode);
int _fileno(FILE *stream);
#define fdopen _fdopen
#define fileno _fileno

/* MSVC printf length modifiers (%I64d, %I32x) are translated for glibc;
see port/n3ds/src/engine_crt.c */
#ifndef HALO_LINUX_PLATFORM_LAYER
int halo_n3ds_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments);
int halo_n3ds_snprintf(char *buffer, size_t count, const char *format, ...);
int halo_n3ds_vsprintf(char *buffer, const char *format, va_list arguments);
int halo_n3ds_sprintf(char *buffer, const char *format, ...);
int halo_n3ds_vfprintf(FILE *stream, const char *format, va_list arguments);
int halo_n3ds_fprintf(FILE *stream, const char *format, ...);
int halo_n3ds_printf(const char *format, ...);
int halo_n3ds_vprintf(const char *format, va_list arguments);
#undef _snprintf
#undef _vsnprintf
#define snprintf halo_n3ds_snprintf
#define vsnprintf halo_n3ds_vsnprintf
#define _snprintf halo_n3ds_snprintf
#define _vsnprintf halo_n3ds_vsnprintf
#define sprintf halo_n3ds_sprintf
#define vsprintf halo_n3ds_vsprintf
#define fprintf halo_n3ds_fprintf
#define vfprintf halo_n3ds_vfprintf
#define printf halo_n3ds_printf
#define vprintf halo_n3ds_vprintf
#endif

/* The game opens files by Xbox path (d:\\debug.txt, z:\\saved\\...);
translate them the way CreateFile does (the native file adapters). */
#ifndef HALO_LINUX_PLATFORM_LAYER
FILE *halo_n3ds_fopen(const char *path, const char *mode);
FILE *halo_n3ds_freopen(const char *path, const char *mode, FILE *stream);
int halo_n3ds_remove(const char *path);
int halo_n3ds_rename(const char *old_path, const char *new_path);
#define fopen halo_n3ds_fopen
#define freopen halo_n3ds_freopen
#define remove halo_n3ds_remove
#define rename halo_n3ds_rename
#endif

#endif
