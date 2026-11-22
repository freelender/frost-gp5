/* Offline factory-bank rendering. Same compact reverb kernels as the pedal;
 * full 512-tap quantized cabinet coefficients, local float FIR (no SPI delay).
 * No audio IO, device access or timing claims. */
#define FROST_REVERB_INT16 1
#include "../engine/bundle.c"
#include "../firmware/protocol/slot_wire.h"
#include <math.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static FrostChain chain;
static FrostDelay delay;
static FrostModulation mod[FROST_MOD_WORKSPACES];
static FrostReverbWorkspace reverb;
API int factory_setup(const uint8_t *record,const float *ir,unsigned taps)
{
    unsigned types[6],i,j;
    frost_chain_init(&chain);frost_chain_bind_delay(&chain,&delay);
    frost_chain_bind_modulation(&chain,mod);frost_chain_bind_reverb(&chain,&reverb);
    for(i=0;i<6;++i)types[i]=record[32+36*i];
    if(!frost_chain_configure(&chain,types,6))return 0;
    for(i=0;i<6;++i){
        const uint8_t *p=record+32+36*i;
        if(types[i]==FROST_EMPTY)continue;
        if(types[i]==FROST_IR && !frost_chain_load_ir(&chain,i,ir,taps))return 0;
        for(j=0;j<p[3];++j){
            union {uint32_t u;float f;} v;v.u=frost_wire_get32(p+4+4*j);
            if(!frost_chain_set(&chain,i,j,v.f))return 0;
            chain.slots[i].current[j]=v.f; /* preset recall starts at exact targets */
        }
        if(!frost_chain_enable(&chain,i,p[1]))return 0;
        chain.slots[i].wet=chain.slots[i].wet_target;
    }
    return 1;
}
API unsigned factory_render(const float *input,float *output,unsigned count)
{
    unsigned i,n;
    for(i=0;i<count;i+=64){n=count-i<64?count-i:64;
        if(!frost_chain_process_guarded(&chain,input+i,output+i,n,0))return 0;}
    return chain.faults==0;
}
/* Explicit guitar-band energy proxy, not LUFS: 80 Hz high-pass followed by
 * 8 kHz low-pass. Double accumulation; caller measures the same active window. */
API double factory_energy(const float *x,unsigned n)
{
    unsigned i;double lo=0,hi=0,energy=0;
    const double a=1.0-exp(-6.283185307179586*80.0/44100.0);
    const double b=1.0-exp(-6.283185307179586*8000.0/44100.0);
    for(i=0;i<n;++i){lo+=a*(x[i]-lo);hi+=b*(x[i]-lo-hi);energy+=hi*hi;}
    return n?energy/n:0;
}
