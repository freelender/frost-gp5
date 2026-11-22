#include "framework_host.c"
#include "../firmware/protocol/slots.c"
static FrostDelay delays[2];
static FrostModulation mods[2][FROST_MOD_WORKSPACES];
static FrostReverb reverbs[2];
static FrostPreset saved[2];
static FrostSlotProtocol protocols[2];
API void pn_init(unsigned c)
{fx_init(c);(void)frost_chain_bind_delay(&chains[c],&delays[c]);(void)frost_chain_bind_modulation(&chains[c],mods[c]);
 (void)frost_chain_bind_reverb(&chains[c],&reverbs[c]);frost_slots_init(&protocols[c]);}
API unsigned pn_bytes(void){return sizeof(FrostPreset);}
API FrostPreset *pn_ptr(unsigned i){return &saved[i];}
API int pn_capture(unsigned c,unsigned i){return frost_chain_capture_preset(&chains[c],&saved[i]);}
API unsigned pn_request(unsigned c,unsigned i,unsigned revision)
{return frost_slots_request_preset(&protocols[c],&chains[c],&saved[i],revision);}
API unsigned pn_request_owned(unsigned c,unsigned i,unsigned revision)
{chains[c].pending_preset=saved[i];return frost_slots_request_preset(&protocols[c],&chains[c],&chains[c].pending_preset,revision);}
API unsigned pn_revision(unsigned c){return protocols[c].revision;}
API void pn_set_revision(unsigned c,unsigned revision){protocols[c].revision=revision;}
API void pn_cache(unsigned c){unsigned i;protocols[c].cached=FROST_CACHE_SLOT;for(i=0;i<8;++i)protocols[c].last_write[i]=i+123;}
API unsigned pn_cache_kind(unsigned c){return protocols[c].cached;}
API unsigned pn_cache_word(unsigned c,unsigned i){return protocols[c].last_write[i];}
API unsigned pn_query(unsigned c)
{uint8_t request[36]={0},reply[48];request[0]=0x40;request[1]=0x7f;request[2]=36;
 frost_wire_put32(request+4,1);frost_wire_put32(request+8,42);frost_wire_put32(request+24,FROST_EMPTY);frost_wire_put32(request+28,UINT32_MAX);
 frost_slots_command(&protocols[c],&chains[c],request,36,reply);return frost_wire_get32(reply+12);}
API int pn_policy(unsigned c,unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&chains[c],slot,policy);}
API unsigned pn_get_policy(unsigned c,unsigned slot){return chains[c].slots[slot].bypass_policy;}
API float pn_current(unsigned c,unsigned slot,unsigned p){return chains[c].slots[slot].current[p];}
API float pn_wet(unsigned c,unsigned slot){return chains[c].slots[slot].wet;}
API unsigned pn_suspended(unsigned c,unsigned slot){return chains[c].slots[slot].suspended;}
API unsigned pn_ir_nonzero_history(unsigned c,unsigned slot)
{unsigned j,n=0;for(j=0;j<512;++j)n+=chains[c].slots[slot].state.ir->history[j]!=0.0f;return n;}
API void pn_detach_large(unsigned c)
{(void)frost_chain_bind_delay(&chains[c],0);(void)frost_chain_bind_modulation(&chains[c],0);(void)frost_chain_bind_reverb(&chains[c],0);}
