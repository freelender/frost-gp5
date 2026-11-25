#include "pingpong.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/stereo.h"
#include "../../engine/overlay_far_on.h"
/* PING: ping-pong delay with one delay line read at two points.
 *
 * The line stores w = x + F^2 * LP(w delayed 2T). Reading it at T gives the
 * odd echoes (T, 3T, 5T ... at 1, F^2, F^4 ...: left); reading it at 2T and
 * scaling by F gives the even echoes (2T, 4T ... at F, F^3 ...: right). So each
 * echo is F times the one before and they alternate sides, with a single
 * write and two reads per sample and no second line (the 800 ms ring holds
 * two hops, so TIME goes up to 400 ms). Repeats pass a ~7 kHz one-pole per
 * hop, like DIGI's delay path; F.BACK up to 0.97, E.LEVEL echo level, direct
 * sound always at unity.
 *
 * Stereo output (and nothing after this block that needs mono): left gets
 * the odd echoes, right the even ones, spread by WIDTH (0: the same echoes on
 * both sides). The mid carries dry + both echo streams at (1 - WIDTH/2), the
 * side WIDTH/2 x (left - right) echoes. Mono output (or folded): every echo
 * at full level into the mono signal, an ordinary delay. */
#define PP_A 0.49933186f   /* frost_onepole(7000.0f) */
void frost_pingpong_reset(FrostPingPong *s)
{
    s->distance=0.0f;s->lp[0]=s->lp[1]=0.0f;
    if(s->ring){s->ring->position=s->ring->valid=0;s->ring->distance=0.0f;}
}
static inline __attribute__((always_inline)) float pp_tap(const FrostDelay *m,float distance)
{
    unsigned d=(unsigned)distance,i,j;float f=distance-(float)d,a;
    if(d+1u>m->valid || d+1u>=FROST_DELAY_PCM)return 0.0f;
    i=m->position>=d?m->position-d:m->position+FROST_DELAY_PCM-d;j=i?i-1u:FROST_DELAY_PCM-1u;
    a=frost_unpcm16(m->line.pcm[i]);return a+f*(frost_unpcm16(m->line.pcm[j])-a);
}
FROST_HOT FROST_NOINLINE FROST_OVL(pingpong) float frost_pingpong_sample(FrostPingPong *s,float x,const float *p)
{
    FrostDelay *m=s->ring;float desired=p[0]*44.1f,fb=0.97f*0.01f*p[1],lvl=1.2f*0.01f*p[2],ea,eb,d;
    if(desired>FROST_PINGPONG_MAX_TIME*44.1f)desired=FROST_PINGPONG_MAX_TIME*44.1f;
    if(desired<1.0f)desired=1.0f;
    /* Time changes glide (~0.2 s, at least 0.25 sample per sample) like DIGI. */
    d=desired-s->distance;
    if(s->distance==0.0f)s->distance=desired;
    else{float step=d*(1.0f/1024.0f);if(step<0.25f && step>-0.25f)step=d>0.25f?0.25f:d< -0.25f?-0.25f:d;s->distance+=step;}
    ea=frost_lp(&s->lp[0],pp_tap(m,s->distance),PP_A);
    eb=fb*frost_lp(&s->lp[1],pp_tap(m,2.0f*s->distance),PP_A);
    m->line.pcm[m->position]=frost_pcm16(x+fb*eb);
    if(++m->position==FROST_DELAY_PCM)m->position=0;
    if(m->valid<FROST_DELAY_PCM)++m->valid;
    if(!frost_side_on())return frost_rail(x+lvl*(ea+eb),8.0f);
    {
        float w=0.01f*p[3];
        frost_side_write(frost_rail(0.5f*lvl*w*(ea-eb),8.0f));
        return frost_rail(x+lvl*(ea+eb)*(1.0f-0.5f*w),8.0f);
    }
}
#include "../../engine/overlay_far_off.h"
