#include "overdrive.h"
#include "../../engine/hot_code.h"
void frost_overdrive_reset(FrostOverdrive *s)
{ s->previous=s->highpass=s->tone=0.0f; }
FROST_HOT float frost_overdrive_sample(FrostOverdrive *s,float input,const float *p)
{
    float driven,shaped,a;
    /* Fixed DC blocker before the nonlinearity; no parameter-dependent design
     * or transcendental functions in the sample loop. */
    s->highpass=input-s->previous+0.995f*s->highpass;s->previous=input;
    driven=s->highpass*p[0];
    /* Odd C1 cubic joins the constant rails with zero slope at +/-1.
     * Native-rate processing is NOT alias-free; see README quality policy. */
    if(driven>=1.0f)shaped=1.0f;
    else if(driven<=-1.0f)shaped=-1.0f;
    else shaped=driven*(1.5f-0.5f*driven*driven);
    a=0.03f+0.47f*p[1]*p[1];
    s->tone+=a*(shaped-s->tone);
    return s->tone*p[2];
}
