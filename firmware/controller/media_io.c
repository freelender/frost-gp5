/* OpenGP5 1.0 USB audio and BLE-MIDI backport. Only compiled by --media-platform.
 * The IR IRQ owns the shared audio exchange; no second SPI driver is started. */
#ifdef FROST_USB_AUDIO
#include <stdint.h>
#include "usb_audio.h"
#include "bt/ble.h"
#include "firmware/protocol/slot_wire.h"
volatile uint8_t dsp_ctl_direct_off;
/* BLE timing compensation uses the actual Frost link-handler interval. */
volatile uint32_t gp5_link_t_in,gp5_link_t_out;
unsigned fw_media_host(const uint8_t *q,unsigned n,uint8_t *p,unsigned max)
{
    unsigned i;uint32_t v[6];
    /* 7E 21 [direct 0/1]; upstream 7E 19 is Frost's live-slot query already. */
    if(n<2 || n>3 || q[0]!=0x7e || q[1]!=0x21 || max<32)return 0;
    if(n==3){if(q[2]>1)return 0;gp5_usb_audio_set_direct(q[2]);}
    p[0]=0x7e;p[1]=0x21;p[2]=1;p[3]=(uint8_t)gp5_usb_audio_direct();
    p[4]=(uint8_t)usb_audio_get_interface(USB_AUDIO_IF_PLAY);
    p[5]=(uint8_t)usb_audio_get_interface(USB_AUDIO_IF_REC);
    p[6]=(uint8_t)ble.state;p[7]=0;
    v[0]=usb_audio_p_under;v[1]=usb_audio_p_over;v[2]=usb_audio_r_over;
    v[3]=(uint32_t)usb_audio_ppm;v[4]=ble.midi_rx;v[5]=ble.port_drop;
    for(i=0;i<6;++i)frost_wire_put32(p+8+4*i,v[i]);
    return 32;
}
#endif
