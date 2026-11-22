#include "multiband.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_multiband_reset(FrostMultiband *s){unsigned i;memset(s,0,sizeof(*s));for(i=0;i<3;++i)s->gain[i]=s->target[i]=1.0f;}
FROST_NOINLINE FROST_OVL(multiband) float frost_multiband_sample(FrostMultiband *s,float x,const float *p)
{
    float b[3],y=0.0f;unsigned i,update=!(s->n++&15u);
    if(update){s->attack=frost_time_ms(p[4]);s->release=frost_time_ms(p[5]);}
    frost_split3(s->split,x,b);
    for(i=0;i<3;++i){float a=frost_absf(b[i]);s->env[i]+=(a-s->env[i])*(a>s->env[i]?s->attack:s->release);
        if(update){float over=frost_gain_to_db(s->env[i])-p[i],db=over>0.0f?over*(1.0f/p[3]-1.0f):0.0f;s->target[i]=frost_db_to_gain(db);}
        s->gain[i]+=(s->target[i]-s->gain[i])*0.01f;y+=b[i]*s->gain[i];
    }
    return (x+(y-x)*p[6]*0.01f)*frost_level(p[7]);
}

#include "../../engine/overlay_far_off.h"
