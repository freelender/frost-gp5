#ifndef FROST_DELAY_H
#define FROST_DELAY_H
#include <stdint.h>
#include <string.h>
/* One caller-owned mono ring, fixed 44.1 kHz. No bulk clear in reset.
 * Valid samples are tracked so stale/uninitialized storage is never read. */
#define FROST_DELAY_SAMPLES 22052u
/* 31b: the ring holds 16-bit words. The generic delay and DM-2 store IEEE
 * binary16 (full rate, 11-bit significand, relative error <= 2^-12, about
 * -72 dB, wet path only); DD-3 keeps its fixed-point int16 at 800 ms
 * (35,280 samples, plus the interpolation neighbour). 88,220 -> 70,812 B.
 * One delay-class instance owns the object at a time. */
#define FROST_DELAY_PCM 35284u
/* Storage words (>= PCM); the spare tail keeps the target's field offsets in
 * the encoding class of the original layout (boot-adjacent code size). */
#define FROST_DELAY_WORDS 35400u
typedef struct {
    union { uint16_t half[FROST_DELAY_SAMPLES]; int16_t pcm[FROST_DELAY_WORDS]; } line;
    unsigned position,valid;
    float distance;
} FrostDelay;
/* Float -> binary16, integer-only so host and pedal agree bit for bit.
 * Normal range: round to nearest even. Below 2^-14 (subnormal binary16) the
 * magnitude is truncated toward zero, so a feedback loop with gain < 1 always
 * decays to exact zero instead of holding a rounding limit cycle. Inputs are
 * finite and railed (|x| <= 16); no overflow/NaN handling is needed. */
uint16_t frost_half_store(float x);
float frost_half_load(uint16_t h);
/* 31h/31i: one inline body. The delay (LARGE) and the DM-2 overlay compile it
 * in; the out-of-line copies (frost_half_store/load) serve the tests. Same
 * integer/VCVTB code, same results. */
__attribute__((always_inline)) static inline uint16_t frost_half_store_inl(float x)
{
    uint32_t u,s,a;memcpy(&u,&x,4);s=(u>>16)&0x8000u;a=u&0x7fffffffu;
    if(a<0x38800000u){float m=x<0.0f?-x:x;return (uint16_t)(s|(uint32_t)(m*16777216.0f));}
#if defined(__arm__) && defined(__ARM_FP)
    /* Normal range: VCVTB is IEEE round-to-nearest-even, identical to the
     * integer form below (FZ only affects subnormals, handled above). */
    {float t;__asm__("vcvtb.f16.f32 %1,%2\n\tvmov %0,%1":"=r"(u),"=&t"(t):"t"(x));(void)s;return (uint16_t)u;}
#else
    return (uint16_t)(s|((a-0x38000000u+0xfffu+((a>>13)&1u))>>13));
#endif
}
__attribute__((always_inline)) static inline float frost_half_load_inl(uint16_t h)
{
    uint32_t s=(uint32_t)(h&0x8000u)<<16,e=h&0x7fffu,u;float r;
    if(e<0x400u){r=(float)e*(1.0f/16777216.0f);return s?-r:r;}
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vmov %0,%1\n\tvcvtb.f32.f16 %0,%0":"=&t"(r):"r"((uint32_t)h));(void)u;return r;
#else
    u=s|((e<<13)+0x38000000u);memcpy(&r,&u,4);return r;
#endif
}
void frost_delay_reset(FrostDelay *s);
/* Valid smoothed parameters: time[1,500] ms, feedback[0,.95], mix[0,1]. */
float frost_delay_sample(FrostDelay *s,float input,const float *parameters);
#endif
