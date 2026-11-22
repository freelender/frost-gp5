#include "slot_protocol_host.c"
API void trace_record(uint32_t begin,uint32_t inp,uint32_t process,uint32_t end,uint32_t pending)
{frost_timing_record(&frost_timing,begin,inp,process,end,pending);}
API unsigned trace_size(void){return sizeof(frost_timing);}
API void trace_saturate(void){frost_timing.blocks=UINT32_MAX;}
API void trace_anchor(uint32_t cycle){frost_timing.last_end=cycle;}
API void trace_inplace(uint8_t *p,unsigned n){frost_timing_read(&frost_timing,1,2,p,n,p);}
API void service_note(uint32_t id,uint32_t begin,uint32_t now){(void)frost_service_note(&frost_service,id,begin,now);}
API void service_iteration(uint32_t begin,uint32_t now){frost_service_iteration(&frost_service,begin,now);}
API unsigned service_open(uint32_t now){return frost_service_open(&frost_service,frost_timing.last_end,now);}
API void service_take(void){frost_service_take(&frost_service);}
API void service_epoch(uint32_t e){frost_service.epoch=frost_service.slot_epoch=e;}
