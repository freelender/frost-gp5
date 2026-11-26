#include "preset_transfer.h"
#include "engine/hot_code.h"
typedef int (*FrostQuickBuiltin)(FrostPreset *);
/* Shared by the normal handler and the RAM dispatcher. Status/probe frames
 * never parse presets or touch assets, and must not evict a mutation receipt.
 * All query identity fields are captured before writing (alias-safe). */
static inline FROST_CTL_INLINE unsigned quick_preset_status(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *q,uint8_t *reply)
{
    uint32_t token=frost_wire_get32(q+8);unsigned status=FROST_SLOT_BAD_FRAME;
    if(q[0]==0x4d && q[1]==0x7f && q[2]==12 && !q[3]){
        if(frost_wire_get32(q+4)!=1u)status=FROST_SLOT_BAD_VERSION;
        else if(!token)status=FROST_SLOT_APPLIED;
        else status=s->cached==FROST_CACHE_QUICK && token==s->last_write[1]?FROST_SLOT_REPLAYED:FROST_SLOT_BAD_REQUEST_ID;
    }
    reply[0]=0x4d;reply[1]=0x7f;reply[2]=FROST_QUICK_REPLY_BYTES;reply[3]=0;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,token);frost_wire_put32(reply+12,status);
    frost_wire_put32(reply+16,s->revision);frost_wire_put32(reply+20,chain->transition);
    frost_wire_put32(reply+24,s->last_write[1]>t->last_request[1]?s->last_write[1]:t->last_request[1]);
    frost_wire_put32(reply+28,FROST_QUICK_CAP_RETAIN_IR);return status;
}
FROST_COLD static unsigned quick_preset_apply(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *q,unsigned n,FrostQuickBuiltin builtin)
{
    FrostPreset *p=&chain->pending_preset;unsigned i,j,pos=FROST_QUICK_HEADER,order;
    uint32_t token=frost_wire_get32(q+8),revision;
    if(frost_wire_get32(q+4)!=1u)return FROST_SLOT_BAD_VERSION;
    if(!token)return n==12u?FROST_SLOT_APPLIED:FROST_SLOT_BAD_REQUEST_ID;
    if(s->cached && token==s->last_write[1]){
        if(s->cached!=FROST_CACHE_QUICK)return FROST_SLOT_BAD_REQUEST_ID;
        if(n!=12u){if(n!=t->quick_bytes)return FROST_SLOT_BAD_REQUEST_ID;for(i=0;i<n;++i)if(q[i]!=t->quick_request[i])return FROST_SLOT_BAD_REQUEST_ID;}
        return FROST_SLOT_REPLAYED;
    }
    if(n==12u)return FROST_SLOT_BAD_REQUEST_ID;
    if(n<FROST_QUICK_HEADER || n>FROST_QUICK_MAX_BYTES || q[16]>6u || q[19]&0xfcu || (q[20]&0xc0u) || q[21])return FROST_SLOT_BAD_FRAME;
    if(chain->transition || t->state==FROST_TRANSFER_RECEIVING)return FROST_SLOT_BUSY;
    revision=frost_wire_get32(q+12);
    if(revision!=s->revision)return FROST_SLOT_STALE_REVISION;
    if(token<=s->last_write[1] || token<=t->last_request[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    p->version=FROST_PRESET_VERSION_COMPACT;p->sample_rate=44100;p->pre_count=q[16];
    order=(unsigned)q[17]|((unsigned)q[18]<<8)|((unsigned)q[19]<<16);
    p->ir_taps[0]=p->ir_taps[1]=0;
    for(i=0;i<FROST_SLOTS;++i){
        unsigned type=q[22+2*i],flags=q[23+2*i];const FrostEffect *d=type==FROST_EMPTY?0:frost_effect_find(type);
        if((type!=FROST_EMPTY && !d) || flags>3u || (type==FROST_EMPTY && flags&1u))return FROST_SLOT_BAD_ROUTE;
        p->types[i]=(uint8_t)type;p->order[i]=(uint8_t)((order>>(3*i))&7u);p->enabled[i]=(uint8_t)(flags&1u);p->bypass[i]=(uint8_t)(flags>>1);
        for(j=0;j<FROST_PARAMETERS;++j){
            union{float f;uint32_t u;} v;v.u=0;
            if(d && j<d->parameter_count){
                unsigned code;if(pos>=n)return FROST_SLOT_BAD_FRAME;code=q[pos++];
                if(code<250u)v.f=(float)((int)code-100);
                else if(code==250u && pos+4u<=n){v.u=frost_wire_get32(q+pos);pos+=4;}
                else return FROST_SLOT_BAD_FRAME;
            }
            p->parameters[i][j]=v.f;
        }
    }
    /* Quick recall carries no cabinet coefficients: the builtin resolver
     * initializes BOTH asset entries, including unused zero tails. Copying
     * the current cabinets first only did work that was immediately replaced.
     * Full preset validation still runs before publishing the transition. */
    p->version=FROST_PRESET_VERSION_EXTENDED;
    if(pos!=n || !builtin(p))return FROST_SLOT_BAD_ROUTE;
    /* Retain only explicitly requested, existing IR slots. The controller
     * checks the selected asset and stored CRC; the revision fences that
     * decision against route mutations before this snapshot is accepted. */
    for(i=0,j=0;i<FROST_SLOTS;++i){
        if(q[20]&(1u<<i)){
            const FrostIR *ir;
            if(p->types[i]!=FROST_IR || chain->slots[i].type!=FROST_IR)return FROST_SLOT_BAD_ROUTE;
            ir=chain->slots[i].state.ir;
            p->ir_taps[j]=ir->taps;
            for(unsigned k=0;k<FROST_IR_MAX;++k)p->ir_coefficients[j][k]=k<ir->taps?ir->coefficients[k]:0.0f;
        }
        if(p->types[i]==FROST_IR)++j;
    }
    {unsigned status=frost_slots_request_preset(s,chain,p,revision);if(status!=FROST_SLOT_ACCEPTED)return status;}
    for(i=0;i<8;++i)s->last_write[i]=0;s->last_write[1]=token;s->cached=FROST_CACHE_QUICK;
    for(i=0;i<n;++i)t->quick_request[i]=q[i];t->quick_bytes=n;
    return FROST_SLOT_ACCEPTED;
}
FROST_COLD void frost_quick_preset_command(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *q,unsigned n,uint8_t *reply,FrostQuickBuiltin builtin)
{
    if(q && n==12u){(void)quick_preset_status(t,s,chain,q,reply);return;}
    unsigned i,status=FROST_SLOT_BAD_FRAME;uint32_t token=n>=12u?frost_wire_get32(q+8):0;
    if(n>=12u && (unsigned)(q[0]|q[1]<<8)==FROST_QUICK_COMMAND && (unsigned)(q[2]|q[3]<<8)==n)
        status=quick_preset_apply(t,s,chain,q,n,builtin);
    for(i=0;i<FROST_QUICK_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=0x4d;reply[1]=0x7f;reply[2]=FROST_QUICK_REPLY_BYTES;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,token);frost_wire_put32(reply+12,status);
    frost_wire_put32(reply+16,s->revision);frost_wire_put32(reply+20,chain->transition);
    frost_wire_put32(reply+24,s->last_write[1]>t->last_request[1]?s->last_write[1]:t->last_request[1]);
    frost_wire_put32(reply+28,FROST_QUICK_CAP_RETAIN_IR);
}
