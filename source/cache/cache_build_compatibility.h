#ifndef HALO_CACHE_BUILD_COMPATIBILITY_H
#define HALO_CACHE_BUILD_COMPATIBILITY_H

/* Header fields are 32 bytes. Keep the documented PAL baseline as the default.
 * Retail 2276 is an explicit experimental input, not a wildcard version bypass.
 * This policy only permits further validation/loading; it does not certify the
 * contents of any particular map. */
static int cache_build_is_supported(const char build[32], int allow_retail_2276)
{
    return !strncmp(build, "01.01.14.2342", 32) ||
        (allow_retail_2276 && !strncmp(build, "01.10.12.2276", 32));
}

#endif
