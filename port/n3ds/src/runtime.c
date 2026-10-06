/* Minimal, checked C-runtime adapters for the source probe. No Xbox services. */
#define BUILDING_CSERIES
#include "cseries.h"
#include "errors.h"

#ifndef HALO_N3DS_ORIGINAL_CSERIES
char temporary[256];
#endif
void n3ds_log(const char *message);

void display_assert(char *information, char *file, long line, boolean fatal)
{
    /* Ship useful file/line diagnostics without the builder's home directory. */
    const char *name=file?file:"unknown";
    for(const char *p=name;*p;++p) if(*p=='/' || *p=='\\') name=p+1;
    file=(char *)name;
    char text[512];
    snprintf(text, sizeof(text), "%s: %s:%ld: %s", fatal ? "FATAL" : "WARNING", file, line, information ? information : "halt");
    n3ds_log(text);
}
void system_exit(long code) { exit((int)code); }
void halt_and_catch_fire(void) { abort(); }
void *debug_malloc(unsigned int size, boolean clear, const char *file, long line)
{
    void *result = clear ? calloc(1, size) : malloc(size);
    if (!result) { display_assert("allocation failed", (char *)file, line, TRUE); abort(); }
    return result;
}
void debug_free(void *pointer, const char *file, long line) { (void)file; (void)line; free(pointer); }
#ifndef HALO_N3DS_ORIGINAL_CSERIES
/* Same four-character encoding as cseries.c, without its Xbox stack walker. */
char *tag_to_string(tag value, char *out)
{
    out[0] = (char)(value >> 24); out[1] = (char)(value >> 16);
    out[2] = (char)(value >> 8); out[3] = (char)value; out[4] = 0;
    return out;
}
void *csmemset(void *buffer, long c, unsigned long size) { return memset(buffer, (int)c, size); }
void *csmemcpy(void *dst, const void *src, unsigned long size) { return memcpy(dst, src, size); }
void *csmemmove(void *dst, const void *src, unsigned long size) { return memmove(dst, src, size); }
unsigned long csstrlen(const char *text) { return strlen(text); }
char *csstrcpy(char *dst, const char *src) { return strcpy(dst, src); }
char *csstrncpy(char *dst, const char *src, unsigned long size) { return strncpy(dst, src, size); }
long csstrcmp(const char *left, const char *right) { return strcmp(left, right); }
long csstrncmp(const char *left, const char *right, unsigned long size) { return strncmp(left, right, size); }
long csmemcmp(const void *left, const void *right, unsigned long size) { return memcmp(left, right, size); }
#endif
#ifndef HALO_N3DS_ORIGINAL_CSERIES
void error(short priority, const char *format, ...)
{
    char message[2048];
    va_list args;
    (void)priority;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    n3ds_log(message);
    /* The probe has no error dialog: an engine error must fail the test. */
    abort();
}
#endif
#ifndef HALO_N3DS_ORIGINAL_CSERIES
char *csprintf(char *buffer, char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);
    return buffer;
}

#endif
