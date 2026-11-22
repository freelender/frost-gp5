#include "slot_protocol_host.c"
API int32_t output_sample(uint32_t bits)
{union {uint32_t u;float f;} v;v.u=bits;return frost_output_sample(&frost_output_meter,v.f);}
API void output_read(uint8_t *p,unsigned n)
{frost_output_read(&frost_output_meter,frost_chain.faults,frost_bench_mute,FROST_AUDIO_SOURCE,p,n,p);}
API void output_set_counts(uint32_t n)
{frost_output_meter.frames=frost_output_meter.outside_unity=frost_output_meter.pcm_rails=frost_output_meter.invalid=n;}
API void output_fault(void)
{frost_chain.slots[2].state.eq.low=0.0f/0.0f;}
API void output_drive(void)
{unsigned i,types[2]={FROST_GAIN,FROST_GAIN};(void)frost_chain_configure(&frost_chain,types,2);
 for(i=0;i<2;++i){(void)frost_chain_enable(&frost_chain,i,1);(void)frost_chain_set(&frost_chain,i,0,4.0f);}}
API unsigned output_audio(unsigned blocks,unsigned adc_seed)
{
    int32_t in[64],left[64],right[64];unsigned i,b,before=reply_count;
    for(i=0;i<64;++i)in[i]=(int32_t)(adc_seed+i);
    for(b=0;b<blocks;++b){fw_process(in,left,right);for(i=0;i<64;++i)if(left[i] || right[i])return 0;}
    return reply_count==before && frost_bench_mute;
}
