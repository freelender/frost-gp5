#include "chain.h"
#include "hot_code.h"
/* Optional adapter hooks; ordinary host/CMake processing has no timer cost. */
#ifndef FROST_PROFILE_CLOCK
#define FROST_PROFILE_CLOCK() 0u
#define FROST_PROFILE_SLOT(slot,type,begin,end) ((void)(begin),(void)(end))
#define FROST_PROFILE_BLOCK(fallback) ((void)0)
#endif
FROST_CTL_RAM static int finite_float(float x)
{
    union {float f;uint32_t u;} v;v.f=x;
    return (v.u & 0x7f800000u)!=0x7f800000u;
}
static int signal_valid(float x){return frost_signal_valid(x);}
void frost_side_route(const FrostChain *s,const FrostAmpStage *amp);   /* beta.14, below */
FROST_COLD void frost_chain_init(FrostChain *s)
{
    unsigned i;s->faults=0;s->started=0;s->delay_workspace=0;s->modulation_pool=0;s->reverb_workspace=0;
    s->pre_count=3;s->transition=FROST_ROUTE_IDLE;s->fade_position=0;
    s->pending_preset_active=0;
    s->pending_policy_slot=FROST_SLOTS;s->pending_policy_value=0;
    /* 31a: an (re)initialized chain starts without FFT workspaces. */
    for(i=0;i<FROST_IR_WORKSPACES;++i)(void)frost_ir_bind_fft(&s->ir_pool[i],0);
    for(i=0;i<FROST_SLOTS;++i){
        s->order[i]=i;
        frost_instance_init(&s->slots[i],frost_default_type(i),i==FROST_IR?&s->ir_pool[0]:0);
    }
}
FROST_COLD int frost_chain_bind_delay(FrostChain *s,FrostDelay *workspace)
{
    unsigned i;if(s->started || s->transition)return 0;
    for(i=0;i<FROST_SLOTS;++i)if(frost_type_is_delay(s->slots[i].type))return 0;
    s->delay_workspace=workspace;return 1;
}
FROST_COLD int frost_chain_bind_modulation(FrostChain *s,FrostModulation *pool)
{
    unsigned i;if(s->started || s->transition)return 0;
    for(i=0;i<FROST_SLOTS;++i)if(frost_type_is_modulation(s->slots[i].type))return 0;
    s->modulation_pool=pool;return 1;
}
int frost_chain_bind_ir_fft(FrostChain *s,FrostIRFFT *pool,unsigned count)
{
    unsigned i;if(s->started || s->transition || count>FROST_IR_WORKSPACES || (count && !pool))return 0;
    for(i=0;i<FROST_IR_WORKSPACES;++i)if(!frost_ir_bind_fft(&s->ir_pool[i],i<count?&pool[i]:0))return 0;
    return 1;
}
FROST_COLD int frost_chain_bind_reverb(FrostChain *s,FrostReverbWorkspace *workspace)
{
    unsigned i;if(s->started || s->transition)return 0;
    for(i=0;i<FROST_SLOTS;++i)if(frost_type_is_reverb(s->slots[i].type))return 0;
    s->reverb_workspace=workspace;return 1;
}
int frost_chain_configure(FrostChain *s,const unsigned *types,unsigned count)
{
    unsigned i,irs=0,delays=0,mods=0,reverbs=0,route[FROST_SLOTS];
    if(s->started || s->transition || count>FROST_SLOTS || (count && !types))return 0;
    for(i=0;i<count;++i){
        if(types[i]!=FROST_EMPTY && !frost_effect_find(types[i]))return 0;
        if(types[i]==FROST_IR && ++irs>FROST_IR_WORKSPACES)return 0;
        if(frost_type_is_delay(types[i]) && (!s->delay_workspace || ++delays>FROST_DELAY_WORKSPACES))return 0;
        if(frost_type_is_modulation(types[i]) && (!s->modulation_pool || ++mods>FROST_MOD_WORKSPACES))return 0;
        if(frost_type_is_reverb(types[i]) && (!s->reverb_workspace || ++reverbs>FROST_REVERB_WORKSPACES))return 0;
        route[i]=types[i];
    }
    /* Validate entire route before mutation. Cold replacement resets all slot
     * parameters, enable state and histories; never resets faults. */
    irs=mods=0;
    for(i=0;i<FROST_SLOTS;++i){
        unsigned type=i<count?route[i]:FROST_EMPTY;
        void *workspace=type==FROST_IR?(void *)&s->ir_pool[irs++]:frost_type_is_delay(type)?(void *)s->delay_workspace:0;
        if(frost_type_is_modulation(type))workspace=&s->modulation_pool[mods++];
        if(frost_type_is_reverb(type))workspace=s->reverb_workspace;
        frost_instance_init_workspace(&s->slots[i],type,workspace);
        s->order[i]=i;
    }
    s->pre_count=count;
    frost_side_route(s,0);
    return 1;
}
/* beta.14 stereo fold rule (engine/stereo.h): the first stereo block keeps
 * its side signal only when every non-empty block after it is a delay,
 * reverb or modulation and no amp stage follows it; otherwise it plays mono.
 * Decided when a route is configured or committed (the silent commit), so
 * audio blocks pay nothing. External linkage, cold flash. */
void frost_side_route(const FrostChain *s,const FrostAmpStage *amp);
FROST_COLD void frost_side_route(const FrostChain *s,const FrostAmpStage *amp)
{
    unsigned p,q,slot=FROST_SLOTS,ok=1u;
    for(p=0;p<FROST_SLOTS;++p)if(frost_type_is_stereo(s->slots[s->order[p]].type)){slot=s->order[p];break;}
    if(slot<FROST_SLOTS){
        if(p<s->pre_count && amp && amp->sample)ok=0u;
        for(q=p+1u;q<FROST_SLOTS && ok;++q){
            unsigned t=s->slots[s->order[q]].type,c;
            if(t==FROST_EMPTY)continue;
            c=frost_type_category(t);
            if(c!=FROST_CATEGORY_DELAY && c!=FROST_CATEGORY_REVERB && c!=FROST_CATEGORY_MODULATION)ok=0u;
        }
    }
    frost_side_slot=slot;frost_side_route_ok=ok;
}
FROST_CTL_RAM unsigned frost_chain_transition(const FrostChain *s){return s->transition;}
FROST_COLD int frost_chain_request_route(FrostChain *s,const unsigned *types,const unsigned *order,unsigned pre_count)
{
    unsigned i,irs=0,delays=0,mods=0,reverbs=0,mask=0,changed=pre_count!=s->pre_count;
    unsigned next_types[FROST_SLOTS],next_order[FROST_SLOTS];
    if(s->transition || !types || !order || pre_count>FROST_SLOTS)return 0;
    for(i=0;i<FROST_SLOTS;++i){
        if(types[i]!=FROST_EMPTY && !frost_effect_find(types[i]))return 0;
        if(types[i]==FROST_IR && ++irs>FROST_IR_WORKSPACES)return 0;
        if(frost_type_is_delay(types[i]) && (!s->delay_workspace || ++delays>FROST_DELAY_WORKSPACES))return 0;
        if(frost_type_is_modulation(types[i]) && (!s->modulation_pool || ++mods>FROST_MOD_WORKSPACES))return 0;
        if(frost_type_is_reverb(types[i]) && (!s->reverb_workspace || ++reverbs>FROST_REVERB_WORKSPACES))return 0;
        if(order[i]>=FROST_SLOTS || (mask&(1u<<order[i])))return 0;
        mask|=1u<<order[i];
        if(types[i]!=s->slots[i].type || order[i]!=s->order[i])changed=1;
        next_types[i]=types[i];next_order[i]=order[i];
    }
    if(!changed)return 1;
    /* Stage all source fields before later processing can mutate slot state. */
    for(i=0;i<FROST_SLOTS;++i){s->pending_types[i]=next_types[i];s->pending_order[i]=next_order[i];}
    s->pending_preset_active=0;
    s->pending_policy_slot=FROST_SLOTS;
    s->pending_pre_count=pre_count;s->fade_position=0;s->transition=FROST_ROUTE_OUT;
    return 1;
}
static void frost_chain_reset_histories(FrostChain *s,const FrostAmpStage *amp,unsigned initialized)
{
    unsigned i;
    for(i=0;i<FROST_SLOTS;++i)if(!(initialized&(1u<<i)))frost_instance_reset_history(&s->slots[i]);
    if(amp && amp->reset)amp->reset(amp->state);
}
/* Stage 2b Cabinet Override: 2 = no change pending. */
unsigned frost_ir_bypass_pending=2u;
#ifdef FROST_IR_LINK
/* Stage 4: IR blocks on the controller (1) or here (0); 2 = no change pending. */
unsigned frost_ir_remote_pending=2u;
#endif
FROST_RECALL_COMMIT_RAM FROST_COLD static void frost_chain_commit_route(FrostChain *s,const FrostAmpStage *amp)
{
    unsigned i,j,used=0,mod_used=0,initialized=0;
    /* Retain workspace identity/coefficient assets for unchanged IR slots. */
    for(i=0;i<FROST_SLOTS;++i)if(s->slots[i].type==FROST_IR && s->pending_types[i]==FROST_IR)
        for(j=0;j<FROST_IR_WORKSPACES;++j)if(s->slots[i].state.ir==&s->ir_pool[j])used|=1u<<j;
    for(i=0;i<FROST_SLOTS;++i)if(frost_type_is_modulation(s->slots[i].type) && s->pending_types[i]==s->slots[i].type)
        for(j=0;j<FROST_MOD_WORKSPACES;++j)if(frost_instance_workspace(&s->slots[i])==&s->modulation_pool[j])mod_used|=1u<<j;
    for(i=0;i<FROST_SLOTS;++i){
        unsigned type=s->pending_types[i];
        if(type!=s->slots[i].type){
            void *workspace=frost_type_is_delay(type)?(void *)s->delay_workspace:0;
            unsigned policy=s->slots[i].bypass_policy;
            if(frost_type_is_reverb(type))workspace=s->reverb_workspace;
            if(type==FROST_IR)for(j=0;j<FROST_IR_WORKSPACES;++j)if(!(used&(1u<<j))){
                workspace=&s->ir_pool[j];used|=1u<<j;break;
            }
            if(frost_type_is_modulation(type))for(j=0;j<FROST_MOD_WORKSPACES;++j)if(!(mod_used&(1u<<j))){
                workspace=&s->modulation_pool[j];mod_used|=1u<<j;break;
            }
            frost_instance_init_workspace(&s->slots[i],type,workspace);
            initialized|=1u<<i;
            s->slots[i].bypass_policy=policy;
        }
        s->order[i]=s->pending_order[i];
    }
    s->pre_count=s->pending_pre_count;
    if(s->pending_preset_active){
        const FrostPreset *p=&s->pending_preset;unsigned ir=0;
        for(i=0;i<FROST_SLOTS;++i){
            FrostInstance *slot=&s->slots[i];
            for(j=0;j<FROST_PARAMETERS;++j)slot->target[j]=slot->current[j]=p->parameters[i][j];
            slot->wet=slot->wet_target=slot->current[FROST_PARAMETERS]=(float)p->enabled[i];slot->bypass_policy=p->bypass[i];
            slot->suspended=slot->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND && !p->enabled[i];
            if(slot->type==FROST_IR){
                /* Already validated/copied under control ownership. Fixed
                 * <=2*512 coefficients; reset/copy timing needs device tests. */
                unsigned same=slot->state.ir->taps==p->ir_taps[ir],k;
                for(k=0;same && k<p->ir_taps[ir];++k){
                    union{float f;uint32_t u;} a,b;a.f=slot->state.ir->coefficients[k];b.f=p->ir_coefficients[ir][k];
                    if(a.u!=b.u)same=0;
                }
                /* Unchanged assets must not invalidate the controller FIR
                 * mirror and mute it for another coefficient scan/reload. */
                if(!same)(void)frost_ir_load(slot->state.ir,p->ir_coefficients[ir],p->ir_taps[ir]);
                ++ir;
            }
        }
        s->pending_preset_active=0;
    }
    if(s->pending_policy_slot<FROST_SLOTS){
        FrostInstance *slot=&s->slots[s->pending_policy_slot];
        slot->bypass_policy=s->pending_policy_value;slot->wet=slot->wet_target;
        slot->suspended=slot->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND && slot->wet_target==0.0f;
        s->pending_policy_slot=FROST_SLOTS;
    }
    if(frost_ir_bypass_pending<2u){frost_ir_bypass=frost_ir_bypass_pending;frost_ir_bypass_pending=2u;}
    frost_side_route(s,amp);   /* beta.14 stereo fold rule */
#ifdef FROST_IR_LINK
    if(frost_ir_remote_pending<2u){frost_ir_remote=frost_ir_remote_pending;frost_ir_remote_pending=2u;}
#endif
    /* init_workspace already reset changed effects; an IR replacement also
     * resets its ring in frost_ir_load. Retained effects still reset here,
     * and the external amp reset always runs once. */
    frost_chain_reset_histories(s,amp,initialized);
#ifdef FROST_ROUTE_COMMIT_HOOK
    FROST_ROUTE_COMMIT_HOOK(s); /* 31i: reload overlays for the new route */
#endif
}
FROST_CTL_RAM int frost_chain_set(FrostChain *s,unsigned slot,unsigned parameter,float value)
{
    const FrostEffect *d;const FrostParameter *p;
    if(s->transition || slot>=FROST_SLOTS || !finite_float(value))return 0;
    d=frost_effect_find(s->slots[slot].type);
    if(!d || parameter>=d->parameter_count)return 0;
    p=&d->parameters[parameter];
    if(value<p->minimum || value>p->maximum)return 0;
    if((p->flags&FROST_PARAM_STEPPED) && value!=(float)(int)value)return 0;
    s->slots[slot].target[parameter]=value;return 1;
}
FROST_CTL_RAM int frost_chain_enable(FrostChain *s,unsigned slot,unsigned enabled)
{
    if(s->transition || slot>=FROST_SLOTS || enabled>1 || !frost_effect_find(s->slots[slot].type))return 0;
    s->slots[slot].wet_target=(float)enabled;return 1;
}
int frost_chain_set_bypass_policy(FrostChain *s,unsigned slot,unsigned policy)
{
    if(s->started || s->transition || slot>=FROST_SLOTS || policy>FROST_BYPASS_CUT_AND_SUSPEND)return 0;
    s->slots[slot].bypass_policy=policy;return 1;
}
FROST_COLD int frost_chain_request_bypass_policy(FrostChain *s,unsigned slot,unsigned policy)
{
    unsigned i;
    if(s->transition || slot>=FROST_SLOTS || policy>FROST_BYPASS_CUT_AND_SUSPEND)return 0;
    if(s->slots[slot].bypass_policy==policy)return 1;
    for(i=0;i<FROST_SLOTS;++i){s->pending_types[i]=s->slots[i].type;s->pending_order[i]=s->order[i];}
    s->pending_pre_count=s->pre_count;s->pending_preset_active=0;
    s->pending_policy_slot=slot;s->pending_policy_value=policy;
    s->fade_position=0;s->transition=FROST_ROUTE_OUT;return 1;
}
/* Cabinet Override on/off through the silent route fade: the flag changes at the
 * commit (silence), IR histories restart with the other histories. 0 = busy. */
int frost_chain_request_ir_bypass(FrostChain *s,unsigned on);
FROST_COLD int frost_chain_request_ir_bypass(FrostChain *s,unsigned on)
{
    unsigned i;
    if(s->transition || on>1u)return 0;
    if(on==frost_ir_bypass)return 1;
    for(i=0;i<FROST_SLOTS;++i){s->pending_types[i]=s->slots[i].type;s->pending_order[i]=s->order[i];}
    s->pending_pre_count=s->pre_count;s->pending_preset_active=0;s->pending_policy_slot=FROST_SLOTS;
    frost_ir_bypass_pending=on;s->fade_position=0;s->transition=FROST_ROUTE_OUT;return 1;
}
#ifdef FROST_IR_LINK
/* Stage 4: switch IR processing between this processor and the controller
 * through the same silent route fade; histories restart. 0 = busy. */
int frost_chain_request_ir_remote(FrostChain *s,unsigned on);
FROST_COLD int frost_chain_request_ir_remote(FrostChain *s,unsigned on)
{
    unsigned i;
    if(s->transition || on>1u)return 0;
    if(on==frost_ir_remote)return 1;
    for(i=0;i<FROST_SLOTS;++i){s->pending_types[i]=s->slots[i].type;s->pending_order[i]=s->order[i];}
    s->pending_pre_count=s->pre_count;s->pending_preset_active=0;s->pending_policy_slot=FROST_SLOTS;
    frost_ir_remote_pending=on;s->fade_position=0;s->transition=FROST_ROUTE_OUT;return 1;
}
#endif
int frost_chain_load_ir(FrostChain *s,unsigned slot,const float *coefficients,unsigned taps)
{
    if(s->transition || slot>=FROST_SLOTS || s->slots[slot].type!=FROST_IR)return 0;
    return frost_ir_load(s->slots[slot].state.ir,coefficients,taps);
}
FROST_COLD int frost_chain_load_ir_q15(FrostChain *s,unsigned slot,const int16_t *q,unsigned taps,float scale)
{
    if(s->transition || slot>=FROST_SLOTS || s->slots[slot].type!=FROST_IR)return 0;
    return frost_ir_load_q15(s->slots[slot].state.ir,q,taps,scale);
}
/* Per-sample fallback (route fades/faults): out-of-line instance calls keep
 * flash bounded; arithmetic and state order are unchanged. */
static int frost_chain_process_core(FrostChain *s,const float *input,float *output,unsigned count,
                                    const FrostAmpStage *amp,unsigned abort_on_fault)
{
    unsigned n,i;
    frost_side_begin();
    if(count)s->started=1;
    for(n=0;n<count;++n){
        float x=input[n];unsigned bad=!signal_valid(x);
        if(!bad){
            for(i=0;i<s->pre_count && !bad;++i){
                if(!(frost_ir_bypass && s->slots[s->order[i]].type==FROST_IR)){
                    x=frost_instance_sample(&s->slots[s->order[i]],x);bad=!signal_valid(x);
                }
            }
            if(!bad && amp && amp->sample){
                x=amp->sample(amp->state,x);
                bad=!signal_valid(x);
            }
            for(i=s->pre_count;i<FROST_SLOTS && !bad;++i){
                if(!(frost_ir_bypass && s->slots[s->order[i]].type==FROST_IR)){
                    x=frost_instance_sample(&s->slots[s->order[i]],x);bad=!signal_valid(x);
                }
            }
        }
        if(bad){
            frost_chain_reset_histories(s,amp,0);
            if(s->faults!=UINT32_MAX)++s->faults;
            x=0.0f;
            if(abort_on_fault){
                /* Discard the complete block, including its processed prefix.
                 * No further input, kernel, amp or transition work this call. */
                for(i=0;i<count;++i)output[i]=0.0f;
                return 0;
            }
        }
        if(s->transition==FROST_ROUTE_OUT){
            x*= (float)(FROST_ROUTE_FADE-1u-s->fade_position)/(float)FROST_ROUTE_FADE;
            if(++s->fade_position==FROST_ROUTE_FADE){
                frost_chain_commit_route(s,amp);s->fade_position=0;s->transition=FROST_ROUTE_IN;
            }
        }else if(s->transition==FROST_ROUTE_IN){
            x*= (float)(s->fade_position+1u)/(float)FROST_ROUTE_FADE;
            if(++s->fade_position==FROST_ROUTE_FADE){s->fade_position=0;s->transition=FROST_ROUTE_IDLE;}
        }
        output[n]=x;
    }
    return 1;
}
void frost_chain_process(FrostChain *s,const float *input,float *output,unsigned count)
{ (void)frost_chain_process_core(s,input,output,count,0,0); }
void frost_chain_process_with_amp(FrostChain *s,const float *input,float *output,unsigned count,const FrostAmpStage *amp)
{ (void)frost_chain_process_core(s,input,output,count,amp,0); }
/* Guarded fault path shared by both processing orders. */
static int frost_chain_guarded_fault(FrostChain *s,float *output,unsigned count,const FrostAmpStage *amp)
{
    unsigned i;
    frost_chain_reset_histories(s,amp,0);
    if(s->faults!=UINT32_MAX)++s->faults;
    for(i=0;i<count;++i)output[i]=0.0f;
    return 0;
}
FROST_HOT FROST_CORE static int frost_chain_amp_block(const FrostAmpStage *amp,float *x,unsigned count)
{
    unsigned n;
    if(!amp || !amp->sample)return 1;
    for(n=0;n<count;++n){x[n]=amp->sample(amp->state,x[n]);if(!signal_valid(x[n]))return 0;}
    return 1;
}
/* Stage-major processing: each slot (and the amp) runs over the whole block
 * before the next. Per-instance arithmetic and state order are unchanged, so
 * valid blocks are bit-identical to the sample-major core. Used only when no
 * route fade is active and every input sample is valid; otherwise the original
 * per-sample core runs, preserving its exact fade/fault semantics. A stage stops
 * at its first invalid output; the block is then discarded as before. */
/* 31a: a route fade whose current phase (out or in) does not end before the
 * block does runs stage-major too: every slot, then the per-sample fade gain,
 * commit and phase change exactly as the sample-major core applies them after
 * the last slot. A phase boundary inside the block (unaligned host chunks)
 * still takes the core. A faulted fading block keeps none of its fade progress
 * (output silenced and histories reset either way). External linkage. */
void frost_chain_fade_block(FrostChain *s,float *x,unsigned count,const FrostAmpStage *amp);
FROST_HOT FROST_CORE FROST_NOINLINE FROST_COLD void frost_chain_fade_block(FrostChain *s,float *x,unsigned count,const FrostAmpStage *amp)
{
    unsigned n;
    for(n=0;n<count;++n){
        if(s->transition==FROST_ROUTE_OUT){
            float g=(float)(FROST_ROUTE_FADE-1u-s->fade_position)/(float)FROST_ROUTE_FADE;
            x[n]*=g;if(frost_side_used && n<FROST_SIDE_BLOCK)frost_side[n]*=g;   /* beta.14: the side fades too */
            if(++s->fade_position==FROST_ROUTE_FADE){
                if(frost_chain_commit_alone && n+1u==count){s->fade_position=0;s->transition=FROST_ROUTE_COMMIT;}
                else{frost_chain_commit_route(s,amp);s->fade_position=0;s->transition=FROST_ROUTE_IN;}
            }
        }else{
            float g=(float)(s->fade_position+1u)/(float)FROST_ROUTE_FADE;
            x[n]*=g;if(frost_side_used && n<FROST_SIDE_BLOCK)frost_side[n]*=g;
            if(++s->fade_position==FROST_ROUTE_FADE){s->fade_position=0;s->transition=FROST_ROUTE_IDLE;}
        }
    }
}
/* beta.14: the stereo block's side signal follows its on/off wet ramp (the
 * chain applies the wet mix to the mid only). Not with TAIL ON: then the
 * effect's input fades and its echoes keep playing at full level, side too.
 * Called only while the block's wet level is not 1 (fades, rests). External
 * linkage, dsp-data flash (boot-adjacent layout rule). */
void frost_side_follow(const FrostInstance *s,float w0,unsigned count);
FROST_XCODE void frost_side_follow(const FrostInstance *s,float w0,unsigned count)
{
    const FrostEffect *d=frost_effect_find(s->type);unsigned n;float w1=s->wet,step;
    if(!d || !d->parameter_count || s->target[d->parameter_count-1u]>=0.5f)return;   /* TAIL ON */
    if(count>FROST_SIDE_BLOCK)count=FROST_SIDE_BLOCK;
    step=count?(w1-w0)/(float)count:0.0f;
    for(n=0;n<count;++n){w0+=step;frost_side[n]*=w0;}
}
unsigned frost_chain_commit_alone;
FROST_HOT FROST_CORE FROST_AUDIO_RAM int frost_chain_process_guarded(FrostChain *s,const float *input,float *output,unsigned count,const FrostAmpStage *amp)
{
    unsigned i,n,result,fading;uint32_t begin,end;
    frost_side_begin();                     /* beta.14: stereo effects write this block's side */
    if(s->transition==FROST_ROUTE_COMMIT){   /* the silent commit-only block */
        for(n=0;n<count;++n)output[n]=0.0f;
        frost_chain_commit_route(s,amp);s->fade_position=0;s->transition=FROST_ROUTE_IN;
        return 1;
    }
    fading=s->transition!=FROST_ROUTE_IDLE;
#if defined(__arm__) && defined(GP5_FIRMWARE)
    /* 31a target: the per-sample core is not linked (DSP flash). With fixed
     * 64-sample blocks a fade phase (128) always ends on a block boundary,
     * and ADC input is converted from integers (always finite, |x| <= 1), so
     * neither fallback trigger can occur. Should one ever occur, the block is
     * faulted exactly like an invalid input in the core: histories reset, the
     * fault counted (the line test then latches mute), output silenced. */
    if(!count)return 1;
    if(fading && FROST_ROUTE_FADE-s->fade_position<count){FROST_PROFILE_BLOCK(1);return frost_chain_guarded_fault(s,output,count,amp);}
    for(n=0;n<count;++n)if(!signal_valid(input[n])){FROST_PROFILE_BLOCK(1);return frost_chain_guarded_fault(s,output,count,amp);}
#else
    if(!count)return frost_chain_process_core(s,input,output,count,amp,1);
    if(fading && FROST_ROUTE_FADE-s->fade_position<count){FROST_PROFILE_BLOCK(1);return frost_chain_process_core(s,input,output,count,amp,1);}
    for(n=0;n<count;++n)if(!signal_valid(input[n])){FROST_PROFILE_BLOCK(1);return frost_chain_process_core(s,input,output,count,amp,1);}
#endif
    FROST_PROFILE_BLOCK(0);
    s->started=1;
    if(output!=input)for(n=0;n<count;++n)output[n]=input[n];
    for(i=0;i<s->pre_count;++i){
        float w0=s->slots[s->order[i]].wet;
        begin=FROST_PROFILE_CLOCK();
        result=frost_instance_process(&s->slots[s->order[i]],output,count);
        end=FROST_PROFILE_CLOCK();FROST_PROFILE_SLOT(s->order[i],s->slots[s->order[i]].type,begin,end);
        if(result!=count)return frost_chain_guarded_fault(s,output,count,amp);
        if(frost_side_used && s->order[i]==frost_side_slot && (w0!=1.0f || s->slots[s->order[i]].wet!=1.0f))
            frost_side_follow(&s->slots[s->order[i]],w0,count);
    }
    if(!frost_chain_amp_block(amp,output,count))return frost_chain_guarded_fault(s,output,count,amp);
    for(i=s->pre_count;i<FROST_SLOTS;++i){
        float w0=s->slots[s->order[i]].wet;
        begin=FROST_PROFILE_CLOCK();
        result=frost_instance_process(&s->slots[s->order[i]],output,count);
        end=FROST_PROFILE_CLOCK();FROST_PROFILE_SLOT(s->order[i],s->slots[s->order[i]].type,begin,end);
        if(result!=count)return frost_chain_guarded_fault(s,output,count,amp);
        if(frost_side_used && s->order[i]==frost_side_slot && (w0!=1.0f || s->slots[s->order[i]].wet!=1.0f))
            frost_side_follow(&s->slots[s->order[i]],w0,count);
    }
    if(fading)frost_chain_fade_block(s,output,count,amp);
    return 1;
}
#ifdef FROST_HOST_REFERENCE
int frost_chain_process_guarded_reference(FrostChain *s,const float *input,float *output,unsigned count,const FrostAmpStage *amp)
{ return frost_chain_process_core(s,input,output,count,amp,1); }
#endif
