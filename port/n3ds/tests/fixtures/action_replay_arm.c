/* Synthetic campaign state and native SD I/O for the standalone ARM test. */
#include <errno.h>
#include "cseries.h"
#include "engine_action_replay.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/ui_widget.h"
#include "memory/data.h"
#include "scenario/scenario.h"

#undef fopen
#undef remove
#undef rename
#undef memcpy
#undef memset
#undef strcmp
#undef strcpy
#undef strlen

static const char output_path[] = "sdmc:/halo-action-replay.bin";
static const char temporary_path[] = "sdmc:/halo-action-replay.tmp";
static byte pending_file[1024];
static uint64_t arena[40000];
static FILE stream;
static size_t offset, written;
static int stream_open, errno_value, menu = 1, allocation_live;
static long current_player = NONE;
static unsigned long local_rng = 27;
static unsigned int map_generation = 2;
static byte tags;
static const struct n3ds_cache_view view = {
    .header = {.checksum = 123, .file_length = 456},
    .tags = &tags, .source_tags_crc = 789,
};
byte journal[1024];
unsigned int journal_bytes;
long global_scenario_index = 12;
struct data_array *player_data;

int *__errno(void) { return &errno_value; }

FILE *fopen(const char *path, const char *mode)
{
    if (stream_open) __builtin_trap();
    offset = 0;
    if (!strcmp(path, output_path) && !strcmp(mode, "rb")) {
        if (!journal_bytes) {
            errno_value = ENOENT;
            return NULL;
        }
        stream_open = 1;
    } else if (!strcmp(path, temporary_path) && !strcmp(mode, "wb")) {
        stream_open = 2;
        written = 0;
    } else {
        __builtin_trap();
    }
    return &stream;
}

size_t fread(void *data, size_t bytes, size_t count, FILE *file)
{
    if (file != &stream || stream_open != 1 || !bytes) __builtin_trap();
    size_t available = (journal_bytes - offset) / bytes;
    if (count > available) count = available;
    memcpy(data, journal + offset, bytes * count);
    offset += bytes * count;
    return count;
}

size_t fwrite(const void *data, size_t bytes, size_t count, FILE *file)
{
    if (file != &stream || stream_open != 2 || bytes * count > sizeof(pending_file) - written)
        __builtin_trap();
    memcpy(pending_file + written, data, bytes * count);
    written += bytes * count;
    return count;
}

int fgetc(FILE *file)
{
    if (file != &stream || stream_open != 1) __builtin_trap();
    return offset < journal_bytes ? journal[offset++] : EOF;
}

int __wrap_fclose(FILE *file)
{
    if (file != &stream || !stream_open) __builtin_trap();
    stream_open = 0;
    return 0;
}

int rename(const char *old, const char *new)
{
    if (stream_open || strcmp(old, temporary_path) || strcmp(new, output_path) || journal_bytes)
        __builtin_trap();
    memcpy(journal, pending_file, written);
    journal_bytes = written;
    return 0;
}

int remove(const char *path)
{
    if (strcmp(path, temporary_path)) __builtin_trap();
    written = 0;
    return 0;
}

void *calloc(size_t count, size_t size)
{
    if (allocation_live || count * size > sizeof(arena)) __builtin_trap();
    allocation_live = 1;
    memset(arena, 0, count * size);
    return arena;
}

void debug_free(void *pointer, const char *file, long line)
{
    if (pointer && (pointer != arena || !allocation_live)) __builtin_trap();
    allocation_live = 0;
}

void n3ds_log(const char *message) {}
int __wrap_halo_n3ds_snprintf(char *out, size_t bytes, const char *format, ...)
{
    if (bytes) out[0] = 0;
    return 0;
}

long csmemcmp(const void *left, const void *right, unsigned long size)
{
    const byte *a = left, *b = right;
    for (unsigned long i = 0; i < size; ++i)
        if (a[i] != b[i]) return (long)a[i] - b[i];
    return 0;
}

void *csmemcpy(void *destination, const void *source, unsigned long size)
{
    return memcpy(destination, source, size);
}

void *csmemset(void *destination, long value, unsigned long size) { return memset(destination, value, size); }
long csstrcmp(const char *left, const char *right) { return strcmp(left, right); }
char *csstrcpy(char *destination, const char *source) { return strcpy(destination, source); }
unsigned long csstrlen(const char *text) { return strlen(text); }

short game_connection(void) { return _game_connection_local; }
short game_difficulty_level_get(void) { return 1; }
boolean game_in_progress(void) { return TRUE; }
boolean main_menu_is_active(void) { return menu; }
boolean n3ds_main_is_solo_multiplayer(void) { return FALSE; }
unsigned int n3ds_engine_tag_generation(void) { return map_generation; }
const struct n3ds_cache_view *n3ds_engine_cache_view(int bsp) { return bsp ? NULL : &view; }
char *tag_get_name(long index) { return index == global_scenario_index ? "levels\\a10\\a10" : NULL; }
short global_structure_bsp_index_get(void) { return 3; }
unsigned long get_random_seed(void) { return 69; }
unsigned long *get_global_local_random_seed_address(void) { return &local_rng; }
uint64_t n3ds_action_replay_script_digest(void) { return 88; }

void data_iterator_new(struct data_iterator *iterator, struct data_array *data) {}
void *data_iterator_next(struct data_iterator *iterator) { return NULL; }
long data_next_index(struct data_array *data, long index) { return index == NONE ? current_player : NONE; }
void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags) {}
void *object_iterator_next(struct object_iterator *iterator) { return NULL; }

#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)

int replay_fixture(unsigned int test)
{
    struct player_action action = {.desired_weapon_index = NONE, .desired_grenade_index = NONE,
        .desired_zoom_level = NONE, .desired_facing = {.yaw = 0.25f, .pitch = -0.f},
        .primary_trigger = 0.5f};

    /* Run the real compatibility translator. The production journal must use
     * the native functions above to access this documented SD root. */
    CHECK(!halo_n3ds_fopen(output_path, "rb") && errno == EINVAL && !stream_open);
    CHECK(n3ds_action_replay_begin_tick(0));
    CHECK(n3ds_action_replay_apply(&action, 1, 1));
    n3ds_action_replay_end_tick();
    n3ds_action_replay_restore();
    CHECK(!n3ds_action_replay_should_exit());
    menu = 0;

    if (test == 6) ++local_rng;
    if (test == 7) journal[journal_bytes - 1] ^= 1;
    if (test == 6 || test == 7) {
        CHECK(!n3ds_action_replay_begin_tick(10) && n3ds_action_replay_failed());
        CHECK(!n3ds_action_replay_finish(1) && !allocation_live);
        return 0;
    }

    CHECK(n3ds_action_replay_begin_tick(10));
    CHECK(n3ds_action_replay_apply(&action, 1, 1)); /* No player, live dequeue succeeded. */
    if (test == 2) ++local_rng;
    n3ds_action_replay_end_tick();

    if (test == 3) n3ds_action_replay_restore();
    if (test == 4) {
        ++map_generation;
        CHECK(n3ds_action_replay_should_exit());
    }
    if (test >= 2 && test <= 4) {
        CHECK(n3ds_action_replay_failed() && n3ds_action_replay_should_exit());
        CHECK(!n3ds_action_replay_begin_tick(11));
        CHECK(!n3ds_action_replay_finish(1) && !allocation_live);
        return 0;
    }

    current_player = 0x12340000;
    CHECK(n3ds_action_replay_begin_tick(11));
    if (test == 5) {
        CHECK(!n3ds_action_replay_apply(&action, 1, 0));
        n3ds_action_replay_end_tick();
        CHECK(n3ds_action_replay_failed() && n3ds_action_replay_should_exit());
        CHECK(!n3ds_action_replay_finish(1) && !allocation_live);
        return 0;
    }

    if (HALO_N3DS_ACTION_REPLAY == 2) action.primary_trigger = 0;
    CHECK(n3ds_action_replay_apply(&action, 1, 1) && action.primary_trigger == 0.5f);
    n3ds_action_replay_end_tick();
    CHECK(!n3ds_action_replay_failed());
    CHECK(n3ds_action_replay_should_exit() == (HALO_N3DS_ACTION_REPLAY == 2));
    CHECK(n3ds_action_replay_finish(1) && journal_bytes && !allocation_live && !stream_open);
    return 0;
}
