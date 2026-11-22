#include "amp_heads.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"

/* Original behavioral parameters, not extracted weights or component values.
 * Deluxe/Twin blackface, AC30 Top Boost, JC clean, 2203 crunch,
 * Rectifier Modern lead, 5150III 6L6 lead are the intended starting voices. */
FROST_LARGE_ONLY FROST_COLD void frost_amphead_reset(FrostAmpHead *s,unsigned voice)
{
    static const float cfg[7][12]={
        /* HP, inter HP, gain, stage gain, power, bias, sag, mids, low/high, bright, trim */
        {38,  55,  7,1.5f,2.2f,.12f,.40f,.48f,180,1100,.45f,.75f},
        {30,  45,  4,1.2f,1.0f,.05f,.06f,.55f,160,1000,.65f,.85f},
        {65, 100, 10,1.8f,1.8f,.16f,.22f,.75f,250,1500,.85f,.65f},
        {20,  30,  2,1.0f,1.0f,   0,   0,1.0f,160,1800,.30f,1.15f},
        {70, 180, 24,2.8f,1.4f,.08f,.15f,1.1f,250,1600,.55f,.60f},
        {45, 115, 48,3.6f,1.5f,.18f,.30f,.65f,160,1200,.35f,.60f},
        {85, 220, 60,4.2f,1.3f,.10f,.12f,.85f,210,1700,.65f,.58f}
    };
    const float *c;
    if(voice>6u)voice=0;
    memset(s,0,sizeof(*s));c=cfg[voice];
    s->input_hp=frost_onepole(c[0]*.5f);s->inter_hp=frost_onepole(c[1]*.5f);
    s->drive=c[2];s->stage_drive=c[3];s->power_drive=c[4];s->bias=c[5];
    s->sag=c[6];s->mid_voice=c[7];s->low_hz=frost_onepole(c[8]*.5f);
    s->high_hz=frost_onepole(c[9]*.5f);s->brightness=c[10];s->trim=c[11];
    s->stages=voice>=4u?3u:1u;s->voice=voice;
    s->zero=frost_soft_clip(s->bias);
}

#include "../../engine/overlay_far_on.h"
FROST_FINLINE float frost_amphead_tone(FrostAmpHead *s,float x,const float *p)
{
    float b=p[1]*.01f,m=p[2]*.01f,t=p[3]*.01f;
    float lo=frost_lp(&s->low,x,s->low_hz),rest=x-lo;
    float mid=frost_lp(&s->mid,rest,s->high_hz),hi=rest-mid;
    /* Shared loaded tone network: changing bass/treble also changes loading.
     * This is a compact approximation, not an exact passive RC solver. */
    return (lo*(.2f+2.4f*b*b)+mid*s->mid_voice*(.12f+2.0f*m*m)+
            hi*(.2f+2.4f*t*t))/(.7f+.25f*b+.2f*t);
}
FROST_NOINLINE FROST_OVL(ampheads) float frost_amphead_sample(FrostAmpHead *s,float x,const float *p)
{
    float gain=p[0]*.01f,master=p[5]*.02f,detail=p[4]*.01f;
    float drive=.2f+s->drive*gain*gain,wet=0.0f;
    float supply=1.0f/(1.0f+s->sag*s->envelope),y,a;
    unsigned sub,j;
    /* Two nonlinear substeps, linear interpolation and two-pole reduction.
     * Reduces aliasing; does not make high gain alias-free. Tone position is
     * pre-power for cleans/chime, post-cascade for the lead voices. */
    for(sub=0;sub<2u;++sub){
        y=sub?x:.5f*(s->previous+x);
        y=frost_hp(&s->hp[0],y,s->input_hp)*drive;
        for(j=0;j<s->stages;++j){
            if(j)y=frost_hp(&s->hp[j],y,s->inter_hp)*s->stage_drive;
            /* Asymmetric smooth triode-like stages with zero at zero input. */
            y=frost_soft_clip(y+s->bias)-s->zero;
        }
        y=frost_amphead_tone(s,y,p);
        a=(.2f+master*master)*s->power_drive*supply;
        y=frost_soft_clip(y*a)/(.2f+s->power_drive);
        a=y-frost_lp(&s->presence,y,frost_onepole(1500.0f));
        /* AC30 CUT rises toward darker; other voices add presence/brightness. */
        y+=a*(s->voice==2u?-1.5f*detail:s->brightness*2.0f*detail);
        a=frost_lp(&s->bass,y,frost_onepole(60.0f));
        y+=(s->voice>=5u?.25f:.05f)*a;
        y=frost_lp(&s->aa[0],y,frost_onepole(8000.0f));
        wet+=frost_lp(&s->aa[1],y,frost_onepole(8000.0f));
    }
    s->previous=x;y=.5f*wet;
    a=frost_absf(y);s->envelope+=(a-s->envelope)*frost_time_ms(a>s->envelope?20.0f:180.0f);
    y=frost_hp(&s->dc,y,frost_onepole(15.0f));
    return y*s->trim*master*frost_level(p[6]);
}
/* The block path keeps the scalar arithmetic and nonlinear states, but
 * derives unchanged controls once per chunk rather than twice per sample.
 * The original sample kernel remains the fallback/reference for CUT policy. */
FROST_NOINLINE FROST_OVL(ampheads) void frost_amphead_block(void *state,const float *x,float *out,unsigned n,const float *p,const float *p_start)
{
    FrostAmpHead *s=(FrostAmpHead *)state;unsigned i,sub,j;
    float gain=p[0]*.01f,master=p[5]*.02f,detail=p[4]*.01f;
    float b=p[1]*.01f,m=p[2]*.01f,t=p[3]*.01f;
    float drive=.2f+s->drive*gain*gain,power=(.2f+master*master)*s->power_drive;
    float low=.2f+2.4f*b*b,mid=.12f+2.0f*m*m,high=.2f+2.4f*t*t;
    float denominator=.7f+.25f*b+.2f*t,level=frost_level(p[6]);
    float presence=s->voice==2u?-1.5f*detail:s->brightness*2.0f*detail;
    (void)p_start;
    for(i=0;i<n;++i){
        float input=x[i],supply=1.0f/(1.0f+s->sag*s->envelope),y,a,wet=0.0f;
        for(sub=0;sub<2u;++sub){
            float lo,rest,md,hi;
            y=sub?input:.5f*(s->previous+input);
            y=frost_hp(&s->hp[0],y,s->input_hp)*drive;
            for(j=0;j<s->stages;++j){
                if(j)y=frost_hp(&s->hp[j],y,s->inter_hp)*s->stage_drive;
                y=frost_soft_clip(y+s->bias)-s->zero;
            }
            lo=frost_lp(&s->low,y,s->low_hz);rest=y-lo;
            md=frost_lp(&s->mid,rest,s->high_hz);hi=rest-md;
            y=(lo*low+md*s->mid_voice*mid+hi*high)/denominator;
            a=power*supply;y=frost_soft_clip(y*a)/(.2f+s->power_drive);
            a=y-frost_lp(&s->presence,y,frost_onepole(1500.0f));y+=a*presence;
            a=frost_lp(&s->bass,y,frost_onepole(60.0f));y+=(s->voice>=5u?.25f:.05f)*a;
            y=frost_lp(&s->aa[0],y,frost_onepole(8000.0f));
            wet+=frost_lp(&s->aa[1],y,frost_onepole(8000.0f));
        }
        s->previous=input;y=.5f*wet;
        a=frost_absf(y);s->envelope+=(a-s->envelope)*frost_time_ms(a>s->envelope?20.0f:180.0f);
        y=frost_hp(&s->dc,y,frost_onepole(15.0f));out[i]=y*s->trim*master*level;
    }
}
#include "../../engine/overlay_far_off.h"

#ifdef FROST_OVERLAYS
FrostBlockFn frost_amphead_block_kernel=0;
#else
FrostBlockFn frost_amphead_block_kernel=frost_amphead_block;
#endif
