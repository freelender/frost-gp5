#include "delays.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
#define DD3_SEAM 128.0f
/* Time changes glide exponentially (~0.2 s to settle, at least 0.25 sample
 * per sample), giving the pitch-bend of a moving delay clock without the
 * multi-second crawl of a fixed slew on large jumps. First call snaps. */
static float delay_glide(float distance,float desired)
{
    float d=desired-distance,step=d*(1.0f/1024.0f);
    if(distance==0.0f)return desired;
    if(step<0.25f && step>-0.25f)step=d>0.25f?0.25f:d< -0.25f?-0.25f:d;
    return distance+step;
}
void frost_dd3_reset(FrostDD3 *s)
{
    s->distance=s->hold_length=s->hold_position=0.0f;s->lp[0]=s->lp[1]=0.0f;s->holding=0;
    if(s->ring){s->ring->position=s->ring->valid=0;s->ring->distance=0.0f;}
}
FROST_HOT FROST_NOINLINE static float dd3_tap(const FrostDelay *m,float distance)
{
    unsigned d=(unsigned)distance,i,j;float f=distance-(float)d,a;
    if(d+1u>m->valid || d+1u>=FROST_DELAY_PCM)return 0.0f;
    i=m->position>=d?m->position-d:m->position+FROST_DELAY_PCM-d;j=i?i-1u:FROST_DELAY_PCM-1u;
    a=frost_unpcm16(m->line.pcm[i]);return a+f*(frost_unpcm16(m->line.pcm[j])-a);
}
#ifdef FROST_OVERLAYS
FROST_LARGE_ONLY float frost_ovl_dd3_tap(const FrostDelay *m,float d);
FROST_LARGE_ONLY float frost_ovl_dd3_tap(const FrostDelay *m,float d){return dd3_tap(m,d);}
static float (*volatile frost_far_dd3_tap)(const FrostDelay *,float)=frost_ovl_dd3_tap;
#define dd3_tap(m,d) (frost_far_dd3_tap((m),(d)))
#endif
/* DD-3-style digital delay: 13..800 ms (manual: 12.5..800 ms, S/M/L ranges
 * folded into one ms control), F.BACK up to 0.97, ~7 kHz delay-path
 * bandwidth, direct sound always at unity with E.LEVEL echo level.
 * HOLD freezes the recording and loops the last TIME window (the manual's
 * HOLD mode, 200..800 ms there; any TIME here) with a 3 ms seam fade; the
 * live signal still passes. Releasing HOLD resumes normal delay. The FOOT
 * parameter selects whether the controller footswitch toggles bypass or acts
 * as a momentary HOLD for this block (documented controller mode). */
FROST_HOT FROST_NOINLINE FROST_OVL(dd3) float frost_dd3_sample(FrostDD3 *s,float x,const float *p)
{
    FrostDelay *m=s->ring;float desired=p[2]*44.1f,echo,a=frost_onepole(7000.0f);
    if(desired>35280.0f)desired=35280.0f;
    s->distance=delay_glide(s->distance,desired);
    if(p[3]>0.5f){
        float g;
        if(!s->holding){s->holding=1;s->hold_length=s->distance<2205.0f?2205.0f:s->distance;s->hold_position=0.0f;
            if(s->hold_length>(float)m->valid-2.0f)s->hold_length=(float)m->valid-2.0f;}
        if(s->hold_length<2.0f)return x;
        echo=dd3_tap(m,s->hold_length-s->hold_position+1.0f);
        g=frost_clampf(s->hold_position*(1.0f/DD3_SEAM),0.0f,1.0f);
        if(s->hold_length-s->hold_position<DD3_SEAM)g*= (s->hold_length-s->hold_position)*(1.0f/DD3_SEAM);
        if(++s->hold_position>=s->hold_length)s->hold_position=0.0f;
        echo=frost_lp(&s->lp[1],frost_lp(&s->lp[0],echo*g,a),a);
        return frost_rail(x+1.2f*p[0]*0.01f*echo,8.0f);
    }
    s->holding=0;
    echo=frost_lp(&s->lp[1],frost_lp(&s->lp[0],dd3_tap(m,s->distance),a),a);
    m->line.pcm[m->position]=frost_pcm16(x+0.97f*p[1]*0.01f*echo);
    if(++m->position==FROST_DELAY_PCM)m->position=0;
    if(m->valid<FROST_DELAY_PCM)++m->valid;
    return frost_rail(x+1.2f*p[0]*0.01f*echo,8.0f);
}
void frost_dm2_reset(FrostDM2 *s)
{
    s->distance=s->hp=s->phase=0.0f;s->lp[0]=s->lp[1]=0.0f;
    if(s->ring){s->ring->position=s->ring->valid=0;s->ring->distance=0.0f;}
}
/* 31b: returns the stored binary16 word in a float's bits (0.0f = word 0 = +0
 * outside the valid span); the caller decodes. A float-returning leaf like
 * before, so the boot-adjacent layout holds. */
FROST_HOT FROST_NOINLINE static float dm2_tap(const FrostDelay *m,unsigned d)
{
    union {uint32_t u;float f;} v;unsigned i;if(d>m->valid || d>=FROST_DELAY_SAMPLES)return 0.0f;
    i=m->position>=d?m->position-d:m->position+FROST_DELAY_SAMPLES-d;v.u=m->line.half[i];return v.f;
}
#ifdef FROST_OVERLAYS
FROST_LARGE_ONLY float frost_ovl_dm2_tap(const FrostDelay *m,unsigned d);
FROST_LARGE_ONLY float frost_ovl_dm2_tap(const FrostDelay *m,unsigned d){return dm2_tap(m,d);}
static float (*volatile frost_far_dm2_tap)(const FrostDelay *,unsigned)=frost_ovl_dm2_tap;
#define dm2_tap(m,d) (frost_far_dm2_tap((m),(d)))
#endif
static inline uint16_t dm2_word(float f){union {float f;uint32_t u;} v;v.f=f;return (uint16_t)v.u;}
/* DM-2-style analog (BBD) delay: REPEAT RATE 20..300 ms. A 4096-stage BBD's
 * clock falls as delay rises, so repeat bandwidth narrows with time: modeled
 * as a two-pole low-pass at 0.45 x half the implied clock, 1.5..4.5 kHz. The
 * repeats recirculate through that filter, an 80 Hz high-pass and a soft
 * compander/BBD saturation (stable even when INTENS runs past unity), with a
 * slight clock wobble. Changing RATE bends pitch like a BBD clock sweep.
 * ECHO is the echo level, INTENS the regeneration; direct sound at unity. */
FROST_HOT FROST_NOINLINE FROST_OVL(dm2) float frost_dm2_sample(FrostDM2 *s,float x,const float *p)
{
    FrostDelay *m=s->ring;float desired=p[0]*44.1f,read,echo,fc,a;unsigned d;
    s->distance=delay_glide(s->distance,desired);
    read=s->distance*(1.0f+0.0015f*frost_sin01(s->phase));s->phase=frost_phase_advance(s->phase,0.6f);
    d=(unsigned)read;{float f=read-(float)d,n=frost_half_load(dm2_word(dm2_tap(m,d)));echo=n+f*(frost_half_load(dm2_word(dm2_tap(m,d+1u)))-n);}
    fc=frost_clampf(0.45f*0.5f*2048.0f/(s->distance*(1.0f/44100.0f)),1500.0f,4500.0f);a=frost_onepole(fc);
    echo=frost_lp(&s->lp[1],frost_lp(&s->lp[0],echo,a),a);
    echo=frost_hp(&s->hp,echo,frost_onepole(80.0f));
    m->line.half[m->position]=frost_half_store(frost_rail(x+1.05f*p[2]*0.01f*frost_soft_clip(echo),4.0f));
    if(++m->position==FROST_DELAY_SAMPLES)m->position=0;
    if(m->valid<FROST_DELAY_SAMPLES)++m->valid;
    return frost_rail(x+1.1f*p[1]*0.01f*echo,8.0f);
}
#include "../../engine/overlay_far_off.h"
#ifdef FROST_OVERLAYS
#undef dd3_tap
#undef dm2_tap
#endif
