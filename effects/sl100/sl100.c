#include "sl100.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
/* SL-100 (beta.14): original behavioral voicing inspired by the Soldano
 * SLO-100's three channels, built like the Frost amp heads (effects/amp_heads):
 * 2x oversampled asymmetric triode-like stages, a loaded three-band tone stack
 * after the preamp, a sagging EL34-style power stage with PRESENCE and a fixed
 * low resonance (DEPTH, voiced per channel), two-pole decimation and DC block.
 *
 *  CLEAN   one stage, low drive, open and glassy, little low resonance.
 *  CRUNCH  two cascaded stages, mid-forward, medium resonance.
 *  LEAD    three cascaded stages with a cold-biased (asymmetric) clipper, a
 *          tighter input (second pre-clip high-pass), ~2.5 kHz pre-clip
 *          low-passes per stage (Miller roll-off) and full resonance.
 *  BRT     (CLEAN BRT, CRUNCH BRT) the bright cap across the gain control:
 *          a treble lift ahead of the first stage that fades out as GAIN
 *          rises, like the real cap. LEAD has none.
 *
 * Changing MODE re-voices the stages without clearing their histories (a
 * channel switch, not a reset). Coefficients below are one-pole constants at
 * the 2x rate (frost_onepole(f/2)) unless noted; the tables live with the code.
 * Trims: at the defaults (LEVEL 70) LEAD and CRUNCH match UK-800 at its
 * defaults and CLEAN sits ~2 dB below (host chord, tests/test_sl100.py).
 * Not a measured match, manufacturer implementation or endorsement. */
typedef struct {
    float in,inter,drive,stage,power,bias,sag,mids,lowc,highc,pres,depth,trim;
    unsigned stages;
} Sl100Voice;
static const Sl100Voice sl100_voice[3]={
    /* in(30)      inter(45)    drive stage power bias  sag   mids  low(160)     high(1000)   pres depth trim */
    {0.002132580f,0.003195463f,  5.0f,1.0f,1.6f,.06f,.10f,.60f,0.011269617f,0.066500566f,.45f,.15f,1.24f,1u},
    /* in(80)      inter(150)                                         low(220)     high(1500) */
    {0.005666739f,0.010572713f, 24.0f,2.6f,1.5f,.10f,.10f,1.05f,0.015430512f,0.096540839f,.50f,.55f,.284f,2u},
    /* in(120)     inter(250)                                         low(220)     high(1600) */
    {0.008476093f,0.017497854f, 48.0f,3.4f,1.4f,.20f,.08f,.95f,0.015430512f,0.102318368f,.50f,1.0f,.187f,3u}
};
#define SL_PRES   0.176082523f   /* frost_onepole(1500) per substep, as the amp heads */
#define SL_BASS   0.008476093f   /* frost_onepole(60) */
#define SL_AA_CLN 0.532668102f   /* frost_onepole(8000) */
#define SL_AA_LD  0.363018144f   /* frost_onepole(4000) */
#define SL_MILLER 0.262639997f   /* frost_onepole(2500) */
#define SL_DC     0.002132580f   /* frost_onepole(15) */
#define SL_BRIGHT 0.146007907f   /* frost_onepole(1200), 1x rate */
FROST_XCODE FROST_COLD void frost_sl100_reset(FrostSl100 *s)
{memset(s,0,sizeof(*s));s->channel=3u;}
#include "../../engine/overlay_far_on.h"
FROST_HOT FROST_NOINLINE FROST_OVL(sl100) float frost_sl100_sample(FrostSl100 *s,float x,const float *p)
{
    unsigned mode=(unsigned)(p[0]+.5f),ch=mode<2u?0u:mode<4u?1u:2u,bright=mode==1u||mode==3u,sub,j;
    const Sl100Voice *v=&sl100_voice[ch];
    float gain=p[1]*.01f,b=p[2]*.01f,m=p[3]*.01f,t=p[4]*.01f,presence=p[5]*.01f,master=p[6]*.02f;
    float drive=.2f+v->drive*gain*gain,supply,y,a,wet=0.0f,low,mid,high,den,power;
    float aa=ch==2u?SL_AA_LD:SL_AA_CLN;
    if(ch!=s->channel){s->channel=ch;s->zero=frost_soft_clip(v->bias);}
    /* Bright cap: treble around the gain control, strongest at low GAIN. */
    if(bright)x+=(1.6f-1.4f*gain)*frost_hp(&s->bright,x,SL_BRIGHT);
    if(ch==2u)x=frost_hp(&s->tight,x,2.0f*v->in);
    low=.2f+2.4f*b*b;mid=v->mids*(.12f+2.0f*m*m);high=.2f+2.4f*t*t;den=.7f+.25f*b+.2f*t;
    supply=1.0f/(1.0f+v->sag*s->env);power=(.2f+master*master)*v->power*supply;
    for(sub=0;sub<2u;++sub){
        float lo,rest,md;
        y=sub?x:.5f*(s->prev+x);
        y=frost_hp(&s->hp_in,y,v->in)*drive;
        for(j=0;j<v->stages;++j){
            if(j)y=frost_hp(&s->hp[j],y,v->inter)*v->stage;
            if(ch)y=frost_lp(&s->miller[j],y,SL_MILLER);
            y=frost_soft_clip(y+v->bias)-s->zero;
        }
        lo=frost_lp(&s->low,y,v->lowc);rest=y-lo;
        md=frost_lp(&s->mid,rest,v->highc);
        y=(lo*low+md*mid+(rest-md)*high)/den;
        y=frost_soft_clip(y*power)/(.2f+v->power);
        a=y-frost_lp(&s->pres,y,SL_PRES);y+=a*v->pres*2.0f*presence;
        y+=v->depth*frost_lp(&s->bass,y,SL_BASS);
        y=frost_lp(&s->aa[0],y,aa);
        wet+=frost_lp(&s->aa[1],y,aa);
    }
    s->prev=x;y=.5f*wet;
    a=frost_absf(y);s->env+=(a-s->env)*frost_time_ms(a>s->env?20.0f:180.0f);
    y=frost_hp(&s->dc,y,SL_DC);
    return y*v->trim*master*frost_level(p[7]);
}
/* Settled block path (beta.14 optimization): the channel table (flash) and
 * every knob-derived value are read once per block into locals; the per-sample
 * arithmetic is the sample kernel's, in the same order, so the output is
 * bit-identical (tests/test_sl100.py). p_start is unused, as for the heads. */
FROST_HOT FROST_NOINLINE FROST_OVL(sl100) void frost_sl100_block(void *state,const float *x,float *out,unsigned n,const float *p,const float *p_start)
{
    FrostSl100 *s=(FrostSl100 *)state;
    unsigned mode=(unsigned)(p[0]+.5f),ch=mode<2u?0u:mode<4u?1u:2u,bright=mode==1u||mode==3u,i,sub,j;
    const Sl100Voice *v=&sl100_voice[ch];
    float gain=p[1]*.01f,b=p[2]*.01f,m=p[3]*.01f,t=p[4]*.01f,presence=p[5]*.01f,master=p[6]*.02f;
    float drive=.2f+v->drive*gain*gain,low,mid,high,den,base;
    float aa=ch==2u?SL_AA_LD:SL_AA_CLN;
    float in=v->in,inter=v->inter,stage=v->stage,bias=v->bias,sag=v->sag,vpow=v->power,lowc=v->lowc,highc=v->highc;
    float pres=v->pres,depth=v->depth,tight=2.0f*v->in,brt=1.6f-1.4f*gain,trim=v->trim;
    unsigned stages=v->stages;
    (void)p_start;
    if(ch!=s->channel){s->channel=ch;s->zero=frost_soft_clip(v->bias);}
    low=.2f+2.4f*b*b;mid=v->mids*(.12f+2.0f*m*m);high=.2f+2.4f*t*t;den=.7f+.25f*b+.2f*t;
    base=(.2f+master*master)*vpow;
    {
        float level=frost_level(p[7]),zero=s->zero;
        for(i=0;i<n;++i){
            float xi=x[i],supply,power,y,a,wet=0.0f;
            if(bright)xi+=brt*frost_hp(&s->bright,xi,SL_BRIGHT);
            if(ch==2u)xi=frost_hp(&s->tight,xi,tight);
            supply=1.0f/(1.0f+sag*s->env);power=base*supply;
            for(sub=0;sub<2u;++sub){
                float lo,rest,md;
                y=sub?xi:.5f*(s->prev+xi);
                y=frost_hp(&s->hp_in,y,in)*drive;
                for(j=0;j<stages;++j){
                    if(j)y=frost_hp(&s->hp[j],y,inter)*stage;
                    if(ch)y=frost_lp(&s->miller[j],y,SL_MILLER);
                    y=frost_soft_clip(y+bias)-zero;
                }
                lo=frost_lp(&s->low,y,lowc);rest=y-lo;
                md=frost_lp(&s->mid,rest,highc);
                y=(lo*low+md*mid+(rest-md)*high)/den;
                y=frost_soft_clip(y*power)/(.2f+vpow);
                a=y-frost_lp(&s->pres,y,SL_PRES);y+=a*pres*2.0f*presence;
                y+=depth*frost_lp(&s->bass,y,SL_BASS);
                y=frost_lp(&s->aa[0],y,aa);
                wet+=frost_lp(&s->aa[1],y,aa);
            }
            s->prev=xi;y=.5f*wet;
            a=frost_absf(y);s->env+=(a-s->env)*frost_time_ms(a>s->env?20.0f:180.0f);
            y=frost_hp(&s->dc,y,SL_DC);
            out[i]=y*trim*master*level;
        }
    }
}
#include "../../engine/overlay_far_off.h"
#ifdef FROST_OVERLAYS
FrostBlockFn frost_sl100_block_kernel=0;        /* set by frost_ovl_load to the loaded copy */
#else
FrostBlockFn frost_sl100_block_kernel=frost_sl100_block;
#endif
