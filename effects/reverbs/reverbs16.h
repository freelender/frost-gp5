#ifndef FROST_REVERBS16_H
#define FROST_REVERBS16_H
#include "reverbs.h"
#include "../reverb/reverb16.h"
typedef struct { FrostReverb16 *ws; } FrostReverbModel16;
void frost_16_rv6_reset(FrostReverbModel16 *s);
float frost_16_rv6_sample(FrostReverbModel16 *s,float x,const float *p);
void frost_16_rv6_block(FrostReverbModel16 *s,const float *x,float *out,unsigned count,const float *p);
void frost_16_spring_reset(FrostReverbModel16 *s);
float frost_16_spring_sample(FrostReverbModel16 *s,float x,const float *p);
void frost_16_spring_block(FrostReverbModel16 *s,const float *x,float *out,unsigned count,const float *p);
float frost_16_spring_sample_reference(FrostReverbModel16 *s,float x,const float *p);
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
float frost_16_rv6_sample_reference(FrostReverbModel16 *s,float x,const float *p);
#endif
#endif
