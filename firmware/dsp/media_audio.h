#include "firmware/protocol/media_audio.h"
/* Called after the finished stereo chain and before the listening master.
 * USB words use staging buffers outside the SPI DMA frames. The next early
 * IR exchange carries this recording block; playback was snapshotted before
 * that exchange began. No in-flight DMA buffer is modified here. */
__attribute__((section(".frost_xcode"),noinline)) static void frost_media_output(const int32_t *left,const int32_t *right,
                              volatile uint32_t *out_left,volatile uint32_t *out_right,uint32_t requested)
{
    FrostIrlState *s=FROST_IRL;int32_t lmix[GP5_BLOCK],rmix[GP5_BLOCK];unsigned i;

    float g0=s->usb_direct_gain,target=1.0f;
    if(s->frame_ok && s->usb_direct_off)target=0.0f;
    for(i=0;i<GP5_BLOCK;++i){
        float l=frost_output_unpack(left[i]),r=frost_output_unpack(right[i]);
        uint32_t play=s->frame_ok?FROST_MEDIA_PLAY[i]:0u;
        float g=g0+(target-g0)*(float)(i+1u)*(1.0f/GP5_BLOCK);
        if(!(l>=-16.0f && l<=16.0f && r>=-16.0f && r<=16.0f))
            {frost_output_count(&frost_output_meter.invalid);frost_line_latch(&line_output,FROST_LINE_INVALID); }
        if(line_output.flags)l=r=0.0f;
        FROST_MEDIA_RECORD[i]=(int32_t)frost_media_record(l,r);
        l=l*g+frost_media_play(play,0);r=r*g+frost_media_play(play,1);
        /* A valid chain plus playback can exceed the chain-only envelope. */
        if(l>16.0f)l=16.0f;if(l< -16.0f)l=-16.0f;
        if(r>16.0f)r=16.0f;if(r< -16.0f)r=-16.0f;
        lmix[i]=frost_output_pack(l);rmix[i]=frost_output_pack(r);
    }
    s->usb_direct_gain=target;
    /* Fail the whole recording block closed, just as the line output does. */
    if(line_output.flags)for(i=0;i<GP5_BLOCK;++i)FROST_MEDIA_RECORD[i]=0;
    frost_line_render(&line_output,&frost_output_meter,lmix,rmix,out_left,out_right,GP5_BLOCK,requested);
}
