#include "engine_graphics_options.h"

/* Native settings use a tiny shared font and one white texel, keeping
 * text at the LCD's resolution independently of the world rendering scale. */
static void *controls_white;
static void controls_quad(float x,float y,float w,float h,unsigned int color,int top)
{
    if(!controls_white) {
        unsigned int pixels[64];
        for(int i=0;i<64;++i)pixels[i]=~0u;
        controls_white=n3ds_gpu_texture_create(pixels,8,8);
    }
    if(!controls_white)return;
    n3ds_gpu_texture_bind(0,controls_white);
    if(!n3ds_gpu_text_begin(top?400:320,240,color,1,0))return;
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
    struct native_text_vertex quad[4]={
        {{x,y},{1,1,1,1},{0,0}},{{x+w,y},{1,1,1,1},{1,0}},
        {{x+w,y+h},{1,1,1,1},{1,1}},{{x,y+h},{1,1,1,1},{0,1}}
    };
    n3ds_gpu_text_draw(quad);
}
static void controls_text_size(float x,float y,const char *text,unsigned int color,int top,int size)
{
    if(!loading_font)status_text(NULL,0,1);
    if(!loading_font)return;
    n3ds_gpu_texture_bind(0,loading_font);
    if(!n3ds_gpu_text_begin(top?400:320,240,color,1,0))return;
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    int batch=n3ds_gpu_text_batch_begin();
    for(const char *p=text;*p;++p,x+=size) {
        unsigned int c=(unsigned char)*p;
        float u=(c%16)/16.f,v=(c/16)/8.f;
        struct native_text_vertex quad[4]={
            {{x,y},{1,1,1,1},{u,v}},{{x+size,y},{1,1,1,1},{u+1.f/16,v}},
            {{x+size,y+size},{1,1,1,1},{u+1.f/16,v+1.f/8}},{{x,y+size},{1,1,1,1},{u,v+1.f/8}}
        };
        n3ds_gpu_text_draw(quad);
    }
    if(batch)n3ds_gpu_text_batch_end();
}
static void controls_text(float x,float y,const char *text,unsigned int color,int top)
{
    controls_text_size(x,y,text,color,top,8);
}
static void controls_text_right(int right,int y,const char *text,unsigned int color,int top)
{
    controls_text(right-8*(int)strlen(text),y,text,color,top);
}
void n3ds_gpu_controls_entry(void)
{
    void *saved=n3ds_gpu_texture_bound(0);
    n3ds_gpu_hud_screen(1);
    n3ds_gpu_hud_layout(0);
    if(!n3ds_graphics_options_available()) {
        controls_quad(24,NATIVE_OPTIONS_ENTRY_Y,272,NATIVE_OPTIONS_ENTRY_HEIGHT,0xff53391a,0);
        controls_text(28,213,"SELECT / Tap: Controller Settings",0xffffffff,0);
    } else {
        controls_quad(NATIVE_CONTROLS_ENTRY_X,NATIVE_OPTIONS_ENTRY_Y,NATIVE_CONTROLS_ENTRY_WIDTH,
            NATIVE_OPTIONS_ENTRY_HEIGHT,0xff53391a,0);
        controls_quad(NATIVE_GRAPHICS_ENTRY_X,NATIVE_OPTIONS_ENTRY_Y,NATIVE_GRAPHICS_ENTRY_WIDTH,
            NATIVE_OPTIONS_ENTRY_HEIGHT,0xff53391a,0);
        controls_text(12,207,"Controller Settings",0xffffffff,0);
        controls_text(40,220,"SELECT / Tap",0xffe9d6aa,0);
        controls_text(180,207,"Graphics Options",0xffffffff,0);
        controls_text(216,220,"Y / Tap",0xffe9d6aa,0);
    }
    n3ds_gpu_hud_screen(0);
    n3ds_gpu_texture_bind(0,saved);
    n3ds_gpu_world_state_restore();
}
static void controls_button(int x,int y,int width,int height,const char *label,unsigned int color,unsigned int ink)
{
    controls_quad(x,y,width,height,color,0);
    controls_text(x+(width-8*(int)strlen(label))/2,y+(height-8)/2,label,ink,0);
}

static const char *controls_binding_summary(char summary[20],const char *name,int count)
{
    if(!name) return "Unbound";
    if(count<=1) return name;
    snprintf(summary,20,"%s +%d",name,count-1);
    return summary;
}

void n3ds_gpu_controls_draw(const struct native_controls_view *view)
{
    static const char *const tabs[NATIVE_CONTROLS_TABS]={"Layout","Buttons","Aim"};
    static const char *const titles[NATIVE_CONTROLS_TABS]={"Control layout","Button bindings","Aim options"};
    const unsigned int backdrop=0xff24150c, panel=0xff35251a, selected=0xff674826;
    const unsigned int button=0xff765435, accent=0xffeec171, ink=0xffffffff, muted=0xffe9d6aa;
    const unsigned int dim=0xffa28c74, warning=0xff80aaff;
    char fire_summary[20], grenade_summary[20];
    const char *fire=controls_binding_summary(fire_summary,view->fire,view->fire_bindings);
    const char *grenade=controls_binding_summary(grenade_summary,view->grenade,view->grenade_bindings);
    int choosing_binding=!view->picking && view->tab==NATIVE_CONTROLS_BUTTONS;
    void *saved=n3ds_gpu_texture_bound(0);
    n3ds_gpu_hud_layout(0);
    n3ds_gpu_hud_screen(0);
    controls_quad(0,0,400,240,backdrop,1);
    controls_text(16,15,"CONTROLLER SETTINGS",accent,1);
    controls_text_right(384,15,view->original?"Original 3DS / 2DS":"New 3DS / 2DS",muted,1);
    controls_text_size(16,31,view->picking?"Choose action":titles[view->tab],ink,1,16);
    if(view->save_error) controls_text_right(384,38,"SAVE FAILED",warning,1);
    else if(view->dirty) controls_text_right(384,38,"Unsaved changes",accent,1);
    controls_quad(16,55,368,2,selected,1);

    controls_quad(16,65,368,84,panel,1);
    controls_quad(16,65,3,84,accent,1);
    if(view->focus_label) controls_text(28,75,view->focus_label,accent,1);
    if(view->preset_pending) controls_text_right(372,75,"Preset preview",muted,1);
    if(view->focus_value) {
        int size=strlen(view->focus_value)<=21?16:8;
        controls_text_size(28,91,view->focus_value,ink,1,size);
    }
    for(int i=0;i<2;++i)
        if(view->help[i]) controls_text(28,115+14*i,view->help[i],muted,1);

    controls_quad(16,158,180,28,panel,1);
    controls_quad(204,158,180,28,panel,1);
    controls_text(28,168,"FIRE",accent,1);
    controls_text(216,168,"THROW",accent,1);
    controls_text_right(184,168,fire,!strcmp(fire,"Unbound")?warning:ink,1);
    controls_text_right(372,168,grenade,!strcmp(grenade,"Unbound")?warning:ink,1);
    if(view->picking) {
        controls_text(16,202,"Up/Down selects  |  Left/Right pages",muted,1);
        controls_text(16,219,"A chooses the action  |  B goes back",muted,1);
    } else if(view->save_error) {
        controls_text(16,202,"Could not save settings to the SD card.",warning,1);
        controls_text(16,219,"START retries. B discards your edits.",muted,1);
    } else {
        controls_text(16,202,choosing_binding?"A opens the action picker  |  L/R switch tabs":
            view->preset_pending?"A applies the preset  |  L/R switch tabs":
            "L/R switch tabs",muted,1);
        controls_text(16,219,"Up/Down selects  |  Left/Right changes",muted,1);
    }

    n3ds_gpu_hud_screen(1);
    controls_quad(0,0,320,240,backdrop,0);
    for(int tab=0;tab<NATIVE_CONTROLS_TABS;++tab) {
        int x=12+100*tab, active=tab==view->tab;
        controls_button(x,10,96,26,tabs[tab],active?selected:panel,view->picking && !active?dim:ink);
        if(active) controls_quad(x,34,96,2,accent,0);
    }
    int visible=view->count-view->first;
    if(visible>NATIVE_CONTROLS_VISIBLE_ROWS)visible=NATIVE_CONTROLS_VISIBLE_ROWS;
    for(int row=0;row<visible;++row) {
        int y=NATIVE_CONTROLS_ROW_Y+NATIVE_CONTROLS_ROW_STRIDE*row;
        int focused=view->first+row==view->selected;
        controls_quad(12,y,296,NATIVE_CONTROLS_ROW_HEIGHT,focused?selected:panel,0);
        if(focused) controls_quad(12,y,3,NATIVE_CONTROLS_ROW_HEIGHT,accent,0);
        if(view->picking) {
            controls_text(22,y+6,view->labels[row],ink,0);
            if(view->values[row][0])
                controls_button(228,y+2,74,17,view->values[row],backdrop,accent);
        } else {
            controls_text(22,y+2,view->labels[row],muted,0);
            controls_text(22,y+11,view->values[row],ink,0);
            if(choosing_binding) controls_text(200,y+6,">",focused?accent:muted,0);
            else if(view->tab==NATIVE_CONTROLS_LAYOUT && view->first+row==0 && view->preset_pending)
                controls_text(154,y+2,"Preview",accent,0);
            controls_button(218,y,38,NATIVE_CONTROLS_ROW_HEIGHT,"-",focused?button:selected,ink);
            controls_button(264,y,38,NATIVE_CONTROLS_ROW_HEIGHT,"+",focused?button:selected,ink);
        }
    }
    if(view->count>NATIVE_CONTROLS_VISIBLE_ROWS) {
        const int page_height=NATIVE_CONTROLS_PAGE_END-NATIVE_CONTROLS_PAGE_Y;
        int previous=view->first>0, next=view->first+visible<view->count;
        char range[24];
        snprintf(range,sizeof(range),"%d-%d of %d",view->first+1,view->first+visible,view->count);
        controls_button(12,NATIVE_CONTROLS_PAGE_Y,88,page_height,"< Prev",previous?selected:panel,previous?ink:dim);
        controls_button(100,NATIVE_CONTROLS_PAGE_Y,120,page_height,range,backdrop,muted);
        controls_button(220,NATIVE_CONTROLS_PAGE_Y,88,page_height,"Next >",next?selected:panel,next?ink:dim);
    }
    if(view->picking) {
        controls_button(112,202,96,30,"B Back",selected,ink);
    } else {
        controls_button(12,202,92,30,view->preset_pending?"A Apply":"X Reset tab",view->preset_pending?selected:panel,ink);
        controls_button(112,202,96,30,"B Cancel",panel,ink);
        const char *save=view->save_error?"Retry Save":view->preset_pending?"Save edits":"START Save";
        controls_button(216,202,92,30,save,selected,view->save_error?warning:ink);
    }
    n3ds_gpu_hud_screen(0);
    n3ds_gpu_texture_bind(0,saved);
    n3ds_gpu_world_state_restore();
}

#include "engine_graphics_options_draw.inl"
