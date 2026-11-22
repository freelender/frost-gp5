#include "modulation.h"
#include "../../engine/hot_code.h"
void frost_modulation_reset(FrostModulation *s)
{s->position=s->valid=0;s->phase=s->distance=0.0f;}
FROST_HOT static float modulation_tap(const FrostModulation *s,unsigned distance)
{
    unsigned i;if(distance>s->valid)return 0.0f;
    i=s->position>=distance?s->position-distance:s->position+FROST_MOD_SAMPLES-distance;
    return s->line.samples[i];
}
FROST_HOT FROST_LARGE_ONLY float frost_modulation_sample(FrostModulation *s,float input,const float *p,unsigned flanger)
{
    /* Cubic easing of a triangle: periodic C1 LFO in [-1,1], no sin/table IO.
     * Rate Hz [.05,5], depth [0,1]. Chorus p2=mix[0,1], flanger p2=regen[-.9,.9]. */
    float triangle=s->phase<0.5f?4.0f*s->phase-1.0f:3.0f-4.0f*s->phase;
    float lfo=triangle*(1.5f-0.5f*triangle*triangle);
    float desired=(flanger?3.0f+2.5f*p[1]*lfo:15.0f+10.0f*p[1]*lfo)*44.1f;
    float fraction,near,far,echo,write,mix=flanger?0.5f:p[2];unsigned distance;
    if(s->distance==0.0f)s->distance=desired;
    else if(desired>s->distance+0.5f)s->distance+=0.5f;
    else if(desired<s->distance-0.5f)s->distance-=0.5f;
    else s->distance=desired;
    distance=(unsigned)s->distance;fraction=s->distance-(float)distance;
    near=modulation_tap(s,distance);far=modulation_tap(s,distance+1u);
    echo=near+fraction*(far-near);
    write=input+(flanger?p[2]*echo:0.0f);
    if(write>16.0f)write=16.0f;else if(write< -16.0f)write=-16.0f;
    s->line.samples[s->position]=write;
    if(++s->position==FROST_MOD_SAMPLES)s->position=0;
    if(s->valid<FROST_MOD_SAMPLES)++s->valid;
    s->phase+=p[0]*(1.0f/44100.0f);if(s->phase>=1.0f)s->phase-=1.0f;
    return input+mix*(echo-input);
}
