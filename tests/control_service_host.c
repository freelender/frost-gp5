#include <stdint.h>
#include <string.h>
#define FROST_CONTROL_SCHEDULER 1
#include "firmware/dsp/link_rx.h"
#include "firmware/dsp/link_tx.h"
#include "firmware/dsp/timing_watch.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static FrostLinkRx frost_link_rx;
static uint8_t buf[512],fifo[31],report_payload[24];
static uint32_t stats[32],clock_now,tick,delay,read_have,read_pos,tx_block,dispatches,reports;
#define ST stats
#define UART0 0x40005000u
static uint32_t read_reg(uint32_t a){
    if(a==0xe0001004u)return clock_now;
    if(a==0xe000e010u){uint32_t r=tick;tick=0;clock_now+=delay;return r;}
    if(a==UART0+12u)return ((read_have-read_pos)<<4)|(tx_block<<11);
    if(a==UART0)return read_pos<read_have?fifo[read_pos++]:0;
    return 0;
}
#define REG(a) read_reg(a)
static unsigned frost_uart_tx_used(void *p){(void)p;return tx_block;}
static void frost_uart_tx_put(void *p,uint8_t b){(void)p;(void)b;}
static void send_frame(const uint8_t *p,unsigned n){
    if(n==24){memcpy(report_payload,p,24);++reports;}
    frost_tx_frame(&frost_link_tx,p,n);
}
static void handle(const uint8_t *p,unsigned n){(void)p;(void)n;++dispatches;}
/* 25c: the scheduler routes slot frames to a RAM-resident copy of the same
 * fw_command case after logging; in this model both count as one dispatch. */
#include "firmware/protocol/slot_wire.h"
#include "firmware/protocol/quick_preset_wire.h"
static unsigned slot_fast;
static void log_frame(uint32_t c,const uint8_t *a,uint32_t n){(void)c;(void)a;(void)n;}
static void frost_slot_command_ram(const uint8_t *p,unsigned n){++slot_fast;handle(p,n);}
API void cs_init(void){
    memset(stats,0,sizeof stats);memset(buf,0,sizeof buf);memset(report_payload,0,24);
    frost_rx_init(&frost_link_rx);frost_tx_init(&frost_link_tx);frost_service_init(&frost_service);
    frost_load_init(&frost_load);memset(&frost_timing,0,sizeof frost_timing);
    frost_timing.last_end=100000;frost_service.epoch=1;
    clock_now=100000;tick=delay=read_have=read_pos=tx_block=dispatches=reports=slot_fast=0;
}
API void cs_feed(const uint8_t *p,unsigned n){while(n--)frost_rx_push(&frost_link_rx,buf,*p++,clock_now);}
API void cs_fifo(const uint8_t *p,unsigned n){if(n>31)n=31;memcpy(fifo,p,n);read_have=n;read_pos=0;}
API void cs_audio(void){frost_timing.last_end+=278494u;++frost_service.epoch;}
API void cs_poll(unsigned offset,unsigned countflag,unsigned imposed_delay){
    clock_now=frost_timing.last_end+offset;tick=countflag?0x10000u:0;delay=imposed_delay;
    uint32_t frost_svc=clock_now;
#include "firmware/dsp/control_service.inc"
}
API void cs_room(unsigned n,unsigned blocked){frost_link_tx.have=1024u-n;tx_block=blocked?16u:0u;}
API unsigned cs_value(unsigned i){
    switch(i){
    case 0:return dispatches;case 1:return reports;case 2:return frost_link_rx.progress;
    case 3:return frost_link_rx.ready;case 4:return frost_link_rx.have;
    case 5:return frost_link_tx.report_pending;case 6:return frost_link_tx.rx_bytes_seen;
    case 7:return frost_link_tx.control_flags;case 8:return frost_link_rx.bad_crc;
    case 9:return frost_link_rx.accepted;case 10:return frost_link_tx.dropped;
    case 11:return sizeof(FrostLinkTx);case 12:return slot_fast;default:return 0;
    }
}
API unsigned cs_report(unsigned i){return i<24?report_payload[i]:0;}
