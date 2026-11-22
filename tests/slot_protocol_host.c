#include <stdint.h>
#include "gp5_firmware.h"
static uint32_t GP5_CYCLES_LAST,GP5_CYCLES_WORST,GP5_LATE_BLOCKS;
#define GP5_BLOCK 64u
#define GP5_FULL_SCALE 2147483648.0f
static uint8_t response[84];static unsigned response_bytes,reply_count;
static uint32_t gp5_get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void gp5_ack(void){++reply_count;}
static void gp5_reply(const uint8_t *p,uint32_t n)
{ unsigned i;response_bytes=n;++reply_count;for(i=0;i<n && i<sizeof(response);++i)response[i]=p[i]; }
#include "../firmware/dsp/firmware.h"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void protocol_init(void){fw_init();response_bytes=reply_count=0;}
API int protocol_send(uint32_t cmd,const uint8_t *p,unsigned n)
{ response_bytes=0;return fw_command(cmd,p,n); }
API unsigned protocol_reply(unsigned i){return i<response_bytes?response[i]:0;}
API unsigned protocol_reply_bytes(void){return response_bytes;}
API unsigned protocol_revision(void){return slot_protocol.revision;}
API unsigned protocol_commands(void){return commands;}
API unsigned protocol_rejected(void){return rejected;}
API int protocol_configure(const unsigned *types,unsigned n)
{ int result=frost_chain_configure(&frost_chain,types,n);if(result)frost_slots_init(&slot_protocol);return result; }
API void protocol_audio(const float *x,float *y,unsigned n)
{ frost_chain_process(&frost_chain,x,y,n); }
API float protocol_current(unsigned slot,unsigned parameter){return frost_chain.slots[slot].current[parameter];}
API void protocol_set_revision(unsigned value){slot_protocol.revision=value;slot_protocol.cached=0;}
API unsigned protocol_callback_has_no_reply(void)
{ int32_t x[64]={0},l[64],r[64];unsigned i,before=reply_count;fw_process(x,l,r);
  for(i=0;i<64;++i)if(l[i] || r[i])return 0;
  return before==reply_count && frost_bench_mute; }
API unsigned protocol_inplace(uint8_t *buffer,unsigned n)
{ frost_slots_command(&slot_protocol,&frost_chain,buffer,n,buffer);return FROST_SLOT_REPLY_BYTES; }
API unsigned protocol_route(const unsigned *types,const unsigned *order,unsigned pre,uint32_t revision)
{ return frost_slots_request_route(&slot_protocol,&frost_chain,types,order,pre,revision); }
