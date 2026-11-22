#include "lowpass.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_lowpass_reset(FrostLowpass *s){memset(s,0,sizeof(*s));s->last[0]=-1.0f;}
FROST_NOINLINE FROST_OVL(lowpass) float frost_lowpass_sample(FrostLowpass *s,float x,const float *p)
{

    unsigned i,n=p[2]>0.5f?2u:1u;
    if(!(s->n++&15u) && (p[0]!=s->last[0] || p[1]!=s->last[1])){
        float ph=p[0]/FROST_FS,sn=frost_sin01(ph),co=frost_sin01(ph+0.25f);
        s->c[0]=sn/(1.0f+co);s->c[1]=1.0f/p[1];s->c[2]=1.0f/(1.0f+s->c[0]*(s->c[0]+s->c[1]));
        s->last[0]=p[0];s->last[1]=p[1];
    }
    for(i=0;i<n;++i){float *z=s->z+2*i,g=s->c[0],v1=(z[0]+g*(x-z[1]))*s->c[2],v2=z[1]+g*v1;
        z[0]=2.0f*v1-z[0];z[1]=2.0f*v2-z[1];x=v2;
    }
    return x;
}
#include "../../engine/overlay_far_off.h"
