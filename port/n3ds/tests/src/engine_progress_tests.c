#include "cseries.h"
#include "progress_bar.h"
#include "rasterizer.h"
#include "engine_renderer.h"
void progress_bar_eachframe(void);
void n3ds_log(const char *message);
#define CHECK(x) do { if(!(x)) { n3ds_log("FAIL: native loading: " #x); return 1; } } while(0)
int halo_engine_progress_tests(void)
{
    struct rasterizer_frame_begin_parameters frame={0};
    struct rasterizer_window_begin_parameters window={0};
    static const real values[5]={.25f,.25f,.75f,1.f,1.f};
    unsigned int stage,i;
    window.camera.forward.i=1; window.camera.up.k=1;
    window.camera.vertical_field_of_view=1.f; window.camera.z_near=.05f; window.camera.z_far=100;
    window.camera.viewport_bounds.x1=400; window.camera.viewport_bounds.y1=240;
    window.camera.window_bounds=window.camera.viewport_bounds;
    progress_bar_initialize();
    CHECK(!progress_bar_is_active() && !progress_bar_is_stuff_ready());
    progress_bar_display(.5f); /* An inactive request must not require a frame. */
    progress_bar_begin(FALSE); CHECK(progress_bar_is_active());
    progress_bar_display(0); CHECK(!progress_bar_is_stuff_ready());
    progress_bar_enable(FALSE);
    for(stage=0;stage<5;++stage) {
        if(stage==1) progress_bar_enable(TRUE);
        if(stage==3) progress_bar_end();
        if(stage==4) progress_bar_begin(TRUE);
        for(i=0;i<30;++i) {
            rasterizer_frame_begin(&frame); rasterizer_windows_begin(); rasterizer_window_begin(&window);
            progress_bar_display(values[stage]);
            rasterizer_window_end(); progress_bar_eachframe(); rasterizer_windows_end(); rasterizer_frame_end(); rasterizer_present(NULL,NULL);
        }
        CHECK(progress_bar_is_active()==(stage!=3));
        CHECK(progress_bar_is_stuff_ready()==(stage!=0));
        CHECK(!n3ds_gpu_loading_capture(stage));
    }
    progress_bar_end(); progress_bar_dispose(); progress_bar_dispose();
    CHECK(!progress_bar_is_active() && !progress_bar_is_stuff_ready());
    n3ds_log("PASS: native loading lifecycle renders 25/75/100 percent, honors disabled/inactive states and both capture modes, then disposes without owning Xbox resources");
    return 0;
}
