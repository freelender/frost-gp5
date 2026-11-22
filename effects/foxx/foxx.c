#include "foxx.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_foxx_reset(FrostFoxx *s){memset(s,0,sizeof(*s));}
FROST_NOINLINE FROST_OVL(foxx) float frost_foxx_sample(FrostFoxx *s,float x,const float *p)
{
    float y,wet=p[6]*0.01f;unsigned i,stages=p[3]>0.5f?10u:6u;
    s->phase=frost_phase_advance(s->phase,p[0]);
    if(!(s->n++&7u)){
        float sweep=p[5]>0.5f?p[4]*0.01f:0.5f+0.005f*p[1]*frost_triangle01(s->phase);
        s->coef=frost_allpass_coef(140.0f+2700.0f*sweep*sweep);
    }
    y=x+0.006f*p[2]*s->feedback;
    for(i=0;i<stages;++i)y=frost_allpass1(s->ap+i,y,s->coef);
    s->feedback=frost_rail(y,4.0f);
    return x+(0.5f*(x+y)-x)*wet;
}

#include "../../engine/overlay_far_off.h"
