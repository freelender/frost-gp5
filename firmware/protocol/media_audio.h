#ifndef FROST_MEDIA_AUDIO_H
#define FROST_MEDIA_AUDIO_H
#include <stdint.h>
/* UAC1 stereo frames: signed 16-bit left in the low half. Frost's internal
 * float headroom is retained for the listening volume; recording saturates
 * at full scale before that volume, and excludes computer playback. */
static inline int frost_media_s16(float x)
{return x>=1.0f?32767:x<=-1.0f?-32768:(int)(x*32768.0f);}
static inline uint32_t frost_media_record(float left,float right)
{return (uint32_t)(uint16_t)frost_media_s16(left)|((uint32_t)(uint16_t)frost_media_s16(right)<<16);}
static inline float frost_media_play(uint32_t frame,unsigned right)
{return (float)(int16_t)(frame>>(right?16u:0u))*(1.0f/32768.0f);}
#endif
