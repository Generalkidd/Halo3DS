#include "engine_prepared_object_marker.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

enum { CAP = 256, OBJECTS = 300, REFERENCES = OBJECTS + 5 };
struct object_state {
    uint32_t marker, magic[OBJECTS];
};

static size_t reference_discovery(struct object_state *state, const int *references,
    const int *visible, int *result)
{
    size_t count = 0;
    ++state->marker;
    for (size_t i = 0; i < REFERENCES; ++i) {
        int id = references[i];
        if (state->magic[id] != state->marker && visible[id] && count < CAP) {
            result[count++] = id;
            state->magic[id] = state->marker;
        }
    }
    return count;
}

static void test_marker_path(uint32_t base, unsigned int intervening_queries,
    int stale_id, uint32_t stale_offset, int query_marked_id, int should_use_prepared)
{
    struct object_state initial = {.marker = base};
    int references[REFERENCES] = {0, 1, 1, 0, 1};
    int visible[OBJECTS];
    for (int i = 0; i < OBJECTS; ++i) {
        initial.magic[i] = base - 17;
        references[i + 5] = i;
        visible[i] = i != 1;
    }
    if (stale_id >= 0)
        initial.magic[stale_id] = base + stale_offset;

    int prepared[CAP], seen[OBJECTS] = {0};
    size_t prepared_count = 0;
    struct n3ds_prepared_marker dependency = {.expected = base + 1};
    for (size_t i = 0; i < REFERENCES; ++i) {
        int id = references[i];
        if (!seen[id] && n3ds_prepared_marker_unmarked(&dependency, initial.magic[id])
                && visible[id] && prepared_count < CAP) {
            prepared[prepared_count++] = id;
            seen[id] = 1;
        }
    }
    assert(prepared_count == CAP);

    /* Model the render obscurer ray's object-mark pass. The first ordinary
     * ray with BSP leaves advances once and marks its visited object IDs.
     * Both paths retain exactly the same intervening writes. */
    for (unsigned int i = 0; i < intervening_queries; ++i) {
        ++initial.marker;
        if (query_marked_id >= 0)
            initial.magic[query_marked_id] = initial.marker;
    }
    struct object_state expected = initial, candidate = initial;
    int expected_ids[CAP], candidate_ids[CAP];
    size_t expected_count = reference_discovery(&expected, references, visible, expected_ids);
    uint32_t next = candidate.marker + 1;
    enum n3ds_prepared_marker_use eligibility = n3ds_prepared_marker_eligibility(&dependency, next);
    int use_prepared = eligibility != N3DS_PREPARED_MARKER_FALLBACK;
    if (eligibility == N3DS_PREPARED_MARKER_RECHECK) {
        for (size_t i = 0; i < prepared_count; ++i)
            use_prepared &= candidate.magic[prepared[i]] != next;
    }
    assert(use_prepared == should_use_prepared);
    size_t candidate_count;
    if (use_prepared) {
        candidate_count = prepared_count;
        memcpy(candidate_ids, prepared, candidate_count * sizeof(prepared[0]));
        ++candidate.marker;
        for (size_t i = 0; i < candidate_count; ++i)
            candidate.magic[prepared[i]] = candidate.marker;
    } else {
        candidate_count = reference_discovery(&candidate, references, visible, candidate_ids);
    }
    assert(candidate_count == expected_count && expected_count == CAP);
    assert(!memcmp(candidate_ids, expected_ids, expected_count * sizeof(expected_ids[0])));
    assert(candidate.marker == expected.marker);
    assert(!memcmp(candidate.magic, expected.magic, sizeof(expected.magic)));
}

int main(void)
{
    test_marker_path(10, 0, -1, 0, -1, 1);
    test_marker_path(10, 1, -1, 0, 0, 1);
    test_marker_path(10, 4, -1, 0, 2, 1);
    test_marker_path(UINT32_MAX - 1, 1, -1, 0, 2, 1);
    test_marker_path(UINT32_MAX, 3, -1, 0, 0, 1);
    test_marker_path(10, 0, 0, 1, -1, 1);
    test_marker_path(10, 1, 0, 1, 2, 0);
    test_marker_path(10, 1, 0, 2, 2, 0);
    test_marker_path(10, 1, 0, 2, 0, 1);
    test_marker_path(UINT32_MAX - 1, 1, 0, 2, 2, 0);
    test_marker_path(10, 1, OBJECTS - 1, 2, 2, 1);
    test_marker_path(10, 4, 0, 5, 2, 0);
    return 0;
}
