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
/* beta.11 tail codec for every delay line except SHIMMER's pitch line (its
 * splice search compares raw linear samples). Linear Q11/Q12 left a fading
 * tail only a few bits: grainy, then cut to zero near -66..-72 dBFS (tester
 * report: comb-filter / noise-gate sound as reverbs fade). Each sample is the
 * IEEE half-float of value*2^11: 11 significant bits from +-32 down to about
 * 3e-8 (-150 dBFS), then half subnormals. Round to nearest even, with the
 * line's error feedback as before. Code 0 is +0.0f, so zero-filled memory
 * still reads as silence. The Cortex-M4 DSP uses VCVTB (FPSCR.FZ does not
 * apply to half precision); hosts use the bit-exact C model below. */
#define FROST_RV16F_SCALE 2048.0f
#define FROST_RV16F_INVERSE (1.0f/2048.0f)
#if defined(__arm__) && defined(GP5_FIRMWARE)
static inline __attribute__((always_inline)) float frost_rv16f_half_to_float(uint32_t h)
{float r;__asm__("vmov %0, %1" : "=t"(r) : "r"(h));__asm__("vcvtb.f32.f16 %0, %0" : "+t"(r));return r;}
static inline __attribute__((always_inline)) uint32_t frost_rv16f_float_to_half(float x)
{float h;uint32_t r;__asm__("vcvtb.f16.f32 %0, %1" : "=t"(h) : "t"(x));__asm__("vmov %0, %1" : "=r"(r) : "t"(h));return r&0xffffu;}
#else
static inline uint32_t frost_rv16f_float_to_half(float x)
{
    union {float f;uint32_t u;} v;uint32_t s,a,m,sh,h,rem,half;
    v.f=x;s=(v.u>>16)&0x8000u;a=v.u&0x7fffffffu;
    if(a>0x7f800000u)return s|0x7e00u;                                /* NaN */
    if(a>=0x477ff000u)return s|0x7c00u;                               /* rounds past 65504 */
    if(a>=0x38800000u){a-=0x38000000u;return s|((a+0x0fffu+((a>>13)&1u))>>13);}
    if(a<0x33000000u)return s;                                        /* below half the least subnormal */
    m=(a&0x007fffffu)|0x00800000u;sh=126u-(a>>23);                     /* 14..24 */
    h=m>>sh;rem=m&((1u<<sh)-1u);half=1u<<(sh-1u);
    if(rem>half || (rem==half && (h&1u)))++h;
    return s|h;
}
static inline float frost_rv16f_half_to_float(uint32_t h)
{
    union {float f;uint32_t u;} v;uint32_t s=(h&0x8000u)<<16,e=(h>>10)&31u,m=h&0x3ffu;
    if(e==31u){v.u=s|0x7f800000u|(m<<13);return v.f;}
    if(e){v.u=s|((e+112u)<<23)|(m<<13);return v.f;}
    v.f=(float)m*5.9604644775390625e-8f;return s?-v.f:v.f;            /* subnormal: m*2^-24 */
}
#endif
static inline __attribute__((always_inline)) float frost_rv16f_get(int16_t code)
{return frost_rv16f_half_to_float((uint16_t)code)*FROST_RV16F_INVERSE;}
static inline __attribute__((always_inline)) int16_t frost_rv16f_put(float value,float *error)
{
    float q=value+*error;uint32_t h=frost_rv16f_float_to_half(q*FROST_RV16F_SCALE);
    *error=q-frost_rv16f_half_to_float(h)*FROST_RV16F_INVERSE;
    return (int16_t)h;
}
void frost_reverb16_reset(FrostReverb16 *s);
float frost_reverb16_sample(FrostReverb16 *s,float input,const float *p);
void frost_reverb16_block(FrostReverb16 *s,const float *in,float *out,unsigned n,const float (*p)[3]);
#endif
