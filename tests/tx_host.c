#include "slot_protocol_host.c"
#define UART0 0x40005000u
static uint32_t ST[32],tx_register,status_register,clock_register,rx_register;
static unsigned used_words,pending_write,status_reads,data_writes,wire_count,rx_mode;
static uint8_t wire[8192],rx_fifo[512];static unsigned rx_head,rx_count,dispatches;
static void commit_write(void)
{if(pending_write){if(wire_count<sizeof(wire))wire[wire_count++]=(uint8_t)tx_register;
 ++used_words;++data_writes;pending_write=0;}}
static uint32_t *test_reg(uint32_t address)
{
    if(address==0xE0001004u){++clock_register;return &clock_register;}
    if(address==UART0+0x0cu){commit_write();++status_reads;
        status_register=(used_words<<11)|((rx_count>16?16:rx_count)<<4);return &status_register;}
    if(rx_mode){rx_register=rx_count?rx_fifo[rx_head++]:0;if(rx_count)--rx_count;return &rx_register;}
    pending_write=1;return &tx_register;
}
#define REG(a) (*test_reg(a))
#include "firmware/dsp/link_tx_platform.inc"
static void handle(const uint8_t *p,unsigned n)
{++dispatches;response_bytes=0;
 if(fw_command((uint32_t)p[0]|((uint32_t)p[1]<<8),p,n)){
  if(response_bytes)send_frame(response,response_bytes);else send_ack();
 }else send_ack();}
API void tx_init(void)
{protocol_init();used_words=pending_write=status_reads=data_writes=wire_count=rx_mode=0;
 rx_head=rx_count=dispatches=clock_register=0;for(unsigned i=0;i<32;++i)ST[i]=0;}
API unsigned tx_frame(const uint8_t *p,unsigned n){return frost_tx_frame(&frost_link_tx,p,n);}
API void tx_platform_frame(const uint8_t *p,unsigned n){send_frame(p,n);}
API void tx_platform_ack(void){send_ack();}
API unsigned tx_service(unsigned used)
{used_words=used;status_reads=data_writes=0;unsigned before=frost_link_tx.have;
#include "firmware/dsp/link_tx.inc"
 commit_write();return before-frost_link_tx.have;}
API unsigned tx_value(unsigned field)
{uint32_t v[]={frost_link_tx.have,frost_link_tx.frames,frost_link_tx.dropped,frost_link_tx.written,
 frost_link_tx.blocked,frost_link_tx.high_water,frost_link_tx.deferred,status_reads,data_writes,wire_count,
 ST[7],ST[9],dispatches,frost_link_rx.have};return field<14?v[field]:0;}
API unsigned tx_byte(unsigned i){return i<wire_count?wire[i]:0;}
API unsigned tx_ready(unsigned pending){return frost_tx_dispatch_ready(&frost_link_tx,pending);}
API void tx_saturate(void){frost_link_tx.frames=frost_link_tx.dropped=frost_link_tx.written=
 frost_link_tx.blocked=frost_link_tx.deferred=UINT32_MAX;}
API void tx_inplace(uint8_t *p,unsigned n){frost_tx_read(&frost_link_tx,FROST_AUDIO_SOURCE,p,n,p);}
API void tx_feed(const uint8_t *p,unsigned n)
{rx_head=0;rx_count=n<sizeof(rx_fifo)?n:sizeof(rx_fifo);for(unsigned i=0;i<rx_count;++i)rx_fifo[i]=p[i];}
API void tx_rx_pass(void)
{static uint8_t storage[512];uint8_t *buf=storage;rx_mode=1;
#include "firmware/dsp/link_rx.inc"
 rx_mode=0;}
