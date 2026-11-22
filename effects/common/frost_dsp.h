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
