#ifndef FROST_DROP_TUNE_H
#define FROST_DROP_TUNE_H
#include "../modulation/modulation.h"
/* Stage 5 drop tune (docs/User%20Guide.md): pitch the whole signal down by 1..6
 * semitones (DROP 0..5 = 1/2..3 steps), chords included.
 *
 * One read head moves through a 16-bit history (the slot's exclusive
 * FrostModulation ring) at 2^(-n/12) samples per sample, read with 4-point
 * cubic (Hermite) interpolation. It falls behind the write position by
 * (1 - ratio) per sample; before it drifts too far it jumps forward by J
 * samples, where J is chosen by a normalized correlation search so that the
 * waveform at the new position continues the old one (a whole number of
 * periods for a note; the best common match for a chord). A 256-sample
 * crossfade covers the jump. The jump is taken as soon as a good match
 * (normalized correlation >= 0.9) exists, so latency stays near one period
 * of the lowest note playing; it is forced at the maximum jump.
 * The coarse search evaluates one candidate every 4 samples (16 points),
 * the fine one per sample with a parabolic sub-sample peak, so no block
 * carries a spike. MIX blends the original back in. */
#define FROST_DROP_MIN_DISTANCE 72.0f   /* closest the head reads behind the input */
#define FROST_DROP_JUMP_MIN 48u
#define FROST_DROP_JUMP_MAX 1200u       /* > one common period of a low power chord (E2+B2+E3: 1070) */
#define FROST_DROP_FADE 256u
typedef struct {
    FrostModulation *ring;
    float d,d_old,best_score,last,left,right;
    unsigned fade,state,cand,cap,best,lo,hi,right_pending;
} FrostDropTune;
void frost_drop_reset(FrostDropTune *s);
float frost_drop_sample(FrostDropTune *s,float x,const float *p);
#endif
