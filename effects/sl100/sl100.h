#ifndef FROST_SL100_H
#define FROST_SL100_H
#include <stdint.h>
#include "../common/block.h"
/* SL-100: a three-channel 100 W lead head in the style of the Soldano SLO-100
 * (beta.14). Original behavioral model like the other Frost amp heads, not a
 * circuit solver or capture. Parameters (human units): MODE 0..4 (CLEAN,
 * CLEAN BRT, CRUNCH, CRUNCH BRT, LEAD), GAIN, BASS, MID, TREBLE, PRESENCE,
 * MASTER, LEVEL. State fits FrostEffectState; no workspace. */
typedef struct {
    float hp_in,tight,hp[3],miller[3],low,mid,pres,bass,aa[2],dc,env,prev,bright,zero;
    uint32_t channel;   /* 0 clean, 1 crunch, 2 lead; 3 = not set yet */
} FrostSl100;
void frost_sl100_reset(FrostSl100 *s);
float frost_sl100_sample(FrostSl100 *s,float x,const float *p);
void frost_sl100_block(void *state,const float *x,float *y,unsigned n,const float *p,const float *p_start);
extern FrostBlockFn frost_sl100_block_kernel;   /* settled block path (engine/effect.c) */
#endif
