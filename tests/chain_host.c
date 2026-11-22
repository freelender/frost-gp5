#include "../engine/bundle.c"
static FrostChain state;
static FrostDelay delay_workspace;
static FrostModulation modulation_pool[FROST_MOD_WORKSPACES];
static FrostReverb reverb_workspace;
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void chain_init(void){frost_chain_init(&state);(void)frost_chain_bind_delay(&state,&delay_workspace);(void)frost_chain_bind_modulation(&state,modulation_pool);(void)frost_chain_bind_reverb(&state,&reverb_workspace);}
API int chain_set(unsigned e,unsigned p,float v){return frost_chain_set(&state,e,p,v);}
API int chain_enable(unsigned e,unsigned on){return frost_chain_enable(&state,e,on);}
API int chain_bypass_policy(unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&state,slot,policy);}
API void chain_process(const float *x,float *y,unsigned n){frost_chain_process(&state,x,y,n);}
API unsigned chain_bytes(void){return sizeof(state);}
API unsigned chain_faults(void){return state.faults;}
API unsigned chain_effect_count(void){return FROST_EFFECTS;}
API unsigned chain_workspace_capacity(unsigned group)
{return group==0?FROST_IR_WORKSPACES:group==1?FROST_DELAY_WORKSPACES:group==2?FROST_MOD_WORKSPACES:group==3?FROST_REVERB_WORKSPACES:0;}
API unsigned chain_type_workspace(unsigned type){return frost_type_workspace(type);}
API unsigned chain_parameter_flags(unsigned type,unsigned p){const FrostEffect *d=frost_effect_find(type);return d && p<d->parameter_count?d->parameters[p].flags:0;}
API unsigned chain_parameter_count(unsigned type){const FrostEffect *d=frost_effect_find(type);return d?d->parameter_count:0;}
API const char *chain_parameter_name(unsigned type,unsigned p)
{const FrostEffect *d=frost_effect_find(type);return d && p<d->parameter_count?d->parameters[p].name:0;}
API float chain_parameter_bound(unsigned type,unsigned p,unsigned field)
{const FrostEffect *d=frost_effect_find(type);const FrostParameter *v;
 if(!d || p>=d->parameter_count)return 0.0f;v=&d->parameters[p];return field==0?v->minimum:field==1?v->maximum:v->initial;}
API int chain_configure(const unsigned *types,unsigned count){return frost_chain_configure(&state,types,count);}
API int chain_slot_ir(unsigned slot,const float *v,unsigned taps){return frost_chain_load_ir(&state,slot,v,taps);}
API int chain_load_ir(const float *coefficients,unsigned taps){return frost_chain_load_ir(&state,FROST_IR,coefficients,taps);}
static struct {unsigned before;FrostIR ir;unsigned after;} guarded_ir;
API void ir_guard_init(void){guarded_ir.before=0x12345678;guarded_ir.after=0x87654321;}
API int ir_load(const float *coefficients,unsigned taps){return frost_ir_load(&guarded_ir.ir,coefficients,taps);}
API void ir_process(const float *x,float *y,unsigned count){unsigned i;for(i=0;i<count;++i)y[i]=frost_ir_sample(&guarded_ir.ir,x[i]);}
API void ir_process_pairs(const float *x,float *y,unsigned count)
{unsigned i=0;for(;i+1<count;i+=2){float a=x[i],b=x[i+1];frost_ir_pair(&guarded_ir.ir,a,b,y+i,y+i+1);}
 if(i<count)y[i]=frost_ir_sample(&guarded_ir.ir,x[i]);}
/* 25a block kernel: one scalar sample, then a block (as the engine does), or
 * the scalar path when the block does not fit the history tail. */
API void ir_process_block(const float *x,float *y,unsigned count)
{if(!count)return;y[0]=frost_ir_sample(&guarded_ir.ir,x[0]);
 if(count>1 && frost_ir_block_fits(&guarded_ir.ir,count-1))frost_ir_block(&guarded_ir.ir,x+1,y+1,count-1);
 else {unsigned i;for(i=1;i<count;++i)y[i]=frost_ir_sample(&guarded_ir.ir,x[i]);}}
API int ir_guard(void){return guarded_ir.before==0x12345678 && guarded_ir.after==0x87654321;}
