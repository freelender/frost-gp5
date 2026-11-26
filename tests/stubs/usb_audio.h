#ifndef FROST_USB_AUDIO_STUB_H
#define FROST_USB_AUDIO_STUB_H
#include <stdint.h>
void gp5_usb_audio_set_direct(int on);
int gp5_usb_audio_direct(void);
void usb_audio_link_block(const int32_t *,int32_t *,uint32_t);
#endif
