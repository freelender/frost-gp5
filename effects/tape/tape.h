#ifndef FROST_TAPE_H
#define FROST_TAPE_H
#include "../delay/delay.h"
typedef struct { FrostDelay *ring; float distance,phase,flutter,lp[2],hp; } FrostTape;
void frost_tape_reset(FrostTape *s);
float frost_tape_sample(FrostTape *s,float x,const float *p);
#endif
