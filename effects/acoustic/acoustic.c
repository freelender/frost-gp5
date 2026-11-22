#include "acoustic.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_acoustic_reset(FrostAcoustic *s){memset(s,0,sizeof(*s));}
FROST_NOINLINE FROST_OVL(acoustic) float frost_acoustic_sample(FrostAcoustic *s,float x,const float *p)
{
    float dry=x,body=p[0]*0.01f,top=p[1]*0.01f,y;
    float a,attack,low,wood,box,presence,edge,harmonic;
    int mode=(int)p[2]; /* 0 standard, 1 jumbo, 2 piezo (stored), 3 enhance */

    /* Body air/wood modes, driven in parallel. Normalize each band by its
     * damping so BODY changes resonance amount rather than runaway gain.
     * Moderate Q leaves a short woody decay, not a wah-like sustained peak.
     * Constants/branches stay inside the relocatable overlay (no table). */
    x=frost_hp(&s->hp,x,frost_onepole(65.0f));
    low=0.22f*frost_svf(s->z,x,frost_svf_g(mode==1?95.0f:mode==2?150.0f:110.0f),0.22f);
    wood=0.28f*frost_svf(s->z+2,x,frost_svf_g(mode==1?190.0f:mode==2?310.0f:235.0f),0.28f);
    box=1.1f*frost_svf(s->z+4,x,frost_svf_g(mode==2?1100.0f:800.0f),1.1f);
    presence=0.8f*frost_svf(s->z+6,x,frost_svf_g(mode==2?2400.0f:3200.0f),0.8f);
    y=x-(mode==2?0.42f:0.72f)*box;
    y+=(0.15f+1.25f*body)*(mode==2?0.28f:1.0f)*
        ((mode==1?0.85f:0.65f)*low+0.65f*wood);

    /* TOP is soundboard/pick detail, not an unbounded high shelf. The fast
     * and slow followers emphasize note onset without compressing the body.
     * ENHANCE gives more onset/presence; PIEZO has less body and more quack. */
    a=frost_absf(x);
    s->fast+=(a-s->fast)*frost_time_ms(a>s->fast?0.5f:8.0f);
    s->slow+=(a-s->slow)*frost_time_ms(a>s->slow?18.0f:65.0f);
    attack=frost_clampf((s->fast-s->slow)/(0.02f+s->fast),0.0f,1.0f);
    edge=x-frost_lp(&s->top,x,frost_onepole(1800.0f));
    y+=((2.0f*top-0.65f)+top*attack*(mode==3?1.5f:0.55f))*edge;
    y+=(mode==2?0.65f:mode==3?0.35f:0.12f)*top*presence;

    /* Gentle, band-limited even-harmonic excitation adds upper detail to a
     * magnetic pickup. Smooth rectification avoids a hard clipping knee;
     * remove its DC and roll off the generated upper band. No noise source. */
    a=frost_lp(&s->excite_lp,edge,frost_onepole(2800.0f));
    harmonic=a*a/(0.08f+frost_absf(a));
    harmonic=frost_hp(&s->excite_hp,harmonic,frost_onepole(900.0f));
    harmonic=frost_lp(&s->excite_out,harmonic,frost_onepole(6500.0f));
    y+=top*(mode==3?0.45f:0.25f)*harmonic;
    y=frost_lp(&s->air,y,frost_onepole(12000.0f));
    return (dry+(y-dry)*p[3]*0.01f)*frost_level(p[4]);
}

#include "../../engine/overlay_far_off.h"
