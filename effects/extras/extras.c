#include "extras.h"
#include <string.h>
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* 32l effects. Parameters arrive in the library's human units (engine/library.h):
 * knobs 0..100, rates in Hz, times in ms, gains in dB, switches 0/1. */

/* ROTARY: rotating-speaker style. An 800 Hz split sends the lows to a slow
 * "drum" (amplitude modulation) and the highs to a "horn" (amplitude
 * modulation plus a short Doppler delay). SPEED switches SLOW/FAST; both
 * rotors glide to the new speed with inertia (horn ~0.6 s, drum ~2.5 s).
 * BALANCE 50 = equal drum and horn; LEVEL 50 = unity. */
void frost_rotary_reset(FrostRotary *s){memset(s,0,sizeof(*s));s->horn_rate=0.8f;s->drum_rate=0.67f;s->drum_phase=0.25f;}
FROST_HOT FROST_NOINLINE FROST_OVL(rotary) float frost_rotary_sample(FrostRotary *s,float x,const float *p)
{
    float fast=p[0]>0.5f?1.0f:0.0f,depth=p[1]*0.01f,bal=p[2]*0.01f;
    float lo,hi,sh,sd,d,f,a,b,wh,wd;unsigned i,j,k;
    s->horn_rate+=(0.8f+6.0f*fast-s->horn_rate)*0.0000378f;
    s->drum_rate+=(0.67f+5.23f*fast-s->drum_rate)*0.0000091f;
    s->horn_phase=frost_phase_advance(s->horn_phase,s->horn_rate);
    s->drum_phase=frost_phase_advance(s->drum_phase,s->drum_rate);
    sh=frost_sin01(s->horn_phase);sd=frost_sin01(s->drum_phase);
    lo=frost_lp(&s->split,x,0.10231837f);                  /* frost_onepole(800) */
    hi=x-lo;
    s->line[s->pos]=hi;
    d=6.0f+5.0f*depth*sh;                                   /* 1..11 samples (~0.25 ms swing) */
    i=(unsigned)d;f=d-(float)i;
    j=(s->pos+FROST_ROTARY_LINE-i)&(FROST_ROTARY_LINE-1u);k=(j+FROST_ROTARY_LINE-1u)&(FROST_ROTARY_LINE-1u);
    a=s->line[j];b=s->line[k];
    s->pos=(s->pos+1u)&(FROST_ROTARY_LINE-1u);
    hi=(a+f*(b-a))*(1.0f-0.45f*depth*(0.5f+0.5f*sh));
    hi=frost_lp(&s->tone,hi,0.46087434f);                  /* frost_onepole(6000): horn top roll-off */
    lo*=1.0f-0.30f*depth*(0.5f+0.5f*sd);
    wh=bal*2.0f;if(wh>1.0f)wh=1.0f;wd=(1.0f-bal)*2.0f;if(wd>1.0f)wd=1.0f;
    return (wd*lo+wh*hi)*frost_level(p[3]);
}

/* RING: classic ring modulator, the input times a sine carrier of FREQ Hz,
 * blended with the dry signal by MIX. */
void frost_ringmod_reset(FrostRingMod *s){s->phase=0.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(ringmod) float frost_ringmod_sample(FrostRingMod *s,float x,const float *p)
{
    float c=frost_sin01(s->phase),m=p[1]*0.01f;
    s->phase=frost_phase_advance(s->phase,p[0]);
    return x+m*(x*c-x);
}

/* CRUSH: bit crusher. BITS sets the amplitude resolution (2..16 bits over
 * the +-1 range), RATE the sample-and-hold rate in kHz, MIX the wet share. */
void frost_crusher_reset(FrostCrusher *s){s->held=0.0f;s->acc=1.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(crusher) float frost_crusher_sample(FrostCrusher *s,float x,const float *p)
{
    s->acc+=p[1]*(1000.0f/FROST_FS);                        /* RATE in kHz */
    if(s->acc>=1.0f){
        float q=frost_exp2(p[0]-1.0f),v=frost_clampf(x,-1.0f,1.0f)*q;
        int n=(int)(v<0.0f?v-0.5f:v+0.5f);
        s->held=(float)n/q;s->acc-=(float)(int)s->acc;
    }
    return x+p[2]*0.01f*(s->held-x);
}

/* H-TREM: harmonic tremolo. A ~650 Hz two-pole split; lows and highs are
 * amplitude-modulated in opposite phase, giving the phasey "brownface" pulse.
 * DEPTH 100 swings each band fully; gains are smoothed (~0.7 ms). */
void frost_harmtrem_reset(FrostHarmTrem *s){memset(s,0,sizeof(*s));s->gl=s->gh=1.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(harmtrem) float frost_harmtrem_sample(FrostHarmTrem *s,float x,const float *p)
{
    float lfo=frost_sin01(s->phase),dep=p[1]*0.01f,lo,hi;
    s->phase=frost_phase_advance(s->phase,p[0]);
    s->gl+=(1.0f-dep*(0.5f+0.5f*lfo)-s->gl)*0.032f;
    s->gh+=(1.0f-dep*(0.5f-0.5f*lfo)-s->gh)*0.032f;
    lo=frost_lp(&s->lo2,frost_lp(&s->lo1,x,0.08475976f),0.08475976f);   /* frost_onepole(650) twice */
    hi=x-lo;
    return (lo*s->gl+hi*s->gh)*1.25f*frost_level(p[2]);
}

/* BIG PI (internal id MUFF): two cascaded clipping stages (anti-aliased soft clip) and a passive
 * tone stack: TONE blends a ~450 Hz low-pass with a ~1.2 kHz high-pass, so the
 * middle of the knob scoops the mids. SUSTAIN sets the gain (about +17 to
 * +45 dB); VOLUME 50 is near unity at full sustain. */
void frost_muff_reset(FrostMuff *s){memset(s,0,sizeof(*s));}
FROST_HOT FROST_NOINLINE FROST_OVL(muff) float frost_muff_sample(FrostMuff *s,float x,const float *p)
{
    float g=p[0]*0.01f,t=p[1]*0.01f,y,lo,hi;
    y=frost_hp(&s->in_hp,x,0.00566674f);                    /* frost_onepole(40) */
    y=frost_soft_clip_aa(&s->aa[0],(7.0f+170.0f*g*g)*y);
    y=frost_lp(&s->mid_lp,y,0.41601723f);                   /* frost_onepole(5000) */
    y=frost_soft_clip_aa(&s->aa[2],6.0f*y);
    y=frost_hp(&s->out_hp,y,0.00847609f);                   /* frost_onepole(60) */
    lo=frost_lp(&s->tone_lp,y,0.06025118f);                 /* frost_onepole(450) */
    hi=frost_hp(&s->tone_hp,y,0.14600791f);                 /* frost_onepole(1200) */
    return ((1.0f-t)*1.6f*lo+t*1.3f*hi)*0.5f*frost_level(p[2]);
}

/* BOOST: clean boost, GAIN 0..+20 dB; BRIGHT adds a gentle treble lift
 * (+4 dB shelf above ~2.5 kHz). No clipping of its own. */
void frost_boost_reset(FrostBoost *s){s->bright=0.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(boost) float frost_boost_sample(FrostBoost *s,float x,const float *p)
{
    float y=x;
    if(p[1]>0.5f)y+=0.6f*frost_hp(&s->bright,x,0.26264000f);   /* frost_onepole(2500) */
    return y*frost_db_to_gain(p[0]);
}

/* SWELL: automatic volume swell. When the input envelope falls below half the
 * threshold (silence between notes) the gain resets to zero; the next note
 * then fades in over ATTACK ms (squared ramp). SENS 0..100 lowers the
 * threshold from -30 to -60 dBFS. LEVEL 50 = unity. */
void frost_swell_reset(FrostSwell *s){s->env=0.0f;s->gain=0.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(swell) float frost_swell_sample(FrostSwell *s,float x,const float *p)
{
    float a=frost_absf(x),th=frost_db_to_gain(-30.0f-0.3f*p[1]);
    s->env+=(a-s->env)*(a>s->env?0.0222f:0.00038f);          /* ~1 ms attack, ~60 ms release */
    if(s->env<0.5f*th)s->gain=0.0f;
    else if(s->gain<1.0f){s->gain+=1000.0f/(FROST_FS*p[0]);if(s->gain>1.0f)s->gain=1.0f;}
    return x*s->gain*s->gain*frost_level(p[2]);
}
#include "../../engine/overlay_far_off.h"
