/* Platform side: normal newlib ABI, with explicit adapters for game stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

int _strnicmp(const char *left, const char *right, size_t count)
{
    while (count--) {
        unsigned char a = (unsigned char)*left++, b = (unsigned char)*right++;
        int difference = tolower(a) - tolower(b);
        if (difference || !a || !b) return difference;
    }
    return 0;
}
int _stricmp(const char *left, const char *right) { return _strnicmp(left, right, (size_t)-1); }

static char *format_copy(const char *format)
{
    /* Same length-modifier policy as the upstream Linux CRT adapter. The output
     * only shrinks; allocate for the complete input instead of truncating. */
    size_t length = strlen(format), out = 0, i;
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    for (i = 0; i < length; ++i) {
        copy[out++] = format[i];
        if (format[i] != '%') continue;
        if (format[i+1] == '%') { copy[out++] = format[++i]; continue; }
        while (format[i+1] && strchr("-+ #0123456789.*", format[i+1])) copy[out++] = format[++i];
        if (!strncmp(format+i+1, "I64", 3)) { copy[out++] = 'l'; copy[out++] = 'l'; i += 3; }
        else if (!strncmp(format+i+1, "I32", 3)) i += 3;
    }
    copy[out] = 0;
    return copy;
}
int halo_n3ds_vsnprintf(char *out, size_t count, const char *format, va_list args)
{
    char *copy = format_copy(format);
    if (!copy) return -1;
    int result = vsnprintf(out, count, copy, args);
    free(copy); return result;
}
int halo_n3ds_snprintf(char *out, size_t count, const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = halo_n3ds_vsnprintf(out, count, format, args);
    va_end(args); return result;
}
int halo_n3ds_vsprintf(char *out, const char *format, va_list args)
{
    char *copy = format_copy(format);
    if (!copy) return -1;
    int result = vsprintf(out, copy, args);
    free(copy); return result;
}
int halo_n3ds_sprintf(char *out, const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = halo_n3ds_vsprintf(out, format, args);
    va_end(args); return result;
}
int halo_n3ds_vfprintf(FILE *stream, const char *format, va_list args)
{
    char *copy = format_copy(format);
    if (!copy) return -1;
    int result = vfprintf(stream, copy, args);
    free(copy); return result;
}
int halo_n3ds_fprintf(FILE *stream, const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = halo_n3ds_vfprintf(stream, format, args);
    va_end(args); return result;
}
int halo_n3ds_vprintf(const char *format, va_list args) { return halo_n3ds_vfprintf(stdout, format, args); }
int halo_n3ds_printf(const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = halo_n3ds_vfprintf(stdout, format, args);
    va_end(args); return result;
}

static int translate_path(const char *path, char *out, size_t capacity)
{
    const char *base = "sdmc:/halo-source/", *relative = path;
    size_t i, length;
    if (!path || !*path) { errno = EINVAL; return -1; }
    if (!strncmp(path, base, strlen(base))) relative += strlen(base);
    else if (path[1] == ':') {
        int drive = tolower((unsigned char)path[0]);
        if (drive == 'z' || drive == 'e') base = "sdmc:/halo-source/state/";
        else if (drive != 'd') { errno = ENOENT; return -1; }
        relative += 2;
        while (*relative == '\\' || *relative == '/') ++relative;
    } else if (*path == '/' || *path == '\\') { errno = EINVAL; return -1; }
    length = strlen(base);
    if (strlen(relative) >= capacity - length) { errno = ENAMETOOLONG; return -1; }
    memcpy(out, base, length);
    for (i = 0; relative[i]; ++i) out[length+i] = relative[i] == '\\' ? '/' : relative[i];
    out[length+i] = 0;
    for (const char *part = out+length; *part;) {
        const char *end = strchr(part, '/');
        size_t size = end ? (size_t)(end-part) : strlen(part);
        if ((size == 2 && part[0] == '.' && part[1] == '.') || memchr(part, ':', size)) { errno = EINVAL; return -1; }
        if (!end) break;
        part = end+1;
    }
    return 0;
}
FILE *halo_n3ds_fopen(const char *path, const char *mode)
{
    char translated[1024];
    return translate_path(path, translated, sizeof(translated)) ? NULL : fopen(translated, mode);
}
FILE *halo_n3ds_freopen(const char *path, const char *mode, FILE *stream)
{
    char translated[1024];
    if (!path) return freopen(NULL, mode, stream);
    return translate_path(path, translated, sizeof(translated)) ? NULL : freopen(translated, mode, stream);
}
int halo_n3ds_remove(const char *path)
{
    char translated[1024];
    return translate_path(path, translated, sizeof(translated)) ? -1 : remove(translated);
}
int halo_n3ds_rename(const char *old_path, const char *new_path)
{
    char old_name[1024], new_name[1024];
    if (translate_path(old_path, old_name, sizeof(old_name)) || translate_path(new_path, new_name, sizeof(new_name))) return -1;
    return rename(old_name, new_name);
}
