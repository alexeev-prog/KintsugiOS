#ifndef KINTSUGI_AUDIO_H
#define KINTSUGI_AUDIO_H
#include "../kklibc/ctypes.h"
void audio_init(void);
void audio_tone(u32 frequency, u32 milliseconds);
void audio_tick(void);
void audio_volume(u32 value);
u32 audio_get_volume(void);
int audio_is_enabled(void);
#endif
