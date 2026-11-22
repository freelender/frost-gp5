#include "slot_protocol_host.c"
API float input_sample(int32_t q){return frost_input_sample(&frost_input_meter,q);}
API void input_inplace(uint8_t *p,unsigned n){frost_input_read(&frost_input_meter,frost_bench_mute,p,n,p);}
API void input_counts(uint32_t n){frost_input_meter.frames=frost_input_meter.rails=frost_input_meter.nonzero=n;}
API void input_empty(void){unsigned types[6]={255,255,255,255,255,255};(void)frost_chain_configure(&frost_chain,types,6);}
API unsigned input_audio(const int32_t *p,unsigned blocks)
{int32_t left[64],right[64];unsigned i,b,before=reply_count;
 for(b=0;b<blocks;++b){fw_process(p,left,right);for(i=0;i<64;++i)if(left[i] || right[i])return 0;}
 return before==reply_count && frost_bench_mute;}
