#include "delay.h"
#include "../../engine/hot_code.h"
uint16_t frost_half_store(float x){return frost_half_store_inl(x);}
float frost_half_load(uint16_t h){return frost_half_load_inl(h);}
void frost_delay_reset(FrostDelay *s)
{s->position=s->valid=0;s->distance=0.0f;}
FROST_HOT static float delay_tap(const FrostDelay *s,unsigned distance)
{
    unsigned index;
    if(distance>s->valid)return 0.0f;
    index=s->position>=distance?s->position-distance:s->position+FROST_DELAY_SAMPLES-distance;
    return frost_half_load_inl(s->line.half[index]);
}
FROST_HOT FROST_AUDIO_RAM float frost_delay_sample(FrostDelay *s,float input,const float *p)
{
    float desired=p[0]*44.1f,fraction,near,far,echo,write;
    unsigned distance;
    if(s->distance==0.0f)s->distance=desired;
    else if(desired>s->distance+0.25f)s->distance+=0.25f;
    else if(desired<s->distance-0.25f)s->distance-=0.25f;
    else s->distance=desired;
    distance=(unsigned)s->distance;fraction=s->distance-(float)distance;
    near=delay_tap(s,distance);far=delay_tap(s,distance+1u);
    echo=near+fraction*(far-near);
    /* Feedback rail protects storage under high upstream gain. It is part of
     * the effect, not a substitute for final output/headroom qualification. */
    write=input+p[1]*echo;
    if(write>16.0f)write=16.0f;else if(write< -16.0f)write=-16.0f;
    s->line.half[s->position]=frost_half_store_inl(write);
    if(++s->position==FROST_DELAY_SAMPLES)s->position=0;
    if(s->valid<FROST_DELAY_SAMPLES)++s->valid;
    return input+p[2]*(echo-input);
}
