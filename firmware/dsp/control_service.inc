/* Opt-in main-loop experiment. Never included by the default profile.
 * Admit before TX/report/RX bookkeeping; use ONE token for ONE heavy phase.
 * Captured admission is not a WCET guarantee. Keep the existing 16k window.
 */
        unsigned admitted=frost_service_open(&frost_service,frost_timing.last_end,REG(0xE0001004u));
        if(REG(0xE000E010u)&0x10000u)frost_link_tx.report_pending=1;
        uint32_t now=REG(0xE0001004u);
        frost_rx_expire(&frost_link_rx,now);
        uint32_t n=(REG(UART0+0x0Cu)>>4)&31u;
        while(n--){
            frost_rx_push(&frost_link_rx,buf,(uint8_t)REG(UART0),now);
            frost_tx_count(&frost_link_tx.rx_bytes_seen);
        }
        /* Incomplete plausible frames wait for bytes, without stealing report
         * epochs. Invalid headers still need bounded scanning to resynchronize. */
        unsigned rx_work=frost_link_rx.ready;
        if(!rx_work && frost_link_rx.have>=4u){
            unsigned len=(unsigned)buf[1]|((unsigned)buf[2]<<8);
            rx_work=len<6u || len>512u || buf[3]!=1u || frost_link_rx.have>=len;
        }
        if(admitted)frost_link_tx.control_flags|=2u;
        else if(rx_work)frost_link_tx.control_flags|=1u;
        if(admitted){
            /* A verified request waiting for room owns the next 516 bytes.
             * TX continues below; do not fill that room with periodic reports. */
            unsigned blocked=frost_link_rx.ready && !frost_tx_dispatch_ready(&frost_link_tx,frost_link_rx.have);
            unsigned report=frost_link_tx.report_pending && frost_tx_room(&frost_link_tx)>=28u;
            if(!blocked && (rx_work || report)){
                frost_service_take(&frost_service);
                if(report && !frost_link_rx.ready && (!rx_work || frost_link_tx.prefer_report)){
                    /* Same 24-byte unsolicited report envelope. App23s stores
                     * these formerly zero meter bytes for read-only diagnostics. */
                    uint8_t r[24]={1,0x40,24,0};
                    unsigned flags=frost_link_tx.control_flags;
                    if(frost_link_rx.bad_crc)flags|=32u;
                    if(frost_link_rx.expired)flags|=64u;
                    if(frost_link_rx.overflows)flags|=128u;
                    frost_wire_put32(r+4,0x31585246u); /* FRX1 */
                    frost_wire_put32(r+8,frost_link_tx.rx_bytes_seen);
                    frost_wire_put32(r+12,ST[6]);
                    uint32_t rx_state=frost_link_rx.have|(frost_link_rx.progress<<10)|(frost_link_rx.ready<<20);
                    uint32_t usage=0;
#ifdef FROST_DSP_USAGE
                    /* Reuse a completed timing window; no new query, frame,
                     * byte, timer read, division, or audio-path instrumentation. */
                    if(frost_load.seq && (uint32_t)(now-frost_timing.last_end)<FROST_SERVICE_STALE){
                        uint32_t peak=frost_load.ring[frost_load.seq%FROST_LOAD_WINDOWS].total_max;
                        usage=peak>=510u*FROST_DSP_USAGE_UNIT?FROST_DSP_USAGE_MASK:((peak+1024u)>>11)+1u;
                        if(usage>FROST_DSP_USAGE_MASK)usage=FROST_DSP_USAGE_MASK;
                        rx_state|=(usage>>7)<<FROST_DSP_USAGE_SHIFT;
                    }
#endif
                    frost_wire_put32(r+16,rx_state);
                    frost_wire_put32(r+20,(usage&127u)|(frost_link_tx.last_action<<7)|(flags<<10)|((frost_service.epoch&16383u)<<18));
                    send_frame(r,sizeof r);ST[12]++;
                    frost_link_tx.report_pending=0;frost_link_tx.prefer_report=0;
                    frost_svc=frost_service_note(&frost_service,FROST_SERVICE_REPORT,frost_svc,REG(0xE0001004u));
                }else{
                    frost_link_tx.prefer_report=1;
                    if(frost_link_rx.ready){
                        uint32_t cmd=(uint32_t)buf[4]|((uint32_t)buf[5]<<8);
                        uint32_t frame=frost_link_rx.ready;
                        frost_link_tx.last_action=2;frost_link_tx.control_flags|=8u;
                        ST[6]++;
                        /* 25c: slot frames take the RAM path; handle() would only log
                         * then reach fw_command's identical slot case. */
                        if(cmd==FROST_SLOT_COMMAND || (cmd==FROST_QUICK_COMMAND && frame==16u)){
                            log_frame(cmd,buf+8,frame-8);frost_slot_command_ram(buf+4,frame-4);
                        }
                        else handle(buf+4,frame-4);
                        frost_rx_consume(&frost_link_rx,buf);
                        frost_svc=frost_service_note(&frost_service,cmd,frost_svc,REG(0xE0001004u));
                    }else{
                        frost_link_tx.last_action=1;frost_link_tx.control_flags|=4u;
                        frost_rx_next(&frost_link_rx,buf);
                        frost_svc=frost_service_note(&frost_service,FROST_SERVICE_RX,frost_svc,REG(0xE0001004u));
                    }
                }
            }
        }
        ST[8]=frost_link_rx.bad_crc;
#include "link_tx.inc"
        frost_svc=frost_service_note(&frost_service,FROST_SERVICE_TX,frost_svc,REG(0xE0001004u));
