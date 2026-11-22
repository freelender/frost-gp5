#include "preset_read_host.c"
API unsigned policy_inplace(uint8_t *p,unsigned n){frost_policy_command(&slot_protocol,&frost_chain,p,n,p);return 48;}
API unsigned policy_actual(unsigned slot){return frost_chain.slots[slot].bypass_policy;}
API unsigned policy_suspended(unsigned slot){return frost_chain.slots[slot].suspended;}
API float policy_wet(unsigned slot){return frost_chain.slots[slot].wet;}
API int policy_native(unsigned slot,unsigned value){return frost_chain_request_bypass_policy(&frost_chain,slot,value);}
API unsigned policy_faults(void){return frost_chain.faults;}
