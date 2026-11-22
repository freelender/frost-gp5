#ifndef FROST_DELAYS_H
#define FROST_DELAYS_H
#include "../delay/delay.h"
/* DD-3- and DM-2-inspired delays sharing the single exclusive delay-class
 * workspace (FrostDelay). DD-3 style stores int16 samples (800 ms maximum);
 * DM-2 style stores binary16 (20..300 ms), like the generic delay.
 * Per-instance filter/hold state lives in the slot. */
typedef struct { FrostDelay *ring; float distance,lp[2],hold_length,hold_position;unsigned holding; } FrostDD3;
typedef struct { FrostDelay *ring; float distance,lp[2],hp,phase; } FrostDM2;
void frost_dd3_reset(FrostDD3 *s);
float frost_dd3_sample(FrostDD3 *s,float x,const float *p);
void frost_dm2_reset(FrostDM2 *s);
float frost_dm2_sample(FrostDM2 *s,float x,const float *p);
#endif
