#include "policy.h"
#include "engine/hot_code.h"
/* Cold control path (between audio blocks, never per sample): size-optimized
 * on the GCC target to bound DSP flash; host/clang builds unchanged. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
static unsigned policy_apply(FrostSlotProtocol *s,FrostChain *chain,const uint32_t *v)
{
    unsigned i;
    if(v[0]!=1)return FROST_SLOT_BAD_VERSION;
    if(!v[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(v[2]>FROST_POLICY_SET)return FROST_SLOT_BAD_OPERATION;
    if(v[4]>=FROST_SLOTS)return FROST_SLOT_BAD_SLOT;
    if(v[2]==FROST_POLICY_GET && v[6]!=UINT32_MAX)return FROST_SLOT_BAD_FRAME;
    if(v[2]==FROST_POLICY_SET){
        if(v[6]>1)return FROST_SLOT_BAD_VALUE;
        if(s->cached && v[1]==s->last_write[1]){
            if(s->cached!=FROST_CACHE_POLICY)return FROST_SLOT_BAD_REQUEST_ID;
            for(i=0;i<8;++i)if(v[i]!=s->last_write[i])return FROST_SLOT_BAD_REQUEST_ID;
            return FROST_SLOT_REPLAYED;
        }
    }
    if(chain->transition)return FROST_SLOT_BUSY;
    if(v[3]!=s->revision)return FROST_SLOT_STALE_REVISION;
    if(v[5]!=chain->slots[v[4]].type)return FROST_SLOT_WRONG_TYPE;
    if(v[2]==FROST_POLICY_GET)return FROST_SLOT_APPLIED;
    if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    if(!frost_chain_request_bypass_policy(chain,v[4],v[6]))return FROST_SLOT_BAD_VALUE;
    ++s->revision;
    for(i=0;i<8;++i)s->last_write[i]=v[i];
    s->cached=FROST_CACHE_POLICY;
    return chain->transition?FROST_SLOT_ACCEPTED:FROST_SLOT_APPLIED;
}
void frost_policy_command(FrostSlotProtocol *s,FrostChain *chain,const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[8]={0};unsigned i,status=FROST_SLOT_BAD_FRAME,valid=0;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==32 && request[0]==0x45 && request[1]==0x7f && request[2]==32 && !request[3]){
        for(i=0;i<7;++i)v[i]=frost_wire_get32(request+4+4*i);
        status=policy_apply(s,chain,v);valid=v[0]==1 && v[1]!=0;
    }
    for(i=0;i<48;++i)reply[i]=0;
    reply[0]=0x45;reply[1]=0x7f;reply[2]=48;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    if(valid){
        frost_wire_put32(reply+16,s->revision);frost_wire_put32(reply+20,v[4]);
        frost_wire_put32(reply+24,FROST_EMPTY);
        if(v[4]<FROST_SLOTS){
            frost_wire_put32(reply+24,chain->slots[v[4]].type);
            frost_wire_put32(reply+28,chain->slots[v[4]].bypass_policy);
        }
        frost_wire_put32(reply+32,chain->transition);frost_wire_put32(reply+36,chain->fade_position);
        frost_wire_put32(reply+40,v[2]==FROST_POLICY_SET?v[6]:v[4]<FROST_SLOTS?chain->slots[v[4]].bypass_policy:0);
    }
}
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC pop_options
#endif
