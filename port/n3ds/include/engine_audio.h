#ifndef N3DS_ENGINE_AUDIO_H
#define N3DS_ENGINE_AUDIO_H
/* Fixed-width API across the game/compiler and libctru boundary. */
int n3ds_audio_initialize(int channels);
void n3ds_audio_dispose(void);
void n3ds_audio_update(void);
int n3ds_audio_state(int channel);
int n3ds_audio_queue(int channel,const void *data,unsigned int bytes,int encoding,int compression);
void n3ds_audio_stop(int channel);
void n3ds_audio_pause(int paused);
void n3ds_audio_properties(int channel,float rate,float left,float right);
unsigned int n3ds_xbox_adpcm_decode(const unsigned char *data,unsigned int bytes,int channels,short *pcm,unsigned int capacity);
int n3ds_audio_codec_tests(void);
#endif
