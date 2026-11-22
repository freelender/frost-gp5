#include "dsp_usage.h"
void frost_dsp_usage_init(FrostDSPUsage *s,uint32_t now)
{s->peak=s->last_report=0;s->mark=now;s->pending=s->available=s->valid=s->percent=0;}
void frost_dsp_usage_report(FrostDSPUsage *s,const uint8_t *p,unsigned n,uint32_t now)
{
    uint32_t word,code,cycles;
    if(!p || n<8 || p[0]!=1 || p[1]!=0x40)return;
    if(n==28 && p[2]==28 && !p[3] && p[4]=='F' && p[5]=='L' && p[6]=='T' && p[7]=='1'){
        s->available=s->pending=0;s->peak=0;return;
    }
    if(n!=24 || p[2]!=24 || p[3] || p[4]!='F' || p[5]!='R' || p[6]!='X' || p[7]!='1')return;
    word=(uint32_t)p[16]|((uint32_t)p[17]<<8)|((uint32_t)p[18]<<16)|((uint32_t)p[19]<<24);
    code=(word>>FROST_DSP_USAGE_SHIFT)<<7;
    code|=p[20]&127u;
    if(!code){s->available=s->pending=0;s->peak=0;return;}
    cycles=(code-1u)*FROST_DSP_USAGE_UNIT;
    if(!s->pending || cycles>s->peak)s->peak=cycles;
    s->pending=s->available=1;s->last_report=now;
}
unsigned frost_dsp_usage_tick(FrostDSPUsage *s,uint32_t now)
{
    unsigned valid,percent,changed;
    if((uint32_t)(now-s->mark)<FROST_DSP_USAGE_REFRESH_MS)return 0;
    s->mark=now;valid=s->available && (uint32_t)(now-s->last_report)<=FROST_DSP_USAGE_STALE_MS;
    percent=s->pending?(s->peak*100u+FROST_DSP_USAGE_BUDGET/2u)/FROST_DSP_USAGE_BUDGET:s->percent;
    changed=valid!=s->valid || (valid && percent!=s->percent);
    s->valid=valid;s->percent=percent;s->pending=0;s->peak=0;return changed;
}
void frost_dsp_usage_text(const FrostDSPUsage *s,char *out)
{
    unsigned i=0,p=s->percent;
    out[i++]='D';out[i++]='S';out[i++]='P';out[i++]=' ';
    if(!s->valid){out[i++]='-';out[i++]='-';}
    else{
        if(p>=100)out[i++]=(char)('0'+p/100u);
        if(p>=10)out[i++]=(char)('0'+p/10u%10u);
        out[i++]=(char)('0'+p%10u);
    }
    out[i++]='%';out[i]=0;
}
