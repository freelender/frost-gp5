#include "modulators.h"
#include "../common/frost_dsp.h"
#include "../../engine/metadata.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* ---- state-only phase/amplitude modulators ---------------------------- */
void frost_phase90_reset(FrostPhase90 *s){unsigned i;for(i=0;i<4;++i)s->ap[i]=0.0f;s->fb=s->phase=s->c=0.0f;s->n=0;}
/* Phase 90-style: four JFET-controlled first-order allpass stages swept
 * together by one LFO (exponential JFET resistance law, ~180 Hz..2.5 kHz
 * break frequency), summed 50/50 with the dry signal for two moving notches.
 * BLOCK voice adds the later units' feedback from stage 4 to the input for a
 * more resonant sweep; SCRIPT voice has none. SPEED in Hz. */
FROST_HOT FROST_NOINLINE FROST_OVL(phase90) float frost_phase90_sample(FrostPhase90 *s,float x,const float *p)
{
    float y,c;unsigned i;
    /* Break frequency updated at an 8-sample control rate (<= 10 Hz LFO). */
    if(!(s->n++&7u)){float u=0.5f+0.5f*frost_triangle01(s->phase);s->c=frost_allpass_coef(180.0f*frost_exp2(3.807f*u));}
    c=s->c;
    y=x+(p[1]>0.5f?0.45f:0.0f)*s->fb;
    for(i=0;i<4;++i)y=frost_allpass1(&s->ap[i],y,c);
    s->fb=frost_rail(y,4.0f);
    s->phase=frost_phase_advance(s->phase,p[0]);
    return 0.5f*(x+y);
}
void frost_univibe_reset(FrostUniVibe *s){unsigned i;for(i=0;i<4;++i)s->ap[i]=s->c[i]=0.0f;s->lamp=s->phase=s->lp=0.0f;s->n=0;}
/* Uni-Vibe-style: an LFO drives a lamp whose thermal lag (fast heat, slower
 * cooling) shapes the sweep; one lamp lights four LDRs feeding four phase
 * stages with STAGGERED capacitors (15n, 220n, 470p, 4.7n), so each stage
 * sweeps a different band -- the source of the uneven "throb" that a uniform
 * chorus/phaser kernel does not have. CHORUS mode mixes dry and shifted
 * signals; VIBRATO outputs only the shifted signal. VOLUME 50 = unity. */
static const float vibe_caps[4]={15e-9f,220e-9f,470e-12f,4.7e-9f};
FROST_HOT FROST_NOINLINE FROST_OVL(univibe) float frost_univibe_sample(FrostUniVibe *s,float x,const float *p)
{
    float drive=0.5f+0.5f*frost_sin01(s->phase),target=drive*drive*(p[1]*0.01f),y;unsigned i;
    s->lamp+=(target-s->lamp)*(target>s->lamp?0.0045f:0.0009f);
    /* LDR/stage coefficients follow the lamp at an 8-sample control rate. */
    if(!(s->n++&7u)){
        float r=300e3f*frost_exp2(-4.32f*s->lamp); /* 300k dark .. 15k bright */
        for(i=0;i<4;++i)s->c[i]=frost_allpass_coef(frost_clampf(1.0f/(2.0f*FROST_PI*r*vibe_caps[i]),1.0f,16000.0f));
    }
    y=1.25f*frost_soft_clip(0.8f*x);
    for(i=0;i<4;++i)y=frost_allpass1(&s->ap[i],y,s->c[i]);
    y=frost_lp(&s->lp,y,frost_onepole(9000.0f));
    s->phase=frost_phase_advance(s->phase,p[0]);
    y=p[2]>0.5f?y:0.5f*(x+y);
    return frost_rail(y*p[3]*0.02f,8.0f);
}
void frost_tr2_reset(FrostTR2 *s){s->phase=0.0f;s->gain=1.0f;}
/* TR-2-style VCA tremolo: WAVE morphs a triangle LFO towards a square by
 * steepening and limiting it; a ~0.7 ms gain smoother avoids clicks. DEPTH
 * 100 reaches full chop. */
FROST_HOT FROST_NOINLINE FROST_OVL(tr2) float frost_tr2_sample(FrostTR2 *s,float x,const float *p)
{
    float w=p[1]*0.01f,v=frost_clampf(frost_triangle01(s->phase)*(1.0f+30.0f*w*w),-1.0f,1.0f);
    float target=1.0f-p[2]*0.01f*(0.5f-0.5f*v);
    s->gain+=(target-s->gain)*0.032f;
    s->phase=frost_phase_advance(s->phase,p[0]);
    return x*s->gain;
}
/* ---- ring-based bucket-brigade style models --------------------------- */
void frost_modstate_reset(FrostModState *s)
{
    s->phase=s->distance=s->rise=s->fb=0.0f;s->lp[0]=s->lp[1]=0.0f;
    if(s->ring){s->ring->position=s->ring->valid=0;s->ring->phase=s->ring->distance=0.0f;}
}
static float ring_tap(const FrostModulation *m,unsigned d)
{
    unsigned i;if(d>m->valid || d>=FROST_MOD_SAMPLES)return 0.0f;
    i=m->position>=d?m->position-d:m->position+FROST_MOD_SAMPLES-d;return m->line.samples[i];
}
FROST_LARGE_ONLY static float ring_read(FrostModState *s,float desired)
{
    unsigned d;float f,a;
    desired=frost_clampf(desired,1.0f,(float)(FROST_MOD_SAMPLES-3u));
    if(s->distance==0.0f)s->distance=desired;
    else if(desired>s->distance+0.5f)s->distance+=0.5f;
    else if(desired<s->distance-0.5f)s->distance-=0.5f;
    else s->distance=desired;
    d=(unsigned)s->distance;f=s->distance-(float)d;a=ring_tap(s->ring,d);
    return a+f*(ring_tap(s->ring,d+1u)-a);
}
FROST_LARGE_ONLY static void ring_write(FrostModState *s,float v)
{
    FrostModulation *m=s->ring;
    m->line.samples[m->position]=frost_rail(v,4.0f);
    if(++m->position==FROST_MOD_SAMPLES)m->position=0;
    if(m->valid<FROST_MOD_SAMPLES)++m->valid;
}
static float bbd_filter(FrostModState *s,float v,float hz)
{float a=frost_onepole(hz);return frost_lp(&s->lp[1],frost_lp(&s->lp[0],v,a),a);}
#define MS(x) ((x)*44.1f)
#include "../../engine/overlay_far_off.h"
/* CE-2-style BBD chorus: triangle LFO, ~5.5 ms centre swept up to +/-3.5 ms
 * by DEPTH, two-pole ~6.5 kHz BBD/reconstruction roll-off, fixed equal
 * dry/wet mono sum (the CE-2 mono output). */
FROST_HOT FROST_NOINLINE FROST_LARGE_ONLY float frost_ce2_sample(FrostModState *s,float x,const float *p)
{
    float wet=bbd_filter(s,ring_read(s,MS(5.5f+3.5f*p[1]*0.01f*frost_triangle01(s->phase))),6500.0f);
    ring_write(s,x);s->phase=frost_phase_advance(s->phase,p[0]);
    return 0.55f*(x+wet);
}
#include "../../engine/overlay_far_on.h"
#ifdef FROST_OVERLAYS
static float (*volatile frost_far_ring_read)(FrostModState *,float)=ring_read;
static void (*volatile frost_far_ring_write)(FrostModState *,float)=ring_write;
#define ring_read(s,d) (frost_far_ring_read((s),(d)))
#define ring_write(s,v) (frost_far_ring_write((s),(v)))
#endif
/* Electric Mistress-style flanger: slow triangle LFO, RANGE sets the sweep
 * extent (0.35..6.85 ms), COLOR positive regeneration up to 0.85, darker
 * SAD1024-like wet roll-off, fixed blend. FILTER MATRIX stops the LFO and
 * RANGE becomes the manual comb position. */
FROST_HOT FROST_NOINLINE FROST_OVL(mistress) float frost_mistress_sample(FrostModState *s,float x,const float *p)
{
    float range=p[1]*0.01f,sweep=p[3]>0.5f?1.0f:0.5f+0.5f*frost_triangle01(s->phase);
    float wet=bbd_filter(s,ring_read(s,MS(0.35f+6.5f*range*sweep)),5500.0f);
    ring_write(s,x+0.85f*p[2]*0.01f*wet);
    if(p[3]<=0.5f)s->phase=frost_phase_advance(s->phase,p[0]);
    return 0.5f*(x+wet);
}
/* M117-style flanger: MANUAL sets the centre delay (0.25..5.75 ms), WIDTH the
 * LFO excursion (up to +/-4 ms, floor 0.1 ms), SPEED the triangle rate, REGEN
 * positive regeneration up to 0.92 for the pronounced jet sweep; brighter
 * (MN3207-like ~9 kHz) wet path, fixed blend. */
FROST_HOT FROST_NOINLINE FROST_OVL(m117) float frost_m117_sample(FrostModState *s,float x,const float *p)
{
    float d=0.25f+5.5f*p[0]*0.01f+4.0f*p[1]*0.01f*frost_triangle01(s->phase);
    float wet=bbd_filter(s,ring_read(s,MS(d<0.1f?0.1f:d)),9000.0f);
    ring_write(s,x+0.92f*p[3]*0.01f*wet);
    s->phase=frost_phase_advance(s->phase,p[2]);
    return 0.5f*(x+wet);
}
/* VB-2-style BBD vibrato: wet-only pitch modulation around 3 ms (+/-2.5 ms at
 * DEPTH 100, sine LFO). RISE: after the block is engaged, depth and rate ramp
 * from zero (rate from 30%) to the set values over RISE ms; disengaging
 * resets the ramp. The kernel keeps running while bypassed (KEEP policy), so
 * the delay line is warm, as the original passes signal through its BBD.
 * MODE UNLATCH is a controller footswitch mode: the block is enabled only
 * while the footswitch is held (see docs). */
FROST_HOT FROST_NOINLINE FROST_OVL(vb2) float frost_vb2_sample(FrostModState *s,float x,const float *p)
{
    float wet,depth;
    if(p[FROST_PARAMETERS]>0.5f){s->rise+=1.0f/(p[2]*44.1f);if(s->rise>1.0f)s->rise=1.0f;}
    else s->rise=0.0f;
    depth=p[1]*0.01f*s->rise;
    wet=bbd_filter(s,ring_read(s,MS(3.0f+2.5f*depth*frost_sin01(s->phase))),7500.0f);
    ring_write(s,x);s->phase=frost_phase_advance(s->phase,p[0]*(0.3f+0.7f*s->rise));
    return wet;
}
#include "../../engine/overlay_far_off.h"
#ifdef FROST_OVERLAYS
#undef ring_read
#undef ring_write
#endif
