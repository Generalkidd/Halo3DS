#ifndef HALO_N3DS_PREPARED_OBJECT_MARKER_H
#define HALO_N3DS_PREPARED_OBJECT_MARKER_H

#include <stdint.h>

/* Legacy object marks are equality tests on a wrapping 32-bit sequence.
 * The obscurer ray can advance the sequence after early discovery. */
struct n3ds_prepared_marker {
    uint32_t expected;
    int skipped_expected, saw_following;
};

static inline int n3ds_prepared_marker_unmarked(struct n3ds_prepared_marker *marker, uint32_t magic)
{
    marker->skipped_expected |= magic == marker->expected;
    marker->saw_following |= magic == marker->expected + UINT32_C(1);
    return magic != marker->expected;
}

enum n3ds_prepared_marker_use {
    N3DS_PREPARED_MARKER_USE,
    N3DS_PREPARED_MARKER_RECHECK,
    N3DS_PREPARED_MARKER_FALLBACK
};

static inline enum n3ds_prepared_marker_use n3ds_prepared_marker_eligibility(
    const struct n3ds_prepared_marker *marker, uint32_t next)
{
    if (next == marker->expected)
        return N3DS_PREPARED_MARKER_USE;
    /* A previously skipped object could enter the ordered list after an
     * advance. It had no geometry test, so its omission cannot be validated. */
    if (marker->skipped_expected)
        return N3DS_PREPARED_MARKER_FALLBACK;
    /* One intervening query can only write expected. It cannot introduce
     * expected + 1 if the following value was absent in the traversal. */
    if (next == marker->expected + UINT32_C(1) && !marker->saw_following)
        return N3DS_PREPARED_MARKER_USE;
    return N3DS_PREPARED_MARKER_RECHECK;
}

#endif
