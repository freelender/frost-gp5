/* Inside the platform's serialized host_dsp_cmd(), before DSP bridge dispatch.
 * Controller-local readback; pl is the platform's100-byte bounded reply buffer. */
if (proto_req_len >= 2 && proto_req[0] == 0x7e && proto_req[1] == 0x46) {
    extern unsigned fw_bank_read(const uint8_t *,unsigned,uint8_t *);
    unsigned bytes;
    if (sizeof pl < FROST_BANK_READ_REPLY_BYTES+2u) return;
    bytes=fw_bank_read(proto_req+2,proto_req_len-2,pl+2);
    if (bytes && bytes<=sizeof pl-2) {
        pl[0]=0x7e;pl[1]=0x46;
        proto_reply(0x01,0,pl,bytes+2);
    }
    return;
}
/* AMP model files (fcm_files.h): 7E 05/07/08/0D/0E, OpenGP5 0.5 wire, run A only. */
{
    extern unsigned fw_files_host(const uint8_t *,unsigned,uint8_t *,unsigned);
    unsigned bytes;
    pl[0]=0x7e;pl[1]=proto_req_len>=2?proto_req[1]:0;
    bytes=fw_files_host(proto_req,proto_req_len,pl,sizeof pl);
    if (bytes) { proto_reply(0x01,0,pl,bytes); return; }
}
