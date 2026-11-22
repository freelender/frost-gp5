/* Generic editor: one bounded serialized query/write per eligible pass. */
extern unsigned fw_editor_interval(void);
if (dsp_ctl_step == DSP_CTL_DONE && (!fw_editor_interval() || (int32_t)(gp5_ms() - fx_next) >= 0)) {
    extern unsigned fw_editor_request(uint8_t *);
    extern int fw_editor_reply(const uint8_t *,unsigned);
    extern void fw_editor_transport_status(unsigned);
    extern unsigned fw_editor_ready(void);
    uint8_t request[FROST_QUICK_MAX_BYTES],reply[FROST_SLOT_REPLY_BYTES],status;
    unsigned n=fw_editor_request(request);
    if (n) {
        unsigned received=dsp_link_raw(request,n,reply,sizeof reply,100,&status);
        fw_editor_transport_status(status);
        fw_editor_reply(status==1?reply:0,status==1?received:0);
    }
    fx_startup_acked=fw_editor_ready()?1u:0u;
    fx_next=gp5_ms()+fw_editor_interval();
}
