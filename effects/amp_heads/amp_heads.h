#ifndef FROST_AMP_HEADS_H
#define FROST_AMP_HEADS_H
#include <stdint.h>
#include "../common/block.h"
/* Seven original algorithmic voices, no captures, speaker filter or workspace.
 * The voice coefficients are copied on reset; all signal histories are local. */
typedef struct {
    float hp[3],low,mid,presence,bass,previous,aa[2],dc,envelope;
    float input_hp,inter_hp,drive,stage_drive,power_drive,bias,sag,mid_voice;
    float low_hz,high_hz,brightness,trim;
    float zero;
    uint32_t stages,voice;
} FrostAmpHead;
void frost_amphead_reset(FrostAmpHead *s,unsigned voice);
float frost_amphead_sample(FrostAmpHead *s,float x,const float *p);
void frost_amphead_block(void *state,const float *x,float *y,unsigned n,const float *p,const float *p_start);
extern FrostBlockFn frost_amphead_block_kernel;
#endif
