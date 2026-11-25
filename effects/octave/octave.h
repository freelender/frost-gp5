#ifndef FROST_OCTAVE_H
#define FROST_OCTAVE_H
#include "../modulation/modulation.h"
#include "../common/pitch_shift.h"
/* EXPERIMENTAL Micro POG-inspired polyphonic octave (dry / -1 oct / +1 oct).
 * Time-domain splicing pitch shifters (effects/common/pitch_shift.h) on one
 * history held in the slot's exclusive FrostModulation workspace: the input
 * is anti-aliased (~10 kHz) for the octave up, which reads at exactly 2x, and
 * the octave down reads at 0.5x. Each voice is one read head that jumps by a
 * correlation-chosen whole number of periods (coarse on a /8 low-passed copy,
 * then per sample with a sub-sample peak) and crossfades with gains
 * normalized by the measured match, so chords stay steady instead of
 * warbling. The sub jumps to its minimum delay on a pick attack. SUB OCT is
 * low-passed at 2.5 kHz, OCT UP high-passed at 150 Hz (as before).
 * Not POG-grade: a time-domain shifter still smears very dense chords. */
#define FROST_OCT_SUB_MIN 48.0f
#define FROST_OCT_UP_MIN 32.0f
#define FROST_OCT_HOLD 1100u
typedef struct { FrostModulation *ring; FrostPsInput in; FrostPsSplicer up,down; float sub_lp,up_hp; } FrostOctave;
void frost_octave_reset(FrostOctave *s);
float frost_octave_sample(FrostOctave *s,float x,const float *p);
#endif
