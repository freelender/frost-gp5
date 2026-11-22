#ifndef FROST_OUTPUT_H
#define FROST_OUTPUT_H
#include "engine/hot_code.h"
#include <stdint.h>
#include "firmware/protocol/output_wire.h"
typedef struct {
    uint32_t frames,outside_unity,pcm_rails,invalid;
    float peak;
} FrostOutputMeter;
static void frost_output_init(FrostOutputMeter *m)
{m->frames=m->outside_unity=m->pcm_rails=m->invalid=0;m->peak=0.0f;}
static void frost_output_count(uint32_t *n){if(*n!=UINT32_MAX)++*n;}
/* The playable adapter carries binary32 samples in its existing int32 scratch
 * buffers until the master stage. Use unions, never pointer aliasing. These
 * words are NOT PCM and must reach fw_output_block before the audio DMA. */
static inline int32_t frost_output_pack(float y)
{union {float f;int32_t i;} v;v.f=y;return v.i;}
static inline float frost_output_unpack(int32_t y)
{union {float f;int32_t i;} v;v.i=y;return v.f;}
/* Same truncating binary32-to-signed-Q31 conversion as the muted adapter.
 * Validate before casting: NaN/infinity and outside the chain's +/-16 envelope
 * become silence. Meter before the hard mute; no IO or allocation here. */
FROST_HOT FROST_CORE static int32_t frost_output_sample(FrostOutputMeter *m,float y)
{
    float a;frost_output_count(&m->frames);
    if(!(y>=-16.0f && y<=16.0f)){frost_output_count(&m->invalid);return 0;}
    a=y<0.0f?-y:y;if(a>m->peak)m->peak=a;
    if(a>1.0f)frost_output_count(&m->outside_unity);
    if(y>=0.999999940395355224609375f){frost_output_count(&m->pcm_rails);return 2147483520;}
    if(y<=-1.0f){frost_output_count(&m->pcm_rails);return (-2147483647-1);}
    return (int32_t)(y*2147483648.0f);
}
/* Serialized with fw_process; reply storage cannot alias the meter. Decode
 * first so request/reply may overlap or be unaligned. Errors disclose no data. */
static void frost_output_read(const FrostOutputMeter *m,uint32_t faults,unsigned mute,unsigned source,
                              const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t token=0,status=FROST_SLOT_BAD_FRAME,v[11]={1,0,0};unsigned i;
    if(p && n>=12)token=frost_wire_get32(p+8);
    if(p && n==12 && p[0]==0x35 && p[1]==0x7f && p[2]==12 && !p[3]){
        if(frost_wire_get32(p+4)!=1)status=FROST_SLOT_BAD_VERSION;
        else if(!token)status=FROST_SLOT_BAD_REQUEST_ID;
        else status=FROST_SLOT_APPLIED;
    }
    v[1]=token;v[2]=status;
    if(status==FROST_SLOT_APPLIED){
        union {float f;uint32_t u;} peak;peak.f=m->peak;
        v[3]=m->frames;v[4]=peak.u;v[5]=m->outside_unity;v[6]=m->pcm_rails;
        v[7]=m->invalid;v[8]=faults;v[9]=!!mute;v[10]=source;
    }
    reply[0]=0x35;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
