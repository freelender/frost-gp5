/* Stage 4: the DSP's half of the IR link (docs/Build%20Firmware.md,
 * firmware/protocol/ir_link_wire.h). Included by firmware.h after frost_chain.
 *
 * SPI 1 + DMA0 as OpenGP5 0.5.5 platform/dsp/audio.h (MIT): the DSP is the
 * master. Buffers and state live in the platform area 0x1001E800..0x1001F3FF
 * (unused by Frost: no AUDIO_SPI1), not in LARGE or VARS.
 *
 * Per block: frost_irl_block_begin() before the chain, the IR blocks call
 * frost_irl_stream() (through frost_ir_remote_fn) in remote mode, and
 * frost_irl_block_end() after the chain. The first stream call (or the end)
 * checks the frame received in the previous exchange; the last expected
 * stream (or the end) starts the next exchange. Remote/local mode changes
 * only at a silent route commit (frost_chain_request_ir_remote).
 * All functions have external linkage and stay in flash (boot layout rule;
 * LARGE is full). */
#include "firmware/protocol/ir_link_wire.h"
#ifndef FROST_IRL_CLOCK
#define FROST_IRL_CLOCK() 0u
#endif
#ifndef FROST_IRL_ASSETS_BUSY
#define FROST_IRL_ASSETS_BUSY 0
#endif
#define FROST_IRL_GOOD_BLOCKS 64u      /* valid frames before switching to remote (93 ms) */
#define FROST_IRL_BAD_BLOCKS 690u      /* invalid frames before falling back to local (1 s) */
#define FROST_IRL_BOOT_BLOCKS 256u     /* first exchange after boot (0.37 s) */
/* Resync (31y10): a controller flash erase stalls it with interrupts off; on
 * return it re-arms mid-exchange and every later frame is shifted (31y9 on
 * the pedal: valid for 5 s, then sync found in ~4% of frames for good). The
 * controller re-arms cleanly only after 20 ms WITHOUT exchanges, so after
 * this many bad frames in a row the DSP stops exchanging for PAUSE blocks. */
#ifndef FROST_IRL_RESYNC_BAD
#define FROST_IRL_RESYNC_BAD 32u       /* 46 ms */
#endif
#define FROST_IRL_RESYNC_PAUSE 64u     /* 93 ms of silence on the wire */
typedef struct {
    float out[FROST_IRL_STREAMS][FROST_IRL_BLOCK];    /* what the IR block mixes (gain-ramped) */
#if defined(FROST_USB_AUDIO) && !defined(FROST_IRL_HOST)
    float (*hold)[FROST_IRL_BLOCK]; /* free platform RAM at 0x1001FC00; keeps DMA/state below ABUF */
#else
    float hold[FROST_IRL_STREAMS][FROST_IRL_BLOCK];   /* last valid controller block, for a fade-out */
#endif
#ifdef FROST_USB_AUDIO
    float usb_direct_gain;uint32_t usb_direct_off;
#endif
    float scale[FROST_IRL_STREAMS],inv[FROST_IRL_STREAMS],gain[FROST_IRL_STREAMS];
    uint32_t sum[FROST_IRL_STREAMS][FROST_IRL_CHUNKS];/* per-chunk coefficient fingerprints */
    uint32_t version[FROST_IRL_STREAMS];
    uint32_t block,expect,written,checked,frame_ok,started,quiet,ready,rxoff,cursor;
    uint32_t good_run,bad_run,generation,dirty,scan_c,scan_changed,zeroed;
    uint32_t t_begin,cyc_run,cyc_max,cyc_prev,cyc_blocks;
    uint32_t sync_seen,magic_ok,last_lag,last_tag,raw0,raw1;   /* diagnostics (0x7F4E status) */
    uint32_t pause,bad_seq,resyncs;   /* resync: exchanges paused after a run of bad frames */   /* DSP link cycles per block: running window max */
    uint32_t frames_ok,frames_bad,busy,ctl_cycles,switches;
} FrostIrlState;

#ifdef FROST_USB_AUDIO
#ifdef FROST_IRL_HOST
static uint32_t frost_media_record_host[64],frost_media_play_host[64];
#define FROST_MEDIA_RECORD frost_media_record_host
#define FROST_MEDIA_PLAY frost_media_play_host
#else
/* Free platform RAM, above the audio DMA area and the relocated IR hold.
 * Never read/write a USB payload while SPI DMA owns the frame containing it. */
#define FROST_MEDIA_RECORD ((uint32_t *)0x1001FE00u)
#define FROST_MEDIA_PLAY ((uint32_t *)0x1001FF00u)
#endif
#endif
#ifdef FROST_IRL_HOST
static int32_t frost_irl_host_rx[(FROST_IRL_BYTES+3u)/4u],frost_irl_host_tx[(FROST_IRL_BYTES+3u)/4u];
static FrostIrlState frost_irl_host_state;
static uint32_t frost_irl_host_busy,frost_irl_host_starts,frost_irl_host_finishes;
#define FROST_IRL_RX ((volatile int32_t *)frost_irl_host_rx)
#define FROST_IRL_TX ((volatile int32_t *)frost_irl_host_tx)
#define FROST_IRL (&frost_irl_host_state)
#define FROST_IRL_RXB ((const uint8_t *)frost_irl_host_rx)
#define FROST_IRL_TXW ((int32_t *)frost_irl_host_tx)
#define FROST_IRL_BUSY() (frost_irl_host_busy)
#define FROST_IRL_FINISH() (++frost_irl_host_finishes)
#define FROST_IRL_KICK() do{__asm__ volatile("" ::: "memory");++frost_irl_host_starts;}while(0)
#define FROST_IRL_SPI_INIT() do{frost_irl_host_busy=0;}while(0)
#define FROST_IRL_SPI_QUIET() do{frost_irl_host_busy=0;}while(0)
#else
#ifdef FROST_USB_AUDIO
#define FROST_IRL_BUFFER_BYTES 0x3C0u
#else
#define FROST_IRL_BUFFER_BYTES 0x2C0u
#endif
#define FROST_IRL_RAW_RX 0x1001E800u
#define FROST_IRL_RAW_TX (FROST_IRL_RAW_RX+FROST_IRL_BUFFER_BYTES)
#define FROST_IRL_STATE (FROST_IRL_RAW_TX+FROST_IRL_BUFFER_BYTES)
#define FROST_IRL_RX ((volatile int32_t *)FROST_IRL_RAW_RX)
#define FROST_IRL_TX ((volatile int32_t *)FROST_IRL_RAW_TX)
#define FROST_IRL ((FrostIrlState *)FROST_IRL_STATE)
#define FROST_IRL_RXB ((const uint8_t *)FROST_IRL_RAW_RX)
#define FROST_IRL_TXW ((int32_t *)FROST_IRL_RAW_TX)
#define FROST_IRL_SPI1 0x4000C000u
#define FROST_IRL_DMA0 0x40020000u
#define FROST_IRL_BUSY() (REG(FROST_IRL_DMA0+0x20u)&0x2u)
#define FROST_IRL_FINISH() (REG(FROST_IRL_SPI1+0x04u)|=0x1000u)
#define FROST_IRL_KICK() frost_irl_kick()
#define FROST_IRL_SPI_INIT() frost_irl_spi_init()
#define FROST_IRL_SPI_QUIET() frost_irl_spi_quiet()
typedef char frost_irl_fits[(FROST_IRL_STATE+sizeof(FrostIrlState)<=0x1001F400u)?1:-1];
typedef char frost_irl_buffers[(FROST_IRL_BYTES<=FROST_IRL_BUFFER_BYTES)?1:-1];
void frost_irl_spi_init(void);
void frost_irl_spi_init(void)
{
    uint32_t a;
    for(a=FROST_IRL_RAW_RX;a<FROST_IRL_STATE;a+=4u)REG(a)=0;
    REG(FROST_IRL_DMA0+0x108u)=FROST_IRL_SPI1;REG(FROST_IRL_DMA0+0x10Cu)=FROST_IRL_RAW_RX;
    REG(FROST_IRL_DMA0+0x100u)=0x800200C0u;
    REG(FROST_IRL_DMA0+0x104u)=0x14D7u;
    REG(FROST_IRL_DMA0+0x228u)=0x23u;
    REG(FROST_IRL_DMA0+0x128u)=FROST_IRL_RAW_TX;REG(FROST_IRL_DMA0+0x12Cu)=FROST_IRL_SPI1;
    REG(FROST_IRL_DMA0+0x120u)=0x80020090u;
    REG(FROST_IRL_DMA0+0x124u)=0x324Fu;
    REG(FROST_IRL_DMA0+0x224u)=0x22u;
    REG(FROST_IRL_DMA0+0x28u)=0xFFu;
    REG(FROST_IRL_DMA0+0x24u)=1u;
    REG(FROST_IRL_SPI1+0x04u)=0x000CC110u;
    REG(FROST_IRL_SPI1+0x10u)=0x000003EAu;
    REG(FROST_IRL_SPI1+0x08u)=FROST_IRL_BYTES;
    REG(FROST_IRL_SPI1+0x04u)=0x000CC110u;
}
/* Before a loader hand-over: SPI 1 as a firmware without the link, DMA off. */
void frost_irl_spi_quiet(void);
void frost_irl_spi_quiet(void)
{
    REG(FROST_IRL_DMA0+0x100u)&=~1u;REG(FROST_IRL_DMA0+0x120u)&=~1u;
    REG(FROST_IRL_DMA0+0x28u)=0xFFu;REG(FROST_IRL_SPI1+0x04u)=0x00021000u;
}
void frost_irl_kick(void);
void frost_irl_kick(void)
{
    __asm__ volatile("dmb" ::: "memory");   /* the frame is in RAM before the DMA starts */
    REG(FROST_IRL_SPI1+0x04u)&=~0x1000u;
    REG(FROST_IRL_SPI1+0x08u)=FROST_IRL_BYTES;
    REG(FROST_IRL_DMA0+0x114u)=FROST_IRL_BYTES;
    REG(FROST_IRL_DMA0+0x108u)=FROST_IRL_SPI1;REG(FROST_IRL_DMA0+0x10Cu)=FROST_IRL_RAW_RX;
    REG(FROST_IRL_DMA0+0x100u)=0x800200C1u;
    REG(FROST_IRL_DMA0+0x134u)=FROST_IRL_BYTES;
    REG(FROST_IRL_DMA0+0x128u)=FROST_IRL_RAW_TX;REG(FROST_IRL_DMA0+0x12Cu)=FROST_IRL_SPI1;
    REG(FROST_IRL_DMA0+0x120u)=0x80020091u;
    REG(FROST_IRL_DMA0+0x28u)=0xFFu;
}
#endif

void frost_irl_init(void);
void frost_irl_init(void)
{
    FrostIrlState *s=FROST_IRL;unsigned i,j;
    for(i=0;i<sizeof *s/4u;++i)((volatile uint32_t *)s)[i]=0;
    for(j=0;j<FROST_IRL_STREAMS;++j){s->version[j]=1u;for(i=0;i<FROST_IRL_CHUNKS;++i)s->sum[j][i]=0xffffffffu;}
#if defined(FROST_USB_AUDIO) && !defined(FROST_IRL_HOST)
    s->hold=(float (*)[FROST_IRL_BLOCK])0x1001FC00u;
    for(j=0;j<FROST_IRL_STREAMS;++j)for(i=0;i<FROST_IRL_BLOCK;++i)s->hold[j][i]=0.0f;
#endif
#ifdef FROST_USB_AUDIO
    s->usb_direct_gain=1.0f;
    for(i=0;i<FROST_IRL_BLOCK;++i)FROST_MEDIA_RECORD[i]=FROST_MEDIA_PLAY[i]=0;
#endif
    s->generation=frost_ir_generation;s->dirty=3u;    /* fingerprint everything first */
    FROST_IRL_SPI_INIT();
    FROST_IRL_TX[0]=(int32_t)FROST_IRL_SYNC_D;
}
/* Coefficient tap i of stream j as sent (taps past the IR length are 0). */
static inline __attribute__((always_inline)) float frost_irl_tap(const FrostIR *ir,unsigned i)
{return i<ir->taps?ir->coefficients[i]:0.0f;}
uint32_t frost_irl_chunk_sum(const FrostIR *ir,unsigned c);
uint32_t frost_irl_chunk_sum(const FrostIR *ir,unsigned c)
{
    uint32_t h=0x811c9dc5u;unsigned i;
    for(i=c*FROST_IRL_CHUNK;i<(c+1u)*FROST_IRL_CHUNK;++i){union{float f;uint32_t u;}v;v.f=frost_irl_tap(ir,i);h=(h^v.u)*16777619u;}
    return h;
}
/* Re-fingerprint one chunk of the first dirty stream per exchange (64 taps,
 * about 1k instructions) so a coefficient change never costs one long block.
 * A dirty stream is muted. After its last chunk, a change sets the new scale
 * and bumps the version (the controller then reloads it). */
void frost_irl_scan_step(void);
void frost_irl_scan_step(void)
{
    FrostIrlState *s=FROST_IRL;unsigned j=s->dirty&1u?0u:1u,c=s->scan_c,i;const FrostIR *ir=&frost_chain.ir_pool[j];uint32_t h;
    if(!s->dirty)return;
    h=frost_irl_chunk_sum(ir,c);if(h!=s->sum[j][c]){s->sum[j][c]=h;s->scan_changed=1;}
    if(++s->scan_c<FROST_IRL_CHUNKS)return;
    if(s->scan_changed){
        float m=0.0f;
        for(i=0;i<FROST_IRL_TAPS;++i){float a=frost_irl_tap(ir,i);if(a<0.0f)a=-a;if(a>m)m=a;}
        s->scale[j]=m>0.0f?m/32767.0f:1.0f;s->inv[j]=m>0.0f?32767.0f/m:0.0f;
        s->version[j]=(s->version[j]+1u)&0xffu;
    }
    s->dirty&=~(1u<<j);s->scan_c=0;s->scan_changed=0;
}
/* A little-endian word of the received frame. The slave's byte lag shifts
 * the frame (OpenGP5 measured 1 and 5 bytes). 31y4..31y6 used unaligned LDR
 * here and never saw a valid frame on the pedal (probe: 0 ok / 66k bad);
 * 31y2's byte reads worked. As OpenGP5: words when aligned, else bytes. */
static inline __attribute__((always_inline)) uint32_t frost_irl_ld(const uint8_t *p)
{
    /* volatile: GCC otherwise merges the four byte loads back into one
     * unaligned LDR (31y7 build 1 had zero LDRB). */
    const volatile uint8_t *v=(const volatile uint8_t *)p;
    if(!((uintptr_t)p&3u))return *(const uint32_t *)(const void *)p;
    return (uint32_t)v[0]|((uint32_t)v[1]<<8)|((uint32_t)v[2]<<16)|((uint32_t)v[3]<<24);
}
/* The frame received in the last exchange: valid when its tag is the block
 * number two blocks back. Once per block (flash; LARGE is full). */
void frost_irl_check(void);
FROST_AUDIO_RAM void frost_irl_check(void)
{
    FrostIrlState *s=FROST_IRL;const uint8_t *r;unsigned o;
    s->checked=1;s->frame_ok=0;
    if(!s->started){return;}
    /* Match the platform's spi1_block(): close the preceding SPI transfer
     * before testing DMA completion, then KICK clears this bit for the next
     * transfer. Without this boundary an interrupted slave exchange can stay
     * shifted across every subsequent DMA re-arm. Keep it before BUSY too. */
    FROST_IRL_FINISH();
    if(FROST_IRL_BUSY()){++s->busy;return;}
    __asm__ volatile("" ::: "memory");   /* the DMA finished: read the buffer now */
    r=FROST_IRL_RXB;
    {const volatile uint8_t *v=(const volatile uint8_t *)r;
     for(o=0;o<=FROST_IRL_PAD;++o)
        if(v[o]==(uint8_t)FROST_IRL_SYNC_N && v[o+1]==(uint8_t)(FROST_IRL_SYNC_N>>8) &&
           v[o+2]==(uint8_t)(FROST_IRL_SYNC_N>>16) && v[o+3]==(uint8_t)(FROST_IRL_SYNC_N>>24))break;}
    s->raw0=frost_irl_ld(r);s->raw1=frost_irl_ld(r+4u);
    if(o>FROST_IRL_PAD)return;
    ++s->sync_seen;s->last_lag=o;
    r+=o+4u;s->rxoff=o+4u;  /* byte offset of word 0 */
    if(frost_irl_ld(r+4u*FROST_IRL_W_MAGIC)!=FROST_IRL_MAGIC_N)return;
    ++s->magic_ok;s->last_tag=frost_irl_ld(r+4u*FROST_IRL_W_TAG);
    if(s->last_tag!=s->block-2u)return;
    s->ready=frost_irl_ld(r+4u*FROST_IRL_W_FLAGS);s->ctl_cycles=frost_irl_ld(r+4u*FROST_IRL_W_SIDE);s->frame_ok=1;
#ifdef FROST_USB_AUDIO
    /* Snapshot before any new exchange can replace this RX buffer. */
    s->usb_direct_off=frost_irl_ld(r+4u*FROST_IRL_W_INFO)==1u;
    for(o=0;o<FROST_IRL_BLOCK;++o)FROST_MEDIA_PLAY[o]=frost_irl_ld(r+4u*(FROST_IRL_W_USB+o));
#endif
}
/* Coefficient sideband chunk: a stream the controller has not loaded first,
 * else round robin. Stage 4b: only while a stream is dirty or unloaded, and
 * every 16th block to keep verifying (flash). */
void frost_irl_side(volatile int32_t *t);
void frost_irl_side(volatile int32_t *t)
{
    FrostIrlState *s=FROST_IRL;unsigned j,i,c,pick;const FrostIR *ir;
    frost_irl_scan_step();
    pick=s->cursor;
    for(j=0;j<FROST_IRL_STREAMS;++j){
        if(FROST_IRL_READY(s->ready,j)==s->version[j])continue;
        if(((pick>>3)&1u)!=j)pick=j<<3;   /* keep cycling chunks of the first unloaded stream */
        break;
    }
    j=(pick>>3)&1u;c=pick&7u;ir=&frost_chain.ir_pool[j];
    if(!(s->dirty&(1u<<j)) && frost_irl_chunk_sum(ir,c)!=s->sum[j][c]){s->dirty|=1u<<j;if(s->dirty==(1u<<j)){s->scan_c=0;s->scan_changed=0;}}
    for(i=0;i<FROST_IRL_CHUNK/2u;++i){
        float a=frost_irl_tap(ir,c*FROST_IRL_CHUNK+2u*i)*s->inv[j],b=frost_irl_tap(ir,c*FROST_IRL_CHUNK+2u*i+1u)*s->inv[j];
        int32_t qa=(int32_t)(a+(a<0.0f?-0.5f:0.5f)),qb=(int32_t)(b+(b<0.0f?-0.5f:0.5f));
        t[FROST_IRL_W_TAPS+i]=(int32_t)(((uint32_t)qa&0xffffu)|((uint32_t)qb<<16));
    }
    {union{float f;uint32_t u;}v;v.f=s->scale[j];t[FROST_IRL_W_SCALE]=(int32_t)v.u;}
    t[FROST_IRL_W_SIDE]=(int32_t)(0x80000000u|j|(c<<4)|(s->version[j]<<8));
    s->cursor=(pick+1u)&15u;
}
/* Header and start of the next exchange (unwritten streams: 0). */
void frost_irl_send(void);
FROST_AUDIO_RAM void frost_irl_send(void)
{
    FrostIrlState *s=FROST_IRL;volatile int32_t *t=FROST_IRL_TX+1;unsigned j,n;
    if(s->quiet || s->pause)return;
    if(FROST_IRL_BUSY()){++s->busy;return;}
    if(!FROST_IRL_ASSETS_BUSY && frost_ir_generation!=s->generation){s->generation=frost_ir_generation;s->dirty=3u;s->scan_c=0;s->scan_changed=0;}
    for(j=0;j<FROST_IRL_STREAMS;++j)if(!(s->written&(1u<<j)) && !(s->zeroed&(1u<<j))){
        for(n=0;n<FROST_IRL_BLOCK;++n)t[FROST_IRL_W_AUDIO+j*FROST_IRL_BLOCK+n]=0;
        s->zeroed|=1u<<j;
    }
    if(!FROST_IRL_ASSETS_BUSY && (s->dirty || FROST_IRL_READY(s->ready,0)!=s->version[0] || FROST_IRL_READY(s->ready,1)!=s->version[1] || !(s->block&15u)))frost_irl_side(t);
    else t[FROST_IRL_W_SIDE]=0;
    t[FROST_IRL_W_MAGIC]=(int32_t)FROST_IRL_MAGIC_D;
    t[FROST_IRL_W_FLAGS]=(int32_t)((frost_ir_remote&1u)|(s->written<<8));
    t[FROST_IRL_W_VERSIONS]=(int32_t)(s->version[0]|(s->version[1]<<8));
    t[FROST_IRL_W_INFO]=(int32_t)(s->cyc_prev>s->cyc_max?s->cyc_prev:s->cyc_max);
#ifdef FROST_USB_AUDIO
    /* Recording prepared after the previous chain block: one extra block of
     * recording latency keeps Frost's original early IR exchange boundary. */
    for(n=0;n<FROST_IRL_BLOCK;++n)t[FROST_IRL_W_USB+n]=(int32_t)FROST_MEDIA_RECORD[n];
#endif
    t[FROST_IRL_W_TAG]=(int32_t)s->block;
    FROST_IRL_KICK();s->started=1;s->written|=0x100u;  /* bit 8: sent this block */
}
/* Remote IR stream for ir: sends x, returns the controller's raw FIR sums of
 * the input two blocks back (faded to 0 while unavailable), or 0 if ir is not
 * a pool workspace. Stage 4b: one fused loop in LARGE. */
const float *frost_irl_stream(FrostIR *ir,const float *x);
FROST_AUDIO_RAM const float *frost_irl_stream(FrostIR *ir,const float *x)
{
    FrostIrlState *s=FROST_IRL;unsigned j,n,bit;int32_t *t;float g,k,step,*h,*y;
    if(ir==&frost_chain.ir_pool[0])j=0;else if(ir==&frost_chain.ir_pool[1])j=1;else return 0;
    if(!s->checked)frost_irl_check();
    bit=1u<<j;t=FROST_IRL_TXW+1+FROST_IRL_W_AUDIO+j*FROST_IRL_BLOCK;h=s->hold[j];y=s->out[j];g=s->gain[j];
    if(s->frame_ok && !(s->dirty&bit) && FROST_IRL_READY(s->ready,j)==s->version[j]){
        const uint8_t *r=FROST_IRL_RXB+s->rxoff+4u*(FROST_IRL_W_AUDIO+j*FROST_IRL_BLOCK);
        k=s->scale[j]*(1.0f/FROST_IRL_OUT_SHIFT);
        if(!((uintptr_t)r&3u)){const int32_t *rw=(const int32_t *)(const void *)r;
            for(n=0;n<FROST_IRL_BLOCK;++n){t[n]=(int32_t)(x[n]*FROST_IRL_IN_SCALE);h[n]=(float)rw[n]*k;}}
        else for(n=0;n<FROST_IRL_BLOCK;++n){const volatile uint8_t *q=(const volatile uint8_t *)(r+4u*n);
            t[n]=(int32_t)(x[n]*FROST_IRL_IN_SCALE);h[n]=(float)(int32_t)((uint32_t)q[0]|((uint32_t)q[1]<<8)|((uint32_t)q[2]<<16)|((uint32_t)q[3]<<24))*k;}
        if(g==1.0f)y=h;
        else{step=(1.0f-g)*(1.0f/FROST_IRL_BLOCK);for(n=0;n<FROST_IRL_BLOCK;++n){g+=step;y[n]=h[n]*g;}g=1.0f;}
    }else{
        for(n=0;n<FROST_IRL_BLOCK;++n)t[n]=(int32_t)(x[n]*FROST_IRL_IN_SCALE);
        if(g!=0.0f){step=-g*(1.0f/FROST_IRL_BLOCK);for(n=0;n<FROST_IRL_BLOCK;++n){g+=step;y[n]=h[n]*g;}g=0.0f;}
        else for(n=0;n<FROST_IRL_BLOCK;++n)y[n]=0.0f;
        for(n=0;n<FROST_IRL_BLOCK;++n)h[n]=0.0f;   /* repeat a held block once at most */
    }
    s->gain[j]=g;s->written|=bit;s->zeroed&=~bit;
    if((s->written&s->expect)==s->expect)frost_irl_send();
    return y;
}
void frost_irl_block_begin(void);
FROST_AUDIO_RAM void frost_irl_block_begin(void)
{
    FrostIrlState *s=FROST_IRL;unsigned i;uint32_t t0=FROST_IRL_CLOCK();
    s->t_begin=t0;frost_ir_remote_cycles=0;
    ++s->block;s->checked=0;s->written=0;s->expect=0;
    /* A dedicated silent commit block does not run any IR instances. Treat
     * it like bypass here so its exchange never slips to block_end between
     * two early exchanges (preset and upload-bypass transitions both use it). */
    if(frost_ir_remote && !frost_ir_bypass && frost_chain.transition!=FROST_ROUTE_COMMIT)
        for(i=0;i<FROST_SLOTS;++i)if(frost_chain.slots[i].type==FROST_IR){
            const FrostIR *ir=frost_chain.slots[i].state.ir;
            if(ir==&frost_chain.ir_pool[0])s->expect|=1u;else if(ir==&frost_chain.ir_pool[1])s->expect|=2u;
        }
    if(!s->expect){
        s->expect=0x200u;
        /* IR uploads bypass the cabinet for many blocks. Keep the exchange
         * early even then: starting after a heavy chain leaves the slave too
         * little time before the next block checks the reply. USB recording
         * is already staged from the previous block. No current IR input is
         * needed, so send zero streams now instead of waiting for block_end. */
        if(!s->quiet && s->block>=FROST_IRL_BOOT_BLOCKS){frost_irl_check();frost_irl_send();}
    }
    s->cyc_run=FROST_IRL_CLOCK()-t0;
}
/* After the chain: send if no stream did, then health and mode. */
void frost_irl_block_end(void);
FROST_AUDIO_RAM void frost_irl_block_end(void)
{
    FrostIrlState *s=FROST_IRL;uint32_t t0=FROST_IRL_CLOCK(),c;
    if(s->quiet || s->block<FROST_IRL_BOOT_BLOCKS)return;
    /* DSP cost of the link this block: begin + IR blocks (mix included) +
     * this end; reported to the controller (W_INFO) as a ~1 s window max. */
    c=s->cyc_run+frost_ir_remote_cycles;
    if(!s->checked)frost_irl_check();
    if(!(s->written&0x100u)){s->written&=3u;frost_irl_send();}
    c+=FROST_IRL_CLOCK()-t0;if(c>s->cyc_max)s->cyc_max=c;
    if(++s->cyc_blocks>=689u){s->cyc_prev=s->cyc_max;s->cyc_max=0;s->cyc_blocks=0;}
    if(s->frame_ok){++s->frames_ok;++s->good_run;s->bad_run=0;s->bad_seq=0;}
    else{if(s->started)++s->frames_bad;++s->bad_run;s->good_run=0;
        if(s->started && !s->pause && ++s->bad_seq>=FROST_IRL_RESYNC_BAD){
            s->pause=FROST_IRL_RESYNC_PAUSE;s->bad_seq=0;++s->resyncs;
            /* A stuck master DMA must also stop clocking, not just skip KICK. */
            FROST_IRL_SPI_QUIET();
        }}
    if(s->pause && !--s->pause){
        /* Rebuild both DMA descriptors after a genuine quiet interval. */
        FROST_IRL_SPI_INIT();FROST_IRL_TX[0]=(int32_t)FROST_IRL_SYNC_D;
        s->started=s->frame_ok=s->zeroed=0;s->ready=0;s->bad_seq=0;
    }
#ifndef FROST_IRL_REQUIRE_REMOTE
    if(!frost_ir_remote && s->good_run>=FROST_IRL_GOOD_BLOCKS){if(frost_chain_request_ir_remote(&frost_chain,1))++s->switches;}
    else if(frost_ir_remote && s->bad_run>=FROST_IRL_BAD_BLOCKS){if(frost_chain_request_ir_remote(&frost_chain,0))++s->switches;}
#endif
}
/* Controller command before a loader hand-over: stop exchanging for good. */
void frost_irl_quiet(void);
void frost_irl_quiet(void){FROST_IRL->quiet=1;FROST_IRL_SPI_QUIET();}
/* Flash is a two-sided hand-off: acknowledge a stopped master before the
 * slave is disabled, then resume only after the slave is ready. Loader quiet
 * is terminal and cannot be undone by a flash-resume or a duplicate request. */
void frost_irl_flash_control(uint32_t op);
void frost_irl_flash_control(uint32_t op)
{
    FrostIrlState *s=FROST_IRL;
    if(op==FROST_IRL_FLASH_PAUSE && !s->quiet){s->quiet=2;FROST_IRL_SPI_QUIET();}
    else if(op==FROST_IRL_FLASH_RESUME && s->quiet==2u){
        FROST_IRL_SPI_INIT();FROST_IRL_TX[0]=(int32_t)FROST_IRL_SYNC_D;
        s->started=s->frame_ok=s->zeroed=s->ready=s->bad_seq=s->pause=0;
        s->good_run=0;++s->resyncs;s->quiet=0;
    }
}
