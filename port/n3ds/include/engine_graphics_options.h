#ifndef HALO_N3DS_ENGINE_GRAPHICS_OPTIONS_H
#define HALO_N3DS_ENGINE_GRAPHICS_OPTIONS_H

#include "engine_textures.h"
#include "engine_world_resolution.h"

enum {
    NATIVE_OPTIONS_ENTRY_Y=202, NATIVE_OPTIONS_ENTRY_HEIGHT=30,
    NATIVE_CONTROLS_ENTRY_X=8, NATIVE_CONTROLS_ENTRY_WIDTH=160,
    NATIVE_GRAPHICS_ENTRY_X=176, NATIVE_GRAPHICS_ENTRY_WIDTH=136,
    NATIVE_GRAPHICS_TAB_Y=10, NATIVE_GRAPHICS_TAB_HEIGHT=26,
    NATIVE_GRAPHICS_RESOLUTION_X=12, NATIVE_GRAPHICS_TEXTURE_X=164, NATIVE_GRAPHICS_TAB_WIDTH=144,
    NATIVE_GRAPHICS_ROW_X=12, NATIVE_GRAPHICS_ROW_WIDTH=296,
    NATIVE_GRAPHICS_ROW_Y=48, NATIVE_GRAPHICS_ROW_HEIGHT=42, NATIVE_GRAPHICS_ROW_STRIDE=46,
    NATIVE_RESOLUTION_ROW_Y=44, NATIVE_RESOLUTION_ROW_HEIGHT=26, NATIVE_RESOLUTION_ROW_STRIDE=30,
    NATIVE_GRAPHICS_CANCEL_X=12, NATIVE_GRAPHICS_SAVE_X=168, NATIVE_GRAPHICS_BUTTON_WIDTH=140,
    NATIVE_GRAPHICS_MODE_COUNT=3
};

enum native_graphics_tab { NATIVE_GRAPHICS_RESOLUTION_TAB, NATIVE_GRAPHICS_TEXTURE_TAB };

struct native_graphics_options_view {
    /* Shared by Clang game code and GCC renderer; enum sizes differ. */
    unsigned int draft_texture,saved_texture;
    unsigned int draft_resolution,saved_resolution;
    unsigned int tab;
    int show_tabs,save_error;
};

int n3ds_graphics_options_available(void);
void n3ds_graphics_options_initialize(void);
void n3ds_graphics_options_open(void);
/* Returns true when the owning menu should close the overlay. */
int n3ds_graphics_options_input(unsigned int fresh,unsigned int x,unsigned int y);
void n3ds_graphics_options_draw(void);
void n3ds_gpu_graphics_options_draw(const struct native_graphics_options_view *view);

#endif
