#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine_textures.h"
#include "engine_renderer.h"
#include "engine_parallel.h"

static FILE *log_file;
static char log_buffer[16*1024];
static int graphics_owned;
static int error_console;
static int loading_console=1;
#include "engine_log_queue.inl"
void n3ds_log_shutdown(void) { log_async_finish(); }
/* libctru applies this during startup: New 3DS 804 MHz CPU and L2 cache. */
bool __ctru_speedup = true;
#ifdef HALO_N3DS_LOW_MEMORY_TEST
/* Explicit developer fixture: approximate the original 64 MiB process after
 * executable/stack overhead, without borrowing Azahar's extra app memory. */
u32 __ctru_heap_size = 24U*1024*1024;
u32 __ctru_linear_heap_size = 32U*1024*1024;
#endif
int halo_engine_boot(void);
static void flush_console(void)
{
    if(!error_console && n3ds_gpu_bottom_owned()) graphics_owned=1;
    if(graphics_owned) return;
    u16 width, height;
    u8 *buffer=gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
    GSPGPU_FlushDataCache(buffer, width*height*gspGetBytesPerPixel(gfxGetScreenFormat(GFX_BOTTOM)));
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}
void n3ds_log(const char *message)
{
    /* Keep the original failure even if the next launch replaces its session
     * log. Fatal paths are rare; close immediately to commit to the SD card. */
    if(strstr(message,"FATAL:") || strstr(message,"FAIL:") || !strcmp(message,"RESULT: FAIL")) {
        FILE *fatal=fopen("sdmc:/halo-source-fatal.log","a");
        if(fatal) {fprintf(fatal,"[%lld] %s\n",(long long)time(NULL),message);fclose(fatal);}
    }
    if(!error_console && n3ds_gpu_bottom_owned()) graphics_owned=1;
    if(!graphics_owned && (!loading_console || error_console)) {printf("%s\n", message);fflush(stdout);}
    if(graphics_owned && !log_thread) log_async_start();
    if(log_thread) log_enqueue(message);
    else if (log_file) {
        fprintf(log_file, "%s\n", message);
        /* Batch routine diagnostics between the existing 30-frame heartbeat.
         * Flush startup, errors and final results immediately. This removes
         * dozens of SD transactions from each diagnostic burst while keeping
         * the wall-clock test observer's frame markers promptly visible. */
        if(!graphics_owned || !strncmp(message,"STARTUP:",8) ||
           !strncmp(message,"NATIVE MEMORY:",14) || !strncmp(message,"FRONTEND FRAME:",15) ||
           !strncmp(message,"MISSION TICK END:",17) ||
           !strncmp(message,"RESULT:",7) || !strncmp(message,"EXIT:",5) || strstr(message,"FATAL:") ||
           strstr(message,"FAIL") || strstr(message,"ERROR") || strstr(message,"ASSERT"))
            fflush(log_file);
    }
    flush_console();
}
#include "engine_log_tests.inl"
int main(void)
{
    gfxInitDefault(); gfxSetDoubleBuffering(GFX_BOTTOM, false); consoleInit(GFX_BOTTOM, NULL);
    printf("\n\n             Halo3DS\n\n\n          Loading game data...\n\n    Initial loading may take a while.\n");
    fflush(stdout);flush_console();
    remove("sdmc:/halo-source-engine.previous.log");
    rename("sdmc:/halo-source-engine.log","sdmc:/halo-source-engine.previous.log");
    log_file = fopen("sdmc:/halo-source-engine.log", "w");
    if(log_file) setvbuf(log_file,log_buffer,_IOFBF,sizeof(log_buffer));
    n3ds_log("Halo native engine bootstrap");
    n3ds_log("BUILD: Halo3DS Original-model adaptive rendering 2026-10-05");
#ifdef HALO_N3DS_LOW_MEMORY_TEST
    n3ds_log("LOW MEMORY FIXTURE: 24 MiB application heap + 32 MiB linear heap");
#endif
    bool new_model=false;APT_CheckNew3DS(&new_model);
    n3ds_log(new_model?"MODEL: New 3DS family; high-speed CPU and L2 cache requested":"MODEL: original 3DS/2DS family; standard CPU speed");
    /* Retain capacity for two A10 corridor views and their effects, while
     * keeping every draw and the live command-capacity checks. */
    n3ds_parallel_initialize();
    int gpu_ready = C3D_Init(8 * C3D_DEFAULT_CMDBUF_SIZE);
    if (gpu_ready) n3ds_log("GPU command buffer: 2097152 bytes (two-eye capacity)");
    int programs_ready = gpu_ready && n3ds_gpu_programs_initialize();
    int textures_ready = programs_ready && n3ds_gpu_textures_initialize();
    extern void n3ds_stall_watch_start(void),n3ds_stall_watch_stop(void);
    n3ds_stall_watch_start();
    int result = textures_ready ? halo_engine_boot() : 1;
    extern void n3ds_prefetch_shutdown(void);
    n3ds_prefetch_shutdown();
    n3ds_stall_watch_stop();
    if (textures_ready) n3ds_gpu_textures_dispose();
    if (gpu_ready) C3D_Fini();
    if (programs_ready) n3ds_gpu_programs_dispose();
    if(result) {
        /* Render targets may have taken over the bottom screen. Reclaim it
         * after GPU shutdown so a startup failure cannot look like a freeze. */
        error_console=1;graphics_owned=0;gfxSet3D(false);gfxSetDoubleBuffering(GFX_BOTTOM,false);
        consoleInit(GFX_BOTTOM,NULL);
        printf("Halo3DS stopped during startup.\n\nSee halo-source-engine.log\non the SD card for details.\n\nPress START to exit.\n");
        fflush(stdout);flush_console();
    }
    n3ds_log(result ? "RESULT: FAIL" : "RESULT: PASS");
    n3ds_parallel_shutdown();log_async_finish();
    if (log_file) {fclose(log_file);log_file=NULL;}
    while (result && aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        flush_console(); gspWaitForVBlank();
    }
    gfxExit(); return result;
}
