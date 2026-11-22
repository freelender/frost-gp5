#include "vintage_fuzz.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_vintage_fuzz_reset(FrostVintageFuzz *s){memset(s,0,sizeof(*s));}
FROST_NOINLINE FROST_OVL(vintage_fuzz) float frost_vintage_fuzz_sample(FrostVintageFuzz *s,float x,const float *p)
{
    float drive=1.0f+0.9f*p[0],bias=0.05f+0.003f*p[2],y=0.0f;unsigned i;
    /* Two substeps through cascaded asymmetric germanium curves. Input-level
     * dependent saturation gives guitar-volume cleanup; no fake digital gate. */
    for(i=0;i<2;++i){float v=i?x:0.5f*(x+s->previous);
        v=frost_germanium_clip(v*drive+bias)-frost_germanium_clip(bias);
        v=frost_germanium_clip(v*3.0f-bias)+frost_germanium_clip(bias);
        y=frost_lp(&s->lp,v,0.38f);
    }
    s->previous=x;y=frost_hp(&s->hp,y,frost_onepole(30.0f));
    return y*frost_level(p[1]);
}

#include "../../engine/overlay_far_off.h"
