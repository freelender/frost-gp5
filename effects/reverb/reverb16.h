#ifndef FROST_REVERB16_H
#define FROST_REVERB16_H
#include <stdint.h>
#include "reverb.h"
/* Opt-in backend. The original float layout remains available to the frozen
 * GP-5 boot-region helpers. No pointers to this type may reach those helpers.
 * Plain reverb: signed Q11 (range -16..16). RV-6/spring delay lines: Q12
 * (range -8..8). Spring's short dispersive allpass state remains float:
 * repeated quantization there otherwise degrades its quiet tail.
 * Each line uses first-order error feedback around nearest rounding; a small
 * float residual avoids the systematic tail loss of truncating every store.
 * Filter arithmetic, sample rate, lengths and parameters remain float/full-rate. */
typedef struct {
    int16_t samples[FROST_REVERB_SAMPLES];
    unsigned position[FROST_REVERB_LINES],valid[FROST_REVERB_LINES];
    float damped[FROST_REVERB_COMBS];
    FrostReverbModelState model;
    float error[12];
    float spring_allpass[120];
} FrostReverb16;
float frost_reverb16_load(int16_t value,float inverse_scale);
int16_t frost_reverb16_store(float value,float scale);
int16_t frost_reverb16_quantize(float value,float scale,float *error);
/* 31k: common finite path, inlined into the LARGE loops. Keep the original
 * out-of-line quantizer above as the exact slow path (including NaN, clipping
 * and the asymmetric negative endpoint). IEEE bit tests replace repeated
 * FPU compare/status transfers; adding signed 0.5 still rounds ties away.
 * External GNU inline linkage avoids emitting a new boot-region static. */
#if defined(__GNUC__)
extern inline __attribute__((gnu_inline,always_inline))
int16_t frost_reverb16_quantize_fast(float value,float scale,float *error)
{
    union {float f;uint32_t u;} q,half;
    int n;
    q.f=value*scale;
    if((q.u&0x7fffffffu)<0x3f800000u){*error=0.0f;return 0;}
    q.f+=*error;
    if((q.u&0x7fffffffu)>0x46fffe00u)
        return frost_reverb16_quantize(value,scale,error);
    half.u=(q.u&0x80000000u)|0x3f000000u;
    /* The guard bounds n to int16 range already. Keep it as int until the
     * store so ARM need not sign-extend a halfword before converting to float. */
    n=(int)(q.f+half.f);
    *error=q.f-(float)n;
    return (int16_t)n;
}
#else
#define frost_reverb16_quantize_fast frost_reverb16_quantize
#endif
void frost_reverb16_reset(FrostReverb16 *s);
float frost_reverb16_sample(FrostReverb16 *s,float input,const float *p);
void frost_reverb16_block(FrostReverb16 *s,const float *in,float *out,unsigned n,const float (*p)[3]);
#endif
