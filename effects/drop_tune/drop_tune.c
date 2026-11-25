#include "drop_tune.h"
#include <stddef.h>
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* Overlay module (FROST_OVL(drop)): everything below is self-contained
 * arithmetic, no calls out of the module; the pitch-shifting functions are
 * this module's own copy (drop_ps_*). */
#define FROST_PS_PREFIX drop
#define FROST_PS_FN FROST_HOT FROST_NOINLINE FROST_OVL(drop) static
#define FROST_PS_SMALL FROST_COLD
#define FROST_PS_LOWER_ONLY 1
#include "../common/pitch_shift_impl.h"
#undef FROST_PS_PREFIX
#undef FROST_PS_FN
#undef FROST_PS_LOWER_ONLY
typedef char frost_drop_history_fits[(sizeof(FrostPsHistory)==sizeof(FrostModulation))?1:-1];
/* 2^(-n/12), n = 1..6 (the only table: DSP code flash has no room for more). */
static const float drop_ratio[6]={0.94387431f,0.89089872f,0.84089642f,0.79370053f,0.74915354f,0.70710678f};
/* Splicer settings, kept in the slot state and rebuilt when DROP changes;
 * field stores keep the constants in this module's code. */
static inline __attribute__((always_inline)) void drop_config(FrostPsConfig *c,float ratio)
{
    c->ratio=ratio;c->dmin=FROST_DROP_MIN_DISTANCE;c->dmax=FROST_DROP_MAX_DISTANCE;c->jmin=FROST_DROP_JUMP_MIN;c->jmax=FROST_DROP_JUMP_MAX;
    c->good=FROST_DROP_GOOD;c->every=FROST_DROP_EVERY;c->phase=0u;c->refresh=1u;c->fade_min=128u;c->fade_max=FROST_DROP_FADE_MAX;
}
void frost_drop_reset(FrostDropTune *s)
{
    /* Everything after the workspace pointer starts at zero: input filters,
     * an idle splicer and cfg.ratio = 0, so the first sample builds cfg. */
    if(s->ring)frost_ps_zero(s->ring,sizeof(FrostPsHistory));
    frost_ps_zero(&s->in,sizeof *s-offsetof(FrostDropTune,in));
    s->head.d=s->head.d_old=FROST_DROP_MIN_DISTANCE;
}
FROST_HOT FROST_NOINLINE FROST_OVL(drop) float frost_drop_sample(FrostDropTune *s,float x,const float *p)
{
    FrostPsHistory *h=(FrostPsHistory *)(void *)s->ring;unsigned k=(unsigned)(p[0]+0.5f);float y;
    if(k>5u)k=5u;
    if(s->cfg.ratio!=drop_ratio[k])drop_config(&s->cfg,drop_ratio[k]);
    if(drop_ps_input(&s->in,h,x,0u,FROST_DROP_HOLD) && s->head.d>FROST_DROP_MIN_DISTANCE+32.0f)
        frost_ps_jump(&s->head,FROST_DROP_MIN_DISTANCE,48u);
    y=drop_ps_splice(&s->head,h,&s->cfg,!h->hold);
    return x+p[1]*0.01f*(y-x);
}
#include "../../engine/overlay_far_off.h"
