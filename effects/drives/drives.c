#include "drives.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* Component values quoted in comments follow public circuit analyses listed
 * in README.md. Values are normalized: 1.0 ~ a hot pickup peak. */
void frost_drive_reset(FrostDrive *s){unsigned i;for(i=0;i<FROST_DRIVE_STATE;++i)s->z[i]=0.0f;}
static float drive_out(float y){return frost_rail(y,8.0f);}

/* TS808/TS9: non-inverting clipping amp. Only content above the 720 Hz
 * (4.7k/47n) corner is amplified by (51k+Rdrive)/4.7k; the 51 pF feedback cap
 * lowers the amp bandwidth as DRIVE rises; antiparallel diodes in the loop
 * clip only the amplified part, so output = input + clipped(mid/treble).
 * Then the 723 Hz passive low-pass and the active treble/tone stage.
 * TS9 vs TS808 differs in the output network (470R/100k vs 100R/10k): a small
 * level/presence difference, modeled as such and not exaggerated. */
FROST_HOT FROST_NOINLINE FROST_OVL(ts) float frost_ts_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,rd=500e3f*t*t,g=(51e3f+rd)*(1.0f/4.7e3f),fc,v,y,lp,tone=p[1]*0.01f,out;
    unsigned ts9=p[3]>0.5f;
    v=frost_hp(&s->z[0],x,frost_onepole(720.0f))*g;
    fc=1.0f/(2.0f*FROST_PI*(51e3f+rd)*51e-12f);if(fc>16000.0f)fc=16000.0f;
    v=frost_lp(&s->z[1],v,frost_onepole(fc));
    y=x+0.45f*frost_soft_clip_aa(&s->z[6],v*(1.0f/0.45f));   /* 32d: ADAA1 */
    lp=frost_lp(&s->z[2],y,frost_onepole(723.0f));
    out=lp+(0.12f+(ts9?1.2f:1.0f)*tone)*(y-lp);
    if(ts9)s->z[3]=out;else out=frost_lp(&s->z[3],out,frost_onepole(9000.0f));
    out=frost_hp(&s->z[4],out,frost_onepole(20.0f));
    return drive_out(0.8f*out*frost_level(p[2])*(ts9?1.12f:1.0f));
}
/* BD-2-style: two cascaded discrete gain stages on a dual-gang GAIN control,
 * low-frequency content retained (no TS mid hump), asymmetric first-stage
 * clipping, harder second stage, ~72 Hz bass lift and ~5 kHz roll-off, then
 * a treble shelf TONE that cuts or boosts above ~1.5 kHz. */
FROST_HOT FROST_NOINLINE FROST_OVL(bd2) float frost_bd2_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,v,low,hi,tone=p[1]*0.01f;
    v=frost_hp(&s->z[0],x,frost_onepole(60.0f))*(1.5f+30.0f*t*t);
    v=frost_soft_clip_aa(&s->z[6],v+0.15f)-frost_soft_clip(0.15f);   /* 32d: ADAA1 */
    v=frost_lp(&s->z[1],v,frost_onepole(6000.0f))*(1.2f+14.0f*t);
    v=0.8f*frost_hard_clip_aa(&s->z[8],v*(1.0f/0.8f));
    low=frost_lp(&s->z[2],v,frost_onepole(72.0f));v+=0.6f*low;
    v=frost_lp(&s->z[3],v,frost_onepole(5000.0f));
    hi=v-frost_lp(&s->z[5],v,frost_onepole(1500.0f));
    v+=(2.0f*tone-1.0f)*0.9f*hi;
    v=frost_hp(&s->z[4],v,frost_onepole(25.0f));
    return drive_out(0.75f*v*frost_level(p[2]));
}
/* Klon-style: clean path summed with a gained, mid-emphasized path that is
 * hard clipped by germanium diodes to ground; the dual-gang GAIN raises the
 * dirty path while reducing clean blend. 18 V rails: generous headroom.
 * TREBLE is an active high shelf (cut..boost). */
FROST_HOT FROST_NOINLINE FROST_OVL(klon) float frost_klon_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,d,sum,hi,tr=p[1]*0.01f;
    d=(x+1.5f*frost_hp(&s->z[0],x,frost_onepole(400.0f)))*(1.0f+60.0f*t*t);
    d=6.0f*frost_soft_clip_aa(&s->z[6],d*(1.0f/6.0f));      /* 32d: ADAA1 */
    d=0.35f*frost_germanium_clip_aa(&s->z[8],d*(1.0f/0.35f))*2.2f;
    d=frost_lp(&s->z[1],d,frost_onepole(4500.0f));
    sum=x*(1.0f-0.8f*t)+d*(0.2f+0.8f*t);
    hi=sum-frost_lp(&s->z[2],sum,frost_onepole(1200.0f));
    sum+=(2.2f*tr-1.0f)*0.8f*hi;
    sum=frost_hp(&s->z[3],sum,frost_onepole(20.0f));
    return drive_out(sum*frost_level(p[2]));
}
/* Bluesbreaker-style: op-amp stage with two series diodes per direction in the
 * feedback loop (higher, softer threshold than TS), flat low end (no 720 Hz
 * pre-filter), then a passive tilt TONE and recovery stage. */
FROST_HOT FROST_NOINLINE FROST_OVL(bb) float frost_bluesbreaker_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,hp,v,y,lp,tone=p[1]*0.01f;
    hp=frost_hp(&s->z[0],x,frost_onepole(50.0f));
    v=frost_lp(&s->z[1],hp*(2.0f+60.0f*t*t),frost_onepole(8000.0f));
    y=hp+0.9f*frost_soft_clip_aa(&s->z[6],v*(1.0f/0.9f));   /* 32d: ADAA1 */
    lp=frost_lp(&s->z[2],y,frost_onepole(1000.0f));
    y=lp+(0.25f+1.5f*tone)*(y-lp);
    y=frost_lp(&s->z[3],y,frost_onepole(7000.0f));
    y=frost_hp(&s->z[4],y,frost_onepole(20.0f));
    return drive_out(0.6f*y*frost_level(p[2]));
}
/* OCD-style: JFET input, overdriven op-amp (slightly asymmetric rail
 * saturation), then MOSFET clipping (higher, rounder knee than silicon
 * diodes) and a variable high-cut TONE. PEAK HP: more gain, output and a
 * 1-2 kHz lift with a higher shelf; LP: smoother, lower shelf. */
FROST_HOT FROST_NOINLINE FROST_OVL(ocd) float frost_ocd_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,v,m,fc,band;unsigned hp_mode=p[3]>0.5f;
    v=frost_hp(&s->z[0],x,frost_onepole(90.0f))*(1.0f+(hp_mode?180.0f:120.0f)*t*t);
    v=4.0f*frost_soft_clip_aa(&s->z[6],(v+0.08f)*0.25f)-4.0f*frost_soft_clip(0.02f);   /* 32d: ADAA1 */
    m=v*(1.0f/0.9f);v=0.9f*(0.6f*frost_hard_clip_aa(&s->z[8],m)+0.4f*frost_soft_clip_aa(&s->z[10],m));
    if(hp_mode){band=frost_hp(&s->z[1],frost_lp(&s->z[2],v,frost_onepole(2000.0f)),frost_onepole(1000.0f));v+=0.5f*band;}
    fc=700.0f*frost_exp2(3.585f*p[1]*0.01f)*(hp_mode?1.0f:0.7f);
    v=frost_lp(&s->z[3],v,frost_onepole(fc));
    v=frost_hp(&s->z[4],v,frost_onepole(20.0f));
    return drive_out(v*frost_level(p[2])*(hp_mode?0.85f:0.7f));
}
#include "../../engine/overlay_far_off.h"
/* DS-1-style: transistor booster, op-amp gain stage, silicon diodes HARD
 * clipping to ground, then the passive LP(234 Hz)/HP(3.2 kHz) blend TONE
 * whose middle settings scoop the mids, plus a recovery gain. */
FROST_HOT FROST_CORE FROST_NOINLINE FROST_AUDIO_RAM float frost_ds1_sample(FrostDrive *s,float x,const float *p)
{
    float b,v,lp,hp,tone=p[1]*0.01f;
    b=frost_hp(&s->z[0],x,frost_onepole(100.0f))*4.0f;b=4.5f*frost_soft_clip(b*(1.0f/4.5f));
    v=b*frost_taper_log(p[0],1.5f,6.06f);
    v=4.5f*frost_soft_clip_aa_call(&s->z[8],v*(1.0f/4.5f));   /* 32d: ADAA1 (out of line: LARGE) */
    v=frost_lp(&s->z[1],v,frost_onepole(7200.0f));
    v=0.6f*frost_hard_clip_aa_call(&s->z[6],v*(1.0f/0.6f));
    lp=frost_lp(&s->z[2],v,frost_onepole(234.0f));
    hp=frost_hp(&s->z[3],v,frost_onepole(3200.0f));
    v=2.2f*((1.0f-tone)*lp+tone*hp);
    v=frost_hp(&s->z[4],v,frost_onepole(20.0f));
    return drive_out(v*frost_level(p[2]));
}
#include "../../engine/overlay_far_on.h"
/* RAT-style: LM308 stage whose gain (1+Rdist/560 above 60 Hz, +Rdist/47
 * above 1.5 kHz) is limited by gain-bandwidth (bandwidth ~1 MHz/gain falls as
 * DIST rises), silicon diodes hard clipping to ground, passive RC FILTER
 * (1.5k+100k, 3.3 nF). Frost reverses FILTER so higher = brighter. */
FROST_HOT FROST_NOINLINE FROST_OVL(rat) float frost_rat_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,rd=100e3f*t*t,g,fc,v,r;
    v=x+(rd*(1.0f/560.0f))*frost_hp(&s->z[0],x,frost_onepole(60.0f))+(rd*(1.0f/47.0f))*frost_hp(&s->z[1],x,frost_onepole(1540.0f));
    g=1.0f+rd*(1.0f/560.0f)+rd*(1.0f/47.0f);fc=frost_clampf(1e6f/g,300.0f,16000.0f);
    v=frost_lp(&s->z[2],v,frost_onepole(fc));
    v=4.0f*frost_soft_clip_aa(&s->z[6],v*0.25f);          /* 32d: ADAA1 */
    v=0.55f*frost_hard_clip_aa(&s->z[8],v*(1.0f/0.55f));
    r=1.5e3f+100e3f*(1.0f-p[1]*0.01f);fc=frost_clampf(1.0f/(2.0f*FROST_PI*r*3.3e-9f),400.0f,16000.0f);
    v=frost_lp(&s->z[3],v,frost_onepole(fc));
    v=frost_hp(&s->z[4],v,frost_onepole(20.0f));
    return drive_out(1.2f*v*frost_level(p[2]));
}
/* Distortion+-style: 741 stage whose DIST pot sets both gain (1+1M/(4.7k+R))
 * and the high-pass corner (47 nF), GBW roll-off, germanium diodes clipping to
 * ground, fixed 1 nF high-cut. No tone control by design. */
FROST_HOT FROST_NOINLINE FROST_OVL(distplus) float frost_distplus_sample(FrostDrive *s,float x,const float *p)
{
    float t=p[0]*0.01f,rp=1e6f*(1.0f-t)*(1.0f-t),rt=4.7e3f+rp,g=1.0f+1e6f/rt,fc,v;
    fc=1.0f/(2.0f*FROST_PI*rt*47e-9f);
    v=x+(g-1.0f)*frost_hp(&s->z[0],x,frost_onepole(fc));
    v=frost_lp(&s->z[1],v,frost_onepole(frost_clampf(1e6f/g,2000.0f,16000.0f)));
    v=3.5f*frost_soft_clip_aa(&s->z[6],v*(1.0f/3.5f));      /* 32d: ADAA1 */
    v=0.3f*frost_germanium_clip_aa(&s->z[8],v*(1.0f/0.3f));
    v=frost_lp(&s->z[2],v,frost_onepole(6000.0f));
    v=frost_hp(&s->z[3],v,frost_onepole(20.0f));
    return drive_out(2.5f*v*frost_level(p[1]));
}
#include "../../engine/overlay_far_off.h"
