#ifndef FROST_EXTRAS_H
#define FROST_EXTRAS_H
/* 32l lightweight effects. Each kernel is its own overlay module and keeps its
 * whole state per slot (no workspace); every state fits FrostEffectState. */
#define FROST_ROTARY_LINE 16u
typedef struct {
    float horn_phase,drum_phase,horn_rate,drum_rate,split,tone;
    float line[FROST_ROTARY_LINE];unsigned pos;
} FrostRotary;
typedef struct { float phase; } FrostRingMod;
typedef struct { float held,acc; } FrostCrusher;
typedef struct { float phase,lo1,lo2,gl,gh; } FrostHarmTrem;
typedef struct { float in_hp,mid_lp,out_hp,tone_lp,tone_hp,aa[4]; } FrostMuff;
typedef struct { float bright; } FrostBoost;
typedef struct { float env,gain; } FrostSwell;
void frost_rotary_reset(FrostRotary *s);
float frost_rotary_sample(FrostRotary *s,float x,const float *p);
void frost_ringmod_reset(FrostRingMod *s);
float frost_ringmod_sample(FrostRingMod *s,float x,const float *p);
void frost_crusher_reset(FrostCrusher *s);
float frost_crusher_sample(FrostCrusher *s,float x,const float *p);
void frost_harmtrem_reset(FrostHarmTrem *s);
float frost_harmtrem_sample(FrostHarmTrem *s,float x,const float *p);
void frost_muff_reset(FrostMuff *s);
float frost_muff_sample(FrostMuff *s,float x,const float *p);
void frost_boost_reset(FrostBoost *s);
float frost_boost_sample(FrostBoost *s,float x,const float *p);
void frost_swell_reset(FrostSwell *s);
float frost_swell_sample(FrostSwell *s,float x,const float *p);
#endif
