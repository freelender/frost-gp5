#ifndef FROST_TUNER_CLIENT_H
#define FROST_TUNER_CLIENT_H
/* Stage 2c tuner, controller side (docs/User%20Guide.md): keeps the DSP tuner state
 * equal to `want` (START/STOP), reads the pitch about every 90 ms while on, and
 * turns frequency into note / cents against the reference A4. One request per
 * serialized editor pass, like the other clients. */
#include "editor_client.h"
#include "firmware/protocol/tuner_wire.h"
#define TUNER_POLL_MS 90u
typedef struct {
    unsigned want,awaiting,dsp_active,known,retries,op;
    uint32_t seq,next_poll;float freq,clarity;
    /* display state */
    int note;float cents;unsigned have;
    uint8_t saved[FROST_TUNER_REQUEST_BYTES];
} FrostTunerClient;
static void tuner_client_init(FrostTunerClient *c,uint32_t now)
{c->want=c->awaiting=c->dsp_active=c->known=c->retries=c->op=0;c->seq=0;c->next_poll=now;c->freq=c->clarity=0.0f;c->note=0;c->cents=0.0f;c->have=0;}
static int tuner_client_due(const FrostTunerClient *c,uint32_t now)
{return c->awaiting==0 && (!c->known || c->want!=c->dsp_active || (c->want && (int32_t)(now-c->next_poll)>=0));}
static unsigned tuner_client_next(FrostTunerClient *c,FrostEditor *e,uint8_t *p,uint32_t now)
{
    uint32_t token;unsigned i;
    if(c->awaiting || !(token=editor_token(e)))return 0;
    c->op=!c->known?FROST_TUNER_READ:c->want!=c->dsp_active?(c->want?FROST_TUNER_START:FROST_TUNER_STOP):FROST_TUNER_READ;
    editor_header(c->saved,FROST_TUNER_COMMAND,FROST_TUNER_REQUEST_BYTES,token);frost_wire_put32(c->saved+12,c->op);
    for(i=0;i<FROST_TUNER_REQUEST_BYTES;++i)p[i]=c->saved[i];
    c->next_poll=now+TUNER_POLL_MS;return FROST_TUNER_REQUEST_BYTES;
}
/* log2 for positive normal floats, |error| < 1e-6 (atanh series on the mantissa). */
static float tuner_log2(float x)
{
    union {float f;uint32_t u;} v;float m,s,s2,ln;int e;
    v.f=x;e=(int)((v.u>>23)&255u)-127;v.u=(v.u&0x007fffffu)|0x3f800000u;m=v.f;
    if(m>1.41421356f){m*=0.5f;++e;}
    s=(m-1.0f)/(m+1.0f);s2=s*s;
    ln=2.0f*s*(1.0f+s2*(1.0f/3.0f+s2*(1.0f/5.0f+s2*(1.0f/7.0f+s2*(1.0f/9.0f)))));
    return (float)e+ln*1.44269504f;
}
static void tuner_client_update(FrostTunerClient *c,unsigned ref_hz)
{
    float n,c_new;int note;
    if(c->freq<20.0f || c->clarity<0.5f){c->have=0;return;}
    n=12.0f*tuner_log2(c->freq/(float)ref_hz)+69.0f;
    note=(int)(n+(n>=0.0f?0.5f:-0.5f));c_new=100.0f*(n-(float)note);
    if(!c->have || note!=c->note || c_new-c->cents>30.0f || c->cents-c_new>30.0f)c->cents=c_new;   /* new note: jump */
    else c->cents+=0.35f*(c_new-c->cents);                                                          /* same note: settle */
    c->note=note;c->have=1;
}
/* 1 = the display changed. */
static int tuner_client_reply(FrostTunerClient *c,const uint8_t *p,unsigned n,unsigned ref_hz)
{
    union {uint32_t u;float f;} fq,cl;uint32_t seq;
    c->awaiting=0;
    if(!p || n!=FROST_TUNER_REPLY_BYTES || p[0]!=0x4b || p[1]!=0x7f || p[2]!=n || p[3] ||
       frost_wire_get32(p+4)!=1u || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;
    c->known=1;c->dsp_active=frost_wire_get32(p+16);seq=frost_wire_get32(p+20);
    fq.u=frost_wire_get32(p+24);cl.u=frost_wire_get32(p+28);
    if(c->dsp_active && seq!=c->seq){c->seq=seq;c->freq=fq.f;c->clarity=cl.f;tuner_client_update(c,ref_hz);return 1;}
    return 0;
}
#endif
