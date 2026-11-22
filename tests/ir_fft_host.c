/* 31a FFT cabinet IR fixture. Kernel level: one FrostIR with an FFT workspace
 * against one without (direct form). Chain level: rig A binds FFT workspaces
 * and runs the stage-major guarded path; rig B has none and runs the original
 * sample-major guarded core. Every operation is applied to both; outputs are
 * compared by the Python tests within a rounding tolerance. */
#define FROST_HOST_REFERENCE 1
#include "../engine/bundle.c"
#include "fixtures/diagnostic_ir.h"
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static struct {unsigned before;FrostIR ir;unsigned mid;FrostIRFFT fft;
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    unsigned spectrum_before;float spectrum[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE];unsigned spectrum_after;
#endif
    unsigned after;} k_fft;
static FrostIR k_ref;
API void k_init(unsigned bind)
{
    memset(&k_fft,0,sizeof k_fft);memset(&k_ref,0,sizeof k_ref);
    k_fft.before=0x12345678u;k_fft.mid=0x55aa55aau;k_fft.after=0x87654321u;
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    k_fft.spectrum_before=0x13572468u;k_fft.spectrum_after=0x24681357u;
    k_fft.fft.spectrum=k_fft.spectrum;
#endif
    (void)frost_ir_bind_fft(&k_fft.ir,0);(void)frost_ir_bind_fft(&k_ref,0);(void)frost_ir_bind_fft(&k_fft.ir,bind?&k_fft.fft:0);
}
API int k_load(const float *c,unsigned taps){int a=frost_ir_load(&k_fft.ir,c,taps),b=frost_ir_load(&k_ref,c,taps);return a==b?a:99;}
API int k_load_q15(const int16_t *q,unsigned taps,float scale){int a=frost_ir_load_q15(&k_fft.ir,q,taps,scale),b=frost_ir_load_q15(&k_ref,q,taps,scale);return a==b?a:99;}
API float k_coefficient(unsigned i){return k_fft.ir.coefficients[i];}
API unsigned k_parts(void){return k_fft.fft.parts;}
API unsigned k_fill(void){return k_fft.fft.fill;}
API int k_guard(void){return k_fft.before==0x12345678u && k_fft.mid==0x55aa55aau && k_fft.after==0x87654321u
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    && k_fft.spectrum_before==0x13572468u && k_fft.spectrum_after==0x24681357u
#endif
    ;}
API int k_missing_spectrum(void)
{
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    FrostIRFFT bad;FrostIR before=k_fft.ir;FrostIRFFT *bound=frost_ir_fft_of(&k_fft.ir);
    memset(&bad,0,sizeof bad);
    return !frost_ir_bind_fft(&k_fft.ir,&bad) && frost_ir_fft_of(&k_fft.ir)==bound && !memcmp(&before,&k_fft.ir,sizeof before);
#else
    return -1;
#endif
}
/* mode 0: per-sample on both; mode 1: FFT block (count must be 64, aligned) */
API int k_process(const float *x,float *ya,float *yb,unsigned count,unsigned mode,unsigned inplace)
{
    unsigned i;
    for(i=0;i<count;++i)yb[i]=frost_ir_sample(&k_ref,x[i]);
    if(mode){
        if(count!=FROST_IR_FFT_BLOCK || !frost_ir_fft_ready(&k_fft.ir))return 0;
        if(inplace){memcpy(ya,x,count*sizeof(float));frost_ir_fft_block(&k_fft.ir,ya,ya);}
        else frost_ir_fft_block(&k_fft.ir,x,ya);
    }else for(i=0;i<count;++i)ya[i]=frost_ir_sample(&k_fft.ir,x[i]);
    return 1;
}
API void k_clear(void){frost_ir_clear(&k_fft.ir);frost_ir_clear(&k_ref);}
/* Direct-form state equality (FFT path must keep the time ring in step). */
API int k_history_equal(void)
{return k_fft.ir.position==k_ref.position && !memcmp(k_fft.ir.history,k_ref.history,sizeof k_ref.history);}

typedef struct {FrostChain c;FrostDelay d;FrostModulation m[FROST_MOD_WORKSPACES];FrostReverb r;FrostIRFFT f[FROST_IR_WORKSPACES];
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    float spectrum[FROST_IR_WORKSPACES][FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE];
#endif
} Rig;
static Rig a,b;
static void rig_init(Rig *r,unsigned ffts)
{
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
 unsigned i;for(i=0;i<FROST_IR_WORKSPACES;++i)r->f[i].spectrum=r->spectrum[i];
#endif
 frost_chain_init(&r->c);(void)frost_chain_bind_delay(&r->c,&r->d);(void)frost_chain_bind_modulation(&r->c,r->m);
 (void)frost_chain_bind_reverb(&r->c,&r->r);if(ffts)(void)frost_chain_bind_ir_fft(&r->c,r->f,ffts);}
API void c_init(unsigned ffts){memset(&a,0,sizeof a);memset(&b,0,sizeof b);rig_init(&a,ffts);rig_init(&b,0);}
API unsigned c_route(const unsigned *types,const unsigned *order,unsigned pre)
{unsigned x=frost_chain_request_route(&a.c,types,order,pre),y=frost_chain_request_route(&b.c,types,order,pre);return x==y?x:99;}
API unsigned c_set(unsigned slot,unsigned p,float v){unsigned x=frost_chain_set(&a.c,slot,p,v),y=frost_chain_set(&b.c,slot,p,v);return x==y?x:99;}
API unsigned c_enable(unsigned slot,unsigned on){unsigned x=frost_chain_enable(&a.c,slot,on),y=frost_chain_enable(&b.c,slot,on);return x==y?x:99;}
API unsigned c_policy(unsigned slot,unsigned policy){unsigned x=frost_chain_request_bypass_policy(&a.c,slot,policy),y=frost_chain_request_bypass_policy(&b.c,slot,policy);return x==y?x:99;}
API unsigned c_ir(unsigned slot,unsigned taps){unsigned x=frost_chain_load_ir(&a.c,slot,frost_test_ir,taps),y=frost_chain_load_ir(&b.c,slot,frost_test_ir,taps);return x==y?x:99;}
API unsigned c_ir_values(unsigned slot,const float *v,unsigned taps){unsigned x=frost_chain_load_ir(&a.c,slot,v,taps),y=frost_chain_load_ir(&b.c,slot,v,taps);return x==y?x:99;}
API unsigned c_transition(void){return a.c.transition;}
API unsigned c_type(unsigned slot){return a.c.slots[slot].type;}
API unsigned c_fft_slot(unsigned slot){const FrostInstance *s=&a.c.slots[slot];const FrostIRFFT *f=s->type==FROST_IR?frost_ir_fft_of(s->state.ir):0;return f && f->parts;}
/* Returns 1 when results, faults, fade position and transition match; outputs go to ya/yb. */
API unsigned c_block(const float *x,float *ya,float *yb,unsigned n,unsigned inplace)
{
    int ra,rb;
    if(inplace){memcpy(ya,x,n*sizeof(float));ra=frost_chain_process_guarded(&a.c,ya,ya,n,0);}
    else ra=frost_chain_process_guarded(&a.c,x,ya,n,0);
    rb=frost_chain_process_guarded_reference(&b.c,x,yb,n,0);
    return ra==rb && a.c.faults==b.c.faults && a.c.fade_position==b.c.fade_position && a.c.transition==b.c.transition;
}
API unsigned c_faults(void){return a.c.faults;}
API unsigned c_bytes(void){return sizeof(FrostIRFFT);}
