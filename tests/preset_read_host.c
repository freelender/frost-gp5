#include "preset_transfer_host.c"
API int read_native_snapshot(FrostPreset *out){return frost_chain_capture_preset(&frost_chain,out);}
API unsigned read_inplace(uint8_t *buffer,unsigned n)
{frost_preset_read_command(&slot_protocol,&frost_chain,buffer,n,buffer);return 48;}
