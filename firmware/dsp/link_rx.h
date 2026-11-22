#ifndef FROST_LINK_RX_H
#define FROST_LINK_RX_H
#include "firmware/protocol/link_rx_wire.h"
#define FROST_RX_CAPACITY 512u
#define FROST_RX_HEADER_BUDGET 16u
#define FROST_RX_CRC_BUDGET 64u
/* Inter-drain idle threshold: nominal50ms at192MHz, not a calibrated clock. */
#define FROST_RX_GAP_CYCLES 9600000u
typedef struct {
    uint32_t have,progress,crc,ready,last_cycle;
    uint32_t accepted,bad_crc,discarded,expired,overflows,high_water;
} FrostLinkRx;
static inline void frost_rx_add(uint32_t *v,uint32_t n)
{*v=UINT32_MAX-*v<n?UINT32_MAX:*v+n;}
static inline void frost_rx_init(FrostLinkRx *s)
{s->have=s->progress=s->crc=s->ready=s->last_cycle=0;
 s->accepted=s->bad_crc=s->discarded=s->expired=s->overflows=s->high_water=0;}
static inline void frost_rx_expire(FrostLinkRx *s,uint32_t now)
{
    if(s->have && !s->ready && (uint32_t)(now-s->last_cycle)>=FROST_RX_GAP_CYCLES){
        frost_rx_add(&s->discarded,s->have);frost_rx_add(&s->expired,1);
        s->have=s->progress=s->crc=0;
    }
}
static inline void frost_rx_push(FrostLinkRx *s,uint8_t *buf,uint8_t byte,uint32_t now)
{
    frost_rx_expire(s,now);
    if(s->have==FROST_RX_CAPACITY){
        frost_rx_add(&s->discarded,s->have);frost_rx_add(&s->overflows,1);
        s->have=s->progress=s->crc=s->ready=0;
    }
    buf[s->have++]=byte;s->last_cycle=now;
    if(s->have>s->high_water)s->high_water=s->have;
}
/* At most16 candidate headers,64 CRC bytes and one <=511-byte compaction per
 * call. Incremental CRC never dispatches partial data. One ready frame maximum.
 * Caller handles buf+4 before consume(), outside fw_process. */
static inline unsigned frost_rx_next(FrostLinkRx *s,uint8_t *buf)
{
    unsigned pos=0,headers=0,work=0,i,len;
    if(s->ready)return s->ready;
    while(s->have-pos>=4 && headers<FROST_RX_HEADER_BUDGET){
        ++headers;len=(unsigned)buf[pos+1]|((unsigned)buf[pos+2]<<8);
        if(len<6 || len>FROST_RX_CAPACITY || buf[pos+3]!=1){
            ++pos;frost_rx_add(&s->discarded,1);s->progress=s->crc=0;continue;
        }
        if(s->have-pos<len)break;
        while(s->progress<len-4 && work<
#ifdef FROST_CONTROL_SCHEDULER
              (len<=52u?8u:FROST_RX_CRC_BUDGET)
#else
              FROST_RX_CRC_BUDGET
#endif
              ){
            unsigned bit;s->crc^=buf[pos+4+s->progress++];++work;
            for(bit=0;bit<8;++bit)s->crc=(uint8_t)((s->crc&128u)?(s->crc<<1)^7u:s->crc<<1);
        }
        if(s->progress<len-4)break;
        if(s->crc==buf[pos]){s->ready=len;break;}
        frost_rx_add(&s->bad_crc,1);++pos;frost_rx_add(&s->discarded,1);s->progress=s->crc=0;
    }
    if(pos){for(i=pos;i<s->have;++i)buf[i-pos]=buf[i];s->have-=pos;}
    return s->ready;
}
static inline void frost_rx_consume(FrostLinkRx *s,uint8_t *buf)
{
    unsigned i,n=s->ready;
    if(!n)return;
    for(i=n;i<s->have;++i)buf[i-n]=buf[i];s->have-=n;
    s->ready=s->progress=s->crc=0;frost_rx_add(&s->accepted,1);
}
static inline void frost_rx_read(const FrostLinkRx *s,unsigned source,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME};unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==12 && p[0]==0x37 && p[1]==0x7f && p[2]==12 && !p[3]){
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){v[3]=s->have;v[4]=s->accepted;v[5]=s->bad_crc;v[6]=s->discarded;
        v[7]=s->expired;v[8]=s->overflows;v[9]=s->high_water;v[10]=source;}
    reply[0]=0x37;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
