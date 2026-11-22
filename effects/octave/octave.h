#ifndef FROST_OCTAVE_H
#define FROST_OCTAVE_H
#include "../modulation/modulation.h"
/* EXPERIMENTAL Micro POG-inspired polyphonic octave (dry / -1 oct / +1 oct).
 * Time-domain overlap-add pitch shifting on a 16-bit history held in one
 * exclusive FrostModulation ring: per voice, two Hann-weighted read taps
 * moving at exactly 2.0x (up) or 0.5x (down). Each tap restarts at a
 * correlation-aligned position (bounded WSOLA-style search: 64 candidates
 * x 16 points, once per half-grain) so overlapping grains add coherently.
 * It shifts chords (it is not a single-note divider), but grain-rate
 * modulation, transient smearing and ~12..35 ms latency remain audible:
 * not POG-grade quality. See docs/User%20Guide.md for measurements. */
#define FROST_OCTAVE_UP_GRAIN 1024u
#define FROST_OCTAVE_DOWN_GRAIN 2048u
#define FROST_OCTAVE_SEARCH 64u
typedef struct { float d[2],ph[2]; } FrostOctaveVoice;
typedef struct { FrostModulation *ring; FrostOctaveVoice up,down; float sub_lp,up_hp; } FrostOctave;
void frost_octave_reset(FrostOctave *s);
float frost_octave_sample(FrostOctave *s,float x,const float *p);
#endif
