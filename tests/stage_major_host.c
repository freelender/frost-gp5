/* Two identical chains: A uses the stage-major guarded path, B the original
 * sample-major guarded core. Every operation is applied to both. */
#define FROST_HOST_REFERENCE 1
#include "../engine/bundle.c"
#include "fixtures/diagnostic_ir.h"
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
typedef struct {FrostChain c;FrostDelay d;FrostModulation m[FROST_MOD_WORKSPACES];FrostReverb r;} Rig;
static Rig a,b;static unsigned amp_state_a,amp_state_b;
static float amp_fn(void *st,float x){unsigned *k=(unsigned *)st;++*k;return x*0.75f-x*x*x*0.05f;}
static void amp_reset(void *st){(void)st;}
static void rig_init(Rig *r){frost_chain_init(&r->c);(void)frost_chain_bind_delay(&r->c,&r->d);(void)frost_chain_bind_modulation(&r->c,r->m);(void)frost_chain_bind_reverb(&r->c,&r->r);}
API void sm_init(void){memset(&a,0,sizeof a);memset(&b,0,sizeof b);rig_init(&a);rig_init(&b);amp_state_a=amp_state_b=0;}
API unsigned sm_route(const unsigned *types,const unsigned *order,unsigned pre)
{unsigned x=frost_chain_request_route(&a.c,types,order,pre),y=frost_chain_request_route(&b.c,types,order,pre);return x==y?x:99;}
API unsigned sm_set(unsigned slot,unsigned p,float v){unsigned x=frost_chain_set(&a.c,slot,p,v),y=frost_chain_set(&b.c,slot,p,v);return x==y?x:99;}
API unsigned sm_enable(unsigned slot,unsigned on){unsigned x=frost_chain_enable(&a.c,slot,on),y=frost_chain_enable(&b.c,slot,on);return x==y?x:99;}
API unsigned sm_policy(unsigned slot,unsigned policy){unsigned x=frost_chain_request_bypass_policy(&a.c,slot,policy),y=frost_chain_request_bypass_policy(&b.c,slot,policy);return x==y?x:99;}
API unsigned sm_ir(unsigned slot,unsigned taps){unsigned x=frost_chain_load_ir(&a.c,slot,frost_test_ir,taps),y=frost_chain_load_ir(&b.c,slot,frost_test_ir,taps);return x==y?x:99;}
API unsigned sm_transition(void){return a.c.transition;}
API unsigned sm_type(unsigned slot){return a.c.slots[slot].type;}
API unsigned sm_param_count(unsigned slot){const FrostEffect *d=frost_effect_find(a.c.slots[slot].type);return d?d->parameter_count:0;}
API float sm_bound(unsigned slot,unsigned p,unsigned hi){const FrostEffect *d=frost_effect_find(a.c.slots[slot].type);return hi?d->parameters[p].maximum:d->parameters[p].minimum;}
/* Returns 1 when results, outputs (bitwise), faults, fade position and amp call counts all match. */
API unsigned sm_block(const float *x,unsigned n,unsigned use_amp,unsigned inplace)
{
    static float ya[512],yb[512];FrostAmpStage sa={amp_fn,amp_reset,&amp_state_a},sb={amp_fn,amp_reset,&amp_state_b};
    int ra,rb;if(n>512)return 0;
    if(inplace){memcpy(ya,x,n*sizeof(float));ra=frost_chain_process_guarded(&a.c,ya,ya,n,use_amp?&sa:0);}
    else ra=frost_chain_process_guarded(&a.c,x,ya,n,use_amp?&sa:0);
    rb=frost_chain_process_guarded_reference(&b.c,x,yb,n,use_amp?&sb:0);
    return ra==rb && !memcmp(ya,yb,n*sizeof(float)) && a.c.faults==b.c.faults &&
           a.c.fade_position==b.c.fade_position && a.c.transition==b.c.transition;
}
API unsigned sm_amp_calls_equal(void){return amp_state_a==amp_state_b;}
API unsigned sm_faults(void){return a.c.faults;}
