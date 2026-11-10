#ifndef HALO_CACHE_BUILD_COMPATIBILITY_H
#define HALO_CACHE_BUILD_COMPATIBILITY_H

/* Header fields are 32 bytes. Non-3DS builds retain the upstream PAL/retail
 * policy. The 3DS installer identifies supported discs and warns on others;
 * permitting an unknown revision here does not certify its map contents. */
static int cache_build_is_supported(const char build[32], int allow_retail_2276)
{
#ifdef HALO_N3DS
    /* Disc revisions are checked by setup; unknown revisions may be tested.
     * Cache format, region bounds and relocation records are still validated. */
    (void)allow_retail_2276;
    return memchr(build,0,32)!=NULL;
#else
    return !strncmp(build, "01.01.14.2342", 32) ||
        (allow_retail_2276 && !strncmp(build, "01.10.12.2276", 32));
#endif
}

#endif
