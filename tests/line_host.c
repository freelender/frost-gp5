#define FROST_LINE_TEST 1
#define FROST_AUDIO_ADC 1
#include "slot_protocol_host.c"
static uint32_t left_buffer[66],right_buffer[66];
API void line_init(void){GP5_CYCLES_LAST=GP5_CYCLES_WORST=GP5_LATE_BLOCKS=0;protocol_init();fw_prepare();}
API unsigned line_block(int32_t input,uint32_t requested,unsigned direct)
{int32_t x[64],l[64],r[64];unsigned i;
for(i=0;i<64;++i)x[i]=input;
left_buffer[0]=right_buffer[0]=0x12345678;left_buffer[65]=right_buffer[65]=0xabcdef01;
if(direct){for(i=0;i<64;++i){l[i]=frost_output_pack((float)input*(1.0f/2147483648.0f));r[i]=l[i];}}else fw_process(x,l,r);
fw_output_block(l,r,left_buffer+1,right_buffer+1,requested);
return left_buffer[0]==0x12345678 && right_buffer[0]==0x12345678 && left_buffer[65]==0xabcdef01 && right_buffer[65]==0xabcdef01;}
API int32_t line_sample(unsigned i){return i<64?(int32_t)left_buffer[i+1]:0;}
API void line_float_block(float sample,uint32_t requested)
{int32_t l[64],r[64];unsigned i;for(i=0;i<64;++i)l[i]=r[i]=frost_output_pack(sample);
 fw_output_block(l,r,left_buffer+1,right_buffer+1,requested);}
API void line_bad_tail(uint32_t bits,uint32_t requested)
{int32_t l[64],r[64];unsigned i;for(i=0;i<64;++i)l[i]=r[i]=frost_output_pack(.5f);
 l[63]=r[63]=(int32_t)bits;fw_output_block(l,r,left_buffer+1,right_buffer+1,requested);}
API unsigned line_equal(void){for(unsigned i=1;i<=64;++i)if(left_buffer[i]!=right_buffer[i])return 0;return 1;}
API unsigned line_value(unsigned i){uint32_t v[]={line_output.gain,line_output.flags,line_output.requested,frost_chain.faults,frost_output_meter.pcm_rails,line_output.opened,line_output.late_base,line_output.late_tolerated,line_output.late_recent,line_output.late_in_fades,line_output.shed_blocks};return i<11?v[i]:0;}
API void line_late(unsigned n){GP5_LATE_BLOCKS=n;}
API void line_eq_nan(void){frost_chain.slots[2].state.eq.low=0.0f/0.0f;}
/* 31a: the baked table may be float or q15; taps past it run zero taps, as control.h does. */
static unsigned line_load_taps(unsigned taps)
{if(taps<=FROST_TEST_IR_STORED)return (unsigned)frost_test_ir_load(&frost_chain,3,taps);
 if(!frost_test_ir_load(&frost_chain,3,FROST_TEST_IR_STORED))return 0;frost_chain.slots[3].state.ir->taps=taps;return 1;}
API void line_route_bad(unsigned bad){if(bad)(void)line_load_taps(272);} /* 31a: >256 (FFT limit) */
API unsigned line_ir_taps(unsigned taps){return line_load_taps(taps);}
API unsigned line_route(unsigned variant)
{unsigned types[FROST_SLOTS]={FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_EMPTY,FROST_EMPTY},order[FROST_SLOTS]={0,1,2,3,4,5},pre=3;
 if(variant==1){types[0]=FROST_OVERDRIVE;types[4]=FROST_DELAY;types[5]=FROST_REVERB;}
 if(variant==2){types[4]=FROST_CHORUS;order[0]=1;order[1]=0;pre=6;}
 return (unsigned)frost_chain_request_route(&frost_chain,types,order,pre);}
API unsigned line_transition(void){return frost_chain.transition;}
API float line_ir_tap(unsigned i){return frost_chain.slots[3].state.ir->coefficients[i];}
API unsigned line_ir_len(void){return frost_chain.slots[3].state.ir->taps;}
API unsigned line_ir_fft_parts(void){const FrostIRFFT *f=frost_ir_fft_of(frost_chain.slots[3].state.ir);return f?f->parts:0;} /* 31a */
API void line_overload(void){float identity=1.0f;(void)frost_chain_load_ir(&frost_chain,3,&identity,1);(void)frost_chain_set(&frost_chain,0,0,4.0f);}
API void line_unity_gain(void){(void)frost_chain_set(&frost_chain,0,0,1.0f);}
API unsigned line_drive_ir(unsigned policy)
{
    unsigned types[6]={FROST_EMPTY,FROST_EMPTY,FROST_OVERDRIVE,FROST_IR,FROST_EMPTY,FROST_EMPTY};float h=.1f;
    return frost_chain_configure(&frost_chain,types,6) &&
        frost_chain_set(&frost_chain,2,0,20.0f) && frost_chain_set(&frost_chain,2,2,1.44f) &&
        frost_chain_load_ir(&frost_chain,3,&h,1) &&
        frost_chain_set_bypass_policy(&frost_chain,3,policy) &&
        frost_chain_enable(&frost_chain,2,1) && frost_chain_enable(&frost_chain,3,1);
}
API unsigned line_ir_enable(unsigned enabled)
{
    static uint32_t token=100;uint8_t p[36];unsigned i;
    uint32_t v[8]={1,++token,FROST_SLOT_ENABLE,slot_protocol.revision,3,FROST_IR,UINT32_MAX,enabled};
    p[0]=0x40;p[1]=0x7f;p[2]=36;p[3]=0;
    for(i=0;i<8;++i)frost_wire_put32(p+4+4*i,v[i]);
    return fw_command(FROST_SLOT_COMMAND,p,36) && frost_wire_get32(response+12)==FROST_SLOT_APPLIED;
}
API unsigned line_enabled(unsigned slot){return (unsigned)frost_chain.slots[slot].wet_target;}
