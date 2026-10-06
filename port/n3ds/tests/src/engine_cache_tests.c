#include "cseries.h"
#include "cache_reader.h"
#include "engine_cache.h"
#include "cache_files.h"
#include "crc.h"
#include "data.h"
#include "tag_groups.h"
#include "scenario_definitions.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "collision_bsp_definitions.h"
#include "collision_bsp.h"
#include "cutscene/recorded_animation_definitions.h"
#include <ctype.h>

void n3ds_log(const char *message);
int halo_engine_profile_enumeration_tests(void);

int halo_engine_cache_tests(void)
{
    static const char *names[] = {"ui", "a10"};
    unsigned m;
    if (halo_engine_relocation_tests()) return 1;
    for (m = 0; m < NUMBEROF(names); ++m) {
        const struct n3ds_cache_view *view = n3ds_engine_cache_view(0);
        const struct n3ds_cache_view *bsp = n3ds_engine_cache_view(1);
        struct scenario *scenario;
        struct data_array *syntax;
        struct tag_iterator iterator;
        char message[192];
        long handle, count = 0, b, bsp_handle = NONE;

#define CHECK(expr) do { if (!(expr)) { n3ds_log("CACHE ENGINE FAIL: " #expr); goto fail; } } while (0)
        CHECK(scenario_tags_load("__native_missing_cache__") == NONE);
        CHECK(!view->tags && !bsp->tags);
        CHECK(scenario_tags_load(m ? "levels\\a10\\a10" : "ui") != NONE);
        CHECK(scenario_tags_load(names[m]) == NONE); /* no replacement of a live cache */
        tag_iterator_new(&iterator, NONE);
        while ((handle = tag_iterator_next(&iterator)) != NONE) {
            struct cache_file_tag_instance *entry = &view->instances[(unsigned long)handle & 0xffff];
            if (entry->group_tag != 'sbsp') {
                void *body = tag_get(entry->group_tag, handle);
                CHECK(body && n3ds_cache_resolve(view, body, 1) == body);
            }
            count++;
        }
        CHECK(count == view->tag_header->tag_count);
        scenario = tag_get('scnr', view->tag_header->scenario_tag_index);
        CHECK(tag_loaded('scnr', view->instances[view->tag_header->scenario_tag_index & 0xffff].name) == view->tag_header->scenario_tag_index);
        CHECK(n3ds_cache_resolve(view, scenario, sizeof(*scenario)) == scenario);
        syntax = scenario->hs_syntax_data.address;
        CHECK(syntax && n3ds_cache_resolve(view, syntax, scenario->hs_syntax_data.size) == syntax);
        data_verify(syntax);
        CHECK(syntax->size == 20 && syntax->data == (byte *)syntax+sizeof(*syntax));
        if (scenario->players.count) {
            struct player_starting_location *start = TAG_BLOCK_GET_ELEMENT(&scenario->players, 0, struct player_starting_location);
            CHECK(n3ds_cache_resolve(view, start, sizeof(*start)) == start);
            CHECK(isfinite(start->position.x) && isfinite(start->position.y) && isfinite(start->position.z));
        }
        snprintf(message, sizeof(message), "PASS: %s original tag access: %ld tags, %ld scripts, %d syntax nodes, %ld BSP references",
                 names[m], count, scenario->hs_scripts.count, syntax->actual_count, scenario->structure_bsp_references.count);
        n3ds_log(message);
        if(!m) CHECK(!halo_engine_profile_enumeration_tests());
        CHECK(scenario_get_animation_by_name(scenario, "__native_missing_recording__") == NONE);
        for (b = 0; b < scenario->recorded_animations.count; ++b) {
            struct recorded_animation_definition *recording = TAG_BLOCK_GET_ELEMENT(
                &scenario->recorded_animations, b, struct recorded_animation_definition);
            char upper[TAG_STRING_LENGTH + 1];
            long c, found;
            CHECK(n3ds_cache_resolve(view, recording, sizeof(*recording)) == recording);
            CHECK(memchr(recording->name, 0, sizeof(recording->name)));
            CHECK(recording->event_stream.size >= 0);
            if (recording->event_stream.size)
                CHECK(n3ds_cache_resolve(view, recording->event_stream.address, recording->event_stream.size) == recording->event_stream.address);
            found = scenario_get_animation_by_name(scenario, recording->name);
            /* The original lookup returns the first match if names repeat. */
            CHECK(found >= 0 && found <= b);
            for (c = 0; c < sizeof(upper); ++c)
                upper[c] = (char)toupper((unsigned char)recording->name[c]);
            CHECK(scenario_get_animation_by_name(scenario, upper) == found);
        }
        snprintf(message, sizeof(message), "PASS: %s original recorded-animation lookup and relocated streams: %ld recordings",
                 names[m], scenario->recorded_animations.count);
        n3ds_log(message);
        for (b = 0; b < scenario->structure_bsp_references.count; ++b) {
            struct scenario_structure_bsp_reference *ref = TAG_BLOCK_GET_ELEMENT(&scenario->structure_bsp_references, b, struct scenario_structure_bsp_reference);
            struct structure_bsp *structure;
            struct cache_file_structure_bsp_header *header;
            if (!b) {
                struct scenario_structure_bsp_reference foreign = *ref;
                long saved_offset = ref->file_offset;
                CHECK(!scenario_structure_bsp_load(&foreign));
                ref->file_offset = view->header.file_length;
                CHECK(!scenario_structure_bsp_load(ref));
                ref->file_offset = saved_offset;
                CHECK(!bsp->tags && !view->instances[ref->structure_bsp.index & 0xffff].base_address);
                /* Force a CRC failure after allocation/read; retry must still work. */
                ref->file_offset += 4;
                CHECK(!scenario_structure_bsp_load(ref));
                ref->file_offset = saved_offset;
                CHECK(!bsp->tags);
            }
            CHECK(scenario_structure_bsp_load(ref));
            header = (struct cache_file_structure_bsp_header *)bsp->tags;
            CHECK(header->signature == 'sbsp' && n3ds_cache_resolve(bsp, header->base_address, sizeof(*structure)) == header->base_address);
            bsp_handle = ref->structure_bsp.index;
            CHECK(!scenario_structure_bsp_load(ref)); /* preserve the active BSP */
            structure = tag_get('sbsp', bsp_handle);
            CHECK(structure == header->base_address);
            if (structure->collision_bsp.count) {
                global_collision_bsp = TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp);
                CHECK(n3ds_cache_resolve(bsp, global_collision_bsp, sizeof(*global_collision_bsp)) == global_collision_bsp);
                CHECK(global_collision_bsp_get() == global_collision_bsp);
                if (m == 1 && b == 0) {
                    struct player_starting_location *start = TAG_BLOCK_GET_ELEMENT(&scenario->players, 0, struct player_starting_location);
                    struct collision_bsp_test_vector_result hit;
                    real_point3d point = start->position;
                    real_vector3d down = {{0.f, 0.f, -4.f}};
                    point.z += 0.65f;
                    CHECK(collision_bsp_test_vector(1, global_collision_bsp, 0, NULL, &point, &down, 1.f, &hit));
                    CHECK(fabsf(hit.t - 0.16275f) < 0.0001f && hit.surface_index == 902);
                    n3ds_log("PASS: original floor collision on directly relocated BSP (surface 902)");
                }
            }
            snprintf(message, sizeof(message), "PASS: %s BSP %ld original tag access: %ld surfaces, %ld lightmaps",
                     names[m], b, structure->surfaces.count, structure->lightmaps.count);
            n3ds_log(message);
            global_collision_bsp = NULL;
            scenario_structure_bsp_unload(ref);
            bsp_handle = NONE;
            CHECK(!bsp->tags && !view->instances[ref->structure_bsp.index & 0xffff].base_address);
        }
        /* tags_unload must also release a remaining BSP; reload on next iteration. */
        CHECK(scenario_structure_bsp_load(TAG_BLOCK_GET_ELEMENT(&scenario->structure_bsp_references, 0, struct scenario_structure_bsp_reference)));
        scenario_tags_unload();
        CHECK(!view->tags && !bsp->tags && tag_loaded('scnr', "missing") == NONE);
        CHECK(scenario_tags_load("invalid.map") == NONE);
        CHECK(!view->tags && !bsp->tags);
        n3ds_log("PASS: original cache lifecycle releases BSP/tags and rejects overlapping loads");
        continue;
fail:
        global_collision_bsp = NULL;
        scenario_tags_unload();
        return 1;
#undef CHECK
    }
    return 0;
}
