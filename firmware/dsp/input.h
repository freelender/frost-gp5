#ifndef FROST_INPUT_H
#define FROST_INPUT_H
#include "engine/hot_code.h"
#include "firmware/protocol/input_wire.h"
#ifdef FROST_AUDIO_ADC
#if FROST_AUDIO_ADC != 1
#error FROST_AUDIO_ADC must be1 when enabled
#endif
#define FROST_AUDIO_SOURCE 2u
#else
#define FROST_AUDIO_SOURCE 1u
#endif
/* Measures the platform mono argument, not the individual ADC/DMA lanes. */
typedef struct {uint32_t frames,peak,rails,nonzero;int32_t minimum,maximum;} FrostInputMeter;
static void frost_input_init(FrostInputMeter *m)
{m->frames=m->peak=m->rails=m->nonzero=0;m->minimum=2147483647;m->maximum=(-2147483647-1);}
static void frost_input_count(uint32_t *n){if(*n!=UINT32_MAX)++*n;}
FROST_HOT FROST_CORE static float frost_input_sample(FrostInputMeter *m,int32_t q)
{
    uint32_t a=q<0?0u-(uint32_t)q:(uint32_t)q;
    frost_input_count(&m->frames);if(q)frost_input_count(&m->nonzero);
    if(q==2147483647 || q==(-2147483647-1))frost_input_count(&m->rails);
    if(q<m->minimum)m->minimum=q;if(q>m->maximum)m->maximum=q;if(a>m->peak)m->peak=a;
    /* Signed Q31 to binary32; INT32_MAX rounds to +1.0f. No integer negation,
     * signed overflow, division, allocation or IO, including INT32_MIN. */
    return (float)q*4.656612873077392578125e-10f;
}
static void frost_input_read(const FrostInputMeter *m,unsigned mute,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME};unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==12 && p[0]==0x36 && p[1]==0x7f && p[2]==12 && !p[3]){
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        v[3]=m->frames;v[4]=m->frames?(uint32_t)m->minimum:0;v[5]=m->frames?(uint32_t)m->maximum:0;
        v[6]=m->peak;v[7]=m->rails;v[8]=m->nonzero;v[9]=!!mute;v[10]=FROST_AUDIO_SOURCE;
    }
    reply[0]=0x36;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
