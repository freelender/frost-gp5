/* Muted effects-only diagnostic. NAM is absent, not reduced or substituted. */
#include "timing_watch.h"
#ifdef FROST_DSP_USAGE
#include "firmware/protocol/dsp_usage_wire.h"
#endif
#ifndef FROST_PROFILE_CLOCK
#if defined(__arm__)
#define FROST_PROFILE_CLOCK() (*(volatile uint32_t *)0xE0001004u)
#else
#define FROST_PROFILE_CLOCK() 0u /* native fixture, never physical cycles */
#endif
#endif
#define FROST_PROFILE_SLOT(slot,type,begin,end) frost_slot_load_note(slot,type,begin,end)
#define FROST_PROFILE_BLOCK(is_fallback) ((is_fallback)?++frost_load.current.fallback:++frost_load.current.profiled)
#define FROST_LIBRARY_KERNELS_LATE 1
#ifdef FROST_OVERLAYS
void frost_ovl_load(const void *chain);
int frost_ovl_fits(const unsigned *types);
#define FROST_ROUTE_COMMIT_HOOK(s) frost_ovl_load(s)
#define FROST_ROUTE_ADMIT(types) frost_ovl_fits(types)   /* 32h: refuse chains the pool cannot hold */
#endif
#ifdef FROST_IR_LINK
#define FROST_IRL_REQUIRE_REMOTE 1       /* playable chains budget for controller IR */
#define FROST_IRL_CLOCK() REG(0xE0001004u)   /* DWT cycle counter */
#define FROST_EDIT_BLOCKS 1                     /* 31y9: block paths while knobs move */
#define FROST_LOAD_ADMIT 1                      /* 32i: refuse chains over the DSP budget (engine/load_budget.h) */
#define FROST_LINE_LATE_RELEASE 1378u          /* 31y14: overload mute releases after 2 s on time */
#endif
#include "engine/bundle.c"
#include "input.h"
#include "link_rx.h"
#include "link_tx.h"
#include "output.h"
#include "line_output.h"
#include "stack_watch.h"
#include "timing_watch.h"
#include "firmware/protocol/control.h"
#include "firmware/protocol/flash_hash_wire.h"
#include "firmware/protocol/fcm_wire.h"
#include "firmware/protocol/global_wire.h"
#include "firmware/protocol/tuner_wire.h"
#include "effects/common/frost_dsp.h"
#include "global_eq.h"
#include "effects/tuner/tuner.c"
/* Stage 2c tuner: active = output muted, chain skipped, delay line borrowed. */
static FrostTuner frost_tuner;
/* External linkage (boot-adjacent layout rule); flash code, runs only while tuning. */
void frost_tuner_run(const float *dry,float *wet);
FROST_NOINLINE void frost_tuner_run(const float *dry,float *wet)
{unsigned k;frost_tuner_block(&frost_tuner,dry,GP5_BLOCK);for(k=0;k<GP5_BLOCK;++k)wet[k]=0.0f;}
#ifdef FROST_FLASH_PROBE
#include "flash_probe.h"
#endif
#include "firmware/protocol/slots.c"
#include "firmware/protocol/preset_transfer.c"
#include "firmware/protocol/quick_preset.c"
#include "firmware/protocol/preset_read.c"
#include "firmware/protocol/policy.c"
#ifdef GP5_LARGE
/* 31b: chain state (slots, IR pool, buffers; 13.9 KB) lives in LARGE, the
 * data side of the RAM-resident audio path; VARS keeps code and control. */
static FrostChain frost_chain GP5_LARGE;
#else
static FrostChain frost_chain;
#endif
#include "ir_files.h"
/* Built-in cabinets are prepared in the pending snapshot, before the route
 * fade. They are never replaced by a second post-recall IR transaction. */
#if defined(__arm__)
__attribute__((section(".frost_quick_cold"),noinline))
#endif
FROST_COLD int frost_quick_builtin(FrostPreset *p)
{
    unsigned i,j,b=0,taps=FROST_TEST_IR_STORED;
#ifndef FROST_IR_LINK
    if(taps>128u)taps=128u;
#endif
    for(i=0;i<FROST_SLOTS;++i)if(p->types[i]==FROST_IR){
        if(b>=FROST_PRESET_IRS)return 0;p->ir_taps[b]=taps;
        for(j=0;j<FROST_IR_MAX;++j){
#ifdef FROST_TEST_IR_Q15
            p->ir_coefficients[b][j]=j<taps?(float)frost_test_ir_q15[j]*frost_test_ir_q15_scale:0.0f;
#else
            p->ir_coefficients[b][j]=j<taps?frost_test_ir[j]:0.0f;
#endif
        }
        ++b;
    }
    /* Pending storage may contain a previous larger preset. Always initialize
     * unused assets; never inherit coefficients from an earlier request. */
    for(;b<FROST_PRESET_IRS;++b){p->ir_taps[b]=0;for(j=0;j<FROST_IR_MAX;++j)p->ir_coefficients[b][j]=0.0f;}
    return 1;
}
#ifdef FROST_IR_LINK
/* The receive buffer aliases coefficient storage until expansion completes. */
#define FROST_IRL_ASSETS_BUSY (frost_ir_rx.state==FROST_IR_RECEIVING || frost_ir_rx.state==FROST_IR_VALIDATING || frost_ir_rx.state==FROST_IR_EXPANDING)
#include "ir_link.h"
#if FROST_IRL_QUIET_COMMAND == FROST_QUICK_COMMAND
#error IR status/quiet must not share the quick-preset opcode
#endif
#endif
static FrostOutputMeter frost_output_meter;
static FrostInputMeter frost_input_meter;
static FrostLinkRx frost_link_rx;
#ifdef GP5_LARGE
static FrostDelay frost_delay_workspace GP5_LARGE;
#ifdef FROST_LARGE_CODE
/* 31d: modulation buffers move to VARS so LARGE holds the library code. */
static FrostModulation frost_modulation_pool[FROST_MOD_WORKSPACES];
#else
static FrostModulation frost_modulation_pool[FROST_MOD_WORKSPACES] GP5_LARGE;
#endif
static FrostReverbWorkspace frost_reverb_workspace GP5_LARGE;
/* 31a: one FFT cabinet workspace (the first IR workspace); a second IR keeps
 * the direct form. 4.6 KB of the ~6 KB LARGE left by 30b. */
#if defined(FROST_IR_LINK)
/* Stage 4 (31y5): the IR runs on the controller; the DSP fallback is the
 * 128-tap direct form, so no FFT workspace (frees ~5.6 KB VARS + 4 KB LARGE). */
#elif defined(FROST_LARGE_CODE)
/* 31c: LARGE holds the audio code; the FFT workspace moves to VARS. */
static FrostIRFFT frost_ir_fft_workspace;
#else
static FrostIRFFT frost_ir_fft_workspace GP5_LARGE;
#endif
#else
static FrostIRFFT frost_ir_fft_workspace;
static FrostDelay frost_delay_workspace; /* native fixture, not target layout */
static FrostModulation frost_modulation_pool[FROST_MOD_WORKSPACES];
static FrostReverbWorkspace frost_reverb_workspace;
#endif
static FrostSlotProtocol slot_protocol;
static FrostPresetTransfer preset_transfer;
static uint32_t rejected,commands,sample_number;
#if defined(FROST_IR_FFT_SPLIT_SPECTRUM) && !(defined(FROST_IR_LINK) && defined(GP5_LARGE))
#ifdef GP5_LARGE
static float frost_ir_fft_spectrum[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE] GP5_LARGE;
#else
static float frost_ir_fft_spectrum[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE];
#endif
#endif
#ifdef FROST_OVERLAYS
/* 31i stock-style overlays. Each rarely used kernel is a position-independent
 * module linked in dsp-data flash (.ovl_<id>, verified by
 * build/library/ovl_check.py). On every route commit (all slots silent at the
 * fade midpoint, before any slot runs) the modules of the chain's types are
 * copied into this LARGE pool and the kernel table points at the copies. The
 * pool does NOT hold every six-module set (32d: six largest 10.9 KB), so
 * route and preset requests are admitted only when their module set fits
 * (frost_ovl_fits, the loader's own size arithmetic; FROST_SLOT_CHAIN_FULL
 * otherwise). Before the first load every entry is NULL and overlay types
 * pass their input; a module that still does not fit counts an overflow. */
#define FROST_OVL_POOL 7936u   /* 32l: was 8960; 256 bytes to frost_ovl_kernel; beta.10: 768 to the tail code (LARGE) */
static uint32_t frost_ovl_pool[FROST_OVL_POOL/4u] GP5_LARGE;
#define FROST_OVL_MODULES(M) \
    M(ts,FROST_TS,frost_ts_sample) \
    M(bd2,FROST_BD2,frost_bd2_sample) \
    M(klon,FROST_KLON,frost_klon_sample) \
    M(bb,FROST_BLUESBREAKER,frost_bluesbreaker_sample) \
    M(ocd,FROST_OCD,frost_ocd_sample) \
    M(rat,FROST_RAT,frost_rat_sample) \
    M(distplus,FROST_DISTPLUS,frost_distplus_sample) \
    M(dynacomp,FROST_DYNACOMP,frost_dynacomp_sample) \
    M(compplus,FROST_COMPPLUS,frost_compplus_sample) \
    M(ge7,FROST_GE7,frost_ge7_sample) \
    M(ns2,FROST_NS2,frost_ns2_sample) \
    M(phase90,FROST_PHASE90,frost_phase90_sample) \
    M(univibe,FROST_UNIVIBE,frost_univibe_sample) \
    M(tr2,FROST_TR2,frost_tr2_sample) \
    M(mistress,FROST_MISTRESS,frost_mistress_sample) \
    M(m117,FROST_M117,frost_m117_sample) \
    M(vb2,FROST_VB2,frost_vb2_sample) \
    M(dd3,FROST_DD3,frost_dd3_sample) \
    M(dm2,FROST_DM2,frost_dm2_sample) \
    M(pog,FROST_POG,frost_octave_sample) \
    M(fcm,FROST_FCM,frost_fcm_sample) \
    M(drop,FROST_DROP,frost_drop_sample) \
    M(ns1x,FROST_NS1X,frost_ns1x_sample) \
    M(multiband,FROST_MULTIBAND,frost_multiband_sample) \
    M(highpass,FROST_HIGHPASS,frost_highpass_sample) \
    M(lowpass,FROST_LOWPASS,frost_lowpass_sample) \
    M(autowah,FROST_AUTOWAH,frost_autowah_sample) \
    M(treble_boost,FROST_TREBLE_BOOST,frost_treble_boost_sample) \
    M(foxx,FROST_FOXX,frost_foxx_sample) \
    M(tape,FROST_TAPE,frost_tape_sample) \
    M(vintage_fuzz,FROST_VINTAGE_FUZZ,frost_vintage_fuzz_sample) \
    M(acoustic,FROST_ACOUSTIC,frost_acoustic_sample) \
    M(ampheads,FROST_DELUXE,frost_amphead_sample) \
    M(rotary,FROST_ROTARY,frost_rotary_sample) \
    M(ringmod,FROST_RINGMOD,frost_ringmod_sample) \
    M(crusher,FROST_CRUSHER,frost_crusher_sample) \
    M(harmtrem,FROST_HARMTREM,frost_harmtrem_sample) \
    M(muff,FROST_MUFF,frost_muff_sample) \
    M(boost,FROST_BOOST,frost_boost_sample) \
    M(swell,FROST_SWELL,frost_swell_sample) \
    M(cali_eq,FROST_CALI_EQ,frost_cali_eq_sample) \
    M(pingpong,FROST_PINGPONG,frost_pingpong_sample) \
    M(sl100,FROST_SL100,frost_sl100_sample)
typedef char frost_side_block_matches[(GP5_BLOCK==FROST_SIDE_BLOCK)?1:-1];
#define FROST_OVL_EXTERN(id,type,fn) extern char __ovl_##id##_start[],__ovl_##id##_end[];
FROST_OVL_MODULES(FROST_OVL_EXTERN)
typedef struct {unsigned type;const char *start,*end;const void *entry;} FrostOvlModule;
#define FROST_OVL_ROW(id,type,fn) {type,__ovl_##id##_start,__ovl_##id##_end,(const void *)fn},
static const FrostOvlModule frost_ovl_modules[]={FROST_OVL_MODULES(FROST_OVL_ROW)};
typedef char FrostOverlayMaskBound[(sizeof frost_ovl_modules/sizeof frost_ovl_modules[0]<=64)?1:-1];
uint32_t frost_ovl_overflows,frost_ovl_loaded_bytes;
uint64_t frost_ovl_loaded_inv; /* inverted 64-bit set; 33 distinct modules */
/* All seven amp-head types share one relocatable kernel. Local state carries
 * the voice, and every type gets an alias for both settled and ramped paths. */
static inline unsigned frost_ovl_type(unsigned t)
{return t>=FROST_DELUXE && t<=FROST_5150?FROST_DELUXE:t;}
FROST_LARGE_ONLY float frost_ovl_call(void *state,float x,const float *p,unsigned type)
{FrostOvlKernel k=frost_ovl_kernel[type];return k?k(state,x,p):x;}
/* 32h: bytes the loader would use for these six slot types (same rounding). */
FROST_COLD int frost_ovl_fits(const unsigned *types)
{
    unsigned i,m,need=0;
    for(m=0;m<sizeof frost_ovl_modules/sizeof frost_ovl_modules[0];++m)
        for(i=0;i<FROST_SLOTS;++i)if(frost_ovl_type(types[i])==frost_ovl_modules[m].type){
            need+=((unsigned)(frost_ovl_modules[m].end-frost_ovl_modules[m].start)+7u)&~7u;break;
        }
    return need<=FROST_OVL_POOL;
}
FROST_COLD void frost_ovl_load(const void *chain)
{
    const FrostChain *c=(const FrostChain *)chain;uint8_t *pos=(uint8_t *)frost_ovl_pool,*end=pos+FROST_OVL_POOL;unsigned i,m;uint64_t want=0;
    /* 31y5: copying modules from dsp-data flash in the commit block is the
     * suspected cause of the >200% spike on every route change. The pool
     * layout depends only on the set of used modules, so an unchanged set
     * (e.g. adding a reverb) keeps the resident copies untouched. */
    for(m=0;m<sizeof frost_ovl_modules/sizeof frost_ovl_modules[0];++m)
        for(i=0;i<FROST_SLOTS;++i)if(frost_ovl_type(c->slots[i].type)==frost_ovl_modules[m].type)want|=UINT64_C(1)<<m;
    if(~frost_ovl_loaded_inv==want)return;
    frost_ovl_loaded_inv=~want;
    for(i=0;i<FROST_EFFECTS;++i)frost_ovl_kernel[i]=0;
    frost_fcm_block_kernel=0;frost_amphead_block_kernel=0;frost_sl100_block_kernel=0;
    for(m=0;m<sizeof frost_ovl_modules/sizeof frost_ovl_modules[0];++m){
        const FrostOvlModule *o=&frost_ovl_modules[m];unsigned used=0,n=(unsigned)(o->end-o->start);
        for(i=0;i<FROST_SLOTS;++i)if(frost_ovl_type(c->slots[i].type)==o->type)used=1;
        if(!used)continue;
        if(n>(unsigned)(end-pos)){++frost_ovl_overflows;continue;}
        __builtin_memcpy(pos,o->start,n);
        frost_ovl_kernel[o->type]=(FrostOvlKernel)(uintptr_t)(pos+((uintptr_t)o->entry-(uintptr_t)o->start));
        if(o->type==FROST_DELUXE)for(i=FROST_TWIN;i<=FROST_5150;++i)frost_ovl_kernel[i]=frost_ovl_kernel[o->type];
        if(o->type==FROST_DELUXE)frost_amphead_block_kernel=(FrostBlockFn)(uintptr_t)(pos+((uintptr_t)frost_amphead_block-(uintptr_t)o->start));
        if(o->type==FROST_FCM)frost_fcm_block_kernel=(FrostFcmBlockFn)(uintptr_t)(pos+((uintptr_t)frost_fcm_block-(uintptr_t)o->start));
        if(o->type==FROST_SL100)frost_sl100_block_kernel=(FrostBlockFn)(uintptr_t)(pos+((uintptr_t)frost_sl100_block-(uintptr_t)o->start));
        pos+=(n+7u)&~7u;
    }
    frost_ovl_loaded_bytes=(uint32_t)(pos-(uint8_t *)frost_ovl_pool);
    __asm__ volatile("dsb\nisb" ::: "memory");
}
#endif
#ifdef FROST_LINE_TEST
#if !defined(FROST_AUDIO_ADC) || FROST_AUDIO_ADC != 1
#error Line test requires real ADC input
#endif
static FrostLineOutput line_output;
static const unsigned frost_bench_mute=0;
/* Live edits are allowed: route/type/position/policy/preset changes fade through
 * silence in the engine. Only IR length is admitted here: every IR slot (active or
 * pending) must use 1..128 taps, the longest direct FIR timed on the pedal, or
 * up to the configured FFT capacity (256, or opt-in 512 taps). Anything
 * heavier is caught by the post-open LATE latch. */
FROST_HOT static unsigned frost_line_ir_ok(const FrostInstance *slot)
{
    const FrostIR *ir=slot->state.ir;
    if(slot->type!=FROST_IR)return 1;
    if(!ir || ir->taps<1)return 0;
#ifdef FROST_IR_LINK
    return ir->taps<=FROST_IR_MAX;   /* local fallback reads at most FROST_IR_LOCAL_TAPS */
#else
    {const FrostIRFFT *f=frost_ir_fft_of(ir);return ir->taps<=(f && f->parts?FROST_IR_FFT_PARTS*FROST_IR_FFT_BLOCK:128u);}
#endif
}
FROST_HOT static unsigned frost_line_route_ok(void)
{
    unsigned i;
    for(i=0;i<FROST_SLOTS;++i)if(!frost_line_ir_ok(&frost_chain.slots[i]))return 0;
    return 1;
}
#else
static const unsigned frost_bench_mute=1;
#endif
/* Stage 2b global input gain (linear; the audio path glides to the target). */
static float frost_in_gain=1.0f,frost_in_gain_target=1.0f;
static int32_t frost_global_gain_ddb;static uint32_t frost_global_mode;
static float frost_bypass_mix;
/* Continue the chain/IR transport while crossfading to dry. Tuner and numeric
 * safety mutes take priority. No effect targets or histories are replaced. */
FROST_DSP_RAM static void frost_bypass_blend(const float *dry,float *wet)
{
    unsigned i;float target=(frost_global_mode&FROST_GLOBAL_BYPASS)?1.0f:0.0f;
    float mix=frost_bypass_mix,step=(target-mix)/(float)GP5_BLOCK;
    for(i=0;i<GP5_BLOCK;++i){mix+=step;wet[i]+=mix*(dry[i]-wet[i]);}
    frost_bypass_mix=target;
}
FROST_COLD static void fw_init(void)
{
    frost_bypass_mix=0.0f;frost_global_eq_init();
    frost_in_gain=frost_in_gain_target=1.0f;frost_global_gain_ddb=0;frost_global_mode=0;frost_stereo_output=0;frost_tuner_stop(&frost_tuner);
    frost_ir_file_init();
    frost_ir_bypass=0;frost_ir_bypass_pending=2u;
#ifdef FROST_IR_LINK
    frost_ir_remote=1;frost_ir_remote_pending=2u;frost_ir_remote_fn=frost_irl_stream;frost_irl_init();
    frost_chain_commit_alone=1;   /* 31y14: commit in its own silent block */
#endif
#ifdef FROST_IR_RAM
    __asm__ volatile("dsb\nisb" ::: "memory");
#endif
#ifdef FROST_LINE_TEST
    frost_line_output_init(&line_output);
#if defined(__arm__) && defined(__ARM_FP)
    /* Flush-to-zero (FPSCR.FZ, bit 24): subnormal results/inputs become signed zero.
     * Decaying gate/reverb/delay states otherwise sit in the subnormal range in
     * silence; measured on the GP-5 as default chain + reverb ~255k cycles/block in
     * silence versus ~180k while playing. Affects only |x| < 2^-126. Polled DSP, no
     * FPU use in interrupts, so the setting persists in thread mode. */
    {uint32_t fpscr;__asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));fpscr|=1u<<24;__asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));}
#endif
#endif
    frost_timing_init(&frost_timing);
    unsigned i;frost_tx_init(&frost_link_tx);frost_rx_init(&frost_link_rx);frost_input_init(&frost_input_meter);frost_output_init(&frost_output_meter);frost_chain_init(&frost_chain);rejected=commands=sample_number=0;
#if !(defined(FROST_IR_LINK) && defined(GP5_LARGE))
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    frost_ir_fft_workspace.spectrum=frost_ir_fft_spectrum;
#endif
    (void)frost_ir_bind_fft(&frost_chain.ir_pool[0],&frost_ir_fft_workspace); /* = frost_chain_bind_ir_fft(...,1) before start */
#endif
    (void)frost_chain_bind_delay(&frost_chain,&frost_delay_workspace);
    (void)frost_chain_bind_modulation(&frost_chain,frost_modulation_pool);
    (void)frost_chain_bind_reverb(&frost_chain,&frost_reverb_workspace);
    for(i=0;i<FROST_SETTING_COUNT;++i)
        (void)frost_control_set(&frost_chain,i,(uint32_t)frost_settings[i].control.initial);
#ifdef FROST_LINE_TEST
    /* 31a: a baked cabinet longer than the 128-tap default boots at its full
     * length when the FFT path serves it (baked with its own end fade). */
#ifdef FROST_IR_LINK
    /* Stage 4: full length stays in the coefficients (mirrored to the
     * controller); the local fallback reads only the first 128 taps. */
    if(FROST_TEST_IR_STORED>128u)
#else
    if(FROST_TEST_IR_STORED>128u && FROST_TEST_IR_STORED<=FROST_IR_FFT_PARTS*FROST_IR_FFT_BLOCK)
#endif
        (void)frost_test_ir_load(&frost_chain,FROST_IR,FROST_TEST_IR_STORED);
#endif
    frost_slots_init(&slot_protocol);
    frost_preset_transfer_init(&preset_transfer);
#ifdef FROST_OVERLAYS
    frost_ovl_loaded_inv=0;   /* init always (re)copies */
    frost_ovl_load(&frost_chain);
#endif
}
FROST_HOT FROST_CORE __attribute__((noinline)) FROST_AUDIO_RAM static void fw_process(const int32_t *in,int32_t *left,int32_t *right)
{
    float dry[GP5_BLOCK],wet[GP5_BLOCK],side[GP5_BLOCK],g=frost_in_gain;unsigned i;
    frost_side=side;   /* beta.14: this block's stereo side signal (engine/stereo.h) */
    if(g!=frost_in_gain_target){float d=frost_in_gain_target-g;g=d<1e-4f && d>-1e-4f?frost_in_gain_target:g+0.25f*d;frost_in_gain=g;}
    for(i=0;i<GP5_BLOCK;++i){
        float input=frost_input_sample(&frost_input_meter,in[i]);
#ifdef FROST_AUDIO_ADC
        dry[i]=input*g;++sample_number;
#else
        (void)input;dry[i]=(float)((int)((sample_number++)&255u)-128)*0.001f;
#endif
    }
    /* Existing cumulative fault counter is the boot-lifetime latch. The guarded
     * core returns independently of saturation. Commands/cycle reset cannot
     * resume audio after a numeric fault; only normal initialization clears it. */
#ifdef FROST_IR_LINK
    /* Before the shed/latch decision: a shed block must skip ALL processing
     * (31y2/31y4 had this inside the else and kept processing shed blocks). */
    frost_irl_block_begin();
#endif
    frost_side_used=0;   /* beta.14: set again by a stereo effect in this block's chain run */
#ifdef FROST_LINE_TEST
    if(!frost_line_route_ok())frost_line_latch(&line_output,FROST_LINE_ROUTE);
    if(frost_line_late(&line_output,GP5_LATE_BLOCKS,frost_chain.transition!=FROST_ROUTE_IDLE) || line_output.flags)
        {for(i=0;i<GP5_BLOCK;++i)wet[i]=0.0f;}   /* shed one block after a late, or latched */
    else
#endif
    if(frost_tuner.active)frost_tuner_run(dry,wet);
    else if(frost_chain.faults){for(i=0;i<GP5_BLOCK;++i)wet[i]=0.0f;}
    else {
        (void)frost_chain_process_guarded(&frost_chain,dry,wet,GP5_BLOCK,0);
        if(!frost_chain.faults && ((frost_global_mode&FROST_GLOBAL_BYPASS) || frost_bypass_mix>0.0f))frost_bypass_blend(dry,wet);
        if(!frost_chain.faults && frost_global_eq.active && !frost_global_eq_block(wet,GP5_BLOCK)){++frost_chain.faults;for(i=0;i<GP5_BLOCK;++i)wet[i]=0.0f;}
        if(frost_side_used){   /* beta.14 stereo: the side follows processing bypass and the output EQ */
            if(frost_chain.faults || frost_bypass_mix>=1.0f)frost_side_used=0;
            else{
                if(frost_bypass_mix>0.0f)for(i=0;i<GP5_BLOCK;++i)frost_side[i]*=1.0f-frost_bypass_mix;
                if(frost_global_eq.active)frost_global_eq_side(frost_side,GP5_BLOCK);
            }
        }
    }
#ifdef FROST_IR_LINK
    frost_irl_block_end();
#endif
    for(i=0;i<GP5_BLOCK;++i){
#ifdef FROST_LINE_TEST
        if(frost_side_used){left[i]=frost_output_pack(wet[i]+frost_side[i]);right[i]=frost_output_pack(wet[i]-frost_side[i]);}
        else left[i]=right[i]=frost_output_pack(wet[i]);
#else
        (void)frost_output_sample(&frost_output_meter,wet[i]);
        left[i]=right[i]=0;
#endif
    }
#ifdef FROST_LINE_TEST
    if(frost_chain.faults)frost_line_latch(&line_output,FROST_LINE_NUMERIC);
    /* Keep float headroom through the master stage. fw_output_block performs
     * conversion/metering after VOL and latches any invalid boundary samples.
     * Ordinary finite clipping remains recoverable; numeric faults do not. */
    if(line_output.flags)for(i=0;i<GP5_BLOCK;++i)left[i]=right[i]=0;
#endif
}
#ifdef FROST_LINE_TEST
#ifdef FROST_USB_AUDIO
#include "media_audio.h"
#endif
/* Out of line so the boot rehearsal warms the same instructions the DMA path runs. */
FROST_HOT FROST_CORE __attribute__((noinline)) static void fw_output_block(const int32_t *left,const int32_t *right,
                            volatile uint32_t *out_left,volatile uint32_t *out_right,uint32_t requested)
{
#ifdef FROST_USB_AUDIO
 frost_media_output(left,right,out_left,out_right,requested);
#else
 frost_line_render(&line_output,&frost_output_meter,left,right,out_left,out_right,GP5_BLOCK,requested);
#endif
}
#endif
/* Boot only, before DMA/clock/interface enable. Exercise the same out-of-line
 * processing function, discard private scratch outputs, then restore normal
 * initial histories/settings/meters/phase/protocol/trace. Never repaint stack.
 * Memory clobber keeps the rehearsal observable to the optimizing compiler.
 * Reinitialization may alter instruction residency: timing remains unproven. */
__attribute__((unused,noinline)) static void fw_prepare(void)
{
    int32_t input[GP5_BLOCK]={0},left[GP5_BLOCK],right[GP5_BLOCK];uint32_t faults;
    fw_process(input,left,right);
#ifdef FROST_LINE_TEST
    /* Rehearse the output stage too, into private scratch at level0. */
    {uint32_t scratch_left[GP5_BLOCK],scratch_right[GP5_BLOCK];
     fw_output_block(left,right,scratch_left,scratch_right,0);}
#endif
    __asm__ volatile("" ::: "memory");
    faults=frost_chain.faults;
#ifdef FROST_LINE_TEST
    {uint32_t flags=line_output.flags;fw_init();frost_line_latch(&line_output,flags);}
#else
    fw_init();
#endif
    frost_chain.faults=faults; /* Never hide a failed rehearsal by reinitializing. */
}
#ifdef FROST_CONTROL_SCHEDULER
/* Slot edits and quick-recall completion polls: same replies/counters as the
 * general handler, without bringing the large flash dispatcher into cache. */
FROST_CTL_RAM static void frost_slot_command_ram(const uint8_t *p,uint32_t n)
{
    uint8_t reply[FROST_SLOT_REPLY_BYTES];uint32_t status;unsigned bytes=FROST_SLOT_REPLY_BYTES;
    if(n==12u && p[0]==0x4d && p[1]==0x7f){
        status=quick_preset_status(&preset_transfer,&slot_protocol,&frost_chain,p,reply);bytes=FROST_QUICK_REPLY_BYTES;
        if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED)++rejected;
    }else{
    frost_slots_command(&slot_protocol,&frost_chain,p,n,reply);
    status=frost_wire_get32(reply+12);
    if(status==FROST_SLOT_APPLIED && frost_wire_get32(p+12)!=FROST_SLOT_GET){
        ++commands;
    }else if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED)++rejected;
    }
    send_frame(reply,bytes);
}
#endif
static int fw_command(uint32_t cmd,const uint8_t *p,uint32_t n)
{
    unsigned i,b;
    if(cmd==FROST_LINE_COMMAND){
        uint8_t reply[48];uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME};
        if(p && n>=12)v[1]=frost_wire_get32(p+8);
        if(p && n==12 && p[0]==0x3b && p[1]==0x7f && p[2]==12 && !p[3]){
            v[2]=frost_wire_get32(p+4)!=1?FROST_SLOT_BAD_VERSION:!v[1]?FROST_SLOT_BAD_REQUEST_ID:FROST_SLOT_APPLIED;
        }
        if(v[2]==FROST_SLOT_APPLIED){
#ifdef FROST_LINE_TEST
            v[3]=1;v[4]=FROST_LINE_CAP_Q16;v[5]=line_output.gain;v[6]=line_output.flags;v[8]=line_output.requested;
            v[10]=line_output.flags || !line_output.gain;
#else
            v[10]=1;
#endif
            v[7]=frost_chain.faults;v[9]=FROST_AUDIO_SOURCE;
        }
        reply[0]=0x3b;reply[1]=0x7f;reply[2]=48;reply[3]=0;
        for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_LINK_TX_COMMAND){
        uint8_t reply[48];frost_tx_read(&frost_link_tx,FROST_AUDIO_SOURCE,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_LOAD_COMMAND){
        uint8_t reply[48];frost_load_read(&frost_load,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_SLOT_LOAD_COMMAND){
        uint8_t reply[FROST_SLOT_LOAD_BYTES];frost_slot_load_read(&frost_load,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_SERVICE_COMMAND){
        uint8_t reply[48];frost_service_read(&frost_timing,&frost_service,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_TIMING_COMMAND){
        uint8_t reply[48];
        frost_timing_read(&frost_timing,frost_bench_mute,FROST_AUDIO_SOURCE,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_STACK_COMMAND){
        uint8_t reply[48];uint32_t sp=0;const volatile uint32_t *base=0;
#if defined(__arm__) || defined(__thumb__)
        __asm__ volatile("mrs %0, msp" : "=r"(sp));
        base=(const volatile uint32_t *)FROST_STACK_BOTTOM;
#endif
        frost_stack_read(base,sp,frost_bench_mute,FROST_AUDIO_SOURCE,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_LINK_RX_COMMAND){
        uint8_t reply[48];
        frost_rx_read(&frost_link_rx,FROST_AUDIO_SOURCE,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_INPUT_COMMAND){
        uint8_t reply[FROST_INPUT_REPLY_BYTES];
        frost_input_read(&frost_input_meter,frost_bench_mute,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_OUTPUT_COMMAND){
        uint8_t reply[FROST_OUTPUT_REPLY_BYTES];
        frost_output_read(&frost_output_meter,frost_chain.faults,frost_bench_mute,FROST_AUDIO_SOURCE,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_POLICY_COMMAND){
        uint8_t reply[48];unsigned status;
        frost_policy_command(&slot_protocol,&frost_chain,p,n,reply);status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_ACCEPTED || (status==FROST_SLOT_APPLIED && frost_wire_get32(p+12)==FROST_POLICY_SET))++commands;
        else if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED)++rejected;
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_PRESET_READ_COMMAND){
        uint8_t reply[FROST_PRESET_READ_REPLY_BYTES];
        frost_preset_read_command(&slot_protocol,&frost_chain,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_QUICK_COMMAND){
        uint8_t reply[FROST_QUICK_REPLY_BYTES];unsigned status;
        frost_quick_preset_command(&preset_transfer,&slot_protocol,&frost_chain,p,n,reply,frost_quick_builtin);
        status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_ACCEPTED)++commands;
        else if(status!=FROST_SLOT_REPLAYED && status!=FROST_SLOT_APPLIED)++rejected;
        gp5_reply(reply,sizeof reply);return 1;
    }
    if(cmd==FROST_PRESET_COMMAND){
        uint8_t reply[FROST_PRESET_REPLY_BYTES];uint32_t status;
        frost_preset_command(&preset_transfer,&slot_protocol,&frost_chain,p,n,reply);
        status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_ACCEPTED)++commands;
        else if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED)++rejected;
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_ROUTE_WRITE_COMMAND){
        uint8_t reply[FROST_ROUTE_WRITE_REPLY_BYTES];uint32_t status;
        frost_route_command(&slot_protocol,&frost_chain,p,n,reply);
        status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_ACCEPTED || status==FROST_SLOT_APPLIED)++commands;
        else if(status!=FROST_SLOT_REPLAYED)++rejected;
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_ROUTE_STATUS_COMMAND){
        uint8_t reply[FROST_ROUTE_STATUS_REPLY_BYTES];
        frost_route_status(&slot_protocol,&frost_chain,p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_SETTING_COMMAND){
        uint8_t reply[FROST_SETTING_REPLY_BYTES];uint32_t status;
        frost_control_command(&frost_chain,&slot_protocol,p,n,reply);
        status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_APPLIED)++commands;
        else if(status!=FROST_SLOT_REPLAYED)++rejected;
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_SLOT_COMMAND){
        uint8_t reply[FROST_SLOT_REPLY_BYTES];uint32_t status;
        frost_slots_command(&slot_protocol,&frost_chain,p,n,reply);
        status=frost_wire_get32(reply+12);
        if(status==FROST_SLOT_APPLIED && frost_wire_get32(p+12)!=FROST_SLOT_GET){
            ++commands;
        }else if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED)++rejected;
        /* Link IO stays outside fw_process; APPLIED is not a smoothing ACK. */
        gp5_reply(reply,FROST_SLOT_REPLY_BYTES);return 1;
    }
    if(cmd==0x7f10 && n==12){
        uint32_t id=gp5_get32(p+4),value=gp5_get32(p+8);
        if(slot_protocol.revision!=UINT32_MAX && frost_control_set(&frost_chain,id,value)){
            frost_slots_legacy_changed(&slot_protocol);++commands;
        }else ++rejected;
        gp5_ack();return 1;
    }
    if(cmd==0x7f30 && n==4){
        uint8_t reply[44]={0x30,0x7f,44,0};
        uint32_t v[10]={0x46584332u,GP5_CYCLES_LAST,GP5_CYCLES_WORST,GP5_LATE_BLOCKS,
                       sizeof(frost_chain),frost_chain.faults,rejected,commands,frost_bench_mute,1};
        for(i=0;i<10;++i)for(b=0;b<4;++b)reply[4+4*i+b]=(uint8_t)(v[i]>>(8*b));
        gp5_reply(reply,44);return 1;
    }
    if(cmd==0x7f31 && n==4){
        uint8_t reply[48]={0x31,0x7f,48,0};
        for(i=0;i<FROST_SETTING_COUNT;++i){
            uint32_t value=frost_chain_transition(&frost_chain)?UINT32_MAX:frost_control_actual(&frost_chain,i);
            for(b=0;b<4;++b)reply[4+4*i+b]=(uint8_t)(value>>(8*b));
        }
        gp5_reply(reply,48);return 1;
    }
    if(cmd==0x7f34 && n==4){
        uint8_t reply[8]={0x34,0x7f,8,0};
        for(b=0;b<4;++b)reply[4+b]=(uint8_t)(sample_number>>(8*b));
        gp5_reply(reply,8);return 1;
    }
    if(cmd==0x7f33 && n==4){
        GP5_CYCLES_LAST=GP5_CYCLES_WORST=GP5_LATE_BLOCKS=0;
        gp5_ack();return 1;
    }
#ifdef FROST_FLASH_HASH
    if(cmd==FROST_FLASH_HASH_COMMAND && n==16){
        /* 31e/31f: read-only, bounded FNV-1a of memory-mapped DSP flash.
         * Little-endian reply words: [cmd|16<<16][token][status][fnv]. In range
         * iff 1<=len<=CHUNK and LOW<=a and a+len<=HIGH (unsigned wrap rejects a<LOW). */
        uint32_t r[4],a=gp5_get32(p+8),len=gp5_get32(p+12),h=0x811c9dc5u;
        r[0]=FROST_FLASH_HASH_COMMAND|16u<<16;r[1]=gp5_get32(p+4);r[2]=1u;
        if(len-1u<FROST_FLASH_HASH_CHUNK && a-FROST_FLASH_HASH_LOW<=FROST_FLASH_HASH_HIGH-FROST_FLASH_HASH_LOW-len){
            const volatile uint8_t *q=(const volatile uint8_t *)a;
#ifdef FROST_FLASH_PROBE
            (void)*(const volatile uint32_t *)frost_flash_probe; /* keep the probe linked */
#endif
            for(i=0;i<len;++i)h=(h^q[i])*0x01000193u;
            r[2]=0u;
        }
        r[3]=h;gp5_reply((const uint8_t *)r,16);return 1;
    }
#endif
    if(cmd==FROST_GLOBAL_EQ_COMMAND){
        uint8_t reply[FROST_GLOBAL_REPLY_BYTES];frost_global_eq_command(p,n,reply);gp5_reply(reply,sizeof reply);return 1;
    }
    if(cmd==FROST_GLOBAL_COMMAND){
        /* Stage 2b global audio settings (firmware/protocol/global_wire.h). */
        uint8_t reply[FROST_GLOBAL_REPLY_BYTES];uint32_t v[6]={0},status=FROST_SLOT_BAD_FRAME;
        if(p && n>=12)v[1]=gp5_get32(p+8);
        if(p && n==FROST_GLOBAL_REQUEST_BYTES && p[0]==0x4a && p[1]==0x7f && p[2]==FROST_GLOBAL_REQUEST_BYTES && !p[3]){
            for(i=0;i<6;++i)v[i]=gp5_get32(p+4+4*i);
            if(v[0]!=1)status=FROST_SLOT_BAD_VERSION;
            else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
            else if(v[2]>FROST_GLOBAL_SET)status=FROST_SLOT_BAD_OPERATION;
            else if(v[2]==FROST_GLOBAL_GET)status=FROST_SLOT_APPLIED;
            else if((int32_t)v[3]<FROST_GLOBAL_GAIN_MIN || (int32_t)v[3]>FROST_GLOBAL_GAIN_MAX || v[4]>1 || v[5]>3)status=FROST_SLOT_BAD_VALUE;
            else if(v[4]!=frost_ir_bypass && !frost_chain_request_ir_bypass(&frost_chain,v[4]))status=FROST_SLOT_BUSY;
            else {
                frost_global_gain_ddb=(int32_t)v[3];frost_global_mode=v[5];
                frost_stereo_output=v[5]&1u;   /* beta.14: OUTPUT MODE STEREO enables side signals */
                frost_in_gain_target=frost_db_to_gain((float)frost_global_gain_ddb*0.1f);status=FROST_SLOT_APPLIED;
            }
        }
        for(i=0;i<FROST_GLOBAL_REPLY_BYTES;++i)reply[i]=0;
        reply[0]=0x4a;reply[1]=0x7f;reply[2]=FROST_GLOBAL_REPLY_BYTES;
        {uint32_t r[7]={1,v[1],status,(uint32_t)frost_global_gain_ddb,frost_ir_bypass_pending<2u?frost_ir_bypass_pending:frost_ir_bypass,frost_global_mode,frost_ir_bypass_pending<2u};
         for(i=0;i<7;++i)for(b=0;b<4;++b)reply[4+4*i+b]=(uint8_t)(r[i]>>(8*b));}
        gp5_reply(reply,sizeof(reply));return 1;
    }
#ifdef FROST_IR_LINK
    if(cmd==FROST_IRL_QUIET_COMMAND){
        /* Stage 4 IR link: status, terminal loader quiet, or acknowledged
         * controller-flash pause/resume. Invalid requests remain read-only. */
        uint8_t reply[76];const FrostIrlState *st=FROST_IRL;uint32_t r[18];
        if(p && n==12 && p[0]==0x4e && p[1]==0x7f && p[2]==12 && !p[3] && gp5_get32(p+4)==1){
            uint32_t op=gp5_get32(p+8);
            if(op==FROST_IRL_LOADER_QUIET)frost_irl_quiet();else frost_irl_flash_control(op);
        }
        r[0]=1;r[1]=st->quiet;r[2]=frost_ir_remote;r[3]=st->frames_ok;r[4]=st->frames_bad;r[5]=st->busy;r[6]=st->switches;
        r[7]=st->ctl_cycles;r[8]=st->block;r[9]=st->ready;r[10]=st->version[0]|(st->version[1]<<8);
        r[11]=st->sync_seen;r[12]=st->magic_ok;r[13]=st->last_lag;r[14]=st->last_tag;r[15]=st->raw0;r[16]=st->raw1;r[17]=st->resyncs;
        reply[0]=0x4e;reply[1]=0x7f;reply[2]=76;reply[3]=0;
        for(i=0;i<18;++i)for(b=0;b<4;++b)reply[4+4*i+b]=(uint8_t)(r[i]>>(8*b));
        gp5_reply(reply,sizeof(reply));return 1;
    }
#endif
    if(cmd==FROST_TUNER_COMMAND){
        /* Stage 2c tuner (firmware/protocol/tuner_wire.h). */
        uint8_t reply[FROST_TUNER_REPLY_BYTES];uint32_t v[3]={0},status=FROST_SLOT_BAD_FRAME;
        if(p && n>=12)v[1]=gp5_get32(p+8);
        if(p && n==FROST_TUNER_REQUEST_BYTES && p[0]==0x4b && p[1]==0x7f && p[2]==FROST_TUNER_REQUEST_BYTES && !p[3]){
            for(i=0;i<3;++i)v[i]=gp5_get32(p+4+4*i);
            if(v[0]!=1)status=FROST_SLOT_BAD_VERSION;
            else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
            else if(v[2]>FROST_TUNER_STOP)status=FROST_SLOT_BAD_OPERATION;
            else {
                status=FROST_SLOT_APPLIED;
                if(v[2]==FROST_TUNER_START && !frost_tuner.active &&
                   !frost_tuner_start(&frost_tuner,(void *)frost_delay_workspace.line.pcm,(unsigned)sizeof(frost_delay_workspace.line)))status=FROST_SLOT_BAD_VALUE;
                if(v[2]==FROST_TUNER_STOP && frost_tuner.active){
                    frost_tuner_stop(&frost_tuner);
                    frost_delay_reset(&frost_delay_workspace);           /* the borrowed history is gone */
                    if(frost_chain.transition==FROST_ROUTE_IDLE){frost_chain.fade_position=0;frost_chain.transition=FROST_ROUTE_IN;}  /* fade back in */
                }
            }
        }
        for(i=0;i<FROST_TUNER_REPLY_BYTES;++i)reply[i]=0;
        reply[0]=0x4b;reply[1]=0x7f;reply[2]=FROST_TUNER_REPLY_BYTES;
        {union {float f;uint32_t u;} fq,cl;uint32_t r[7];fq.f=frost_tuner.freq;cl.f=frost_tuner.clarity;
         r[0]=1;r[1]=v[1];r[2]=status;r[3]=frost_tuner.active;r[4]=frost_tuner.seq;r[5]=fq.u;r[6]=cl.u;
         for(i=0;i<7;++i)for(b=0;b<4;++b)reply[4+4*i+b]=(uint8_t)(r[i]>>(8*b));}
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_IR_COMMAND){
        uint8_t reply[FROST_IR_REPLY_BYTES];frost_ir_file_command(p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    if(cmd==FROST_FCM_COMMAND){
        /* AMP model record from the controller (firmware/protocol/fcm_wire.h). */
        uint8_t reply[FROST_FCM_REPLY_BYTES];frost_fcm_command(p,n,reply);
        gp5_reply(reply,sizeof(reply));return 1;
    }
    /* No command can clear the hard mute. */
    return 0;
}
/* Library kernels last: keeps the original hot path/command layout. */
#include "engine/bundle_library.c"
