#include "slot_protocol_host.c"
API unsigned transfer_bytes(void){return sizeof(FrostPresetTransfer);}
API unsigned transfer_state(void){return preset_transfer.state;}
API unsigned transfer_next(void){return preset_transfer.next;}
API unsigned transfer_session(void){return preset_transfer.session;}
API unsigned transfer_phase(void){return frost_chain.transition;}
API unsigned transfer_policy(unsigned slot){return frost_chain.slots[slot].bypass_policy;}
API float transfer_target(unsigned slot,unsigned p){return frost_chain.slots[slot].target[p];}
API unsigned transfer_type(unsigned slot){return frost_chain.slots[slot].type;}
API unsigned transfer_ir_taps(unsigned slot){return frost_chain.slots[slot].state.ir->taps;}
API float transfer_ir_coeff(unsigned slot,unsigned i){return frost_chain.slots[slot].state.ir->coefficients[i];}
API unsigned transfer_inplace(uint8_t *buffer,unsigned n)
{frost_preset_command(&preset_transfer,&slot_protocol,&frost_chain,buffer,n,buffer);return 48;}
