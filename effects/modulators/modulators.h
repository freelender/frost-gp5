#ifndef FROST_MODULATORS_H
#define FROST_MODULATORS_H
#include "../modulation/modulation.h"
/* Pedal-inspired modulation models. Phase 90, Uni-Vibe and TR-2 styles keep
 * all state in the slot. CE-2, Electric Mistress, M117 and VB-2 styles use a
 * caller-owned FrostModulation ring (exclusive per instance) for their
 * bucket-brigade-style delay line; their per-instance filter/LFO state lives
 * in FrostModState. Mono only: no stereo outputs are claimed. */
typedef struct { float ap[4],fb,phase,c;unsigned n; } FrostPhase90;
typedef struct { float ap[4],lamp,phase,lp,c[4];unsigned n; } FrostUniVibe;
typedef struct { float phase,gain; } FrostTR2;
typedef struct { FrostModulation *ring; float phase,distance,lp[2],rise,fb; } FrostModState;
void frost_phase90_reset(FrostPhase90 *s);
float frost_phase90_sample(FrostPhase90 *s,float x,const float *p);
void frost_univibe_reset(FrostUniVibe *s);
float frost_univibe_sample(FrostUniVibe *s,float x,const float *p);
void frost_tr2_reset(FrostTR2 *s);
float frost_tr2_sample(FrostTR2 *s,float x,const float *p);
void frost_modstate_reset(FrostModState *s);
float frost_ce2_sample(FrostModState *s,float x,const float *p);
float frost_mistress_sample(FrostModState *s,float x,const float *p);
float frost_m117_sample(FrostModState *s,float x,const float *p);
/* p[FROST_PARAMETERS] carries the engine ENGAGE input (block enable target). */
float frost_vb2_sample(FrostModState *s,float x,const float *p);
/* Current delay in samples, for tests. */
static inline float frost_modstate_distance(const FrostModState *s){return s->distance;}
#endif
