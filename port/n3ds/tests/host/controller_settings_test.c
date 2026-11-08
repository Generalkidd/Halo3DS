#include "cseries.h"
#include "engine_controls.h"
#include "engine_files.h"
#include "engine_graphics_options.h"
#include "input/input.h"

int halo_engine_input_tests(void);

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "controller settings test, line %d: %s\n", __LINE__, #test); \
    exit(1); \
} } while (0)

/* Only the SD, HID and display boundaries are replaced. All input filtering,
 * held ticks, settings validation and save/restore run in production code. */
static const char *const primary = "t:\\controller-settings.bin";
static const char *const graphics_primary = "t:\\graphics-settings.bin";
enum file_fault { NO_FAULT, OPEN_FAULT, SIZE_FAULT, READ_FAULT, CLOSE_FAULT };
static enum file_fault fault;
static FILE *file;
static int primary_open, original, paused, main_shortcut, back_count, fail_promotion;
static const char widget_identities[2];
static const void *root_widget;
static struct native_controls_view last_view;
static enum n3ds_world_texture_mode requested_mode;
static enum n3ds_world_resolution requested_resolution;
static struct native_graphics_options_view graphics_view;
static int graphics_requests, resolution_requests, resolution_locked;

static const char *host_path(const char *path)
{
    CHECK(!strncmp(path, "t:\\controller-settings.", 23) || !strncmp(path, "t:\\graphics-settings.", 21));
    return path + 3;
}

unsigned int n3ds_file_open(const char *path, unsigned int permissions)
{
    CHECK(!file && (permissions == 1 || permissions == 3));
    primary_open = !strcmp(path, primary);
    if (primary_open && fault == OPEN_FAULT) {
        fault = NO_FAULT;
        return 0;
    }
    file = fopen(host_path(path), permissions == 1 ? "rb" : "r+b");
    return file != NULL;
}

int n3ds_file_close(unsigned int handle)
{
    CHECK(file && handle == 1);
    int success = !fclose(file);
    file = NULL;
    if (primary_open && fault == CLOSE_FAULT) {
        fault = NO_FAULT;
        return 0;
    }
    return success;
}

unsigned int n3ds_file_size(unsigned int handle)
{
    CHECK(file && handle == 1 && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    CHECK(size >= 0 && !fseek(file, 0, SEEK_SET));
    if (primary_open && fault == SIZE_FAULT) {
        fault = NO_FAULT;
        return ~0u;
    }
    return (unsigned int)size;
}

int n3ds_file_read(unsigned int handle, void *buffer, unsigned int size)
{
    CHECK(file && handle == 1);
    if (primary_open && fault == READ_FAULT) {
        fault = NO_FAULT;
        return 0;
    }
    return fread(buffer, 1, size, file) == size;
}

int n3ds_file_write(unsigned int handle, const void *buffer, unsigned int size)
{
    CHECK(file && handle == 1);
    return fwrite(buffer, 1, size, file) == size;
}

int n3ds_file_resize(unsigned int handle, unsigned int size)
{
    /* Controller settings only truncate freshly created files. */
    CHECK(handle == 1 && file && !size);
    return n3ds_file_size(handle) == 0;
}

int n3ds_file_stat(const char *path, struct native_file_info *info)
{
    FILE *stream = fopen(host_path(path), "rb");
    if (!stream) return 0;
    CHECK(!fseek(stream, 0, SEEK_END));
    *info = (struct native_file_info){.size = (unsigned int)ftell(stream)};
    CHECK(!fclose(stream));
    return 1;
}

int n3ds_file_create(const char *path, int directory)
{
    CHECK(!directory);
    FILE *stream = fopen(host_path(path), "wb");
    return stream && !fclose(stream);
}

int n3ds_file_delete(const char *path, int directory)
{
    CHECK(!directory);
    return !remove(host_path(path));
}

int n3ds_file_rename(const char *from, const char *to)
{
    struct native_file_info info;
    if ((fail_promotion && (!strcmp(to, primary) || !strcmp(to, graphics_primary))) || n3ds_file_stat(to, &info))
        return 0;
    return !rename(host_path(from), host_path(to));
}

int n3ds_input_platform_initialize(void) { return 1; }
int n3ds_input_platform_original_model(void) { return original; }
void n3ds_input_platform_dispose(void) {}
static long long input_time_ms;
long long n3ds_engine_ticks(void) { return input_time_ms; }
long long n3ds_engine_tick_frequency(void) { return 1000; }
void n3ds_input_platform_poll(struct native_input_sample *s)
{
    *s = (struct native_input_sample){0};
}
const void *n3ds_ui_controls_root(void) { return root_widget; }
int n3ds_ui_controls_shortcut_active(void) { return paused || main_shortcut; }
void n3ds_ui_controls_back(void) { ++back_count; }
void n3ds_gpu_controls_draw(const struct native_controls_view *view) { last_view = *view; }
void n3ds_gpu_controls_entry(void) {}
void n3ds_gpu_graphics_options_draw(const struct native_graphics_options_view *view) { graphics_view = *view; }
int n3ds_gpu_world_resolution_available(void) { return !resolution_locked; }
void n3ds_gpu_world_resolution_request(enum n3ds_world_resolution mode)
{
    CHECK(!resolution_locked && (unsigned int)mode < N3DS_WORLD_RESOLUTION_COUNT);
    ++resolution_requests;
    requested_resolution = mode;
}
int n3ds_engine_world_texture_request(enum n3ds_world_texture_mode mode)
{
    CHECK(!original && (unsigned int)mode < NATIVE_GRAPHICS_MODE_COUNT);
    ++graphics_requests;
    requested_mode = mode;
    return 1;
}
enum n3ds_world_texture_mode n3ds_engine_world_texture_requested(void) { return requested_mode; }
void n3ds_log(const char *message) { puts(message); }
void display_assert(char *information, char *path, long line, boolean fatal)
{
    fprintf(stderr, "%s:%ld: %s\n", path, line, information ? information : "assertion");
    if (fatal) abort();
}
void system_exit(long code) { exit((int)code); }

static const struct gamepad_state *frame(unsigned int keys)
{
    const struct native_input_sample sample = {.held = keys};
    n3ds_engine_input_sample(&sample);
    return input_get_gamepad_state(0);
}

static void tap(unsigned int keys)
{
    frame(keys);
    frame(0);
}

static void touch(unsigned int x, unsigned int y)
{
    const struct native_input_sample sample = {
        .held = NATIVE_KEY_TOUCH, .touch_x = x, .touch_y = y,
    };
    n3ds_engine_input_sample(&sample);
    frame(0);
}

static void default_mapping(void)
{
    const struct gamepad_state *pad = input_get_gamepad_state(0);
    n3ds_input_gameplay_controls(1);
    frame(NATIVE_KEY_L);
    CHECK(pad->buttons[_gamepad_analog_button_right_trigger] == 1);
    CHECK(pad->analog_buttons[_gamepad_analog_button_right_trigger] == 255);
    CHECK(!pad->buttons[_gamepad_binary_button_left_thumb]);
    frame(NATIVE_KEY_L);
    CHECK(pad->buttons[_gamepad_analog_button_right_trigger] == 2);
    frame(original ? NATIVE_KEY_R | NATIVE_KEY_DUP | NATIVE_KEY_DDOWN
                   : NATIVE_KEY_ZL | NATIVE_KEY_R | NATIVE_KEY_ZR);
    CHECK(!pad->buttons[_gamepad_analog_button_right_trigger]);
    CHECK(pad->buttons[_gamepad_analog_button_left_trigger] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_right_thumb] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_left_thumb] == 1);
    CHECK(!pad->buttons[_gamepad_binary_button_dpad_up]);
    CHECK(!pad->buttons[_gamepad_binary_button_dpad_down]);
    if (original) {
        frame(NATIVE_KEY_ZL | NATIVE_KEY_ZR);
        CHECK(!pad->buttons[_gamepad_analog_button_left_trigger]);
        CHECK(!pad->buttons[_gamepad_analog_button_right_trigger]);
    }
    frame(0);
    n3ds_input_gameplay_controls(0);
    frame(NATIVE_KEY_A | NATIVE_KEY_L | NATIVE_KEY_R | NATIVE_KEY_DUP);
    CHECK(pad->buttons[_gamepad_analog_button_a] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_left_thumb] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_right_thumb] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_dpad_up] == 1);
    CHECK(!pad->buttons[_gamepad_analog_button_left_trigger]);
    CHECK(!pad->buttons[_gamepad_analog_button_right_trigger]);
    frame(0);
}

static void preset_navigation(void)
{
    root_widget = &widget_identities[0];
    n3ds_controls_open(1);
    frame(0);
    n3ds_controls_draw();
    CHECK(last_view.tab == NATIVE_CONTROLS_LAYOUT);
    CHECK(!strcmp(last_view.fire, "L"));
    CHECK(!strcmp(last_view.grenade, original ? "R" : "ZL"));
    tap(NATIVE_KEY_DRIGHT);
    n3ds_controls_draw();
    CHECK(last_view.preset_pending && !last_view.dirty);
    CHECK(n3ds_controls_gameplay_buttons(NATIVE_KEY_L, original) ==
        (1u << _gamepad_analog_button_right_trigger));
    tap(NATIVE_KEY_A);
    n3ds_controls_draw();
    CHECK(!last_view.preset_pending && last_view.dirty);
    CHECK(!strcmp(last_view.fire, "R"));
    CHECK(!strcmp(last_view.grenade, original ? "L" : "ZR"));
    tap(NATIVE_KEY_DRIGHT);
    tap(NATIVE_KEY_A);
    n3ds_controls_draw();
    CHECK(!strcmp(last_view.fire, original ? "L" : "ZR"));

    tap(NATIVE_KEY_R);
    tap(NATIVE_KEY_DUP); /* Wrap from L to the last (off-screen) button. */
    n3ds_controls_draw();
    int last = last_view.selected - last_view.first;
    CHECK(last_view.selected == last_view.count - 1);
    CHECK(last >= 0 && last < NATIVE_CONTROLS_VISIBLE_ROWS);
    CHECK(!strcmp(last_view.labels[last], "D-pad Right"));
    touch(265, 20);
    n3ds_controls_draw();
    CHECK(last_view.tab == NATIVE_CONTROLS_AIM);
    CHECK(last_view.count == (original ? 2 : 6));
    touch(282, 52); /* Change the first sensitivity row via its plus button. */
    n3ds_controls_draw();
    CHECK(!strcmp(last_view.values[0], "105%"));
    tap(NATIVE_KEY_X);
    n3ds_controls_draw();
    CHECK(!strcmp(last_view.values[0], "100%"));
    CHECK(!strcmp(last_view.fire, original ? "L" : "ZR") && last_view.dirty);
    touch(150, 215); /* Cancel and return to the opening menu. */
    CHECK(!n3ds_controls_active() && back_count == 1);
}

static void open_buttons(int from_main)
{
    root_widget = &widget_identities[0];
    if (from_main) {
        n3ds_controls_open(1);
        /* An input already held when the modal opens must stay suppressed. */
        CHECK(!frame(NATIVE_KEY_L)->buttons[_gamepad_binary_button_left_thumb]);
        frame(1u << 24); /* HID's virtual C-stick direction is not a held button. */
        tap(NATIVE_KEY_R);
    } else {
        paused = 1;
        CHECK(!frame(NATIVE_KEY_SELECT)->buttons[_gamepad_binary_button_back]);
        frame(0);
        touch(154, 20);
    }
    CHECK(n3ds_controls_active());
    n3ds_controls_draw();
    CHECK(last_view.tab == NATIVE_CONTROLS_BUTTONS && last_view.selected == 0);
    CHECK(last_view.count == (original ? 10 : 12));
}

static void expect_l_binding(int button)
{
    frame(0);
    n3ds_input_gameplay_controls(1);
    const struct gamepad_state *pad = frame(NATIVE_KEY_L);
    for (int i = 0; i < NUMBER_OF_GAMEPAD_BUTTONS; ++i)
        CHECK(pad->buttons[i] == (i == button));
    frame(0);
    n3ds_input_gameplay_controls(0);
}

static void customize(void)
{
    open_buttons(1);
    tap(NATIVE_KEY_A);
    n3ds_controls_draw();
    CHECK(last_view.picking && !last_view.dirty);
    tap(NATIVE_KEY_DDOWN); /* L: Fire -> Throw grenade. */
    tap(NATIVE_KEY_A);
    n3ds_controls_draw();
    CHECK(!last_view.picking && last_view.dirty);
    CHECK(n3ds_controls_gameplay_buttons(NATIVE_KEY_L, original) ==
        (1u << _gamepad_analog_button_left_trigger));
    tap(NATIVE_KEY_B);
    CHECK(!n3ds_controls_active() && back_count == 1);
    expect_l_binding(_gamepad_analog_button_right_trigger);
    struct native_file_info info;
    CHECK(!n3ds_file_stat(primary, &info));

    open_buttons(0);
    tap(NATIVE_KEY_DRIGHT);
    CHECK(!frame(NATIVE_KEY_START)->buttons[_gamepad_binary_button_start]);
    CHECK(!n3ds_controls_active() && back_count == 1);
    CHECK(!frame(NATIVE_KEY_START)->buttons[_gamepad_binary_button_start]);
    expect_l_binding(_gamepad_analog_button_left_trigger);
    CHECK(n3ds_file_stat(primary, &info));

    open_buttons(0);
    tap(NATIVE_KEY_DRIGHT); /* L: Throw grenade -> Jump. */
    touch(264, 216);        /* Save using the touch footer. */
    CHECK(!n3ds_controls_active());
    expect_l_binding(_gamepad_analog_button_a);
}

static unsigned int visible_row_y(const char *label)
{
    for (int row = 0; row < NATIVE_CONTROLS_VISIBLE_ROWS; ++row)
        if (last_view.labels[row] && !strcmp(last_view.labels[row], label))
            return NATIVE_CONTROLS_ROW_Y + NATIVE_CONTROLS_ROW_HEIGHT / 2
                + NATIVE_CONTROLS_ROW_STRIDE * row;
    return 0;
}

static void touch_picker(void)
{
    int old_back_count = back_count;
    open_buttons(1);
    touch(260, 190); /* Next page makes the bottom D-pad rows reachable by touch. */
    n3ds_controls_draw();
    unsigned int dpad_right_y = visible_row_y("D-pad Right");
    CHECK(last_view.first > 0 && dpad_right_y);
    touch(35, dpad_right_y);
    n3ds_controls_draw();
    CHECK(last_view.picking && !last_view.dirty);
    tap(NATIVE_KEY_DUP);
    tap(NATIVE_KEY_B);
    n3ds_controls_draw();
    CHECK(n3ds_controls_active() && !last_view.picking && !last_view.dirty);
    CHECK(back_count == old_back_count);

    dpad_right_y = visible_row_y("D-pad Right");
    CHECK(dpad_right_y);
    touch(35, dpad_right_y);
    n3ds_controls_draw();
    CHECK(last_view.picking);
    for (int i = 0; i < 3 && last_view.first > 0; ++i) {
        touch(55, 190);
        n3ds_controls_draw();
    }
    unsigned int fire_y = visible_row_y("Fire");
    CHECK(fire_y);
    touch(35, fire_y);
    n3ds_controls_draw();
    CHECK(!last_view.picking && last_view.dirty);
    CHECK(n3ds_controls_gameplay_buttons(NATIVE_KEY_DRIGHT, original) ==
        (1u << _gamepad_analog_button_right_trigger));
    touch(150, 215);
    CHECK(!n3ds_controls_active() && back_count == old_back_count + 1);
    CHECK(n3ds_controls_gameplay_buttons(NATIVE_KEY_DRIGHT, original) ==
        (1u << _gamepad_binary_button_dpad_right));
}

static void replaced_widget(void)
{
    int old_back_count = back_count;
    open_buttons(1);
    tap(NATIVE_KEY_DRIGHT); /* Pending edit under the original widget. */
    root_widget = &widget_identities[1]; /* An error dialog or other replacement. */
    CHECK(!frame(NATIVE_KEY_B)->buttons[_gamepad_analog_button_b]);
    CHECK(!n3ds_controls_active() && back_count == old_back_count);
    CHECK(!frame(NATIVE_KEY_B)->buttons[_gamepad_analog_button_b]);
    frame(1u << 24);
    CHECK(frame(NATIVE_KEY_B)->buttons[_gamepad_analog_button_b] == 1);
    expect_l_binding(_gamepad_analog_button_right_trigger);
}

static void reused_widget_address(void)
{
    int old_back_count = back_count;
    open_buttons(1);
    tap(NATIVE_KEY_DRIGHT);
    n3ds_controls_widget_deleted(&widget_identities[1]); /* Unrelated child or widget. */
    CHECK(n3ds_controls_active());

    const void *deleted = root_widget;
    n3ds_controls_widget_deleted(deleted);
    root_widget = NULL;
    /* The widget pool can reuse the root address before the next input poll. */
    root_widget = deleted;
    CHECK(!frame(NATIVE_KEY_B)->buttons[_gamepad_analog_button_b]);
    CHECK(!n3ds_controls_active() && back_count == old_back_count);
    expect_l_binding(_gamepad_analog_button_right_trigger);
}

static void main_menu_shortcut(void)
{
    int old_back_count = back_count;
    root_widget = &widget_identities[0];
    main_shortcut = 1;
    CHECK(!frame(NATIVE_KEY_SELECT)->buttons[_gamepad_binary_button_back]);
    frame(0);
    CHECK(n3ds_controls_active());
    n3ds_controls_draw();
    CHECK(last_view.tab == NATIVE_CONTROLS_LAYOUT);
    tap(NATIVE_KEY_B);
    CHECK(!n3ds_controls_active() && back_count == old_back_count);
    main_shortcut = 0;
}

static void graphics_options(const char *scenario)
{
    root_widget = &widget_identities[0];
    main_shortcut = 1;
    resolution_locked = !strcmp(scenario, "graphics-locked");
    n3ds_graphics_options_initialize();
    CHECK(graphics_requests == !original && resolution_requests == !resolution_locked);
    if (resolution_locked) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS);
        tap(NATIVE_KEY_Y);
        tap(NATIVE_KEY_DLEFT);
        n3ds_controls_draw();
        CHECK(graphics_view.tab == NATIVE_GRAPHICS_TEXTURE_TAB && !graphics_view.show_tabs);
        tap(NATIVE_KEY_DDOWN);
        tap(NATIVE_KEY_START);
        CHECK(!n3ds_controls_active() && requested_mode == N3DS_WORLD_TEXTURE_64_SINGLE && !resolution_requests);
        return;
    }
    if (original) {
        CHECK(requested_resolution == N3DS_WORLD_RESOLUTION_280);
        tap(NATIVE_KEY_Y);
        tap(NATIVE_KEY_DRIGHT);
        n3ds_controls_draw();
        CHECK(n3ds_controls_active() && graphics_view.tab == NATIVE_GRAPHICS_RESOLUTION_TAB && !graphics_view.show_tabs);
        touch(NATIVE_GRAPHICS_ROW_X + 5,
            NATIVE_RESOLUTION_ROW_Y + N3DS_WORLD_RESOLUTION_400 * NATIVE_RESOLUTION_ROW_STRIDE + 5);
        tap(NATIVE_KEY_START);
        CHECK(!n3ds_controls_active() && !graphics_requests && requested_resolution == N3DS_WORLD_RESOLUTION_400);
        return;
    }
    if (!strcmp(scenario, "graphics-legacy")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS && requested_resolution == N3DS_WORLD_RESOLUTION_AUTO);
        tap(NATIVE_KEY_Y);
        tap(NATIVE_KEY_DDOWN);
        tap(NATIVE_KEY_A);
        CHECK(!n3ds_controls_active() && requested_resolution == N3DS_WORLD_RESOLUTION_400);
        return;
    }
    if (!strcmp(scenario, "graphics-reload-native")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS && requested_resolution == N3DS_WORLD_RESOLUTION_400);
        return;
    }
    if (!strcmp(scenario, "graphics-reload-locked-save")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_SINGLE && requested_resolution == N3DS_WORLD_RESOLUTION_280);
        return;
    }
    if (!strcmp(scenario, "graphics-reload-classic")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_SINGLE && requested_resolution == N3DS_WORLD_RESOLUTION_200);
        return;
    }
    if (!strcmp(scenario, "graphics-reload-balanced")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS && requested_resolution == N3DS_WORLD_RESOLUTION_280);
        return;
    }
    if (!strcmp(scenario, "graphics-retry")) {
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS && requested_resolution == N3DS_WORLD_RESOLUTION_280);
        tap(NATIVE_KEY_Y);
        tap(NATIVE_KEY_DDOWN);
        tap(NATIVE_KEY_DRIGHT);
        tap(NATIVE_KEY_DDOWN);
        fail_promotion = 1;
        tap(NATIVE_KEY_START);
        n3ds_controls_draw();
        CHECK(n3ds_controls_active() && graphics_view.save_error);
        CHECK(graphics_view.draft_texture == N3DS_WORLD_TEXTURE_64_SINGLE && requested_mode == N3DS_WORLD_TEXTURE_64_MIPS);
        CHECK(graphics_view.draft_resolution == N3DS_WORLD_RESOLUTION_200 && requested_resolution == N3DS_WORLD_RESOLUTION_280);
        CHECK(graphics_requests == 1 && resolution_requests == 1);
        fail_promotion = 0;
        tap(NATIVE_KEY_A);
        CHECK(!n3ds_controls_active());
        CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_SINGLE && requested_resolution == N3DS_WORLD_RESOLUTION_200);
        return;
    }

    CHECK(!strcmp(scenario, "graphics-first-save") && requested_mode == N3DS_WORLD_TEXTURE_32_MIPS);
    CHECK(requested_resolution == N3DS_WORLD_RESOLUTION_AUTO);
    CHECK(!frame(NATIVE_KEY_Y)->buttons[_gamepad_analog_button_y]);
    frame(0);
    CHECK(n3ds_controls_active());
    tap(NATIVE_KEY_DDOWN);
    tap(NATIVE_KEY_DDOWN);
    n3ds_controls_draw();
    CHECK(graphics_view.tab == NATIVE_GRAPHICS_RESOLUTION_TAB && graphics_view.show_tabs);
    CHECK(graphics_view.draft_resolution == N3DS_WORLD_RESOLUTION_320 && graphics_view.saved_resolution == N3DS_WORLD_RESOLUTION_AUTO);
    tap(NATIVE_KEY_B);
    CHECK(!n3ds_controls_active() && resolution_requests == 1 && back_count == 0);
    tap(NATIVE_KEY_Y);
    tap(NATIVE_KEY_DRIGHT);
    tap(NATIVE_KEY_DDOWN);
    n3ds_controls_draw();
    CHECK(graphics_view.tab == NATIVE_GRAPHICS_TEXTURE_TAB);
    CHECK(graphics_view.draft_texture == N3DS_WORLD_TEXTURE_64_MIPS && graphics_view.saved_texture == N3DS_WORLD_TEXTURE_32_MIPS);
    n3ds_controls_widget_deleted(root_widget);
    CHECK(!frame(NATIVE_KEY_A)->buttons[_gamepad_analog_button_a]);
    frame(0);
    CHECK(!n3ds_controls_active() && graphics_requests == 1);

    main_shortcut = 0;
    paused = 1;
    touch(NATIVE_GRAPHICS_ENTRY_X + 5, NATIVE_OPTIONS_ENTRY_Y + 5);
    CHECK(n3ds_controls_active());
    touch(NATIVE_GRAPHICS_ROW_X + 5,
        NATIVE_RESOLUTION_ROW_Y + N3DS_WORLD_RESOLUTION_280 * NATIVE_RESOLUTION_ROW_STRIDE + 5);
    touch(NATIVE_GRAPHICS_TEXTURE_X + 5, NATIVE_GRAPHICS_TAB_Y + 5);
    touch(NATIVE_GRAPHICS_ROW_X + 5, NATIVE_GRAPHICS_ROW_Y + NATIVE_GRAPHICS_ROW_STRIDE + 5);
    touch(NATIVE_GRAPHICS_SAVE_X + 5, NATIVE_OPTIONS_ENTRY_Y + 5);
    CHECK(!n3ds_controls_active() && graphics_requests == 2 && resolution_requests == 2 && back_count == 0);
    CHECK(requested_mode == N3DS_WORLD_TEXTURE_64_MIPS && requested_resolution == N3DS_WORLD_RESOLUTION_280);
}

static void remap_movement(void)
{
    struct native_control_settings saved, remapped;
    n3ds_controls_get(&saved);
    remapped = saved;
    /* The physical D-pad up now shoots. It cannot also walk forward. */
    remapped.bindings[original][8] = NATIVE_ACTION_FIRE;
    remapped.bindings[original][1] = NATIVE_ACTION_NONE;
    n3ds_controls_set(&remapped);
    n3ds_input_gameplay_controls(1);
    const struct gamepad_state *pad = frame(NATIVE_KEY_DUP | NATIVE_KEY_R);
    CHECK(pad->buttons[_gamepad_analog_button_right_trigger] == 1);
    CHECK(!pad->buttons[_gamepad_binary_button_dpad_up]);
    CHECK(!pad->buttons[_gamepad_binary_button_right_thumb]);
    CHECK(!pad->buttons[_gamepad_analog_button_left_trigger]);
    frame(NATIVE_KEY_L); /* A second physical binding holds the same action. */
    CHECK(pad->buttons[_gamepad_analog_button_right_trigger] == 2);
    frame(0);
    CHECK(!pad->buttons[_gamepad_analog_button_right_trigger]);
    n3ds_input_gameplay_controls(0);
    frame(NATIVE_KEY_DUP | NATIVE_KEY_R);
    CHECK(pad->buttons[_gamepad_binary_button_dpad_up] == 1);
    CHECK(pad->buttons[_gamepad_binary_button_right_thumb] == 1);
    CHECK(!pad->buttons[_gamepad_analog_button_right_trigger]);
    frame(0);
    n3ds_controls_set(&saved);
}

static unsigned int touch_targets(unsigned int x, unsigned int y)
{
    (void)x;
    return y < NATIVE_TOUCH_AIM_BOTTOM ? NATIVE_TOUCH_AIM : NATIVE_TOUCH_FLASHLIGHT;
}

static void tune_aim(void)
{
    struct native_control_settings saved, tuned;
    n3ds_controls_get(&saved);
    tuned = saved;
    tuned.cstick = 80;
    tuned.touch = 140;
    tuned.invert_cstick_y = 1;
    tuned.invert_touch_y = 1;
    n3ds_controls_set(&tuned);
    n3ds_input_gameplay_controls(1);
    const struct gamepad_state *pad = input_get_gamepad_state(0);
    struct native_input_sample sample = {.circle_y = 156, .cstick_y = 156};
    n3ds_engine_input_sample(&sample);
    CHECK(pad->sticks[0].y == 32767 && pad->sticks[1].y == -32767);
    CHECK(fabsf(n3ds_input_touch_look_scale(1) - .80f) < .0001f);
    sample = (struct native_input_sample){.circle_x = 50, .cstick_x = 50};
    n3ds_engine_input_sample(&sample);
    CHECK(pad->sticks[0].x > 0 && pad->sticks[1].x > 0);
    tuned.cstick_deadzone = 13107; /* 40% */
    n3ds_controls_set(&tuned);
    n3ds_engine_input_sample(&sample);
    CHECK(pad->sticks[0].x > 0 && !pad->sticks[1].x);
    frame(0);
    n3ds_input_touch_handler(touch_targets);
    sample = (struct native_input_sample){.held = NATIVE_KEY_TOUCH, .touch_x = 100, .touch_y = 100};
    n3ds_engine_input_sample(&sample);
    sample.touch_y = 99;
    n3ds_engine_input_sample(&sample);
    CHECK(pad->sticks[1].y == -8192);
    CHECK(fabsf(n3ds_input_touch_look_scale(1) - 1.65f * 1.40f) < .0001f);
    frame(0);
    tuned.touch_aim = 0;
    n3ds_controls_set(&tuned);
    n3ds_engine_input_sample(&sample);
    sample.touch_y = 98;
    n3ds_engine_input_sample(&sample);
    CHECK(!pad->sticks[1].y);
    frame(0);
    sample.touch_y = 200;
    n3ds_engine_input_sample(&sample);
    CHECK(pad->buttons[_gamepad_analog_button_white] == 1);
    frame(0);
    n3ds_input_touch_handler(NULL);
    n3ds_input_gameplay_controls(0);
    n3ds_controls_set(&saved);
}

static unsigned int gesture_targets(unsigned int x,unsigned int y)
{
    if(x>=NATIVE_TOUCH_RELOAD_X && x<NATIVE_TOUCH_RELOAD_X+NATIVE_TOUCH_RELOAD_WIDTH &&
       y>=NATIVE_TOUCH_RELOAD_Y && y<NATIVE_TOUCH_RELOAD_Y+NATIVE_TOUCH_RELOAD_HEIGHT)
        return NATIVE_TOUCH_RELOAD;
    return y<NATIVE_TOUCH_AIM_BOTTOM ? NATIVE_TOUCH_AIM|NATIVE_TOUCH_JUMP : 0;
}

static void touch_sample(unsigned int held,unsigned short x,unsigned short y,unsigned int elapsed)
{
    const struct native_input_sample sample={.held=held,.touch_x=x,.touch_y=y};
    input_time_ms+=elapsed;
    n3ds_engine_input_sample(&sample);
}

static void touch_gestures(void)
{
    const struct gamepad_state *pad=input_get_gamepad_state(0);
    struct native_control_settings saved,tuned;
    n3ds_controls_get(&saved);
    tuned=saved;tuned.touch_aim=0;
    n3ds_controls_set(&tuned);
    n3ds_input_gameplay_controls(1);
    n3ds_input_touch_handler(gesture_targets);

    /* Jump only after both contacts end, with no repeat on a third tap. */
    touch_sample(NATIVE_KEY_TOUCH,100,100,0);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    touch_sample(NATIVE_KEY_TOUCH,105,100,150);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    touch_sample(0,0,0,32);
    CHECK(pad->buttons[_gamepad_analog_button_a]==1);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);

    /* A long gap, held contact, drag or widely separated tap cannot jump. */
    touch_sample(NATIVE_KEY_TOUCH,105,100,351);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,201);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(NATIVE_KEY_TOUCH,114,100,32);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    touch_sample(NATIVE_KEY_TOUCH,130,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);

    /* Reload consumes a new contact and cannot turn into jump or aim. */
    touch_sample(NATIVE_KEY_TOUCH,160,16,60);
    CHECK(n3ds_input_touch_reload_pressed());
    CHECK(!pad->buttons[_gamepad_analog_button_x] && !pad->buttons[_gamepad_analog_button_a]);
    touch_sample(NATIVE_KEY_TOUCH,105,100,32);
    CHECK(!n3ds_input_touch_reload_pressed() && !pad->sticks[1].x && !pad->sticks[1].y);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);

    tuned.touch_aim=1;
    n3ds_controls_set(&tuned);
    touch_sample(NATIVE_KEY_TOUCH,160,41,60);
    touch_sample(NATIVE_KEY_TOUCH,161,40,32);
    CHECK(pad->sticks[1].x && pad->sticks[1].y);
    touch_sample(NATIVE_KEY_TOUCH,161,36,32);
    CHECK(!n3ds_input_touch_reload_pressed() && !pad->sticks[1].x && !pad->sticks[1].y);
    touch_sample(NATIVE_KEY_TOUCH,160,45,32);
    CHECK(!n3ds_input_touch_reload_pressed() && !pad->sticks[1].x && !pad->sticks[1].y);
    touch_sample(0,0,0,32);

    /* Pause, suppression and a new gameplay owner cancel an unfinished pair. */
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    n3ds_input_gameplay_controls(0);
    touch_sample(0,0,0,32);
    n3ds_input_gameplay_controls(1);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    n3ds_input_gameplay_controls(0);
    touch_sample(NATIVE_KEY_TOUCH,160,16,32);
    CHECK(!n3ds_input_touch_reload_pressed());
    n3ds_input_gameplay_controls(1);
    touch_sample(NATIVE_KEY_TOUCH,160,16,32);
    CHECK(!n3ds_input_touch_reload_pressed());
    touch_sample(0,0,0,32);
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    input_suppress();input_update();
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);
    n3ds_input_cancel_touch_actions();
    touch_sample(NATIVE_KEY_TOUCH,105,100,60);
    touch_sample(0,0,0,32);
    CHECK(!pad->buttons[_gamepad_analog_button_a]);

    n3ds_input_touch_handler(NULL);
    n3ds_input_gameplay_controls(0);
    n3ds_controls_set(&saved);
}

static void test_save_failure(int retry, int committed_button)
{
    open_buttons(0);
    tap(NATIVE_KEY_DRIGHT);
    fail_promotion = 1;
    tap(NATIVE_KEY_START);
    CHECK(n3ds_controls_active());
    n3ds_controls_draw();
    CHECK(last_view.save_error);
    if (retry) {
        fail_promotion = 0;
        tap(NATIVE_KEY_START);
    } else tap(NATIVE_KEY_B);
    CHECK(!n3ds_controls_active());
    expect_l_binding(retry ? _gamepad_analog_button_a : committed_button);
}

static void test_primary_io_failure(enum file_fault next_fault)
{
    open_buttons(0);
    tap(NATIVE_KEY_DRIGHT);
    fail_promotion = 1;
    fault = next_fault;
    tap(NATIVE_KEY_START);
    CHECK(fault == NO_FAULT && n3ds_controls_active());
    n3ds_controls_draw();
    CHECK(last_view.save_error);
    struct native_file_info info;
    CHECK(n3ds_file_stat(primary, &info));
    tap(NATIVE_KEY_B);
    expect_l_binding(_gamepad_analog_button_a);
}

static void test_legacy(int swapped)
{
    struct native_control_settings saved;
    n3ds_controls_get(&saved);
    CHECK(saved.cstick == 125 && saved.touch == 80 && saved.xbox_buttons == 1);
    n3ds_input_gameplay_controls(1);
    const struct gamepad_state *pad = frame(NATIVE_KEY_B | (swapped ? NATIVE_KEY_R : NATIVE_KEY_L));
    CHECK(pad->buttons[_gamepad_analog_button_right_trigger] == 1);
    CHECK(pad->buttons[_gamepad_analog_button_a] == 1);
    frame(swapped ? NATIVE_KEY_L : NATIVE_KEY_ZL);
    CHECK(pad->buttons[_gamepad_analog_button_left_trigger] == 1);
    frame(0);
    n3ds_input_gameplay_controls(0);
    CHECK(frame(NATIVE_KEY_B)->buttons[_gamepad_analog_button_a] == 1);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *scenario = argv[1];
    int fresh_defaults = !strcmp(scenario, "new") || !strcmp(scenario, "original");
    original = !strcmp(scenario, "original") || !strcmp(scenario, "read-original") || !strcmp(scenario, "graphics-original");
    if (fresh_defaults) {
        CHECK(!halo_engine_input_tests());
        CHECK(!halo_engine_touch_aim_tests());
    }
    CHECK(input_initialize());
    if (fresh_defaults) {
        default_mapping();
        preset_navigation();
        touch_picker();
        replaced_widget();
        reused_widget_address();
        main_menu_shortcut();
        remap_movement();
        touch_gestures();
        if (!original) tune_aim();
    } else if (!strcmp(scenario, "customize")) customize();
    else if (!strcmp(scenario, "read-primary")) expect_l_binding(_gamepad_analog_button_a);
    else if (!strcmp(scenario, "read-original")) expect_l_binding(_gamepad_analog_button_right_trigger);
    else if (!strcmp(scenario, "read-backup")) expect_l_binding(_gamepad_analog_button_left_trigger);
    else if (!strcmp(scenario, "save-failure")) test_save_failure(0, _gamepad_analog_button_left_trigger);
    else if (!strcmp(scenario, "save-retry")) test_save_failure(1, _gamepad_analog_button_left_trigger);
    else if (!strcmp(scenario, "rotation-failure")) test_save_failure(0, _gamepad_analog_button_a);
    else if (!strcmp(scenario, "open-failure")) test_primary_io_failure(OPEN_FAULT);
    else if (!strcmp(scenario, "size-failure")) test_primary_io_failure(SIZE_FAULT);
    else if (!strcmp(scenario, "read-failure")) test_primary_io_failure(READ_FAULT);
    else if (!strcmp(scenario, "close-failure")) test_primary_io_failure(CLOSE_FAULT);
    else if (!strcmp(scenario, "legacy-default")) test_legacy(0);
    else if (!strcmp(scenario, "legacy-swap")) test_legacy(1);
    else if (!strncmp(scenario, "graphics-", 9)) graphics_options(scenario);
    else CHECK(0);
    input_dispose();
    printf("PASS: controller settings %s\n", scenario);
    return 0;
}
