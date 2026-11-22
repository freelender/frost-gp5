#include "framework_host.c"
#include "../engine/control_catalog.h"
static struct {unsigned before;FrostModulation pool[FROST_MOD_WORKSPACES];unsigned after;} memory[2];
static FrostDelay delay_memory[2];
API int mo_bind(unsigned c){return frost_chain_bind_modulation(&chains[c],memory[c].pool);}
API int mo_unbind(unsigned c){return frost_chain_bind_modulation(&chains[c],0);}
API int mo_bind_delay(unsigned c){return frost_chain_bind_delay(&chains[c],&delay_memory[c]);}
API void mo_poison(unsigned c)
{unsigned i,w;memory[c].before=0x12345678u;memory[c].after=0x87654321u;
 for(w=0;w<FROST_MOD_WORKSPACES;++w)for(i=0;i<FROST_MOD_SAMPLES;++i){union {unsigned u;float f;} v;v.u=0x7fc00000u;memory[c].pool[w].line.samples[i]=v.f;}}
API void mo_reset(unsigned c,unsigned w){frost_modulation_reset(&memory[c].pool[w]);}
API unsigned mo_valid(unsigned c,unsigned w){return memory[c].pool[w].valid;}
API float mo_phase(unsigned c,unsigned w){return memory[c].pool[w].phase;}
API float mo_distance(unsigned c,unsigned w){return memory[c].pool[w].distance;}
API unsigned mo_word(unsigned c,unsigned w,unsigned index)
{union {float f;unsigned u;} v;v.f=memory[c].pool[w].line.samples[index];return v.u;}
API int mo_guard(unsigned c){return memory[c].before==0x12345678u && memory[c].after==0x87654321u;}
API unsigned mo_bytes(void){return sizeof(FrostModulation);}
API float mo_sample(unsigned c,unsigned w,unsigned type,float x,float rate,float depth,float value)
{float p[3]={rate,depth,value};return type==FROST_CHORUS?frost_chorus_sample(&memory[c].pool[w],x,p):frost_flanger_sample(&memory[c].pool[w],x,p);}
API int mo_policy(unsigned c,unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&chains[c],slot,policy);}
API unsigned mo_identity(unsigned c,unsigned slot)
{unsigned w;for(w=0;w<FROST_MOD_WORKSPACES;++w)if(chains[c].slots[slot].state.modulation==&memory[c].pool[w])return w;return 99;}
API int mo_meta(unsigned type,unsigned p,unsigned field)
{const FrostControlParameter *d=&frost_control_find(type)->p[p];return field==0?d->minimum:field==1?d->maximum:field==2?d->initial:d->scale;}
API float mo_bound(unsigned type,unsigned p,unsigned field)
{const FrostParameter *d=&frost_effect_find(type)->parameters[p];return field==0?d->minimum:field==1?d->maximum:d->initial;}
