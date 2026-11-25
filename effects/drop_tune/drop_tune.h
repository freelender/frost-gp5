#ifndef FROST_DROP_TUNE_H
#define FROST_DROP_TUNE_H
#include "../modulation/modulation.h"
#include "../common/pitch_shift.h"
/* Stage 5 drop tune (docs/User%20Guide.md): pitch the whole signal down by 1..6
 * semitones (DROP 0..5 = 1/2..3 steps), chords included.
 *
 * One read head (effects/common/pitch_shift.h splicer) moves through the
 * slot's exclusive history at 2^(-n/12) samples per sample. It falls behind
 * the input by (1 - ratio) per sample; it then jumps forward by J samples,
 * chosen by a normalized correlation search (40 points of a low-passed /8
 * copy over 320 samples, then 64 full-rate points with a sub-sample peak),
 * so the waveform continues: about one period of a note, the best common
 * match for a chord. The jump is crossfaded over J samples (128..1024) with
 * gains normalized by the measured correlation, so imperfect chord matches
 * neither dip nor bump. The first clearly good match is taken, so latency
 * stays near one period of the lowest note playing (head 64..1600 behind).
 * On a pick attack the head jumps straight to the minimum delay (1.5 ms)
 * and no splice is made for the next 25 ms, so attacks are neither delayed
 * nor chopped. The input is stored with +/-4.0 headroom. MIX blends the
 * original back in. */
#define FROST_DROP_MIN_DISTANCE 64.0f
#define FROST_DROP_MAX_DISTANCE 1600.0f
#define FROST_DROP_JUMP_MIN 32u
#define FROST_DROP_JUMP_MAX 1400u
#define FROST_DROP_HOLD 1100u
#ifndef FROST_DROP_FADE_MAX
#define FROST_DROP_FADE_MAX 1024u
#endif
#ifndef FROST_DROP_EVERY
#define FROST_DROP_EVERY 8u   /* coarse candidate every 8 samples */
#endif
#ifndef FROST_DROP_GOOD
#define FROST_DROP_GOOD 0.97f
#endif
typedef struct {
    FrostModulation *ring;   /* history: FrostPsHistory, layout-compatible */
    FrostPsInput in;
    FrostPsSplicer head;
    FrostPsConfig cfg;       /* splicer settings for the current DROP step */
} FrostDropTune;
void frost_drop_reset(FrostDropTune *s);
float frost_drop_sample(FrostDropTune *s,float x,const float *p);
#endif
