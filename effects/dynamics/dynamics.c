#include "dynamics.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
static float time_coef(float ms){return 1.0f/(1.0f+ms*0.001f*FROST_FS);}

/* Dyna Comp-style: OTA gain cell inside a FEEDBACK loop. The detector reads
 * the cell's own output (rectified, fast attack ~2 ms, slow ~0.5 s release),
 * giving the characteristic squash/pump. SENS raises the drive into the cell
 * (more compression); OUTPUT 50 = unity make-up, 100 = +12 dB. */
void frost_dynacomp_reset(FrostDynaComp *s){s->env=s->lp=0.0f;}
FROST_HOT FROST_NOINLINE FROST_OVL(dynacomp) float frost_dynacomp_sample(FrostDynaComp *s,float x,const float *p)
{
    float sens=p[1]*0.01f,k=6.0f*s->env,y,r;
    y=x*(1.0f+15.0f*sens*sens)/(1.0f+k*k);
    r=frost_absf(y);s->env+=(r-s->env)*(r>s->env?time_coef(2.0f):time_coef(500.0f));
    y=frost_lp(&s->lp,y,frost_onepole(8000.0f));
    return frost_rail(0.8f*y*frost_level(p[0]),8.0f);
}
/* Compressor Plus-style: feed-forward smooth-knee compressor (SUSTAIN sets
 * threshold and ratio together), parallel BLEND with the dry signal, TONE high
 * shelf at ~2 kHz (+/-9 dB, higher = brighter) and LEVEL make-up. PICKUP
 * selects detector timing: SINGLE 3/250 ms, HUMBKR 8/400 ms with a pad. */
void frost_compplus_reset(FrostCompPlus *s){s->env=s->tone=s->shelf=0.0f;s->gain=1.0f;s->n=0;}
FROST_HOT FROST_NOINLINE FROST_OVL(compplus) float frost_compplus_sample(FrostCompPlus *s,float x,const float *p)
{
    float blend=p[2]*0.01f,c,hi;
    unsigned hb=p[4]>0.5f;float in=hb?0.7f*x:x,r=frost_absf(in);
    s->env+=(r-s->env)*(r>s->env?time_coef(hb?8.0f:3.0f):time_coef(hb?400.0f:250.0f));
    /* Gain computer and shelf factor at a 4-sample control rate. */
    if(!(s->n++&3u)){
        float sus=p[0]*0.01f,thr=-12.0f-30.0f*sus,ratio=2.0f+8.0f*sus,over=frost_gain_to_db(s->env)-thr,gdb;
        if(over<=-3.0f)gdb=0.0f;
        else if(over<3.0f){float q=over+3.0f;gdb=(1.0f/ratio-1.0f)*q*q*(1.0f/12.0f);}
        else gdb=(1.0f/ratio-1.0f)*over;
        gdb+=-thr*(1.0f-1.0f/ratio)*0.35f;
        s->gain=frost_db_to_gain(gdb);s->shelf=frost_db_to_gain(18.0f*(p[3]*0.01f-0.5f))-1.0f;
    }
    c=in*s->gain;
    hi=c-frost_lp(&s->tone,c,frost_onepole(2000.0f));
    c+=s->shelf*hi;
    return frost_rail(((1.0f-blend)*x+blend*c)*frost_level(p[1]),8.0f);
}
/* GE-7-style seven-band graphic EQ, 100 Hz..6.4 kHz octave bands, +/-15 dB,
 * plus +/-15 dB LEVEL. Each band is a Regalia-Mitra section,
 * H = (1+K)/2 + (1-K)/2 * A(z), with a fixed one-octave second-order lattice
 * allpass A: exactly K (the knob in dB) at the band centre when the other
 * bands are flat, unity far away. Bands are cascaded; neighbouring boosts
 * interact as on a real graphic EQ. Gains are recomputed only on change. */
const float frost_ge7_centres[FROST_GE7_BANDS]={100.0f,200.0f,400.0f,800.0f,1600.0f,3200.0f,6400.0f};
static const float ge7_k[FROST_GE7_BANDS][2]={
    {-0.999898505f,0.989975845f},{-0.999594040f,0.980051172f},{-0.998376490f,0.960492438f},
    {-0.993511231f,0.922485230f},{-0.974129133f,0.850529951f},{-0.897855135f,0.720276504f},
    {-0.612287686f,0.499205866f}};
void frost_ge7_reset(FrostGE7 *s)
{unsigned i;for(i=0;i<FROST_GE7_BANDS;++i)s->s[i][0]=s->s[i][1]=0.0f;for(i=0;i<=FROST_GE7_BANDS;++i){s->db[i]=0.0f;s->k[i]=1.0f;}}
FROST_HOT FROST_NOINLINE FROST_OVL(ge7) float frost_ge7_sample(FrostGE7 *s,float x,const float *p)
{
    unsigned i;
    /* 31o: both loops fully unrolled (target GCC only). The coefficients then
     * come from the overlay's own literal pool (RAM) rather than the flash
     * table, without loop overhead; expressions and order are unchanged. */
    FROST_UNROLL(8)
    for(i=0;i<=FROST_GE7_BANDS;++i)if(p[i]!=s->db[i]){s->db[i]=p[i];s->k[i]=frost_db_to_gain(p[i]);}
    FROST_UNROLL(7)
    for(i=0;i<FROST_GE7_BANDS;++i){
        float k1=ge7_k[i][0],k2=ge7_k[i][1],v,u,a,K=s->k[i];
        v=x-k2*s->s[i][1];u=v-k1*s->s[i][0];
        a=k2*v+s->s[i][1];
        s->s[i][1]=k1*u+s->s[i][0];s->s[i][0]=u;
        x=0.5f*(1.0f+K)*x+0.5f*(1.0f-K)*a;
    }
    return frost_rail(x*s->k[FROST_GE7_BANDS],12.0f);
}
/* NS-2-style suppressor with an internal sidechain: the detector reads a
 * 100 Hz high-passed copy of the block input (rumble does not hold it open),
 * fast peak attack / 15 ms detector release, open/close hysteresis (close at
 * 0.6 x threshold), a steep 1:5 downward expansion below the closing point
 * instead of a hard chop, 0.5 ms gain attack and DECAY 5 ms..2 s release.
 * THRESH 0..100 spans -80..-20 dBFS. MODE MUTE: enabling the block silences
 * the output (tuning mute); bypassing it returns the dry signal. */
void frost_ns2_reset(FrostNS2 *s){s->hp=s->env=0.0f;s->gain=1.0f;s->open=1;}
FROST_HOT FROST_NOINLINE FROST_OVL(ns2) float frost_ns2_sample(FrostNS2 *s,float x,const float *p)
{
    float sc=frost_absf(frost_hp(&s->hp,x,frost_onepole(100.0f))),thr,target,close;
    if(p[2]>0.5f)return 0.0f;
    s->env+=(sc-s->env)*(sc>s->env?0.5f:time_coef(15.0f));
    thr=frost_db_to_gain(-80.0f+0.6f*p[0]);close=0.6f*thr;
    if(s->open){if(s->env<close)s->open=0;}else if(s->env>thr)s->open=1;
    if(s->open)target=1.0f;else{float r=s->env/close;r*=r;target=r*r;if(target>1.0f)target=1.0f;}
    s->gain+=(target-s->gain)*(target>s->gain?time_coef(0.5f):time_coef(5.0f*frost_exp2(p[1]*0.0864f)));
    return x*s->gain;
}
#include "../../engine/overlay_far_off.h"
