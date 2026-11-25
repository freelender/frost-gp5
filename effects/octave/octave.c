#include "octave.h"
#include <stddef.h>
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
#define FROST_PS_PREFIX pog
#define FROST_PS_FN FROST_HOT FROST_NOINLINE FROST_OVL(pog) static
#define FROST_PS_SMALL FROST_COLD
#define FROST_PS_LINEAR 1
#ifndef FROST_OCT_COARSE_W
#define FROST_OCT_COARSE_W 32u   /* 256 samples: as good as 40 here (harness), cheaper */
#endif
#define FROST_PS_COARSE_W FROST_OCT_COARSE_W
#include "../common/pitch_shift_impl.h"
#undef FROST_PS_PREFIX
#undef FROST_PS_FN
#undef FROST_PS_LINEAR
typedef char frost_octave_history_fits[(sizeof(FrostPsHistory)==sizeof(FrostModulation))?1:-1];
typedef char frost_octave_state_fits[(sizeof(FrostOctave)<=120u)?1:-1];
#ifndef FROST_OCT_GOOD
#define FROST_OCT_GOOD 0.97f
#endif
#ifndef FROST_OCT_UP_FADE
#define FROST_OCT_UP_FADE 256u
#endif
#define FROST_OCT_UP_START (FROST_OCT_UP_MIN+(float)FROST_OCT_UP_FADE+536.0f)   /* first grain: 12 ms */
#ifndef FROST_OCT_UP_EVERY
#define FROST_OCT_UP_EVERY 2u
#endif
#ifndef FROST_OCT_SUB_EVERY
#define FROST_OCT_SUB_EVERY 4u
#endif
#ifndef FROST_OCT_SUB_FADE
#define FROST_OCT_SUB_FADE 1024u
#endif
void frost_octave_reset(FrostOctave *s)
{
    /* Zero history, filters and idle splicers; then each head's start delay. */
    if(s->ring)frost_ps_zero(s->ring,sizeof(FrostPsHistory));
    frost_ps_zero(&s->in,sizeof *s-offsetof(FrostOctave,in));
    s->up.d=s->up.d_old=FROST_OCT_UP_START;s->down.d=s->down.d_old=FROST_OCT_SUB_MIN;
}
#ifdef FROST_FCM_BENCH
/* EXPERIMENTAL FCM timing candidate (IIR engine): the POG slot runs the
 * Frost Capture Model from effects/fcm/fcm_iir with a baked model
 * (effects/fcm/model_bank.h, generated, private). frost_octave_reset() above
 * is unchanged; it zeroes ring->position/valid, which marks the FCM state for
 * lazy setup. The FCM state (about 2.2 KB) lives in the POG slot's own
 * modulation workspace (line storage). Per-sample entry with 64-sample
 * buffering: +64 samples latency in this benchmark build only.
 * Output = FCM(x) x (UP knob / 50); the other POG knobs are ignored. */
#include "../fcm/fcm_iir.h"
#if defined(__GNUC__) && !defined(__clang__)
#define FROST_FCM_BENCH_OPT __attribute__((optimize("no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_FCM_BENCH_OPT
#endif
#define FCMI_FN FROST_HOT FROST_OVL(pog) FROST_FCM_BENCH_OPT
#include "../fcm/model_bank.h"
#include "../fcm/fcm_iir.c"
typedef struct { FcmiState fcm; float in[FCMI_BLOCK],out[FCMI_BLOCK]; unsigned n; } FcmBenchWs;
typedef char fcm_bench_fits[sizeof(FcmBenchWs)<=sizeof(((FrostModulation *)0)->line)?1:-1];
FROST_HOT FROST_NOINLINE FROST_OVL(pog) FROST_FCM_BENCH_OPT float frost_octave_sample(FrostOctave *s,float x,const float *p)
{
    FrostModulation *r=s->ring;FcmBenchWs *w=(FcmBenchWs *)(void *)&r->line;float y;unsigned k;
    if(!r->valid){fcmi_reset(&w->fcm,&fcmi_model);for(k=0;k<FCMI_BLOCK;++k)w->out[k]=0.0f;w->n=0;r->valid=1u;}
    w->in[w->n]=x;y=w->out[w->n];
    if(++w->n==FCMI_BLOCK){fcmi_process(&w->fcm,w->in,w->out,FCMI_BLOCK);w->n=0;}
    return frost_rail(p[2]*0.02f*y,8.0f);
}
#else
/* sub: 0.5x, the head falls behind and jumps forward a whole number of
 * periods; up: 2x, the head approaches the input and jumps back. Built per
 * sample with field stores: the constants stay in this module's code (DSP
 * code flash has no room for const tables). */
static inline __attribute__((always_inline)) void oct_config(FrostPsConfig *c,float ratio,float dmin,float dmax,unsigned jmax,unsigned every,unsigned phase,unsigned fade_min,unsigned fade_max)
{c->ratio=ratio;c->dmin=dmin;c->dmax=dmax;c->jmin=64u;c->jmax=jmax;c->good=FROST_OCT_GOOD;c->every=every;c->phase=phase;c->refresh=0u;c->fade_min=fade_min;c->fade_max=fade_max;}
FROST_HOT FROST_NOINLINE FROST_OVL(pog) float frost_octave_sample(FrostOctave *s,float x,const float *p)
{
    FrostPsHistory *h=(FrostPsHistory *)(void *)s->ring;float up=0.0f,sub=0.0f;FrostPsConfig c;
    unsigned onset=pog_ps_input(&s->in,h,x,1u,FROST_OCT_HOLD);
    if(onset){if(s->down.d>FROST_OCT_SUB_MIN+32.0f)frost_ps_jump(&s->down,FROST_OCT_SUB_MIN,48u);s->up.best=0;}   /* new note: the up voice rescans from the minimum jump */
    /* A voice at level 0 is off (no work) and restarts at its minimum delay. */
    if(p[1]!=0.0f || s->down.fade){oct_config(&c,0.5f,FROST_OCT_SUB_MIN,1600.0f,1400u,FROST_OCT_SUB_EVERY,1u,128u,FROST_OCT_SUB_FADE);sub=pog_ps_splice(&s->down,h,&c,!h->hold);}
    else frost_ps_splicer_reset(&s->down,FROST_OCT_SUB_MIN);
    if(p[2]!=0.0f || s->up.fade){oct_config(&c,2.0f,FROST_OCT_UP_MIN,1650.0f,1336u,FROST_OCT_UP_EVERY,0u,64u,FROST_OCT_UP_FADE);up=pog_ps_splice(&s->up,h,&c,1u);}
    else frost_ps_splicer_reset(&s->up,FROST_OCT_UP_START);
    sub=frost_lp(&s->sub_lp,sub,frost_onepole(2500.0f));
    up=frost_hp(&s->up_hp,up,frost_onepole(150.0f));
    return frost_rail(p[0]*0.01f*x+p[1]*0.01f*sub+p[2]*0.01f*up,8.0f);
}
#endif /* FROST_FCM_BENCH */
#include "../../engine/overlay_far_off.h"
