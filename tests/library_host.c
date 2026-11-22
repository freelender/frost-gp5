/* Host fixture for the pedal-inspired model library: two independent chains
 * with their own workspaces, guarded (stage-major) and reference processing,
 * native preset capture/recall and registry metadata. No production hooks. */
#define FROST_HOST_REFERENCE
#include "../engine/bundle.c"
#include "../firmware/protocol/slots.c"
#include "../firmware/protocol/preset_transfer.c"
#include "../firmware/protocol/preset_read.c"
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
typedef struct {
    FrostChain chain;FrostDelay delay;FrostModulation mod[FROST_MOD_WORKSPACES];FrostReverb reverb;
    FrostSlotProtocol protocol;FrostPresetTransfer transfer;uint8_t reply[48];
} Fixture;
static Fixture fx[2];
static FrostPreset snapshot;
API unsigned lib_effect_count(void){return FROST_EFFECTS;}
API unsigned lib_parameter_count(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->parameter_count:0;}
API const char *lib_name(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->name:0;}
API const char *lib_parameter_name(unsigned type,unsigned p){const FrostEffect *d=frost_effect_find(type);return d && p<d->parameter_count?d->parameters[p].name:0;}
API float lib_bound(unsigned type,unsigned p,unsigned field)
{const FrostEffect *d=frost_effect_find(type);const FrostParameter *v;if(!d || p>=d->parameter_count)return -1e9f;v=&d->parameters[p];
 return field==0?v->minimum:field==1?v->maximum:field==2?v->initial:(float)v->flags;}
API unsigned lib_workspace(unsigned type){return frost_type_workspace(type);}
API unsigned lib_category(unsigned type){return frost_type_category(type);}
API unsigned lib_instance_bytes(void){return sizeof(FrostInstance);}
API unsigned lib_state_bytes(void){return sizeof(FrostEffectState);}
API unsigned lib_chain_bytes(void){return sizeof(FrostChain);}
API unsigned lib_delay_bytes(void){return sizeof(FrostDelay);}
API unsigned lib_mod_bytes(void){return sizeof(FrostModulation);}
API unsigned lib_reverb_bytes(void){return sizeof(FrostReverb);}
API unsigned lib_rv6_samples(unsigned mode){return frost_rv6_mode_samples(mode);}
/* 27a: block vs per-sample RV-6, bitwise, through warmup, all modes, TIME/
 * TONE/LEVEL/mode changes between blocks and steady wraps. -1 = identical. */
static FrostReverb rv6_wa,rv6_wb;
API int lib_rv6_block_matches(unsigned total,unsigned seed)
{
    FrostReverbModel a={&rv6_wa},b={&rv6_wb};unsigned i=0,k,n;uint32_t r=seed*2654435761u+1u;
    float in[64],ya[64],yb[64],p[4]={50.0f,50.0f,50.0f,1.0f};
    memset(&rv6_wa,0,sizeof rv6_wa);memset(&rv6_wb,0,sizeof rv6_wb);
    /* Stale workspace contents (e.g. a previous reverb-class model) must never
     * be heard: the reference masks them by validity, 27b by zero-filling. */
    for(k=0;k<FROST_REVERB_SAMPLES;++k)rv6_wa.samples[k]=rv6_wb.samples[k]=0.25f+(float)(k%7u);
    frost_rv6_reset(&a);frost_rv6_reset(&b);
    while(i<total){
        r=r*1664525u+1013904223u;n=1u+(r>>26)%64u;if(n>total-i)n=total-i;
        if(((r>>8)&63u)==0)p[3]=(float)((r>>16)%5u);
        if(((r>>9)&15u)==0)p[2]=(float)((r>>12)%101u);
        if(((r>>10)&15u)==0)p[1]=(float)((r>>14)%101u)+0.25f;
        if(((r>>11)&15u)==0)p[0]=(float)((r>>18)%101u);
        for(k=0;k<n;++k)in[k]=(i+k)%4000u<3?0.9f:0.3f*(float)((int)((i+k)*7919u%2001u)-1000)/1000.0f;
        for(k=0;k<n;++k)ya[k]=frost_rv6_sample_reference(&a,in[k],p);
        if(n&1u)for(k=0;k<n;++k)yb[k]=frost_rv6_sample(&b,in[k],p);
        else frost_rv6_block(&b,in,yb,n,p);
        for(k=0;k<n;++k)if(memcmp(&ya[k],&yb[k],sizeof(float)))return (int)(i+k);
        i+=n;
    }
    /* 27b: buffers may differ only in never-readable slots (zero-filled vs
     * stale); positions and filter/phase state must match exactly. */
    if(memcmp(rv6_wa.model.position,rv6_wb.model.position,sizeof rv6_wa.model.position) ||
       memcmp(rv6_wa.model.z,rv6_wb.model.z,sizeof rv6_wa.model.z))return 2000000000;
    return -1;
}
API unsigned lib_spring_samples(void){return frost_spring_samples();}
API unsigned lib_reverb_capacity(void){return FROST_REVERB_SAMPLES;}
API float lib_ge7_centre(unsigned i){return i<FROST_GE7_BANDS?frost_ge7_centres[i]:0.0f;}
API void lib_init(unsigned c)
{
    Fixture *f=&fx[c];memset(f,0,sizeof(*f));frost_chain_init(&f->chain);
    (void)frost_chain_bind_delay(&f->chain,&f->delay);(void)frost_chain_bind_modulation(&f->chain,f->mod);
    (void)frost_chain_bind_reverb(&f->chain,&f->reverb);frost_slots_init(&f->protocol);frost_preset_transfer_init(&f->transfer);
}
API int lib_configure(unsigned c,const unsigned *types,unsigned n){return frost_chain_configure(&fx[c].chain,types,n);}
API int lib_route(unsigned c,const unsigned *types,const unsigned *order,unsigned pre){return frost_chain_request_route(&fx[c].chain,types,order,pre);}
API int lib_set(unsigned c,unsigned slot,unsigned p,float v){return frost_chain_set(&fx[c].chain,slot,p,v);}
API int lib_enable(unsigned c,unsigned slot,unsigned on){return frost_chain_enable(&fx[c].chain,slot,on);}
API int lib_policy(unsigned c,unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&fx[c].chain,slot,policy);}
API unsigned lib_type(unsigned c,unsigned slot){return fx[c].chain.slots[slot].type;}
API float lib_target(unsigned c,unsigned slot,unsigned p){return fx[c].chain.slots[slot].target[p];}
API unsigned lib_faults(unsigned c){return fx[c].chain.faults;}
API unsigned lib_transition(unsigned c){return fx[c].chain.transition;}
API const void *lib_workspace_ptr(unsigned c,unsigned slot){return frost_instance_workspace(&fx[c].chain.slots[slot]);}
API const void *lib_mod_pool(unsigned c,unsigned i){return &fx[c].mod[i];}
/* Guarded production path in chunks; returns number of failed blocks. */
API unsigned lib_process(unsigned c,const float *x,float *y,unsigned n,unsigned chunk)
{
    unsigned i,bad=0;
    for(i=0;i<n;i+=chunk){unsigned k=n-i<chunk?n-i:chunk;if(!frost_chain_process_guarded(&fx[c].chain,x+i,y+i,k,0))++bad;}
    return bad;
}
API unsigned lib_process_reference(unsigned c,const float *x,float *y,unsigned n,unsigned chunk)
{
    unsigned i,bad=0;
    for(i=0;i<n;i+=chunk){unsigned k=n-i<chunk?n-i:chunk;if(!frost_chain_process_guarded_reference(&fx[c].chain,x+i,y+i,k,0))++bad;}
    return bad;
}
API void lib_reset(unsigned c){unsigned i;for(i=0;i<FROST_SLOTS;++i)frost_instance_reset_history(&fx[c].chain.slots[i]);}
/* Native snapshot helpers. */
API int lib_capture(unsigned c){return frost_chain_capture_preset(&fx[c].chain,&snapshot);}
API int lib_recall(unsigned c){return frost_chain_request_preset(&fx[c].chain,&snapshot);}
API unsigned lib_snapshot_version(void){return snapshot.version;}
API float lib_snapshot_parameter(unsigned slot,unsigned p){return snapshot.parameters[slot][p];}
API void lib_snapshot_set_parameter(unsigned slot,unsigned p,float v){snapshot.parameters[slot][p]=v;}
API void lib_snapshot_set_version(unsigned v){snapshot.version=v;}
API void lib_snapshot_set_type(unsigned slot,unsigned t){snapshot.types[slot]=(uint8_t)t;}
API unsigned lib_snapshot_bytes(void){return sizeof(FrostPreset);}
/* Wire endpoints on the same fixture (slot 0x7f40, preset 0x7f43/0x7f44). */
API const uint8_t *lib_slot(unsigned c,const uint8_t *request,unsigned n)
{frost_slots_command(&fx[c].protocol,&fx[c].chain,request,n,fx[c].reply);return fx[c].reply;}
API const uint8_t *lib_upload(unsigned c,const uint8_t *request,unsigned n)
{frost_preset_command(&fx[c].transfer,&fx[c].protocol,&fx[c].chain,request,n,fx[c].reply);return fx[c].reply;}
API const uint8_t *lib_download(unsigned c,const uint8_t *request,unsigned n)
{frost_preset_read_command(&fx[c].protocol,&fx[c].chain,request,n,fx[c].reply);return fx[c].reply;}
API unsigned lib_revision(unsigned c){return fx[c].protocol.revision;}
API int lib_pending_valid(unsigned c){return preset_valid(&fx[c].chain,&fx[c].chain.pending_preset);}
API float lib_pending_parameter(unsigned c,unsigned slot,unsigned p){return fx[c].chain.pending_preset.parameters[slot][p];}
API unsigned lib_pending_field(unsigned c,unsigned which,unsigned i)
{const FrostPreset *p=&fx[c].chain.pending_preset;return which==0?p->version:which==1?p->types[i]:which==2?p->enabled[i]:which==3?p->ir_taps[i]:p->pre_count;}
