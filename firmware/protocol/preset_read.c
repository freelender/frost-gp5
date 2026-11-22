#include "preset_read.h"
#include "engine/hot_code.h"
/* Cold control path (between audio blocks, never per sample): size-optimized
 * on the GCC target to bound DSP flash; host/clang builds unchanged. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
static uint32_t read_float_bits(float value)
{union {float f;uint32_t u;} v;v.f=value;return v.u;}
static const FrostIR *read_ir(const FrostChain *chain,unsigned bank)
{
    unsigned i;
    for(i=0;i<FROST_SLOTS;++i)if(chain->slots[i].type==FROST_IR){
        if(!bank)return chain->slots[i].state.ir;
        --bank;
    }
    return 0;
}
/* noinline: keeps its original out-of-line copy in the boot-adjacent layout
 * (docs: DSP boot layout rule); the compact helpers below must not absorb it. */
__attribute__((noinline)) static uint32_t read_version(const FrostChain *chain)
{
    unsigned i;
    for(i=0;i<FROST_SLOTS;++i)if(chain->slots[i].type!=FROST_EMPTY && chain->slots[i].type>=FROST_LEGACY_EFFECTS)
        return FROST_PRESET_VERSION_EXTENDED;
    return FROST_PRESET_VERSION;
}
static uint32_t read_preset_word(const FrostChain *chain,unsigned index)
{
    const FrostIR *ir;
    if(index==0)return read_version(chain);
    if(index==1)return 44100;
    if(index==2)return chain->pre_count;
    if(index<9)return chain->slots[index-3].type;
    if(index<15)return chain->order[index-9];
    if(index<21)return (unsigned)chain->slots[index-15].wet_target;
    if(index<27)return chain->slots[index-21].bypass_policy;
    if(index<45){
        unsigned slot=(index-27)/3,p=(index-27)%3;
        const FrostEffect *d=frost_effect_find(chain->slots[slot].type);
        return d && p<d->parameter_count?read_float_bits(chain->slots[slot].target[p]):0;
    }
    if(index<47){ir=read_ir(chain,index-45);return ir?ir->taps:0;}
    if(index>=FROST_PRESET_WORDS){
        unsigned slot=(index-FROST_PRESET_WORDS)/(FROST_PARAMETERS-3u),p=3u+(index-FROST_PRESET_WORDS)%(FROST_PARAMETERS-3u);
        const FrostEffect *d=frost_effect_find(chain->slots[slot].type);
        return d && p<d->parameter_count?read_float_bits(chain->slots[slot].target[p]):0;
    }
    index-=47;ir=read_ir(chain,index/FROST_IR_MAX);
    return ir && index%FROST_IR_MAX<ir->taps?read_float_bits(ir->coefficients[index%FROST_IR_MAX]):0;
}
/* Version-3 compact body (no IR samples): words 0..46 with version 3 and zero
 * taps, then the 30 extended parameter words. */
uint32_t frost_read_compact_word(const FrostChain *chain,unsigned index);
uint32_t frost_read_compact_word(const FrostChain *chain,unsigned index)
{
    if(index==0)return FROST_PRESET_VERSION_COMPACT;
    if(index==45 || index==46)return 0;
    if(index<47)return read_preset_word(chain,index);
    return read_preset_word(chain,FROST_PRESET_WORDS+index-47u);
}
uint32_t frost_read_body_word(const FrostChain *chain,unsigned compact,unsigned index);
uint32_t frost_read_body_word(const FrostChain *chain,unsigned compact,unsigned index)
{return compact?frost_read_compact_word(chain,index):read_preset_word(chain,index);}
unsigned frost_read_body_words(const FrostChain *chain,unsigned compact);
unsigned frost_read_body_words(const FrostChain *chain,unsigned compact)
{return compact?FROST_PRESET_COMPACT_WORDS:frost_preset_words(read_version(chain));}
static uint32_t read_preset_crc(const FrostChain *chain,unsigned compact)
{
    uint32_t crc=UINT32_MAX;unsigned i,j,k;
    unsigned words=frost_read_body_words(chain,compact);
    for(i=0;i<words;++i){
        uint32_t word=frost_read_body_word(chain,compact,i);
        for(j=0;j<4;++j){crc^=(word>>(8*j))&255u;
            for(k=0;k<8;++k)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
        }
    }
    return crc^UINT32_MAX;
}
void frost_preset_read_command(const FrostSlotProtocol *s,const FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[5]={0},data[5]={0};unsigned i,index=0,count=0,status=FROST_SLOT_BAD_FRAME;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==24 && request[0]==0x44 && request[1]==0x7f && request[2]==24 && !request[3]){
        for(i=0;i<5;++i)v[i]=frost_wire_get32(request+4+4*i);
        if(v[0]!=1)status=FROST_SLOT_BAD_VERSION;
        else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
        else if(v[2]>FROST_PRESET_READ_COMPACT)status=FROST_SLOT_BAD_OPERATION;
        else {
            unsigned compact=v[2]>=FROST_PRESET_DESCRIBE_COMPACT,read=v[2]==FROST_PRESET_READ || v[2]==FROST_PRESET_READ_COMPACT;
            if(!read && v[4])status=FROST_SLOT_BAD_OFFSET;
            else if(read && v[4]>=frost_read_body_words(chain,compact))status=FROST_SLOT_BAD_OFFSET;
            else if(chain->transition)status=FROST_SLOT_BUSY;
            else if((!v[3] && read) || (v[3] && v[3]!=s->revision))status=FROST_SLOT_STALE_REVISION;
            else {
                status=FROST_SLOT_APPLIED;
                if(!read){data[0]=read_preset_crc(chain,compact);data[1]=frost_read_body_words(chain,compact);}
                else {
                    index=v[4];count=frost_read_body_words(chain,compact)-index;if(count>5)count=5;
                    for(i=0;i<count;++i)data[i]=frost_read_body_word(chain,compact,index+i);
                }
            }
        }
    }
    for(i=0;i<48;++i)reply[i]=0;
    reply[0]=0x44;reply[1]=0x7f;reply[2]=48;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    if(v[0]==1 && v[1])frost_wire_put32(reply+16,s->revision);
    frost_wire_put32(reply+20,index);frost_wire_put32(reply+24,count);
    for(i=0;i<5;++i)frost_wire_put32(reply+28+4*i,data[i]);
}
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC pop_options
#endif
