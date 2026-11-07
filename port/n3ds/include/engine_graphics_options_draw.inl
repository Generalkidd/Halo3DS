void n3ds_gpu_graphics_options_draw(const struct native_graphics_options_view *view)
{
    static const char *const texture_names[NATIVE_GRAPHICS_MODE_COUNT]={
        [N3DS_WORLD_TEXTURE_64_SINGLE]="Classic",
        [N3DS_WORLD_TEXTURE_64_MIPS]="Balanced",
        [N3DS_WORLD_TEXTURE_32_MIPS]="Performance"
    };
    static const char *const texture_details[NATIVE_GRAPHICS_MODE_COUNT]={
        [N3DS_WORLD_TEXTURE_64_SINGLE]="64px detail, single level",
        [N3DS_WORLD_TEXTURE_64_MIPS]="64px detail + mipmaps",
        [N3DS_WORLD_TEXTURE_32_MIPS]="32px detail + mipmaps (default)"
    };
    static const char *const resolution_names[N3DS_WORLD_RESOLUTION_COUNT]={
        [N3DS_WORLD_RESOLUTION_AUTO]="Auto",
        [N3DS_WORLD_RESOLUTION_400]="Native",
        [N3DS_WORLD_RESOLUTION_320]="Balanced",
        [N3DS_WORLD_RESOLUTION_280]="Performance",
        [N3DS_WORLD_RESOLUTION_200]="Lowest"
    };
    static const char *const resolution_sizes[N3DS_WORLD_RESOLUTION_COUNT]={
        [N3DS_WORLD_RESOLUTION_AUTO]="Adaptive",
        [N3DS_WORLD_RESOLUTION_400]="400x240",
        [N3DS_WORLD_RESOLUTION_320]="320x192",
        [N3DS_WORLD_RESOLUTION_280]="280x168",
        [N3DS_WORLD_RESOLUTION_200]="200x120"
    };
    static const char *const resolution_help[N3DS_WORLD_RESOLUTION_COUNT]={
        [N3DS_WORLD_RESOLUTION_AUTO]="Adjusts the world resolution as needed.",
        [N3DS_WORLD_RESOLUTION_400]="Full detail on the top screen.",
        [N3DS_WORLD_RESOLUTION_320]="36% fewer world pixels than native.",
        [N3DS_WORLD_RESOLUTION_280]="51% fewer world pixels than native.",
        [N3DS_WORLD_RESOLUTION_200]="75% fewer world pixels than native."
    };
    const unsigned int backdrop=0xff24150c, panel=0xff35251a, selected=0xff674826;
    const unsigned int accent=0xffeec171, ink=0xffffffff, muted=0xffe9d6aa, warning=0xff80aaff;
    const int resolution=view->tab==NATIVE_GRAPHICS_RESOLUTION_TAB;
    const char *title=resolution?"World resolution":"World texture detail";
    void *saved_texture=n3ds_gpu_texture_bound(0);
    n3ds_gpu_hud_layout(0);
    n3ds_gpu_hud_screen(0);
    controls_quad(0,0,400,240,backdrop,1);
    controls_text(16,15,"GRAPHICS OPTIONS",accent,1);
    controls_text_size(16,31,title,ink,1,16);
    controls_quad(16,55,368,2,selected,1);
    controls_quad(16,65,368,100,panel,1);
    controls_quad(16,65,3,100,accent,1);
    if(resolution) {
        controls_text(28,77,resolution_names[view->draft_resolution],accent,1);
        controls_text(28,94,resolution_sizes[view->draft_resolution],ink,1);
        controls_text(28,118,resolution_help[view->draft_resolution],muted,1);
        controls_text(28,132,"HUD and menus stay sharp.",muted,1);
        controls_text(28,150,"Applies as soon as you save.",ink,1);
    } else {
        controls_text(28,77,texture_names[view->draft_texture],accent,1);
        controls_text(28,94,texture_details[view->draft_texture],ink,1);
        controls_text(28,118,"Mipmaps smooth supported world detail",muted,1);
        controls_text(28,132,"as it recedes into the distance.",muted,1);
        controls_text(28,150,"Applies on the next map load.",ink,1);
    }
    if(view->show_tabs) controls_text(16,180,"Left/Right switches tabs.",muted,1);
    if(view->save_error) {
        controls_text(16,198,"Could not save settings to the SD card.",warning,1);
        controls_text(16,218,"A / START retries. B cancels.",muted,1);
    } else {
        controls_text(16,198,"Up/Down or tap selects a mode.",muted,1);
        controls_text(16,218,"A / START saves. B cancels.",muted,1);
    }

    n3ds_gpu_hud_screen(1);
    controls_quad(0,0,320,240,backdrop,0);
    if(view->show_tabs) {
        controls_button(NATIVE_GRAPHICS_RESOLUTION_X,NATIVE_GRAPHICS_TAB_Y,NATIVE_GRAPHICS_TAB_WIDTH,
            NATIVE_GRAPHICS_TAB_HEIGHT,"Resolution",resolution?selected:panel,resolution?accent:ink);
        controls_button(NATIVE_GRAPHICS_TEXTURE_X,NATIVE_GRAPHICS_TAB_Y,NATIVE_GRAPHICS_TAB_WIDTH,
            NATIVE_GRAPHICS_TAB_HEIGHT,"Texture detail",resolution?panel:selected,resolution?ink:accent);
    } else controls_text(12,22,title,accent,0);
    int count=resolution?N3DS_WORLD_RESOLUTION_COUNT:NATIVE_GRAPHICS_MODE_COUNT;
    for(int row=0;row<count;++row) {
        int mode=resolution?row:count-1-row;
        int y=resolution?NATIVE_RESOLUTION_ROW_Y+row*NATIVE_RESOLUTION_ROW_STRIDE:
            NATIVE_GRAPHICS_ROW_Y+row*NATIVE_GRAPHICS_ROW_STRIDE;
        int height=resolution?NATIVE_RESOLUTION_ROW_HEIGHT:NATIVE_GRAPHICS_ROW_HEIGHT;
        int focused=resolution?mode==(int)view->draft_resolution:mode==(int)view->draft_texture;
        int saved=resolution?mode==(int)view->saved_resolution:mode==(int)view->saved_texture;
        controls_quad(NATIVE_GRAPHICS_ROW_X,y,NATIVE_GRAPHICS_ROW_WIDTH,height,focused?selected:panel,0);
        if(focused) controls_quad(NATIVE_GRAPHICS_ROW_X,y,3,height,accent,0);
        controls_text(22,y+9,resolution?resolution_names[mode]:texture_names[mode],ink,0);
        if(resolution) controls_text(168,y+9,resolution_sizes[mode],muted,0);
        else controls_text(22,y+25,texture_details[mode],muted,0);
        if(saved) controls_text_right(298,y+9,"Saved",accent,0);
    }
    controls_button(NATIVE_GRAPHICS_CANCEL_X,NATIVE_OPTIONS_ENTRY_Y,NATIVE_GRAPHICS_BUTTON_WIDTH,
        NATIVE_OPTIONS_ENTRY_HEIGHT,"B Cancel",panel,ink);
    controls_button(NATIVE_GRAPHICS_SAVE_X,NATIVE_OPTIONS_ENTRY_Y,NATIVE_GRAPHICS_BUTTON_WIDTH,
        NATIVE_OPTIONS_ENTRY_HEIGHT,view->save_error?"Retry Save":"A / START Save",selected,view->save_error?warning:ink);
    n3ds_gpu_hud_screen(0);
    n3ds_gpu_texture_bind(0,saved_texture);
    n3ds_gpu_world_state_restore();
}
