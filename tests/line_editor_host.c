#define FROST_LINE_TEST 1
#define FROST_AUDIO_ADC 1
#include "slot_editor_host.c"
API unsigned le_level(void){return fw_level();}
API unsigned le_armed(void){return line_control.level!=0;}
API void le_knob(unsigned n){fw_knob(n);}
API void le_reconnect(void){fw_dsp_ready();}
API unsigned le_output(void){int32_t x[64]={0};uint32_t l[64],r[64];fw_output_block(x,x,l,r,0);return l[0]|r[0];}
