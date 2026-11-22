#include "slot_protocol_host.c"
static struct {uint32_t before;uint8_t data[512];uint32_t after;} storage;
static uint8_t last[508];static unsigned last_size,dispatches;
API void rx_init(void){protocol_init();storage.before=0x12345678;storage.after=0xabcdef01;last_size=dispatches=0;}
API void rx_feed(const uint8_t *p,unsigned n,uint32_t now)
{unsigned i;for(i=0;i<n;++i)frost_rx_push(&frost_link_rx,storage.data,p[i],now);}
API unsigned rx_poll(uint32_t now)
{unsigned i,n;frost_rx_expire(&frost_link_rx,now);n=frost_rx_next(&frost_link_rx,storage.data);if(!n)return 0;
 last_size=n-4;for(i=0;i<last_size;++i)last[i]=storage.data[i+4];++dispatches;
 (void)fw_command((uint32_t)last[0]|((uint32_t)last[1]<<8),last,last_size);
 frost_rx_consume(&frost_link_rx,storage.data);return last_size;}
API unsigned rx_value(unsigned field)
{uint32_t v[]={frost_link_rx.have,frost_link_rx.accepted,frost_link_rx.bad_crc,frost_link_rx.discarded,
 frost_link_rx.expired,frost_link_rx.overflows,frost_link_rx.high_water,frost_link_rx.progress,dispatches};return field<9?v[field]:0;}
API unsigned rx_last(unsigned i){return i<last_size?last[i]:0;}
API unsigned rx_guard(void){return storage.before==0x12345678 && storage.after==0xabcdef01;}
API unsigned rx_gap(void){return FROST_RX_GAP_CYCLES;}
API void rx_saturate(void){frost_link_rx.accepted=frost_link_rx.bad_crc=frost_link_rx.discarded=
 frost_link_rx.expired=frost_link_rx.overflows=UINT32_MAX;}
API void rx_inplace(uint8_t *p,unsigned n){frost_rx_read(&frost_link_rx,FROST_AUDIO_SOURCE,p,n,p);}
