#include <string.h>
#include "slot_protocol_host.c"
static volatile unsigned process_calls;
static volatile unsigned inject_fault;
/* Host-only instrumentation proves rehearsal actually enters fw_process. */
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *function,void *caller)
{(void)caller;if(function==(void *)fw_process)++process_calls;}
__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *function,void *caller)
{(void)caller;if(function==(void *)fw_process && inject_fault){frost_chain.faults=7;inject_fault=0;}}
API void startup_init(void){protocol_init();process_calls=inject_fault=0;}
API void startup_fail_rehearsal(void){inject_fault=1;}
API unsigned startup_faults(void){return frost_chain.faults;}
API unsigned startup_prepare(void)
{unsigned before=reply_count;fw_prepare();return reply_count==before && frost_bench_mute;}
API unsigned startup_calls(void){return process_calls;}
API unsigned startup_snapshot(uint8_t *out)
{
    unsigned offset=0;
#define SNAP(value) do {if(out)memcpy(out+offset,&(value),sizeof(value));offset+=sizeof(value);}while(0)
    SNAP(frost_chain);SNAP(frost_input_meter);SNAP(frost_output_meter);SNAP(frost_link_rx);
    SNAP(slot_protocol);SNAP(preset_transfer);SNAP(frost_timing);SNAP(rejected);SNAP(commands);SNAP(sample_number);
#undef SNAP
    return offset;
}
API unsigned startup_audio(unsigned seed)
{int32_t input[64],left[64],right[64];unsigned i,before=reply_count;
 for(i=0;i<64;++i)input[i]=(i&1)?-(int32_t)(seed+i):(int32_t)(seed+i);
 fw_process(input,left,right);
 for(i=0;i<64;++i)if(left[i] || right[i])return 0;
 return before==reply_count && frost_bench_mute;}
API void startup_wet(const float *input,float *output,unsigned n)
{frost_chain_process(&frost_chain,input,output,n);}
