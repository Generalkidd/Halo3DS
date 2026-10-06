/* No Bink decoder is available on this target. Like upstream's Linux port,
 * requests fail to open a movie. Do this at the game-facing boundary so we
 * never allocate Xbox textures/audio buffers or suppress input for a movie
 * which cannot start. This does not handle engine-rendered cinematics. */
#include "cseries.h"
#include "bink_playback.h"
#include "cache_files.h"

void n3ds_log(const char *message);
boolean debug_bink=FALSE;
static boolean initialized;

void bink_playback_initialize(void) { initialized=TRUE; }
void bink_playback_dispose(void) { initialized=FALSE; }
boolean bink_playback_active(void) { return FALSE; }
boolean bink_playback_in_progress(void) { return FALSE; }
boolean bink_playback_ui_rendering_inhibited(void) { return FALSE; }
void bink_playback_stop(void) { /* No movie has acquired resources or UI state. */ }
void bink_playback_render(void) { /* No decoded frame exists. */ }
void bink_playback_update(void) { /* No decoder work is pending. */ }
void bink_playback_start(const char *path,unsigned long flags)
{
    char message[256];
    (void)flags;
    if(!initialized || cache_files_precache_in_progress()) return;
    snprintf(message,sizeof(message),"Bink video unavailable on New 3DS; skipped: %.180s",path?path:"(no path)");
    n3ds_log(message);
}

int halo_engine_bink_tests(void)
{
    unsigned long bit;
    bink_playback_start("before-initialize.bik",0);
    bink_playback_initialize();
    for(bit=0;bit<NUMBER_OF_BINK_PLAYBACK_FLAGS;++bit) {
        bink_playback_start("unsupported-video.bik",1UL<<bit);
        bink_playback_update(); bink_playback_render();
        if(bink_playback_active() || bink_playback_in_progress() || bink_playback_ui_rendering_inhibited()) return 1;
        bink_playback_stop();
    }
    bink_playback_start(NULL,0);
    bink_playback_dispose(); bink_playback_dispose();
    if(bink_playback_active() || bink_playback_in_progress() || bink_playback_ui_rendering_inhibited()) return 1;
    n3ds_log("PASS: unavailable Bink videos never become active, inhibit UI or claim decoded frames across all playback flags and repeated disposal");
    return 0;
}
