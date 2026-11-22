/* Inside dsp_link_raw: controller/control-plane only, never DSP audio.
 * Keep <=16-byte wire frames unchanged. Longer frames use8-byte bursts with
 * 3 controller-millisecond ticks (nominally >=2ms) idle between fully drained bursts. Same bytes,
 * same one outstanding RPC; the reply wait begins after the complete send. */
{
    unsigned offset=0;
    while(offset<len){
        unsigned count=len-offset;
        if(len>16u && count>8u)count=8u;
        if(offset){uint32_t since=gp5_ms();while((uint32_t)(gp5_ms()-since)<3u){}}
        if(gp5_uart0_send(tx+offset,count,100)!=count){want_active=0;return 0;}
        offset+=count;
    }
}
