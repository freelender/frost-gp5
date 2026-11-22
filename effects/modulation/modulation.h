#ifndef FROST_MODULATION_H
#define FROST_MODULATION_H
#include <stdint.h>
/* Shared caller-owned kernel workspace for chorus OR flanger, not a third
 * selectable effect. Fixed 44.1 kHz; positive read distances below 1151. */
#define FROST_MOD_SAMPLES 1152u
#define FROST_MOD_PCM (2u*FROST_MOD_SAMPLES)
/* Union ring: float for chorus/flanger/BBD models, 16-bit for the octave
 * model's longer grain history. One instance owns each object at a time. */
typedef struct {
    union { float samples[FROST_MOD_SAMPLES]; int16_t pcm[FROST_MOD_PCM]; } line;
    unsigned position,valid;
    float phase,distance;
} FrostModulation;
void frost_modulation_reset(FrostModulation *s);
float frost_modulation_sample(FrostModulation *s,float input,const float *p,unsigned flanger);
#endif
