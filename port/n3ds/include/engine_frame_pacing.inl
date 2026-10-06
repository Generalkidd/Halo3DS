/* Observe individual frames, rather than hiding short hitches in averages.
 * RAM counters only; the occasional summary uses the background log queue. */
static void frame_pacing_observe(long long start,const long long stages[5],int gameplay)
{
    extern long long n3ds_engine_tick_frequency(void);
    static unsigned int frames,over40,over66,over100;
    static double maximum,sum;
    double scale=1000.0/n3ds_engine_tick_frequency();
    double ms=(n3ds_engine_ticks()-start)*scale;
    if(!gameplay) {frames=over40=over66=over100=0;maximum=sum=0;return;}
    ++frames;sum+=ms;if(ms>maximum) maximum=ms;
    over40+=ms>40;over66+=ms>66.667;over100+=ms>100;
    if(ms>100) {
        char text[240];snprintf(text,sizeof(text),"FRAME HITCH: time=%ld work_ms=%.2f services_ms=%.2f input_ms=%.2f simulation_ms=%.2f audio_ms=%.2f render_ms=%.2f",
            game_time_get(),ms,(stages[0]-start)*scale,(stages[1]-stages[0])*scale,
            (stages[2]-stages[1])*scale,(stages[3]-stages[2])*scale,(stages[4]-stages[3])*scale);n3ds_log(text);
    }
    if(frames==300) {
        char text[200];snprintf(text,sizeof(text),"FRAME PACING: gameplay_frames=%u mean_work_ms=%.2f max_work_ms=%.2f over40=%u over67=%u over100=%u",frames,sum/frames,maximum,over40,over66,over100);n3ds_log(text);
        frames=over40=over66=over100=0;maximum=sum=0;
    }
}
