#include "tape.h"
#include <string.h>
#include "../common/filters.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"

void frost_tape_reset(FrostTape *s)
{FrostDelay *r=s->ring;memset(s,0,sizeof(*s));s->ring=r;if(r)r->position=r->valid=0;}
FROST_NOINLINE FROST_OVL(tape) float frost_tape_sample(FrostTape *s,float x,const float *p)
{
    FrostDelay *m=s->ring;float desired=p[0]*44.1f,diff,read,echo,a,wet=p[2]*0.01f;unsigned d,i,j;
    if(!s->distance)s->distance=desired;
    diff=desired-s->distance;s->distance+=frost_clampf(diff*(1.0f/1024.0f),-2.0f,2.0f);
    s->phase=frost_phase_advance(s->phase,0.7f);s->flutter=frost_phase_advance(s->flutter,6.3f);
    read=s->distance+0.01f*p[3]*(10.0f*frost_sin01(s->phase)+1.5f*frost_sin01(s->flutter));
    read=frost_clampf(read,2.0f,35280.0f);d=(unsigned)read;echo=0.0f;
    if(d<=m->valid){float older;
        i=m->position>=d?m->position-d:m->position+FROST_DELAY_PCM-d;j=i?i-1u:FROST_DELAY_PCM-1u;
        echo=frost_unpcm16(m->line.pcm[i]);older=d+1u<=m->valid?frost_unpcm16(m->line.pcm[j]):0.0f;
        echo+=(read-(float)d)*(older-echo);}
    a=frost_onepole(7000.0f-55.0f*p[4]);echo=frost_lp(&s->lp[1],frost_lp(&s->lp[0],echo,a),a);
    echo=frost_hp(&s->hp,echo,frost_onepole(80.0f));
    /* Tape compression belongs to the recording path, including first repeat. */
    {float drive=1.0f+0.04f*p[5],v=x+0.95f*p[1]*0.01f*echo;
        m->line.pcm[m->position]=frost_pcm16(frost_soft_clip(v*drive)/drive);}
    if(++m->position==FROST_DELAY_PCM)m->position=0;if(m->valid<FROST_DELAY_PCM)++m->valid;
    return x+(echo-x)*wet;
}

#include "../../engine/overlay_far_off.h"
