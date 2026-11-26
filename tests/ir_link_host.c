/* Stage 4 IR link fixture: the DSP half (firmware/dsp/ir_link.h) and the
 * controller half (firmware/controller/ir_link.h) in one host library, joined
 * by a simulated SPI wire. Each exchange copies the DSP's TX frame into the
 * controller's armed receive buffer and the controller's armed TX buffer into
 * the DSP's RX, both shifted by `lag` bytes (the slave's queued bytes), then
 * runs the controller's exchange-done handler. Host check, not a pedal test. */
#define FROST_IR_LINK 1
#define FROST_IRL_HOST 1
#include "../engine/bundle.c"
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static FrostChain frost_chain;
static FrostIRFFT ws;
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
static float ws_spectrum[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE];
#endif
#include "../firmware/dsp/ir_link.h"
#include "../firmware/controller/ir_link.h"
#ifdef FROST_USB_AUDIO
volatile uint8_t dsp_ctl_direct_off;
volatile uint32_t gp5_link_t_in,gp5_link_t_out;
static unsigned media_blocks;static uint32_t media_record[64],media_play;
void usb_audio_link_block(const int32_t *in,int32_t *out,uint32_t t)
{unsigned i;(void)t;++media_blocks;for(i=0;i<64;++i){media_record[i]=(uint32_t)in[FROST_IRL_W_USB+i];out[FROST_IRL_W_USB+i]=(int32_t)media_play;}}
API void ln_media_dma_error(void){irl_host_dma_error=1;}
API unsigned ln_media_blocks(void){return media_blocks;}
API void ln_media_play(unsigned word){media_play=word;}
API unsigned ln_media_record(unsigned i){return media_record[i];}
API unsigned ln_media_received(unsigned i){return FROST_IRL->frame_ok?FROST_MEDIA_PLAY[i]:0;}
API void ln_media_overwrite_rx(void){for(unsigned i=0;i<64;++i)memset((uint8_t *)frost_irl_host_rx+FROST_IRL->rxoff+4u*(FROST_IRL_W_USB+i),0,4);}
API unsigned ln_media_legacy_peer(void){uint32_t old=0x324c4946u;memcpy((uint8_t *)frost_irl_host_rx+FROST_IRL->rxoff,&old,4);frost_irl_check();return FROST_IRL->frame_ok;}
#endif
static unsigned ln_starts_seen,ln_lag,ln_deliver=1,ln_ctl_skip,ln_shifted,ln_ms,ln_lost_seen;
API void ln_init(unsigned two)
{
    unsigned types[6]={FROST_IR,255,255,255,255,255};
    memset(&frost_chain,0,sizeof frost_chain);memset(&ws,0,sizeof ws);
    if(two)types[1]=FROST_IR;
    frost_chain_init(&frost_chain);
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    ws.spectrum=ws_spectrum;
#endif
    (void)ws;   /* 31y5 target: no FFT workspace with the IR link (128-tap direct fallback) */
    (void)frost_chain_configure(&frost_chain,types,6);
    (void)frost_chain_enable(&frost_chain,0,1);if(two)(void)frost_chain_enable(&frost_chain,1,1);
#ifdef FROST_IRL_REQUIRE_REMOTE
    frost_ir_remote=1;
#else
    frost_ir_remote=0;
#endif
    frost_ir_remote_pending=2u;frost_ir_bypass=0;frost_ir_bypass_pending=2u;frost_ir_remote_fn=frost_irl_stream;
    frost_irl_init();irl_start();
    ln_starts_seen=frost_irl_host_starts;ln_lag=0;ln_deliver=1;ln_ctl_skip=0;frost_irl_host_busy=0;ln_shifted=0;ln_ms=0;ln_lost_seen=irl.lost;irl_seen_blocks=0;irl_quiet_since=0;
    irl_flash_depth=irl_flash_writes=0;irl_host_pause_fail=irl_host_resume_fail=irl_host_controls=0;
}
API int ln_load(unsigned slot,const float *h,unsigned taps)
{
    /* Cold load as a route commit would do (histories restart). */
    return frost_ir_load(frost_chain.slots[slot].state.ir,h,taps);
}
API float ln_level(unsigned slot){return frost_chain.slots[slot].target[0];}
static void ln_wire(void)
{
#ifdef FROST_USB_AUDIO
    if(irl_host_dma_error)return;
#endif
    const uint8_t *dsp_tx=(const uint8_t *)frost_irl_host_tx,*ctl_tx=(const uint8_t *)irl_tx[irl.cur];
    uint8_t *ctl_rx=(uint8_t *)irl_rx[irl.cur],*dsp_rx=(uint8_t *)frost_irl_host_rx;unsigned i;
    /* A shifted slave (re-armed mid-exchange) sees each frame offset by
     * half a frame: no sync in either direction until it re-arms cleanly. */
    unsigned lag=ln_shifted?FROST_IRL_BYTES/2u:ln_lag;
    for(i=0;i<FROST_IRL_BYTES;++i){
        ctl_rx[i]=i<lag?0x5a:dsp_tx[i-lag];
        dsp_rx[i]=i<lag?0xa5:ctl_tx[i-lag];
    }
    if(ln_ctl_skip){--ln_ctl_skip;return;}   /* controller handler did not run (e.g. stalled by a flash erase) */
    gp5_link_irq();
}
/* One 64-sample block through the chain with the link. */
API int ln_block(const float *in,float *out)
{
    int r;
#ifdef FROST_USB_AUDIO
    for(unsigned i=0;i<64;++i)FROST_MEDIA_RECORD[i]=((FROST_IRL->block+1u)<<16)|i;
#endif
    frost_irl_block_begin();
    r=frost_chain_process_guarded(&frost_chain,in,out,64,0);
    frost_irl_block_end();

    {unsigned kicked=frost_irl_host_starts!=ln_starts_seen;
     if(kicked){ln_starts_seen=frost_irl_host_starts;if(ln_deliver)ln_wire();}
     /* the controller's main loop: 1.45 ms per block, its 20 ms re-arm when no
      * valid frame arrived. A re-arm while the DSP keeps clocking lands
      * mid-exchange again (stays shifted); only one during wire silence is clean. */
     ln_ms+=(ln_ms%20u==0u)?2u:1u;irl_poll(ln_ms);
     if(irl.lost!=ln_lost_seen){ln_lost_seen=irl.lost;if(!kicked || irl_host_controls)ln_shifted=0;}}
    return r;
}
API void ln_set(unsigned lag,unsigned deliver,unsigned ctl_skip){ln_lag=lag;ln_deliver=deliver;ln_ctl_skip=ctl_skip;}
API void ln_shift(void){ln_shifted=1;}
API void ln_stick_master(void){frost_irl_host_busy=1;}
API int ln_flash_begin(void){return frost_irl_flash_begin();}
API void ln_flash_end(void){frost_irl_flash_end();}
API void ln_control_fail(unsigned pause,unsigned resume){irl_host_pause_fail=pause;irl_host_resume_fail=resume;}
API void ln_control(unsigned op){frost_irl_flash_control(op);}
API void ln_loader_quiet(void){frost_irl_quiet();}
API unsigned ln_flash_state(unsigned i)
{
    unsigned v[]={FROST_IRL->quiet,irl_flash_depth,irl_flash_writes,irl_host_controls,irl_host_armed};
    return i<5?v[i]:0;
}
API unsigned ln_finish_busy(void)
{
    unsigned before=frost_irl_host_finishes;
    FROST_IRL->started=1;frost_irl_host_busy=1;
    frost_irl_check();
    return frost_irl_host_finishes-before;
}
API void ln_commit_alone(unsigned on){frost_chain_commit_alone=on;}
API int ln_route(const unsigned *types){unsigned order[6]={0,1,2,3,4,5};return frost_chain_request_route(&frost_chain,types,order,6);}
API unsigned ln_type(unsigned i){return frost_chain.slots[i].type;}
API unsigned ln_remote(void){return frost_ir_remote;}
API unsigned ln_transition(void){return frost_chain.transition;}
API unsigned ln_stat(unsigned i)
{
    const FrostIrlState *s=FROST_IRL;
    uint32_t v[]={s->frames_ok,s->frames_bad,s->busy,s->switches,s->block,s->ready,s->version[0],s->version[1],
                  irl.blocks,irl.refused,irl.ready,s->good_run,s->bad_run,frost_irl_host_starts,s->resyncs,irl.lost};
    return i<sizeof v/sizeof v[0]?v[i]:0;
}
API float ln_scale(unsigned j){return FROST_IRL->scale[j];}
/* Kernel alone: q[512] taps, in[64] new inputs per call; history kept. */
static IrcStream ln_k;
API void ln_kernel_reset(const int16_t *q){unsigned i;irc_clear(&ln_k);for(i=0;i<256;++i)ln_k.q[i]=(uint16_t)q[2*i]|((uint32_t)(uint16_t)q[2*i+1]<<16);}
API void ln_kernel(const int32_t *in,int32_t *out){irc_block(&ln_k,in,out);}
