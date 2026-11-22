#ifndef FROST_REVERBS_H
#define FROST_REVERBS_H
#include "../reverb/reverb.h"
/* RV-6-inspired reverb family (ROOM/HALL/PLATE/MOD/SHIMMER) and a Fender
 * 6G15-inspired spring reverb. Both use the single exclusive reverb-class
 * workspace (sample memory + FrostReverbModelState); the slot holds only the
 * pointer. Changing RV-6 MODE re-partitions memory and restarts the tail. */
enum { FROST_RV6_ROOM, FROST_RV6_HALL, FROST_RV6_PLATE, FROST_RV6_MOD, FROST_RV6_SHIMMER, FROST_RV6_MODES };
#define FROST_SPRING_STAGES 12u
#define FROST_SPRING_STRETCH 5u
typedef struct { FrostReverb *ws; } FrostReverbModel;
void frost_rv6_reset(FrostReverbModel *s);
float frost_rv6_sample(FrostReverbModel *s,float x,const float *p);
/* Exact block form with parameters constant over the block (bitwise equal to
 * calling frost_rv6_sample per sample); in and out must not alias. */
void frost_rv6_block(FrostReverbModel *s,const float *x,float *out,unsigned count,const float *p);
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
float frost_rv6_sample_reference(FrostReverbModel *s,float x,const float *p);
#endif
void frost_spring_reset(FrostReverbModel *s);
float frost_spring_sample(FrostReverbModel *s,float x,const float *p);
/* Total workspace samples used by a mode (host contract test). */
unsigned frost_rv6_mode_samples(unsigned mode);
unsigned frost_spring_samples(void);
#endif
