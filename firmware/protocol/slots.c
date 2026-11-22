#include "slots.h"
#include "engine/hot_code.h"
/* Cold control path (between audio blocks, never per sample): size-optimized
 * on the GCC target to bound DSP flash; host/clang builds unchanged. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
void frost_slots_init(FrostSlotProtocol *s)
{ s->revision=1;s->cached=0; }
void frost_slots_legacy_changed(FrostSlotProtocol *s)
{ if(s->revision!=UINT32_MAX)++s->revision;s->cached=0; }
FROST_COLD unsigned frost_slots_request_route(FrostSlotProtocol *s,FrostChain *chain,
    const unsigned *types,const unsigned *order,unsigned pre_count,uint32_t expected_revision)
{
    if(frost_chain_transition(chain))return FROST_SLOT_BUSY;
    if(expected_revision!=s->revision)return FROST_SLOT_STALE_REVISION;
    if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    if(!frost_chain_request_route(chain,types,order,pre_count))return FROST_SLOT_BAD_ROUTE;
    frost_slots_legacy_changed(s);return FROST_SLOT_APPLIED;
}
FROST_COLD unsigned frost_slots_request_preset(FrostSlotProtocol *s,FrostChain *chain,const FrostPreset *preset,uint32_t expected_revision)
{
    if(frost_chain_transition(chain))return FROST_SLOT_BUSY;
    if(expected_revision!=s->revision)return FROST_SLOT_STALE_REVISION;
    if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    if(!frost_chain_request_preset(chain,preset))return FROST_SLOT_BAD_ROUTE;
    frost_slots_legacy_changed(s);return FROST_SLOT_ACCEPTED;
}
FROST_CTL_RAM static unsigned frost_slots_apply(FrostSlotProtocol *s,FrostChain *chain,const uint32_t *v)
{
    unsigned i;FrostInstance *slot;const FrostEffect *d;
    if(v[0]!=FROST_SLOT_VERSION && v[0]!=FROST_SLOT_VERSION_WINDOWED)return FROST_SLOT_BAD_VERSION;
    if(!v[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(v[2]>FROST_SLOT_ENABLE_MASK)return FROST_SLOT_BAD_OPERATION;
    if(v[4]>=FROST_SLOTS)return FROST_SLOT_BAD_SLOT;
    if(v[2]==FROST_SLOT_GET){
        if(v[3] || v[5]!=FROST_EMPTY || v[7] || (v[0]==FROST_SLOT_VERSION?v[6]!=FROST_SLOT_UNUSED:
           (v[6]%FROST_SLOT_WINDOW || v[6]>=FROST_PARAMETERS)))return FROST_SLOT_BAD_FRAME;
        if(frost_chain_transition(chain))return FROST_SLOT_BUSY;
        return FROST_SLOT_APPLIED;
    }
    if(frost_chain_transition(chain))return FROST_SLOT_BUSY;
    if(s->cached && v[1]==s->last_write[1]){
        if(s->cached!=FROST_CACHE_SLOT)return FROST_SLOT_BAD_REQUEST_ID;
        for(i=0;i<8;++i)if(v[i]!=s->last_write[i])return FROST_SLOT_BAD_REQUEST_ID;
        return FROST_SLOT_REPLAYED;
    }
    if(v[3]!=s->revision)return FROST_SLOT_STALE_REVISION;
    if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    if(v[2]==FROST_SLOT_ENABLE_MASK){
        if(v[4] || v[5]!=FROST_EMPTY || v[6]!=FROST_SLOT_UNUSED || v[7]>63u)return FROST_SLOT_BAD_FRAME;
        for(i=0;i<FROST_SLOTS;++i)if(chain->slots[i].type==FROST_EMPTY && (v[7]&(1u<<i)))return FROST_SLOT_BAD_VALUE;
        for(i=0;i<FROST_SLOTS;++i)if(chain->slots[i].type!=FROST_EMPTY)(void)frost_chain_enable(chain,i,(v[7]>>i)&1u);
    }else{
    slot=&chain->slots[v[4]];d=frost_effect_find(slot->type);
    if(!d || slot->type!=v[5])return FROST_SLOT_WRONG_TYPE;
    if(v[2]==FROST_SLOT_PARAMETER){
        union { uint32_t u;float f; } value;value.u=v[7];
        if(v[6]>=d->parameter_count)return FROST_SLOT_BAD_PARAMETER;
        if(!frost_chain_set(chain,v[4],v[6],value.f))return FROST_SLOT_BAD_VALUE;
    }else{
        if(v[6]!=FROST_SLOT_UNUSED)return FROST_SLOT_BAD_PARAMETER;
        if(!frost_chain_enable(chain,v[4],v[7]))return FROST_SLOT_BAD_VALUE;
    }
    }
    ++s->revision;
    for(i=0;i<8;++i)s->last_write[i]=v[i];
    s->cached=FROST_CACHE_SLOT;return FROST_SLOT_APPLIED;
}
static void frost_route_snapshot(const FrostSlotProtocol *s,const FrostChain *chain,
    uint8_t *reply,unsigned cmd,uint32_t token,unsigned status,unsigned valid)
{
    unsigned i;
    for(i=0;i<FROST_ROUTE_STATUS_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=(uint8_t)cmd;reply[1]=(uint8_t)(cmd>>8);reply[2]=FROST_ROUTE_STATUS_REPLY_BYTES;
    frost_wire_put32(reply+4,FROST_SLOT_VERSION);frost_wire_put32(reply+8,token);
    frost_wire_put32(reply+12,status);frost_wire_put32(reply+16,s->revision);
    if(valid){
        frost_wire_put32(reply+20,chain->transition);frost_wire_put32(reply+24,chain->fade_position);
        frost_wire_put32(reply+28,chain->pre_count);
        for(i=0;i<FROST_SLOTS;++i){
            reply[32+i]=(uint8_t)chain->slots[i].type;
            reply[38+i]=(uint8_t)chain->order[i];
        }
    }
}
void frost_route_status(FrostSlotProtocol *s,const FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    unsigned status=FROST_SLOT_BAD_FRAME,valid=0;uint32_t token=0;
    if(request && bytes>=12)token=frost_wire_get32(request+8);
    if(request && bytes==FROST_ROUTE_STATUS_REQUEST_BYTES && request[0]==0x41 &&
       request[1]==0x7f && request[2]==12 && !request[3]){
        if(frost_wire_get32(request+4)!=FROST_SLOT_VERSION)status=FROST_SLOT_BAD_VERSION;
        else if(!token)status=FROST_SLOT_BAD_REQUEST_ID;
        else {valid=1;status=chain->transition?FROST_SLOT_BUSY:FROST_SLOT_APPLIED;}
    }
    frost_route_snapshot(s,chain,reply,FROST_ROUTE_STATUS_COMMAND,token,status,valid);
}
static unsigned frost_route_apply(FrostSlotProtocol *s,FrostChain *chain,const uint32_t *v,
    const unsigned *types,const unsigned *order)
{
    unsigned i,status;
    if(v[0]!=FROST_SLOT_VERSION)return FROST_SLOT_BAD_VERSION;
    if(!v[1])return FROST_SLOT_BAD_REQUEST_ID;
    /* Route retries must precede BUSY: accepted routes are asynchronous. */
    if(s->cached && v[1]==s->last_write[1]){
        if(s->cached!=FROST_CACHE_ROUTE)return FROST_SLOT_BAD_REQUEST_ID;
        for(i=0;i<8;++i)if(v[i]!=s->last_write[i])return FROST_SLOT_BAD_REQUEST_ID;
        return FROST_SLOT_REPLAYED;
    }
    status=frost_slots_request_route(s,chain,types,order,v[3],v[2]);
    if(status!=FROST_SLOT_APPLIED)return status;
    for(i=0;i<8;++i)s->last_write[i]=v[i];
    s->cached=FROST_CACHE_ROUTE;
    return chain->transition?FROST_SLOT_ACCEPTED:FROST_SLOT_APPLIED;
}
void frost_route_command(FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[8]={0};unsigned i,types[FROST_SLOTS],order[FROST_SLOTS];
    unsigned status=FROST_SLOT_BAD_FRAME,valid=0;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==FROST_ROUTE_WRITE_REQUEST_BYTES && request[0]==0x42 && request[1]==0x7f &&
       request[2]==FROST_ROUTE_WRITE_REQUEST_BYTES && !request[3]){
        for(i=0;i<7;++i)v[i]=frost_wire_get32(request+4+4*i);
        for(i=0;i<FROST_SLOTS;++i){types[i]=request[20+i];order[i]=request[26+i];}
        valid=1;status=frost_route_apply(s,chain,v,types,order);
    }
    frost_route_snapshot(s,chain,reply,FROST_ROUTE_WRITE_COMMAND,v[1],status,valid);
}
FROST_CTL_RAM void frost_slots_command(FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[8],status=FROST_SLOT_BAD_FRAME,version=FROST_SLOT_VERSION;
    unsigned i,valid=0,base=0;const FrostInstance *slot=0;const FrostEffect *d=0;
    /* Explicit clear (not an aggregate initializer): no library memset call
     * from the RAM-resident control path (25c). Same values. */
    for(i=0;i<8;++i)v[i]=0;
    /* Partial requests never enter the setter or replay path. Safe echo only. */
    v[4]=FROST_SLOT_UNUSED;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes>=24)v[4]=frost_wire_get32(request+20);
    if(request && bytes==FROST_SLOT_REQUEST_BYTES && request[0]==0x40 &&
       request[1]==0x7f && request[2]==FROST_SLOT_REQUEST_BYTES && !request[3]){
        for(i=0;i<8;++i)v[i]=frost_wire_get32(request+4+4*i);
        valid=1;status=frost_slots_apply(s,chain,v);
        /* Echo the windowed version only for a well-formed version-2 frame. */
        if(v[0]==FROST_SLOT_VERSION_WINDOWED && status!=FROST_SLOT_BAD_VERSION){
            version=FROST_SLOT_VERSION_WINDOWED;
            if(v[2]==FROST_SLOT_GET && v[6]<FROST_PARAMETERS)base=v[6]-v[6]%FROST_SLOT_WINDOW;
            else if(v[2]==FROST_SLOT_PARAMETER && v[6]<FROST_PARAMETERS)base=v[6]-v[6]%FROST_SLOT_WINDOW;
        }
    }
    if(valid && v[4]<FROST_SLOTS){slot=&chain->slots[v[4]];d=frost_effect_find(slot->type);}
    for(i=0;i<FROST_SLOT_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=0x40;reply[1]=0x7f;reply[2]=FROST_SLOT_REPLY_BYTES;
    frost_wire_put32(reply+4,version);
    frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    frost_wire_put32(reply+16,s->revision);frost_wire_put32(reply+20,v[4]);
    if(valid && v[2]==FROST_SLOT_ENABLE_MASK){
        unsigned mask=0;for(i=0;i<FROST_SLOTS;++i)if(chain->slots[i].wet_target)mask|=1u<<i;
        frost_wire_put32(reply+24,FROST_EMPTY);frost_wire_put32(reply+28,mask);return;
    }
    frost_wire_put32(reply+24,slot?slot->type:FROST_EMPTY);
    frost_wire_put32(reply+28,slot?(uint32_t)slot->wet_target:0);
    frost_wire_put32(reply+32,(d?d->parameter_count:0)|(base<<16));
    for(i=0;d && i<FROST_SLOT_WINDOW && base+i<d->parameter_count;++i){
        union {float f;uint32_t u;} value;value.f=slot->target[base+i];
        frost_wire_put32(reply+36+4*i,value.u);
    }
}
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC pop_options
#endif
