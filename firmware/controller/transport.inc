/* Included inside the pinned platform main loop, in place of legacy ACK-only
 * setting delivery. Controller link waits remain outside DSP audio callbacks. */
if (dsp_ctl_step == DSP_CTL_DONE && (int32_t)(gp5_ms() - fx_next) >= 0) {
    extern unsigned fw_sync_poll_due(void);
    extern unsigned fw_sync_route_request(uint8_t *);
    extern int fw_sync_route_reply(const uint8_t *,unsigned);
    extern unsigned fw_sync_setting_request(uint32_t,uint32_t,uint8_t *);
    extern int fw_sync_setting_reply(const uint8_t *,unsigned);
    extern void fw_retry(uint32_t);
    if (fw_sync_poll_due()) {
        uint8_t request[FROST_ROUTE_STATUS_REQUEST_BYTES], reply[FROST_ROUTE_STATUS_REPLY_BYTES], status;
        unsigned n = fw_sync_route_request(request);
        if (n) {
            unsigned received = dsp_link_raw(request,n,reply,sizeof reply,100,&status);
            fw_sync_route_reply(status == 1 ? reply : 0,status == 1 ? received : 0);
        }
    }
    {
        uint32_t id,value;
        if (fw_take(&id,&value)) {
            uint8_t request[FROST_SETTING_REQUEST_BYTES],reply[FROST_SETTING_REPLY_BYTES],status;
            unsigned n = fw_sync_setting_request(id,value,request);
            if (n) {
                unsigned received = dsp_link_raw(request,n,reply,sizeof reply,100,&status);
                int applied = fw_sync_setting_reply(status == 1 ? reply : 0,status == 1 ? received : 0);
                if (id < 32) {
                    if (applied) fx_startup_acked |= 1u << id;
                    else fx_startup_acked &= ~(1u << id);
                }
            } else fw_retry(id);
        }
    }
    fx_next = gp5_ms() + 20u;
}
