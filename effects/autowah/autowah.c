#include "autowah.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_autowah_reset(FrostAutowah *s){memset(s,0,sizeof(*s));}
FROST_NOINLINE FROST_OVL(autowah) float frost_autowah_sample(FrostAutowah *s,float x,const float *p)
{
    float a=frost_absf(x),sweep,y;int mode=(int)p[3];
    s->env+=(a-s->env)*(a>s->env?frost_time_ms(2.0f):frost_time_ms(p[2]));
    s->phase=frost_phase_advance(s->phase,p[6]);
    sweep=frost_clampf(s->env*p[0]*0.15f,0.0f,1.0f);
    if(mode==1)sweep=1.0f-sweep;
    if(mode==4)sweep=0.5f+0.5f*frost_triangle01(s->phase);
    if(!(s->n++&7u)){
        float f;
        if(mode==3){ /* Public vowel formant targets, A E I O U; no tables in relocated code. */
            int v1=(int)p[4],v2=(int)p[5];
            float lo1=v1==0?800.0f:v1==1?500.0f:v1==2?300.0f:v1==3?500.0f:350.0f;
            float lo2=v2==0?800.0f:v2==1?500.0f:v2==2?300.0f:v2==3?500.0f:350.0f;
            float hi1=v1==0?1150.0f:v1==1?1900.0f:v1==2?2300.0f:v1==3?900.0f:700.0f;
            float hi2=v2==0?1150.0f:v2==1?1900.0f:v2==2?2300.0f:v2==3?900.0f:700.0f;
            s->g[0]=frost_svf_g(lo1+sweep*(lo2-lo1));s->g[1]=frost_svf_g(hi1+sweep*(hi2-hi1));s->damping=0.18f;
        }else {f=180.0f+18.0f*p[1]+sweep*2200.0f;s->g[0]=frost_svf_g(f);s->damping=mode==2?0.18f:0.4f;}
    }
    y=frost_svf(s->z,x,s->g[0],s->damping)*s->damping;
    if(mode==3)y=0.65f*y+0.35f*s->damping*frost_svf(s->z+2,x,s->g[1],s->damping);
    return x+(2.0f*y-x)*p[7]*0.01f;
}

#include "../../engine/overlay_far_off.h"
