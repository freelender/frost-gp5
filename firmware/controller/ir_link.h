#ifndef FROST_IR_LINK_H
#define FROST_IR_LINK_H
/* Stage 4: the controller's half of the IR link (docs/Build%20Firmware.md,
 * firmware/protocol/ir_link_wire.h). The SPI slave + DMA driver is a port of
 * OpenGP5 0.5.5 platform/nds32/hal/spis.c (MIT, THIRD_PARTY_NOTICES.md); the
 * exchange handler follows its audio_link.c, with two Frost changes:
 *  - it re-arms with the buffer it is about to fill and writes the TAG word
 *    last, so a late fill is refused by the DSP instead of shifting the frame;
 *  - the work (up to two 512-tap streams) runs here, at the lowest interrupt
 *    priority, so the 1 ms tick and the DSP UART link can interrupt it.
 * A host build (FROST_IRL_HOST) replaces the registers with a simulated slave. */
#include <stdint.h>
#include "firmware/protocol/ir_link_wire.h"
#include "ir_conv.h"
#define IRL_BUFW (((FROST_IRL_BYTES+31u)/32u)*8u+8u)   /* whole cache lines + search slack */
typedef struct {
    IrcStream s[FROST_IRL_STREAMS];
    uint32_t load[FROST_IRL_STREAMS][FROST_IRL_TAPS/2u];   /* sideband assembly */
    uint32_t load_mask[FROST_IRL_STREAMS],load_ver[FROST_IRL_STREAMS];
    uint32_t ready;                                         /* FROST_IRL_READY format */
    volatile uint32_t blocks,refused,lost,cycles,worst,remote,streams,failed,started,dsp;
    volatile uint32_t notdone,waiting_max,disables;   /* 31y11 diagnostics (7E 13) */   /* dsp: DSP link cycles (window max) */
    unsigned cur,waiting;
} FrostIrLink;
static FrostIrLink irl;
static int32_t irl_rx[2][IRL_BUFW] __attribute__((aligned(32)));
static int32_t irl_tx[2][IRL_BUFW] __attribute__((aligned(32)));
static int32_t irl_in[FROST_IRL_WORDS];

#ifdef FROST_IRL_HOST
/* Host model: the test drives irl_exchange_done() itself. */
static unsigned irl_host_armed;
static void irl_spis_init(void){irl_host_armed=0;}
static void irl_spis_start(const void *tx,void *rx,uint32_t n){(void)tx;(void)rx;(void)n;irl_host_armed=1;}
static int irl_spis_done(void){return 1;}
static void irl_spis_release(void){irl_host_armed=0;}
#define IRL_LINES_INVAL(p) ((void)(p))
#define IRL_LINES_WB(p,n) ((void)(p),(void)(n))
#define IRL_CYCLES() 0u
#else
#include <nds32_intrinsic.h>
#include "start/irq.h"
#define IRL_SPIS      0x40004000u
#define IRL_SPIS_CTRL (IRL_SPIS+0x00u)
#define IRL_SPIS_CLR  (IRL_SPIS+0x08u)
#define IRL_SPIS_TXD  (IRL_SPIS+0x0Cu)
#define IRL_SPIS_RXD  (IRL_SPIS+0x10u)
#define IRL_DMA       0x4000D000u
#define IRL_CH(n)     (IRL_DMA+0x20u*(n))
#define IRL_DMA_EN    (IRL_DMA+0x100u)
#define IRL_CH_RX     4u
#define IRL_CH_TX     5u
#define IRL_PIN_MUX   0x400100F0u
#define IRL_W(a)      (*(volatile uint32_t *)(a))
#define IRL_IRQ       13u                                   /* DMA channel 4 (receive) done */
static uint32_t irl_mux_before;static int irl_mux_saved;
static void irl_spis_init(void)
{
    IRL_W(IRL_SPIS_CTRL)&=~0x60u;  /* stop the receiver before changing its DMA */
    IRL_W(IRL_DMA_EN)&=~((1u<<IRL_CH_RX)|(1u<<IRL_CH_TX));
    if(!irl_mux_saved){irl_mux_before=IRL_W(IRL_PIN_MUX);irl_mux_saved=1;}
    IRL_W(IRL_PIN_MUX)=(IRL_W(IRL_PIN_MUX)&0x0000FFFFu)|0x18120000u;
    IRL_W(IRL_SPIS_CTRL)=(IRL_W(IRL_SPIS_CTRL)&~0x7u)|0xC8u;
    IRL_W(IRL_CH(IRL_CH_RX)+0x00u)=0x00444000u;
    IRL_W(IRL_CH(IRL_CH_RX)+0x04u)=0;IRL_W(IRL_CH(IRL_CH_RX)+0x08u)=0;IRL_W(IRL_CH(IRL_CH_RX)+0x0Cu)=0;
    IRL_W(IRL_CH(IRL_CH_RX)+0x10u)=0x00010000u;
    IRL_W(IRL_CH(IRL_CH_RX)+0x14u)=IRL_SPIS_RXD;
    IRL_W(IRL_CH(IRL_CH_RX)+0x18u)=0;
    IRL_W(IRL_CH(IRL_CH_RX)+0x1Cu)=0x100u;
    IRL_W(IRL_CH(IRL_CH_TX)+0x00u)=0x00401021u;
    IRL_W(IRL_CH(IRL_CH_TX)+0x04u)=0;IRL_W(IRL_CH(IRL_CH_TX)+0x08u)=0;IRL_W(IRL_CH(IRL_CH_TX)+0x0Cu)=0;
    IRL_W(IRL_CH(IRL_CH_TX)+0x10u)=0x00010000u;
    IRL_W(IRL_CH(IRL_CH_TX)+0x14u)=0;
    IRL_W(IRL_CH(IRL_CH_TX)+0x18u)=IRL_SPIS_TXD;
    IRL_W(IRL_CH(IRL_CH_TX)+0x1Cu)=0x100u;
    IRL_W(IRL_SPIS_CLR)|=0xFu;
    IRL_W(IRL_DMA_EN)|=(1u<<IRL_CH_RX)|(1u<<IRL_CH_TX);
}
/* A channel only (re)starts when its enable bit goes 0 -> 1 (OpenGP5). */
static void irl_spis_start(const void *tx,void *rx,uint32_t n)
{
    IRL_W(IRL_SPIS_CLR)|=0x2u;IRL_W(IRL_SPIS_CLR)|=0x4u;
    IRL_W(IRL_DMA_EN)&=~(1u<<IRL_CH_TX);
    IRL_W(IRL_CH(IRL_CH_TX)+0x14u)=(uint32_t)tx;IRL_W(IRL_CH(IRL_CH_TX)+0x04u)=n;
    IRL_W(IRL_DMA_EN)&=~(1u<<IRL_CH_RX);
    IRL_W(IRL_CH(IRL_CH_RX)+0x18u)=(uint32_t)rx;IRL_W(IRL_CH(IRL_CH_RX)+0x04u)=n;
    IRL_W(IRL_DMA_EN)|=1u<<IRL_CH_TX;
    IRL_W(IRL_DMA_EN)|=1u<<IRL_CH_RX;
    IRL_W(IRL_SPIS_CTRL)|=0x40u;
    IRL_W(IRL_SPIS_CTRL)|=0x20u;
}
static int irl_spis_done(void)
{
    if(!(IRL_W(IRL_CH(IRL_CH_RX)+0x1Cu)&1u) || !(IRL_W(IRL_CH(IRL_CH_TX)+0x1Cu)&1u))return 0;
    IRL_W(IRL_SPIS_CTRL)&=~0x60u;
    IRL_W(IRL_SPIS_CLR)|=0x9u;
    IRL_W(IRL_CH(IRL_CH_RX)+0x1Cu)|=0x100u;
    IRL_W(IRL_CH(IRL_CH_TX)+0x1Cu)|=0x100u;
    return 1;
}
static void irl_spis_release(void)
{
    IRL_W(IRL_SPIS_CTRL)&=~0x60u;
    IRL_W(IRL_DMA_EN)&=~((1u<<IRL_CH_RX)|(1u<<IRL_CH_TX));
    if(irl_mux_saved)IRL_W(IRL_PIN_MUX)=irl_mux_before;
}
/* RAM is cached on this core and the DMA does not see the cache. */
#define IRL_LINES(p,n,op) do{uint32_t a_;for(a_=(uint32_t)(p);a_<(uint32_t)(p)+(n);a_+=32u)__nds32__cctlva_wbinval_one_lvl(op,(void *)a_);__nds32__dsb();}while(0)
#define IRL_LINES_INVAL(p) IRL_LINES(p,FROST_IRL_BYTES,NDS32_CCTL_L1D_VA_INVAL)
#define IRL_LINES_WB(p,n) IRL_LINES(p,n,NDS32_CCTL_L1D_VA_WB)
#define IRL_CYCLES() __nds32__mfsr(NDS32_SR_PFMC0)
#endif

/* The words after the DSP's sync word (copied to irl_in), or 0. */
static const int32_t *irl_find(const int32_t *raw)
{
    const uint8_t *r=(const uint8_t *)raw;uint8_t *d=(uint8_t *)irl_in;unsigned o,i;
    for(o=0;o<=FROST_IRL_PAD;++o)
        if(r[o]==(uint8_t)FROST_IRL_SYNC_D && r[o+1]==(uint8_t)(FROST_IRL_SYNC_D>>8) &&
           r[o+2]==(uint8_t)(FROST_IRL_SYNC_D>>16) && r[o+3]==(uint8_t)(FROST_IRL_SYNC_D>>24))break;
    if(o>FROST_IRL_PAD)return 0;
    for(i=0;i<FROST_IRL_WORDS*4u;++i)d[i]=r[o+4u+i];
    return irl_in;
}
/* One sideband chunk: assemble; a complete version becomes the active set. */
static void irl_sideband(const int32_t *in)
{
    uint32_t h=(uint32_t)in[FROST_IRL_W_SIDE],j,c,v,i;
    if(!(h>>31))return;
    j=h&15u;c=(h>>4)&15u;v=(h>>8)&255u;
    if(j>=FROST_IRL_STREAMS || c>=FROST_IRL_CHUNKS)return;
    if(irl.load_ver[j]!=v){irl.load_ver[j]=v;irl.load_mask[j]=0;}
    for(i=0;i<FROST_IRL_CHUNK/2u;++i)irl.load[j][c*(FROST_IRL_CHUNK/2u)+i]=(uint32_t)in[FROST_IRL_W_TAPS+i];
    irl.load_mask[j]|=1u<<c;
    if(irl.load_mask[j]==(1u<<FROST_IRL_CHUNKS)-1u){
        for(i=0;i<FROST_IRL_TAPS/2u;++i)irl.s[j].q[i]=irl.load[j][i];
        irl.load_mask[j]=0;
        irl.ready=(irl.ready&~((0xffu<<(8u*j))|(1u<<(16u+j))))|(v<<(8u*j))|(1u<<(16u+j));
    }
}
/* rx: the frame just received; tx: the frame for the next exchange (already
 * armed: everything but the tag may go out stale, the tag goes last). */
static void irl_work(const int32_t *rx,int32_t *tx)
{
    uint32_t t0=IRL_CYCLES(),t,j,mask,n;const int32_t *in=irl_find(rx);int32_t *out=tx+1;
    out[FROST_IRL_W_TAG]=-1;
    if(!in || (uint32_t)in[FROST_IRL_W_MAGIC]!=FROST_IRL_MAGIC_D){++irl.refused;
        IRL_LINES_WB(tx,FROST_IRL_BYTES);return;}
    irl_sideband(in);
    irl.dsp=(uint32_t)in[FROST_IRL_W_INFO];
    mask=((uint32_t)in[FROST_IRL_W_FLAGS]>>8)&3u;irl.remote=(uint32_t)in[FROST_IRL_W_FLAGS]&1u;irl.streams=mask;
    for(j=0;j<FROST_IRL_STREAMS;++j){
        int32_t *y=out+FROST_IRL_W_AUDIO+j*FROST_IRL_BLOCK;
        if(mask&(1u<<j))irc_block(&irl.s[j],in+FROST_IRL_W_AUDIO+j*FROST_IRL_BLOCK,y);
        else{irc_clear(&irl.s[j]);for(n=0;n<FROST_IRL_BLOCK;++n)y[n]=0;}
    }
    out[FROST_IRL_W_MAGIC]=(int32_t)FROST_IRL_MAGIC_N;out[FROST_IRL_W_FLAGS]=(int32_t)irl.ready;
    out[FROST_IRL_W_SCALE]=(int32_t)irl.blocks;out[FROST_IRL_W_VERSIONS]=(int32_t)irl.refused;
    t=IRL_CYCLES()-t0;irl.cycles=t;if(irl.blocks>64u && t>irl.worst)irl.worst=t;
    out[FROST_IRL_W_SIDE]=(int32_t)t;
    IRL_LINES_WB(tx,FROST_IRL_BYTES);
    out[FROST_IRL_W_TAG]=in[FROST_IRL_W_TAG];                /* last: the frame is complete */
    IRL_LINES_WB(&out[FROST_IRL_W_TAG],4u);
    ++irl.blocks;
}
/* The exchange is complete: re-arm at once with the other pair, then fill it. */
void gp5_link_irq(void);
void gp5_link_irq(void)
{
    unsigned k=irl.cur,n=k^1u;
    if(!irl_spis_done()){
        /* Receive done before send: comes again. Bounded: a storm would starve
         * the main loop (and its re-arm). 31y10 used 100000 and never reset the
         * count, so after one failure every later re-arm died at once. */
        ++irl.notdone;
        if(++irl.waiting>irl.waiting_max)irl.waiting_max=irl.waiting;
        if(irl.waiting>2000u){
#ifndef FROST_IRL_HOST
            gp5_irq_disable(IRL_IRQ);
#endif
            irl.failed=1;++irl.disables;
        }
        return;
    }
    irl.waiting=0;irl.cur=n;
    irl_tx[n][0]=(int32_t)FROST_IRL_SYNC_N;irl_tx[n][1+FROST_IRL_W_TAG]=-1;
    IRL_LINES_WB(irl_tx[n],FROST_IRL_BYTES);
    irl_spis_start(irl_tx[n],irl_rx[n],FROST_IRL_BYTES);
    IRL_LINES_INVAL(irl_rx[k]);
    irl_work(irl_rx[k],irl_tx[n]);
}
static void irl_start(void)
{
    unsigned i,j;
    for(i=0;i<IRL_BUFW;++i){irl_rx[0][i]=irl_rx[1][i]=0;irl_tx[0][i]=irl_tx[1][i]=0;}
    irl_tx[0][0]=irl_tx[1][0]=(int32_t)FROST_IRL_SYNC_N;irl_tx[0][1+FROST_IRL_W_TAG]=irl_tx[1][1+FROST_IRL_W_TAG]=-1;
    for(j=0;j<FROST_IRL_STREAMS;++j){irc_clear(&irl.s[j]);irl.load_mask[j]=0;irl.load_ver[j]=0x100u;for(i=0;i<FROST_IRL_TAPS/2u;++i)irl.s[j].q[i]=0;}
    irl.ready=0;irl.blocks=irl.refused=irl.lost=irl.cycles=irl.worst=irl.remote=irl.streams=irl.failed=0;irl.cur=0;irl.waiting=0;
#ifndef FROST_IRL_HOST
    __nds32__mtsr(1u,NDS32_SR_PFM_CTL);__nds32__isb();      /* performance counter 0 counts cycles */
    IRL_LINES(irl_rx[0],sizeof irl_rx,NDS32_CCTL_L1D_VA_WBINVAL);IRL_LINES_WB(irl_tx[0],sizeof irl_tx);
#endif
    irl_spis_init();
    irl.started=1;
#ifndef FROST_IRL_HOST
    gp5_irq_priority(IRL_IRQ,3);
#endif
    irl_spis_start(irl_tx[0],irl_rx[0],FROST_IRL_BYTES);
#ifndef FROST_IRL_HOST
    gp5_irq_enable(IRL_IRQ);
#endif
}
/* Before the factory-loader hand-over: no more interrupts, slave released. */
void frost_irl_stop(void);
void frost_irl_stop(void)
{
#ifndef FROST_IRL_HOST
    gp5_irq_disable(IRL_IRQ);
#endif
    irl_spis_release();irl.started=0;
}
int frost_irl_running(void);
int frost_irl_running(void){return (int)irl.started;}
/* Acknowledged main-loop hand-off. Never called from the audio IRQ. A failed
 * pause must not permit a flash write while the master is still clocking. */
#ifdef FROST_IRL_HOST
static unsigned irl_host_pause_fail,irl_host_resume_fail,irl_host_controls,irl_host_pause_misses;   /* misses: transient (32q) */
static int irl_master_control(unsigned op)
{
    ++irl_host_controls;
    if(op==FROST_IRL_FLASH_PAUSE && irl_host_pause_misses){--irl_host_pause_misses;return 0;}
    if((op==FROST_IRL_FLASH_PAUSE && irl_host_pause_fail) ||
       (op==FROST_IRL_FLASH_RESUME && irl_host_resume_fail))return 0;
    frost_irl_flash_control(op);
    return FROST_IRL->quiet==(op==FROST_IRL_FLASH_PAUSE?2u:0u);
}
#else
unsigned dsp_link_raw(const uint8_t *,unsigned,uint8_t *,unsigned,unsigned,uint8_t *);
static int irl_master_control(unsigned op)
{
    uint8_t q[12]={0x4e,0x7f,12,0,1,0,0,0,0,0,0,0},r[76],status=0;unsigned n;
    q[8]=(uint8_t)op;
    n=dsp_link_raw(q,sizeof q,r,sizeof r,50,&status);
    return status==1 && n==sizeof r && r[0]==0x4e && r[1]==0x7f && r[2]==sizeof r && !r[3] &&
        r[4]==1 && !r[5] && !r[6] && !r[7] && r[8]==(op==FROST_IRL_FLASH_PAUSE?2u:0u) && !r[9] && !r[10] && !r[11];
}
#endif
/* Main loop, about every 10 ms: a link that went quiet for 20 ms (no DSP
 * exchanges, e.g. after a flash erase stalled this processor mid-exchange) is
 * re-armed from scratch so the next exchange starts aligned. */
static uint32_t irl_seen_blocks,irl_quiet_since,irl_flash_depth,irl_flash_writes;
static void __attribute__((unused)) irl_poll(uint32_t now)
{
    if(!irl.started || irl_flash_depth)return;
    /* Malformed completed frames are progress too. Do not re-arm mid-clock;
     * let the master's quiet interval provide a clean resync boundary. */
    if(irl.blocks+irl.refused!=irl_seen_blocks){irl_seen_blocks=irl.blocks+irl.refused;irl_quiet_since=now;return;}
    if((int32_t)(now-irl_quiet_since)>=20){
        /* A local re-arm can land in the master's next frame. Establish an
         * acknowledged quiet wire instead of endlessly restarting mid-frame. */
        irl_quiet_since=now;
        if(!irl_master_control(FROST_IRL_FLASH_PAUSE))return;
#ifndef FROST_IRL_HOST
        gp5_irq_disable(IRL_IRQ);
#endif
        irl_spis_init();irl.cur=0;irl.waiting=0;irl.failed=0;irl_spis_start(irl_tx[0],irl_rx[0],FROST_IRL_BYTES);++irl.lost;irl_quiet_since=now;
#ifndef FROST_IRL_HOST
        gp5_irq_enable(IRL_IRQ);
#endif
        (void)irl_master_control(FROST_IRL_FLASH_RESUME);
    }
}
/* Around controller flash writes (fcm_files.h frost_flash_*): stop the slave
 * and the interrupt, then re-arm from scratch. */
int frost_irl_flash_begin(void);
int frost_irl_flash_begin(void)
{
    if(!irl.started)return 1;
    if(irl_flash_depth){++irl_flash_depth;return 1;}
    if(!irl_master_control(FROST_IRL_FLASH_PAUSE))return 0;
    irl_flash_depth=1;
#ifdef FROST_IRL_HOST
    irl_host_armed=0;
#else
    gp5_irq_disable(IRL_IRQ);
    IRL_W(IRL_SPIS_CTRL)&=~0x60u;
    IRL_W(IRL_DMA_EN)&=~((1u<<IRL_CH_RX)|(1u<<IRL_CH_TX));
#endif
    return 1;
}
void frost_irl_flash_end(void);
void frost_irl_flash_end(void)
{
    if(!irl.started || !irl_flash_depth || --irl_flash_depth)return;
    ++irl_flash_writes;
    irl_spis_init();irl.cur=0;irl.waiting=0;irl.failed=0;irl_spis_start(irl_tx[0],irl_rx[0],FROST_IRL_BYTES);
#ifndef FROST_IRL_HOST
    gp5_irq_enable(IRL_IRQ);
#endif
    (void)irl_master_control(FROST_IRL_FLASH_RESUME);
}
/* 7E 13: read-only link status for the computer (tools/probe_ir_link.py). */
static unsigned __attribute__((unused)) irl_host_status(uint8_t *pl,unsigned max)
{
    uint32_t v[17];unsigned i,b;
    if(max<2u+4u*17u)return 0;
    v[0]=irl.started;v[1]=irl.blocks;v[2]=irl.refused;v[3]=irl.lost;v[4]=irl.failed;v[5]=irl.waiting;v[6]=irl.waiting_max;
    v[7]=irl.notdone;v[8]=irl.disables|(irl_flash_writes<<16);v[9]=irl.cur;v[10]=irl.cycles;v[11]=irl.worst;v[12]=irl.remote;v[13]=irl.dsp;
#ifdef FROST_IRL_HOST
    v[14]=v[15]=v[16]=0;
#else
    v[14]=IRL_W(IRL_CH(IRL_CH_RX)+0x1Cu);v[15]=IRL_W(IRL_CH(IRL_CH_TX)+0x1Cu);v[16]=IRL_W(IRL_SPIS_CTRL)|(IRL_W(IRL_DMA_EN)<<16);
#endif
    pl[0]=0x7e;pl[1]=0x13;
    for(i=0;i<17;++i)for(b=0;b<4;++b)pl[2+4*i+b]=(uint8_t)(v[i]>>(8*b));
    return 2u+4u*17u;
}
/* 1 while the DSP runs its IR blocks here and has sent a frame lately. */
static int __attribute__((unused)) irl_active(void){return irl.started && irl.remote && irl.blocks>0u;}
#endif
