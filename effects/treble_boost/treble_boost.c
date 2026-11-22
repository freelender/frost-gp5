#include "treble_boost.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_treble_boost_reset(FrostTrebleBoost *s){memset(s,0,sizeof(*s));}
FROST_NOINLINE FROST_OVL(treble_boost) float frost_treble_boost_sample(FrostTrebleBoost *s,float x,const float *p)
{
    float cutoff=350.0f+5.5f*p[1],drive=1.0f+0.35f*p[0],bias=0.15f,y;
    y=frost_hp(&s->hp,x,frost_onepole(cutoff));
    y=frost_germanium_clip(drive*y+bias)-frost_germanium_clip(bias);
    y=frost_hp(&s->dc,y,frost_onepole(25.0f));
    y=frost_lp(&s->lp,y,frost_onepole(12000.0f));
    return y*frost_level(p[2]);
}

#include "../../engine/overlay_far_off.h"
