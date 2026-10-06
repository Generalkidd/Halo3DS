/* Original sound-manager platform callbacks, replacing Xbox DirectSound.
 * Manager owns software references; this adapter separately pins encoded
 * samples until the DSP streaming source has been retired. */
#include "cseries.h"
#include "sound/sound_preferences.h"
#include "sound/sound_definitions.h"
#include "sound/game_sound.h"
#include "cache/sound_cache.h"
#include "engine_audio.h"
struct platform_sound_channel_properties {real minimum_distance,maximum_distance,pitch,gain,cone_inside_angle,cone_outside_angle,cone_outside_gain,reverb_attenuation;};
struct platform_sound_listener_properties {real_point3d position;real_vector3d forward,up,velocity;const void *environment;};
struct sound_platform_definition {
    short platform_code;byte pad[2];
    boolean (*initialize)(struct sound_preferences *);
    void (*dispose)(void);
    void (*listener)(const struct platform_sound_listener_properties *);
    void (*begin)(void),(*end)(void);
    void (*queue)(short,struct sound_permutation *);
    void (*update)(short),(*stop)(short);
    short (*state)(short);
    void (*pause)(boolean),(*flush)(void);
    void (*location)(short,boolean,const struct sound_location *,real,real,boolean);
    void (*properties)(short,const struct platform_sound_channel_properties *,boolean);
    real direct_path_gain;
};
_Static_assert(sizeof(struct sound_platform_definition)==60,"Sound platform ABI");
struct native_voice {struct sound_permutation *playing,*queued;struct platform_sound_channel_properties properties;struct sound_location location;float obstruction,occlusion;int spatial;};
static struct native_voice voice[24];static int live;static unsigned int queued_count;
void n3ds_log(const char *);
static void refresh(short i)
{
    struct native_voice *v=voice+i;int state=n3ds_audio_state(i);
    if(v->queued && state<2) {sound_cache_sound_hardware_unlock(v->playing);v->playing=v->queued;v->queued=NULL;}
    if(v->playing && !state) {sound_cache_sound_hardware_unlock(v->playing);v->playing=NULL;}
}
static void stop(short i)
{
    assert(i>=0 && i<24);n3ds_audio_stop(i);
    if(voice[i].playing) sound_cache_sound_hardware_unlock(voice[i].playing);
    if(voice[i].queued) sound_cache_sound_hardware_unlock(voice[i].queued);
    voice[i].playing=voice[i].queued=NULL;
}
static void flush(void) {if(live) for(short i=0;i<24;++i) stop(i);}
static void dispose(void) {flush();n3ds_audio_dispose();live=0;}
static boolean initialize(struct sound_preferences *p)
{
    static const short counts[4]={4,14,4,2};
    for(int i=0;i<4;++i) p->actual_channel_counts[i]=p->virtual_channel_counts[i]=counts[i];
    memset(voice,0,sizeof(voice));queued_count=0;live=n3ds_audio_initialize(24);
    return live;
}
static void properties_apply(short i)
{
    struct native_voice *v=voice+i;
    float gain=MAX(0.f,v->properties.gain),pan=0;
    int rate=i>=22?44100:22050;
    if(v->spatial) {
        const real_point3d *p=&v->location.position;
        float distance=sqrtf(p->x*p->x+p->y*p->y+p->z*p->z),minimum=MAX(v->properties.minimum_distance,.001f);
        if(distance>minimum) gain*=minimum/distance;
        if(distance>1e-5f) pan=PIN(-p->y/distance,-1.f,1.f); /* listener +Y is left */
        gain*=PIN(1-v->obstruction,0.f,1.f)*PIN(1-v->occlusion,0.f,1.f);
    }
    float left=gain*(pan>0?1-pan:1),right=gain*(pan<0?1+pan:1);
    n3ds_audio_properties(i,rate*PIN(v->properties.pitch,.125f,4.f),left,right);
}
static void properties(short i,const struct platform_sound_channel_properties *p,boolean gain_only)
{
    assert(i>=0 && i<24);if(gain_only) voice[i].properties.gain=p->gain;else voice[i].properties=*p;properties_apply(i);
}
static void location(short i,boolean spatial,const struct sound_location *p,real obstruction,real occlusion,boolean attenuate)
{
    assert(i>=0 && i<24);voice[i].spatial=spatial;voice[i].location=*p;voice[i].obstruction=obstruction;voice[i].occlusion=occlusion;properties_apply(i);
}
static void queue(short i,struct sound_permutation *p)
{
    assert(i>=0 && i<24 && p && p->unknown1);
    /* Do not refresh mid-manager transaction: queued/playing transitions are
     * observed together at scene begin, before its own state polling. */
    struct native_voice *v=voice+i;
    int channels=i>=18?2:1;
    sound_cache_sound_hardware_lock(p);
    if(!n3ds_audio_queue(i,(void *)p->unknown1,p->samples.size,channels,p->compression)) {
        sound_cache_sound_hardware_unlock(p);n3ds_log("AUDIO ERROR: sample format rejected");return;
    }
    /* Keep the old pending sample pinned until replacement succeeds: a
     * rejected request leaves the platform's existing queue unchanged. */
    if(v->queued) {sound_cache_sound_hardware_unlock(v->queued);v->queued=NULL;}
    if(v->playing) v->queued=p;else v->playing=p;
    if(queued_count<96 || !(queued_count%128)) {char text[240];snprintf(text,sizeof(text),"AUDIO QUEUE: channel=%d tag=%08lx compression=%d bytes=%ld sample=%.32s name=%.110s",i,p->unknown3,p->compression,p->samples.size,p->name,tag_get_name(p->unknown3));n3ds_log(text);}++queued_count;
}
static void begin(void) {n3ds_audio_update();for(short i=0;live && i<24;++i) refresh(i);}
static void end(void) {n3ds_audio_update();} /* submit new requests without another frame of latency */
static void update(short i) {(void)i;}
static short state(short i) {assert(i>=0 && i<24);refresh(i);return n3ds_audio_state(i);}
static void pause(boolean p) {n3ds_audio_pause(p);n3ds_log(p?"AUDIO PAUSE: on":"AUDIO PAUSE: off");}
static void listener(const struct platform_sound_listener_properties *p) {(void)p;}
struct sound_platform_definition platform_sound_dsound={0,{0},initialize,dispose,listener,begin,end,queue,update,stop,state,pause,flush,location,properties,.25f};
