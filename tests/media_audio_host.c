#define FROST_USB_AUDIO 1
#define FROST_IR_LINK 1
#define FROST_IRL_HOST 1
#define REG(a) 0u
#include "line_host.c"
API void media_render(float l,float r,unsigned frame,unsigned off,unsigned valid,unsigned volume)
{
    int32_t left[64],right[64];unsigned i;FrostIrlState *s=FROST_IRL;
    s->frame_ok=valid;s->rxoff=4;s->usb_direct_off=off;
    frost_irl_host_rx[1+FROST_IRL_W_INFO]=(int32_t)off;
    for(i=0;i<64;++i){left[i]=frost_output_pack(l);right[i]=frost_output_pack(r);
        FROST_MEDIA_PLAY[i]=frame;}
    fw_output_block(left,right,left_buffer+1,right_buffer+1,volume);
}
API unsigned media_recorded(unsigned i){return FROST_MEDIA_RECORD[i];}
API int media_right(unsigned i){return (int32_t)right_buffer[i+1];}

API void media_dma_sentinel(void){for(unsigned i=0;i<FROST_IRL_BYTES/4u;++i)frost_irl_host_tx[i]=(int32_t)0xdeadbeefu;}
API unsigned media_dma_word(unsigned i){return (uint32_t)frost_irl_host_tx[i];}
