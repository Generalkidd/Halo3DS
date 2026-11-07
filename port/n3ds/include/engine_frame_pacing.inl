/* Observe individual frames, rather than hiding short hitches in averages.
 * RAM counters only; the occasional summary uses the background log queue. */
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
#include <stdlib.h>

static int compare_frame_work(const void *left, const void *right)
{
    float a = *(const float *)left;
    float b = *(const float *)right;
    return (a > b) - (a < b);
}
#endif

static void frame_pacing_observe(long long start,const long long stages[5],int gameplay)
{
    extern long long n3ds_engine_tick_frequency(void);
    enum { FRAME_WINDOW = 300 };
    static unsigned int frames,over40,over66,over100;
    static double maximum,sum;
    double scale=1000.0/n3ds_engine_tick_frequency();
    double ms=(stages[4]-start)*scale;
    if(gameplay && ms>100) {
        char text[240];snprintf(text,sizeof(text),"FRAME HITCH: time=%ld work_ms=%.2f services_ms=%.2f input_ms=%.2f simulation_ms=%.2f audio_ms=%.2f render_ms=%.2f",
            game_time_get(),ms,(stages[0]-start)*scale,(stages[1]-stages[0])*scale,
            (stages[2]-stages[1])*scale,(stages[3]-stages[2])*scale,(stages[4]-stages[3])*scale);n3ds_log(text);
    }
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
    static float work[FRAME_WINDOW],render_work[FRAME_WINDOW];
    static unsigned int generation, over33, over50, render_over_refresh;
    unsigned int current_generation = n3ds_engine_cache_generation();
    if (!gameplay || generation != current_generation) {
        if (gameplay) {
            char text[128];
            snprintf(text, sizeof(text), "FRAME WORK TRANSITION: time=%ld generation=%u work_ms=%.2f",
                game_time_get(), current_generation, ms);
            n3ds_log(text);
        }
        frames = over33 = over40 = over50 = over66 = over100 = render_over_refresh = 0;
        maximum = sum = 0;
        generation = current_generation;
        return;
    }
    work[frames] = (float)ms;
    render_work[frames] = (float)((stages[4]-stages[3])*scale);
    render_over_refresh += render_work[frames] > N3DS_TOP_REFRESH_MS;
    over33 += ms > 1000.0 / 30.0;
    over50 += ms > 50.0;
#endif
    if(!gameplay) {frames=over40=over66=over100=0;maximum=sum=0;return;}
    ++frames;sum+=ms;if(ms>maximum) maximum=ms;
    over40+=ms>40;over66+=ms>66.667;over100+=ms>100;
    if(frames==FRAME_WINDOW) {
        char text[256];snprintf(text,sizeof(text),"FRAME PACING: gameplay_frames=%u mean_work_ms=%.2f max_work_ms=%.2f over40=%u over67=%u over100=%u",frames,sum/frames,maximum,over40,over66,over100);n3ds_log(text);
#ifdef HALO_N3DS_PERFORMANCE_PROFILE
        qsort(work, frames, sizeof(work[0]), compare_frame_work);
        qsort(render_work, frames, sizeof(render_work[0]), compare_frame_work);
        snprintf(text, sizeof(text),
            "FRAME WORK PROFILE: generation=%u end_time=%ld frames=%u p50_ms=%.2f p95_ms=%.2f p99_ms=%.2f over33=%u over50=%u render_p95_ms=%.2f render_over_refresh=%u",
            generation, game_time_get(), frames, work[(FRAME_WINDOW * 50 + 99) / 100 - 1],
            work[(FRAME_WINDOW * 95 + 99) / 100 - 1], work[(FRAME_WINDOW * 99 + 99) / 100 - 1],
            over33, over50, render_work[(FRAME_WINDOW * 95 + 99) / 100 - 1], render_over_refresh);
        n3ds_log(text);
        over33 = over50 = render_over_refresh = 0;
#endif
        frames=over40=over66=over100=0;maximum=sum=0;
    }
}
