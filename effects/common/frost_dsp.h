#ifndef FROST_DSP_COMMON_H
#define FROST_DSP_COMMON_H
/* Shared, allocation-free per-sample building blocks for the pedal-inspired
 * model library. Fixed 44.1 kHz. No libm calls: cheap polynomial/rational
 * approximations keep coefficient updates affordable inside sample loops. */
#include <stdint.h>
#define FROST_FS 44100.0f
#define FROST_PI 3.14159265f
static inline float frost_clampf(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
/* 2^x for x in [-60,60]; relative error about 1e-4 (0.001 dB). */
float frost_exp2(float x);
/* log2(x) for x>0; absolute error about 1e-4. Non-positive input -> -60. */
float frost_log2(float x);
static inline float frost_gain_to_db(float g){return 6.02059991f*frost_log2(g);}
static inline float frost_db_to_gain(float db){return frost_exp2(db*0.16609640f);}
/* sin(2*pi*phase), phase in [0,1); max error about 1e-4. */
float frost_sin01(float phase);
static inline float frost_triangle01(float phase)
{return phase<0.5f?4.0f*phase-1.0f:3.0f-4.0f*phase;}
static inline float frost_phase_advance(float phase,float hz)
{phase+=hz*(1.0f/FROST_FS);return phase>=1.0f?phase-1.0f:phase;}
/* One-pole coefficient for cutoff hz (matched-ish, a=w/(1+w)). */
static inline float frost_onepole(float hz)
{float w=2.0f*FROST_PI*hz*(1.0f/FROST_FS);return w/(1.0f+w);}
static inline float frost_lp(float *z,float x,float a){*z+=a*(x-*z);return *z;}
static inline float frost_hp(float *z,float x,float a){*z+=a*(x-*z);return x-*z;}
/* First-order allpass with coefficient c: y = c*x + s; s = x - c*y. */
static inline float frost_allpass1(float *s,float x,float c){float y=c*x+*s;*s=x-c*y;return y;}
/* Allpass coefficient for break frequency hz (tan approximated near DC). */
float frost_allpass_coef(float hz);
/* Clippers, all odd and bounded by 1 unless noted.
 * soft: rational tanh (silicon diodes in a feedback loop),
 * hard: C1 cubic knee (diodes to ground), germanium: x/(1+|x|) very soft. */
float frost_soft_clip(float x);
static inline float frost_hard_clip(float x)
{if(x>=1.0f)return 1.0f;if(x<=-1.0f)return -1.0f;return x*(1.5f-0.5f*x*x);}
static inline float frost_germanium_clip(float x)
{return x/(1.0f+(x<0.0f?-x:x));}
static inline float frost_absf(float x){return x<0.0f?-x:x;}
/* Overlay modules run from a relocated copy: these always-inline helpers
 * reach shared code through the absolute far pointers there. */
/* The rare large-jump tails of the ADAA helpers are out of line (flash), so
 * the inline part stays small inside every overlay module; overlays reach
 * them through constant pointers kept in flash (volatile: always loaded, so
 * the call stays absolute). */
float frost_soft_clip_aa_slow(float x,float p);
float frost_germanium_clip_aa_slow(float x,float p);
#ifdef FROST_OVERLAYS
extern float (*volatile frost_far_log2)(float);
extern float (*volatile frost_far_soft_clip)(float);
extern float (*const volatile frost_far_soft_aa_slow)(float,float);
extern float (*const volatile frost_far_germanium_aa_slow)(float,float);
#define FROST_AA_LOG2(x) (frost_far_log2(x))
#define FROST_AA_SOFT_CLIP(x) (frost_far_soft_clip(x))
#define FROST_AA_SOFT_SLOW(x,p) (frost_far_soft_aa_slow(x,p))
#define FROST_AA_GERMANIUM_SLOW(x,p) (frost_far_germanium_aa_slow(x,p))
#else
#define FROST_AA_LOG2(x) (frost_log2(x))
#define FROST_AA_SOFT_CLIP(x) (frost_soft_clip(x))
#define FROST_AA_SOFT_SLOW(x,p) (frost_soft_clip_aa_slow(x,p))
#define FROST_AA_GERMANIUM_SLOW(x,p) (frost_germanium_clip_aa_slow(x,p))
#endif
/* 32d first-order antiderivative anti-aliasing (ADAA1) for the clip shapes:
 * y = (F(x)-F(x_prev))/(x-x_prev), the clip of the segment's average. It
 * cuts aliasing of strongly driven stages at the cost of half a sample of
 * delay and a gentle top-octave roll-off (two-tap average). z[0] = previous
 * input (z[1]: hard clip's cached F; zeroed state is consistent, F(0) = 0).
 * Same flat region: exactly the rail; close inputs: a log-free closed form;
 * otherwise F differences (rare large jumps). */
static inline __attribute__((always_inline)) float frost_hard_clip_af(float x)
{float a=x<0.0f?-x:x,x2=x*x;return a>=1.0f?a-0.375f:x2*(0.75f-0.125f*x2);}
static inline __attribute__((always_inline)) float frost_hard_clip_aa(float *z,float x)
{
    float p=z[0],fp=z[1],f=frost_hard_clip_af(x),d=x-p;z[0]=x;z[1]=f;
    if(x>=1.0f && p>=1.0f)return 1.0f;
    if(x<=-1.0f && p<=-1.0f)return -1.0f;
    if(d>1e-3f || d< -1e-3f)return (f-fp)/d;
    return frost_hard_clip(0.5f*(x+p));
}
/* frost_soft_clip: x(27+x^2)/(27+9x^2) = x/9 + 24x/(9x^2+27) for |x|<=3, so
 * F = x^2/18 + (4/3) ln((9x^2+27)/27); beyond 3, F(3) + |x| - 3. */
static inline __attribute__((always_inline)) float frost_soft_clip_af(float x)
{
    float a=x<0.0f?-x:x;
    if(a>=3.0f)return a-0.65160752f;                       /* F(3)-3 = 0.5+(4/3)ln4-3 */
    return x*x*(1.0f/18.0f)+0.92419624f*FROST_AA_LOG2(1.0f+x*x*(1.0f/3.0f));  /* (4/3)ln2 */
}
/* ln(r) = 2 atanh(t), t = (r-1)/(r+1): 2t*S(t^2), S = 1+t^2/3+t^4/5+t^6/7
 * (|t| <= 1/3: relative error < 2e-5). Lets the ADAA quotients below avoid
 * log calls and cancellation when consecutive inputs are close. */
float frost_soft_clip_aa_call(float *z,float x);
float frost_hard_clip_aa_call(float *z,float x);
static inline __attribute__((always_inline)) float frost_aa_atanh_s(float t2)
{return 1.0f+t2*(0.33333333f+t2*(0.2f+t2*0.14285714f));}
static inline __attribute__((always_inline)) float frost_soft_clip_aa(float *z,float x)
{
    float p=z[0],d=x-p;z[0]=x;
    if(x>=3.0f && p>=3.0f)return 1.0f;
    if(x<=-3.0f && p<=-3.0f)return -1.0f;
    if(x<=3.0f && x>=-3.0f && p<=3.0f && p>=-3.0f){
        /* (F(x)-F(p))/(x-p) = (x+p)/18 + (8/3) S(t^2) (x+p)/(6+x^2+p^2),
         * t = (x-p)(x+p)/(6+x^2+p^2) */
        float s=x+p,q=1.0f/(6.0f+x*x+p*p),t=d*s*q;
        if(t<=0.33333333f && t>=-0.33333333f)return s*(1.0f/18.0f)+2.66666667f*frost_aa_atanh_s(t*t)*s*q;
    }
    (void)d;return FROST_AA_SOFT_SLOW(x,p);
}
/* frost_germanium_clip x/(1+|x|): F = |x| - ln(1+|x|). It never reaches a
 * flat rail, so the step threshold is relative (F grows like |x|). */
static inline __attribute__((always_inline)) float frost_germanium_clip_af(float x)
{float a=x<0.0f?-x:x;return a-0.69314718f*FROST_AA_LOG2(1.0f+a);}
static inline __attribute__((always_inline)) float frost_germanium_clip_aa(float *z,float x)
{
    /* Same sign: (G(x)-G(p))/(x-p) = sign * (1 - 2 S(t^2)/(2+|x|+|p|)),
     * t = (|x|-|p|)/(2+|x|+|p|). */
    float p=z[0],d=x-p,a=x<0.0f?-x:x;z[0]=x;
    if((x>=0.0f)==(p>=0.0f)){
        float b=p<0.0f?-p:p,q=1.0f/(2.0f+a+b),t=(a-b)*q;
        if(t<=0.33333333f && t>=-0.33333333f){float y=1.0f-2.0f*frost_aa_atanh_s(t*t)*q;return x>=0.0f?y:-y;}
    }
    (void)d;(void)a;return FROST_AA_GERMANIUM_SLOW(x,p);
}
/* Knob 0..100 audio taper (log-style): 0 -> lo, 100 -> hi, geometric. */
static inline float frost_taper_log(float knob,float lo,float hi_over_lo_log2)
{return lo*frost_exp2(knob*0.01f*hi_over_lo_log2);}
/* Output LEVEL convention for drive models: 50 = unity, 100 = +12 dB, 0 = mute. */
static inline float frost_level(float knob){float k=knob*0.02f;return k*k;}
static inline float frost_rail(float x,float limit){return x>limit?limit:x< -limit?-limit:x;}
/* 16-bit storage helpers for long workspaces, +/-4.0 full scale. */
static inline int16_t frost_pcm16(float x)
{x=frost_rail(x,3.9998f)*8192.0f;return (int16_t)(x>=0.0f?x+0.5f:x-0.5f);}
static inline float frost_unpcm16(int16_t v){return (float)v*(1.0f/8192.0f);}
/* Out-of-line shared helpers live in frost_dsp.c (one copy in flash). */
#endif
