#include "preset_transfer.h"
#include "engine/hot_code.h"
/* Cold control path (between audio blocks, never per sample): size-optimized
 * on the GCC target to bound DSP flash; host/clang builds unchanged. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
void frost_preset_transfer_init(FrostPresetTransfer *t)
{
    unsigned i;t->quick_bytes=0;t->session=t->base_revision=t->expected_crc=t->next=t->committed_revision=0;
    t->crc=UINT32_MAX;t->state=FROST_TRANSFER_EMPTY;t->cached=0;
    for(i=0;i<11;++i)t->last_request[i]=0;
}
static uint32_t preset_crc_word(uint32_t crc,uint32_t word)
{
    unsigned i,j;
    for(i=0;i<4;++i){crc^=(word>>(8*i))&255u;
        for(j=0;j<8;++j)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc;
}
static void preset_set_word(FrostPreset *p,unsigned index,uint32_t value)
{
    union {uint32_t u;float f;} bits;bits.u=value;
    if(index==0)p->version=value;
    else if(index==1)p->sample_rate=value;
    else if(index==2)p->pre_count=value;
    else if(index<9)p->types[index-3]=(uint8_t)value;
    else if(index<15)p->order[index-9]=(uint8_t)value;
    else if(index<21)p->enabled[index-15]=(uint8_t)value;
    else if(index<27)p->bypass[index-21]=(uint8_t)value;
    else if(index<45)p->parameters[(index-27)/3][(index-27)%3]=bits.f;
    else if(index<47)p->ir_taps[index-45]=value;
    else if(p->version==FROST_PRESET_VERSION_COMPACT)
        p->parameters[(index-47u)/(FROST_PARAMETERS-3u)][3u+(index-47u)%(FROST_PARAMETERS-3u)]=bits.f;
    else if(index<FROST_PRESET_WORDS)p->ir_coefficients[(index-47)/512][(index-47)%512]=bits.f;
    else p->parameters[(index-FROST_PRESET_WORDS)/(FROST_PARAMETERS-3u)][3u+(index-FROST_PRESET_WORDS)%(FROST_PARAMETERS-3u)]=bits.f;
}
/* Version 3: no IR samples travel. Bank b of the new preset keeps the IR of
 * the b-th IR slot of the live chain (the order capture uses); further banks
 * take the IR workspaces no live IR slot uses, else an identity tap. Then it
 * is an ordinary extended (v2) snapshot. */
int frost_preset_keep_irs(FrostChain *chain);
int frost_preset_keep_irs(FrostChain *chain)
{
    FrostPreset *p=&chain->pending_preset;const FrostIR *src[FROST_PRESET_IRS]={0};unsigned b,i,j,n=0,used=0,want=0;
    if(p->ir_taps[0] || p->ir_taps[1])return 0;
    for(i=0;i<FROST_SLOTS && n<FROST_PRESET_IRS;++i)if(chain->slots[i].type==FROST_IR){
        src[n++]=chain->slots[i].state.ir;
        for(j=0;j<FROST_IR_WORKSPACES;++j)if(chain->slots[i].state.ir==&chain->ir_pool[j])used|=1u<<j;
    }
    for(j=0;j<FROST_IR_WORKSPACES && n<FROST_PRESET_IRS;++j)if(!(used&(1u<<j)))src[n++]=&chain->ir_pool[j];
    for(i=0;i<FROST_SLOTS;++i)want+=p->types[i]==FROST_IR;
    for(b=0;b<FROST_PRESET_IRS;++b){
        const FrostIR *ir=b<want?src[b]:0;unsigned taps=ir && ir->taps && ir->taps<=FROST_IR_MAX?ir->taps:0;
        for(j=0;j<FROST_IR_MAX;++j)p->ir_coefficients[b][j]=j<taps?ir->coefficients[j]:0.0f;
        if(b<want && !taps){taps=1;p->ir_coefficients[b][0]=1.0f;}
        p->ir_taps[b]=taps;
    }
    p->version=FROST_PRESET_VERSION_EXTENDED;return 1;
}
static void preset_sync(FrostPresetTransfer *t,const FrostSlotProtocol *s,const FrostChain *chain)
{
    if(t->state==FROST_TRANSFER_RECEIVING && (s->revision!=t->base_revision || chain->transition)){
        t->state=FROST_TRANSFER_INVALID;t->cached=0;
    }else if(t->state==FROST_TRANSFER_ACCEPTED || t->state==FROST_TRANSFER_COMPLETE){
        if(s->revision!=t->committed_revision)t->state=FROST_TRANSFER_INVALID;
        else if(!chain->transition)t->state=FROST_TRANSFER_COMPLETE;
    }
}
static unsigned preset_apply(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,const uint32_t *v)
{
    unsigned i,j,op=v[2],count=0,status,total;
    if(v[0]!=1)return FROST_SLOT_BAD_VERSION;
    if(!v[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(op>FROST_PRESET_ABORT)return FROST_SLOT_BAD_OPERATION;
    if(op==FROST_PRESET_STATUS){
        if(v[4] || v[5])return FROST_SLOT_BAD_FRAME;
        for(i=6;i<11;++i)if(v[i])return FROST_SLOT_BAD_FRAME;
    }else{
        if(!v[3])return FROST_SLOT_BAD_SESSION;
        if(op==FROST_PRESET_WRITE){
            /* Total length follows word 0 (in this request when index is 0). */
            total=frost_preset_words(v[5]?chain->pending_preset.version:v[6]);
            if(v[5]>=total)return FROST_SLOT_BAD_OFFSET;
            count=total-v[5];if(count>5)count=5;
            for(i=count;i<5;++i)if(v[6+i])return FROST_SLOT_BAD_FRAME;
            for(i=0;i<count;++i)if(v[5]+i>=3 && v[5]+i<27 && v[6+i]>255)return FROST_SLOT_BAD_VALUE;
        }else{
            if(op==FROST_PRESET_COMMIT?v[5]!=FROST_PRESET_WORDS && v[5]!=FROST_PRESET_MAX_WORDS && v[5]!=FROST_PRESET_COMPACT_WORDS:v[5]!=0)return FROST_SLOT_BAD_OFFSET;
            for(i=op==FROST_PRESET_ABORT?6:7;i<11;++i)if(v[i])return FROST_SLOT_BAD_FRAME;
        }
        /* Commit receipts share identity/collision rules with other writes.
         * Retry BEFORE BUSY so a lost acceptance never restarts the fade. */
        if(s->cached && v[1]==s->last_write[1]){
            if(s->cached!=FROST_CACHE_PRESET || op!=FROST_PRESET_COMMIT)return FROST_SLOT_BAD_REQUEST_ID;
            for(i=0;i<7;++i)if(v[i]!=s->last_write[i])return FROST_SLOT_BAD_REQUEST_ID;
            preset_sync(t,s,chain);return FROST_SLOT_REPLAYED;
        }
    }
    preset_sync(t,s,chain);
    if(op==FROST_PRESET_STATUS)return !v[3] || v[3]==t->session?FROST_SLOT_APPLIED:FROST_SLOT_BAD_SESSION;
    if(t->cached && v[1]==t->last_request[1] &&
       (t->state==FROST_TRANSFER_RECEIVING || t->state==FROST_TRANSFER_ABORTED)){
        for(i=0;i<11;++i)if(v[i]!=t->last_request[i])return FROST_SLOT_BAD_REQUEST_ID;
        return FROST_SLOT_REPLAYED;
    }
    if(op==FROST_PRESET_ABORT){
        if(v[3]!=t->session || !t->session)return FROST_SLOT_BAD_SESSION;
        if(t->state==FROST_TRANSFER_ACCEPTED || t->state==FROST_TRANSFER_COMPLETE)return FROST_SLOT_BAD_OPERATION;
        if(v[4]!=t->base_revision)return FROST_SLOT_STALE_REVISION;
    }else{
        if(chain->transition)return FROST_SLOT_BUSY;
        if(v[4]!=s->revision)return FROST_SLOT_STALE_REVISION;
        if(op==FROST_PRESET_BEGIN){
            if(s->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
            if(t->state==FROST_TRANSFER_RECEIVING)return FROST_SLOT_BUSY;
            if(v[3]<=t->session)return FROST_SLOT_BAD_SESSION;
        }else{
            if(v[3]!=t->session || !t->session)return FROST_SLOT_BAD_SESSION;
            if(t->state!=FROST_TRANSFER_RECEIVING || v[4]!=t->base_revision)return FROST_SLOT_STALE_REVISION;
        }
    }
    if(v[1]<=t->last_request[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(op==FROST_PRESET_BEGIN){
        if(s->cached==FROST_CACHE_PRESET)s->cached=0;
        t->session=v[3];t->base_revision=v[4];t->expected_crc=v[6];t->crc=UINT32_MAX;
        t->next=t->committed_revision=0;t->state=FROST_TRANSFER_RECEIVING;
        /* A version-1 body never writes the appended words: clear them. */
        for(i=0;i<FROST_SLOTS;++i)for(j=3;j<FROST_PARAMETERS;++j)chain->pending_preset.parameters[i][j]=0.0f;
    }else if(op==FROST_PRESET_WRITE){
        if(v[5]!=t->next)return FROST_SLOT_BAD_OFFSET;
        for(i=0;i<count;++i){preset_set_word(&chain->pending_preset,t->next+i,v[6+i]);t->crc=preset_crc_word(t->crc,v[6+i]);}
        t->next+=count;
    }else if(op==FROST_PRESET_COMMIT){
        if(t->next!=v[5] || v[5]!=frost_preset_words(chain->pending_preset.version))return FROST_SLOT_INCOMPLETE;
        if(v[6]!=t->expected_crc || (t->crc^UINT32_MAX)!=t->expected_crc)return FROST_SLOT_BAD_CHECKSUM;
        if(chain->pending_preset.version==FROST_PRESET_VERSION_COMPACT && !frost_preset_keep_irs(chain))return FROST_SLOT_BAD_ROUTE;
        status=frost_slots_request_preset(s,chain,&chain->pending_preset,v[4]);
        if(status!=FROST_SLOT_ACCEPTED)return status;
        for(i=0;i<7;++i)s->last_write[i]=v[i];
        s->last_write[7]=0;s->cached=FROST_CACHE_PRESET;
        t->state=FROST_TRANSFER_ACCEPTED;t->committed_revision=s->revision;
    }else t->state=FROST_TRANSFER_ABORTED;
    for(i=0;i<11;++i)t->last_request[i]=v[i];
    t->cached=op!=FROST_PRESET_COMMIT;
    return op==FROST_PRESET_COMMIT?FROST_SLOT_ACCEPTED:FROST_SLOT_APPLIED;
}
void frost_preset_command(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[11]={0};unsigned i,status=FROST_SLOT_BAD_FRAME,valid=0;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==48 && request[0]==0x43 && request[1]==0x7f && request[2]==48 && !request[3]){
        for(i=0;i<11;++i)v[i]=frost_wire_get32(request+4+4*i);
        status=preset_apply(t,s,chain,v);valid=v[0]==1 && v[1]!=0;
    }
    for(i=0;i<48;++i)reply[i]=0;
    reply[0]=0x43;reply[1]=0x7f;reply[2]=48;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    if(valid){
        uint32_t floor=t->last_request[1];
        if(s->cached && s->last_write[1]>floor)floor=s->last_write[1];
        frost_wire_put32(reply+16,s->revision);frost_wire_put32(reply+20,t->session);
        frost_wire_put32(reply+24,t->state);frost_wire_put32(reply+28,t->next);
        frost_wire_put32(reply+32,floor);frost_wire_put32(reply+36,chain->transition);
        frost_wire_put32(reply+40,t->committed_revision);frost_wire_put32(reply+44,t->crc^UINT32_MAX);
    }
}
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC pop_options
#endif
