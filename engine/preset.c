#include "chain.h"
#include "hot_code.h"
/* Cold control path (between audio blocks, never per sample): size-optimized
 * on the GCC target to bound DSP flash; host/clang builds unchanged. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
static int preset_finite(float value)
{union {float f;uint32_t u;} v;v.f=value;return (v.u&0x7f800000u)!=0x7f800000u;}
static int preset_valid(const FrostChain *s,const FrostPreset *p)
{
    unsigned i,j,mask=0,ir=0,delay=0,mod=0,reverb=0;
    if((p->version!=FROST_PRESET_VERSION && p->version!=FROST_PRESET_VERSION_EXTENDED) ||
       p->sample_rate!=44100u || p->pre_count>FROST_SLOTS)return 0;
    for(i=0;i<FROST_SLOTS;++i){
        const FrostEffect *d=frost_effect_find(p->types[i]);
        if((!d && p->types[i]!=FROST_EMPTY) || p->order[i]>=FROST_SLOTS || (mask&(1u<<p->order[i])) ||
           p->enabled[i]>1 || (p->types[i]==FROST_EMPTY && p->enabled[i]) || p->bypass[i]>FROST_BYPASS_CUT_AND_SUSPEND)return 0;
        mask|=1u<<p->order[i];
        if(p->version==FROST_PRESET_VERSION && p->types[i]!=FROST_EMPTY && p->types[i]>=FROST_LEGACY_EFFECTS)return 0;
        if(frost_type_is_delay(p->types[i]) && (!s->delay_workspace || ++delay>FROST_DELAY_WORKSPACES))return 0;
        if(frost_type_is_reverb(p->types[i]) && (!s->reverb_workspace || ++reverb>FROST_REVERB_WORKSPACES))return 0;
        if(frost_type_is_modulation(p->types[i]) && (!s->modulation_pool || ++mod>FROST_MOD_WORKSPACES))return 0;
        for(j=0;j<FROST_PARAMETERS;++j){
            float v=p->parameters[i][j];
            if(!preset_finite(v) || (d && j<d->parameter_count?
                v<d->parameters[j].minimum || v>d->parameters[j].maximum ||
                ((d->parameters[j].flags&FROST_PARAM_STEPPED) && v!=(float)(int)v):v!=0.0f))return 0;
        }
        if(p->types[i]==FROST_IR){
            float sum=0.0f;unsigned taps;
            if(ir>=FROST_PRESET_IRS)return 0;
            taps=p->ir_taps[ir];
            if(!taps || taps>FROST_IR_MAX)return 0;
            for(j=0;j<FROST_IR_MAX;++j){
                float v=p->ir_coefficients[ir][j];
                if(!preset_finite(v) || (j>=taps && v!=0.0f))return 0;
                sum+=v<0.0f?-v:v;
            }
            if(sum>8.0f)return 0;
            ++ir;
        }
    }
    for(;ir<FROST_PRESET_IRS;++ir){
        if(p->ir_taps[ir])return 0;
        for(j=0;j<FROST_IR_MAX;++j)if(p->ir_coefficients[ir][j]!=0.0f)return 0;
    }
    return 1;
}
int frost_chain_capture_preset(const FrostChain *s,FrostPreset *out)
{
    unsigned i,j,ir=0;unsigned char *bytes=(unsigned char *)out;
    if(!out || s->transition)return 0;
    for(i=0;i<sizeof(*out);++i)bytes[i]=0;
    out->version=FROST_PRESET_VERSION;out->sample_rate=44100;out->pre_count=s->pre_count;
    for(i=0;i<FROST_SLOTS;++i)if(s->slots[i].type!=FROST_EMPTY && s->slots[i].type>=FROST_LEGACY_EFFECTS)
        out->version=FROST_PRESET_VERSION_EXTENDED;
    for(i=0;i<FROST_SLOTS;++i){
        const FrostInstance *slot=&s->slots[i];
        out->types[i]=(uint8_t)slot->type;out->order[i]=(uint8_t)s->order[i];
        out->enabled[i]=(uint8_t)slot->wet_target;out->bypass[i]=(uint8_t)slot->bypass_policy;
        for(j=0;j<FROST_PARAMETERS;++j)out->parameters[i][j]=slot->target[j];
        if(slot->type==FROST_IR){
            out->ir_taps[ir]=slot->state.ir->taps;
            for(j=0;j<slot->state.ir->taps;++j)out->ir_coefficients[ir][j]=slot->state.ir->coefficients[j];
            ++ir;
        }
    }
    return 1;
}
FROST_RECALL_VALIDATE_RAM int frost_chain_request_preset(FrostChain *s,const FrostPreset *p)
{
    unsigned i;if(!p || s->transition || !preset_valid(s,p))return 0;
    /* Copy before publishing the transition. Self-copy of owned pending data
     * is allowed for a future bounded transport, but never alias live fields. */
    if(p!=&s->pending_preset)s->pending_preset=*p;
    for(i=0;i<FROST_SLOTS;++i){s->pending_types[i]=p->types[i];s->pending_order[i]=p->order[i];}
    s->pending_pre_count=p->pre_count;s->pending_preset_active=1;
    s->pending_policy_slot=FROST_SLOTS;
    s->fade_position=0;s->transition=FROST_ROUTE_OUT;return 1;
}
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__)
#pragma GCC pop_options
#endif
