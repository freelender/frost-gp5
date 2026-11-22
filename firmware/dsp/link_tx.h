#ifndef FROST_LINK_TX_H
#define FROST_LINK_TX_H
#include "firmware/protocol/link_tx_wire.h"
#include "engine/hot_code.h"
#define FROST_TX_CAPACITY 1024u
#define FROST_TX_MAX_PAYLOAD 508u
#define FROST_TX_DISPATCH_RESERVE 516u /* One maximum512-byte frame plus literal4-byte ACK. */
#define FROST_TX_BYTE_BUDGET 16u
typedef struct {
    uint8_t data[FROST_TX_CAPACITY];
    uint32_t head,have,frames,dropped,written,blocked,high_water,deferred;
#ifdef FROST_CONTROL_SCHEDULER
    /* Append only: retain the installed queue prefix and earlier BSS objects. */
    uint32_t report_pending,prefer_report,rx_bytes_seen,last_action,control_flags;
#endif
} FrostLinkTx;
static FrostLinkTx frost_link_tx;
static inline FROST_CTL_INLINE void frost_tx_count(uint32_t *v){if(*v!=UINT32_MAX)++*v;}
static inline void frost_tx_init(FrostLinkTx *s)
{s->head=s->have=s->frames=s->dropped=s->written=s->blocked=s->high_water=s->deferred=0;
#ifdef FROST_CONTROL_SCHEDULER
 s->report_pending=s->prefer_report=s->rx_bytes_seen=s->last_action=s->control_flags=0;
#endif
}
static inline FROST_CTL_INLINE unsigned frost_tx_room(const FrostLinkTx *s){return FROST_TX_CAPACITY-s->have;}
static inline FROST_CTL_INLINE void frost_tx_byte(FrostLinkTx *s,uint8_t b)
{s->data[(s->head+s->have)&(FROST_TX_CAPACITY-1u)]=b;++s->have;}
static inline FROST_CTL_INLINE void frost_tx_committed(FrostLinkTx *s)
{frost_tx_count(&s->frames);if(s->have>s->high_water)s->high_water=s->have;}
static inline FROST_CTL_INLINE unsigned frost_tx_frame(FrostLinkTx *s,const uint8_t *p,unsigned n)
{
    unsigned i,b,len;uint8_t crc=0;
    if(n>FROST_TX_MAX_PAYLOAD || (n && !p) || n+4u>frost_tx_room(s)){
        frost_tx_count(&s->dropped);return 0;
    }
    /* Capacity and size checked before reading payload or modifying the ring. */
    for(i=0;i<n;++i){crc^=p[i];for(b=0;b<8;++b)crc=(uint8_t)((crc&128u)?((uint32_t)crc<<1)^7u:(uint32_t)crc<<1);}
    len=n+4u;frost_tx_byte(s,crc);frost_tx_byte(s,(uint8_t)len);
    frost_tx_byte(s,(uint8_t)(len>>8));frost_tx_byte(s,1);
    for(i=0;i<n;++i)frost_tx_byte(s,p[i]);
    frost_tx_committed(s);return 1;
}
static inline unsigned frost_tx_ack(FrostLinkTx *s)
{
    if(frost_tx_room(s)<4u){frost_tx_count(&s->dropped);return 0;}
    frost_tx_byte(s,1);frost_tx_byte(s,4);frost_tx_byte(s,0);frost_tx_byte(s,0x14);
    frost_tx_committed(s);return 1;
}
static inline unsigned frost_tx_dispatch_ready(FrostLinkTx *s,unsigned pending)
{
    if(frost_tx_room(s)>=FROST_TX_DISPATCH_RESERVE)return 1;
    if(pending)frost_tx_count(&s->deferred);
    return 0;
}
/* Serialized main-loop only, never fw_process. used/put must not wait.
 * A full/stuck FIFO returns immediately; at most16 writes per pass. */
static inline unsigned frost_tx_service(FrostLinkTx *s,unsigned (*used)(void *),
                                       void (*put)(void *,uint8_t),void *context)
{
    unsigned n=0;
    while(n<FROST_TX_BYTE_BUDGET && s->have){
        if(used(context)>=16u){frost_tx_count(&s->blocked);break;}
        put(context,s->data[s->head]);s->head=(s->head+1u)&(FROST_TX_CAPACITY-1u);
        --s->have;++n;frost_tx_count(&s->written);
    }
    return n;
}
static inline void frost_tx_read(const FrostLinkTx *s,unsigned source,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME};unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==12 && p[0]==0x3a && p[1]==0x7f && p[2]==12 && !p[3]){
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        v[3]=s->have;v[4]=s->frames;v[5]=s->dropped;v[6]=s->written;
        v[7]=s->blocked;v[8]=s->high_water;v[9]=s->deferred;v[10]=source;
    }
    reply[0]=0x3a;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
