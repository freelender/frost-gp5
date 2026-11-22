#include <string.h>
#include "../engine/bundle.c"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static FrostChain chains[2];
static struct {unsigned calls,resets,mode;float previous;} amps[2];
static float test_amp_sample(void *context,float input)
{
    unsigned c=*(unsigned *)context;float result=input;
    ++amps[c].calls;
    if(amps[c].mode==1)result=input>.15f?.15f:input<-.15f?-.15f:input;
    if(amps[c].mode==2){union {uint32_t u;float f;} v;v.u=0x7fc00000u;result=v.f;}
    if(amps[c].mode==3)result=17.0f;
    if(amps[c].mode==4){result=input+.5f*amps[c].previous;amps[c].previous=input;}
    return result;
}
static void test_amp_reset(void *context)
{ unsigned c=*(unsigned *)context;++amps[c].resets;amps[c].previous=0.0f; }
static unsigned amp_context[2]={0,1};
API void fx_init(unsigned c){frost_chain_init(&chains[c]);amps[c].calls=amps[c].resets=amps[c].mode=0;amps[c].previous=0.0f;}
API int fx_configure(unsigned c,const unsigned *types,unsigned n){return frost_chain_configure(&chains[c],types,n);}
API int fx_set(unsigned c,unsigned slot,unsigned p,float v){return frost_chain_set(&chains[c],slot,p,v);}
/* Exact cold initial parameter, as preset recall does; isolate FIR arithmetic
 * from smoothing between the product VOL80 default and an explicit unity. */
API int fx_set_initial(unsigned c,unsigned slot,unsigned p,float v)
{if(chains[c].started || !frost_chain_set(&chains[c],slot,p,v))return 0;
 chains[c].slots[slot].current[p]=v;return 1;}
API int fx_enable(unsigned c,unsigned slot,unsigned on){return frost_chain_enable(&chains[c],slot,on);}
API int fx_ir(unsigned c,unsigned slot,const float *v,unsigned n){return frost_chain_load_ir(&chains[c],slot,v,n);}
API void fx_process(unsigned c,const float *x,float *y,unsigned n){frost_chain_process(&chains[c],x,y,n);}
API void fx_inplace(unsigned c,float *x,unsigned n){frost_chain_process(&chains[c],x,x,n);}
API unsigned fx_type(unsigned c,unsigned slot){return chains[c].slots[slot].type;}
API unsigned fx_bytes(void){return sizeof(FrostChain);}
API unsigned fx_instance_bytes(void){return sizeof(FrostInstance);}
API unsigned fx_state_bytes(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->state_bytes:0;}
API unsigned fx_workspace_bytes(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->workspace_bytes:0;}
API unsigned fx_parameters(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->parameter_count:0;}
API int fx_same_slot(unsigned a,unsigned b,unsigned slot)
{
    const FrostInstance *x=&chains[a].slots[slot],*y=&chains[b].slots[slot];
    const FrostEffect *d=frost_effect_find(x->type);
    return x->type==y->type && x->wet==y->wet && x->wet_target==y->wet_target &&
        x->bypass_policy==y->bypass_policy && x->suspended==y->suspended &&
        memcmp(x->current,y->current,sizeof(x->current))==0 &&
        memcmp(x->target,y->target,sizeof(x->target))==0 &&
        (!d || (x->type==FROST_REVERB?memcmp(x->state.reverb,y->state.reverb,sizeof(FrostReverb))==0:
            frost_type_is_modulation(x->type)?memcmp(x->state.modulation,y->state.modulation,sizeof(FrostModulation))==0:
            x->type==FROST_DELAY?memcmp(x->state.delay,y->state.delay,sizeof(FrostDelay))==0:
            x->type==FROST_IR?memcmp(x->state.ir,y->state.ir,sizeof(FrostIR))==0:
            memcmp(&x->state,&y->state,d->state_bytes)==0));
}
API float fx_eq_low(unsigned c,unsigned slot){return chains[c].slots[slot].state.eq.low;}
API int fx_request(unsigned c,const unsigned *types,const unsigned *order,unsigned pre)
{return frost_chain_request_route(&chains[c],types,order,pre);}
API unsigned fx_phase(unsigned c){return frost_chain_transition(&chains[c]);}
API unsigned fx_pre(unsigned c){return chains[c].pre_count;}
API unsigned fx_order(unsigned c,unsigned i){return chains[c].order[i];}
API unsigned fx_faults(unsigned c){return chains[c].faults;}
API void fx_amp_process(unsigned c,const float *x,float *y,unsigned n,unsigned mode)
{
    FrostAmpStage stage={test_amp_sample,test_amp_reset,&amp_context[c]};amps[c].mode=mode;
    frost_chain_process_with_amp(&chains[c],x,y,n,&stage);
}
API unsigned fx_amp_calls(unsigned c){return amps[c].calls;}
API unsigned fx_amp_resets(unsigned c){return amps[c].resets;}
API float fx_ir_coefficient(unsigned c,unsigned slot,unsigned i){return chains[c].slots[slot].state.ir->coefficients[i];}
API unsigned fx_ir_taps(unsigned c,unsigned slot){return chains[c].slots[slot].state.ir->taps;}
API unsigned fx_ir_distinct(unsigned c,unsigned a,unsigned b){return chains[c].slots[a].state.ir!=chains[c].slots[b].state.ir;}
API float fx_target(unsigned c,unsigned slot,unsigned parameter){return chains[c].slots[slot].target[parameter];}
API unsigned fx_enabled(unsigned c,unsigned slot){return (unsigned)chains[c].slots[slot].wet_target;}
