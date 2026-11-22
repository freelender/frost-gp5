/* In the pinned DSP main loop, outside audio. Reuse its fixed512-byte buffer. */
        frost_rx_expire(&frost_link_rx,REG(0xE0001004u));
        uint32_t n=(REG(UART0+0x0C)>>4)&0x1Fu;
        uint32_t now=REG(0xE0001004u);
        while(n--)frost_rx_push(&frost_link_rx,buf,(uint8_t)REG(UART0+0x00),now);
#ifdef FROST_SERVICE_WATCH
        unsigned frost_slot=frost_service_open(&frost_service,frost_timing.last_end,REG(0xE0001004u));
#else
        unsigned frost_slot=1;
#endif
        /* Draining the FIFO above runs every pass. Frame parsing (CRC work, measured
         * 32k..47k cycles when cold) and dispatch run only inside the post-block
         * servicing window, so neither can delay the next audio block. Partial
         * frames expire only after FROST_RX_GAP_CYCLES, far beyond one block. */
        uint32_t frame=frost_slot?frost_rx_next(&frost_link_rx,buf):0;
        if(frame && !frost_tx_dispatch_ready(&frost_link_tx,frost_link_rx.have))frame=0;
#ifdef FROST_SERVICE_WATCH
        frost_svc=frost_service_note(&frost_service,FROST_SERVICE_RX,frost_svc,REG(0xE0001004u));
#endif
        if(frame){
            uint32_t frost_cmd=(uint32_t)buf[4]|((uint32_t)buf[5]<<8);(void)frost_cmd;
#ifdef FROST_SERVICE_WATCH
            frost_service_take(&frost_service);
#endif
            ST[6]++;handle(buf+4,frame-4);frost_rx_consume(&frost_link_rx,buf);
#ifdef FROST_SERVICE_WATCH
            frost_svc=frost_service_note(&frost_service,frost_cmd,frost_svc,REG(0xE0001004u));
#endif
        }
        ST[8]=frost_link_rx.bad_crc;
