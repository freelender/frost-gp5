#ifndef FROST_STACK_WATCH_H
#define FROST_STACK_WATCH_H
#include "firmware/protocol/stack_wire.h"
/* No repaint/reset. The observer/reply also use the actual stack. Read all
 * request fields and scan before reply writes, supporting unaligned in-place
 * request/reply. base is the complete fixed1024-word reserved region. */
static inline void frost_stack_read(const volatile uint32_t *base,uint32_t sp,
                                   unsigned mute,unsigned source,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME};unsigned i,free_words=0,flags=0;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==12 && p[0]==0x38 && p[1]==0x7f && p[2]==12 && !p[3]){
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else if(!base)v[2]=FROST_SLOT_BAD_OPERATION;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        for(i=0;i<FROST_STACK_BYTES/4u && base[i]==FROST_STACK_PATTERN;++i)++free_words;
        if(base[0]==FROST_STACK_PATTERN && base[1]==FROST_STACK_PATTERN &&
           base[2]==FROST_STACK_PATTERN && base[3]==FROST_STACK_PATTERN)flags|=1u;
        if(sp>=FROST_STACK_BOTTOM && sp<=FROST_STACK_TOP && !(sp&7u))flags|=2u;
        v[3]=FROST_STACK_BYTES-4u*free_words;v[4]=4u*free_words;v[5]=sp;
        v[6]=FROST_STACK_BOTTOM;v[7]=FROST_STACK_TOP;v[8]=flags;v[9]=!!mute;v[10]=source;
    }
    reply[0]=0x38;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
