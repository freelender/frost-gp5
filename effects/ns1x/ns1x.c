#include "ns1x.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_ns1x_reset(FrostNS1X *s){unsigned i;memset(s,0,sizeof(*s));for(i=0;i<3;++i)s->gain[i]=1.0f;}
FROST_NOINLINE FROST_OVL(ns1x) float frost_ns1x_sample(FrostNS1X *s,float x,const float *p)
{
    float b[3],y=0.0f;unsigned i,gate=p[3]>0.5f;
    if(p[3]>1.5f)return 0.0f;
    if(!(s->n++&15u)){s->thr=frost_db_to_gain(-90.0f+0.7f*p[0]);s->floor=frost_db_to_gain(-0.8f*p[2]);s->release=frost_time_ms(5.0f+14.95f*p[1]);}
    frost_split3(s->split,x,b);
    for(i=0;i<3;++i){float a=frost_absf(b[i]),v,target;
        s->env[i]+=(a-s->env[i])*(a>s->env[i]?frost_time_ms(0.15f):frost_time_ms(12.0f));
        v=s->env[i]/s->thr;target=gate?(v>1.0f?1.0f:0.0f):frost_clampf(v*v,0.0f,1.0f);
        target=s->floor+(1.0f-s->floor)*target;
        s->gain[i]+=(target-s->gain[i])*(target>s->gain[i]?frost_time_ms(0.2f):s->release);
        y+=b[i]*s->gain[i];
    }
    return y;
}

#include "../../engine/overlay_far_off.h"
