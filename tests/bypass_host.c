/* Count real effect kernel invocations without adding production counters. */
#include "../engine/chain.h"
#include "../effects/gain/gain.h"
#include "../effects/noise_gate/noise_gate.h"
#include "../effects/eq/eq.h"
#define frost_gain_sample original_gain_sample
#define frost_gate_sample original_gate_sample
#define frost_eq_sample original_eq_sample
#define frost_ir_sample original_ir_sample
#define frost_overdrive_sample original_overdrive_sample
#include "../effects/gain/gain.c"
#include "../effects/noise_gate/noise_gate.c"
#include "../effects/eq/eq.c"
#include "../effects/cabinet_ir/ir.c"
#include "../effects/overdrive/overdrive.c"
#include "../effects/delay/delay.c"
#include "../effects/modulation/modulation.c"
#include "../effects/chorus/chorus.c"
#include "../effects/flanger/flanger.c"
#include "../effects/reverb/reverb.c"
#include "../effects/common/frost_dsp.c"
#include "../effects/drives/drives.c"
#include "../effects/dynamics/dynamics.c"
#include "../effects/modulators/modulators.c"
#include "../effects/octave/octave.c"
#include "../effects/drop_tune/drop_tune.c"
#include "../effects/delays/delays.c"
#include "../effects/reverbs/reverbs.c"
#include "../effects/fcm/fcm_effect.c"
#include "../effects/ns1x/ns1x.c"
#include "../effects/multiband/multiband.c"
#include "../effects/highpass/highpass.c"
#include "../effects/lowpass/lowpass.c"
#include "../effects/autowah/autowah.c"
#include "../effects/treble_boost/treble_boost.c"
#include "../effects/extras/extras.c"
#include "../effects/foxx/foxx.c"
#include "../effects/tape/tape.c"
#include "../effects/vintage_fuzz/vintage_fuzz.c"
#include "../effects/acoustic/acoustic.c"
#include "../effects/amp_heads/amp_heads.c"
#undef frost_gain_sample
#undef frost_gate_sample
#undef frost_eq_sample
#undef frost_ir_sample
#undef frost_overdrive_sample
static FrostChain chains[2];
static unsigned active,calls[2][FROST_EFFECTS],ir_calls[2][FROST_IR_WORKSPACES];
float frost_overdrive_sample(FrostOverdrive *s,float x,const float *p)
{++calls[active][FROST_OVERDRIVE];return original_overdrive_sample(s,x,p);}
float frost_gain_sample(float x,float gain){++calls[active][FROST_GAIN];return original_gain_sample(x,gain);}
float frost_gate_sample(float x,float threshold,float *envelope,float *gain,uint32_t *open)
{++calls[active][FROST_GATE];return original_gate_sample(x,threshold,envelope,gain,open);}
float frost_eq_sample(float x,const float *gains,float *low,float *high_low)
{++calls[active][FROST_EQ];return original_eq_sample(x,gains,low,high_low);}
float frost_ir_sample(FrostIR *s,float x)
{
    unsigned i;++calls[active][FROST_IR];
    for(i=0;i<FROST_IR_WORKSPACES;++i)if(s==&chains[active].ir_pool[i])++ir_calls[active][i];
    return original_ir_sample(s,x);
}
#include "../engine/effect.c"
#include "../engine/chain.c"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void bypass_init(unsigned c)
{unsigned i;frost_chain_init(&chains[c]);for(i=0;i<FROST_EFFECTS;++i)calls[c][i]=0;for(i=0;i<FROST_IR_WORKSPACES;++i)ir_calls[c][i]=0;}
API int bypass_configure(unsigned c,const unsigned *types,unsigned n){return frost_chain_configure(&chains[c],types,n);}
API int bypass_policy(unsigned c,unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&chains[c],slot,policy);}
API int bypass_enable(unsigned c,unsigned slot,unsigned on){return frost_chain_enable(&chains[c],slot,on);}
API int bypass_set(unsigned c,unsigned slot,unsigned p,float value){return frost_chain_set(&chains[c],slot,p,value);}
API int bypass_ir(unsigned c,unsigned slot,const float *v,unsigned n){return frost_chain_load_ir(&chains[c],slot,v,n);}
API void bypass_process(unsigned c,const float *x,float *y,unsigned n){active=c;frost_chain_process(&chains[c],x,y,n);}
API unsigned bypass_calls(unsigned c,unsigned type){return calls[c][type];}
API unsigned bypass_ir_calls(unsigned c,unsigned pool){return ir_calls[c][pool];}
API unsigned bypass_suspended(unsigned c,unsigned slot){return chains[c].slots[slot].suspended;}
API unsigned bypass_get_policy(unsigned c,unsigned slot){return chains[c].slots[slot].bypass_policy;}
API float bypass_wet(unsigned c,unsigned slot){return chains[c].slots[slot].wet;}
API float bypass_current(unsigned c,unsigned slot,unsigned p){return chains[c].slots[slot].current[p];}
API unsigned bypass_position(unsigned c,unsigned slot){return chains[c].slots[slot].state.ir->position;}
API float bypass_coefficient(unsigned c,unsigned slot,unsigned i){return chains[c].slots[slot].state.ir->coefficients[i];}
API int bypass_history_zero(unsigned c,unsigned slot)
{
    unsigned i;const FrostInstance *s=&chains[c].slots[slot];
    if(s->type==FROST_GATE)return s->state.gate.envelope==0 && s->state.gate.gain==0 && !s->state.gate.open;
    if(s->type==FROST_EQ)return s->state.eq.low==0 && s->state.eq.high_low==0;
    if(s->type==FROST_OVERDRIVE)return s->state.overdrive.previous==0 && s->state.overdrive.highpass==0 && s->state.overdrive.tone==0;
    if(s->type==FROST_IR){for(i=0;i<s->state.ir->taps;++i)if(s->state.ir->history[i]!=0)return 0;}
    return 1;
}
API int bypass_route(unsigned c,const unsigned *types,const unsigned *order,unsigned pre)
{return frost_chain_request_route(&chains[c],types,order,pre);}
