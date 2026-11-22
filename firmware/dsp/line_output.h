#ifndef FROST_LINE_OUTPUT_H
#define FROST_LINE_OUTPUT_H
#include "firmware/protocol/line_wire.h"
#include "output.h"
/* opened: a nonzero capped target was ever accepted this boot (never clears
 * before initialization). late_base: late count absorbed while never opened. */
#define FROST_LINE_LATE_BURST 128u    /* late blocks within one window that latch (~19%: chronic overload) */
#define FROST_LINE_LATE_WINDOW 689u    /* blocks, ~1 s at 44.1 kHz/64 */
/* Blocks after a route fade whose lates are absorbed (~70 ms). Route fades use
 * the per-sample chain path, which on the pedal can take ~1.8M cycles per block
 * (revision 23f: a type change left a DMA backlog whose cascade latched LATE).
 * Within the grace period, late blocks outside the fade are shed so the DSP
 * catches up; blocks inside the fade are never shed (the fade must advance). */
#define FROST_LINE_FADE_GRACE 48u
/* recovering: length of the current run of consecutive late blocks (0 = on time). */
#define FROST_LINE_RECOVERY_LIMIT 689u /* a catch-up run longer than ~1 s is chronic overload */
typedef struct {uint32_t gain,flags,requested,opened,late_base,late_recent,late_window,late_tolerated,fade_grace,late_in_fades,shed_blocks,recovering,late_quiet,late_releases;} FrostLineOutput;
static inline void frost_line_output_init(FrostLineOutput *s)
{s->gain=s->flags=s->requested=s->opened=s->late_base=s->late_recent=s->late_window=s->late_tolerated=s->fade_grace=s->late_in_fades=s->shed_blocks=s->recovering=s->late_quiet=s->late_releases=0;}
static inline void frost_line_latch(FrostLineOutput *s,unsigned flags)
{s->flags|=flags;if(s->flags)s->gain=0;}
/* Before the output has ever opened, late blocks are absorbed into the baseline.
 * After opening, an isolated late block is tolerated and counted (at most a short
 * glitch, still under the hard cap; e.g. a cold command colliding with audio).
 * FROST_LINE_LATE_BURST late blocks inside one FROST_LINE_LATE_WINDOW mean
 * chronic overload and latch silence until reboot.
 * A counter that moves backwards (reset) also latches. */
/* in_fade: the chain is in a route fade-out/fade-in (output deliberately fading
 * through silence). Lates there, and for FROST_LINE_FADE_GRACE blocks after
 * (a block's late flag is seen one block later), are absorbed and counted in
 * late_in_fades; they cannot produce a loud glitch and the cap still applies. */
/* Returns 1 when a new late block was counted outside a fade: the caller sheds
 * this block's effect processing (outputs silence for one block) so the DSP
 * catches up with the audio DMA instead of cascading. Measured on the pedal: once
 * late, the whole block path ran ~1.6x slower and stayed late for 34 blocks. */
static inline unsigned frost_line_late(FrostLineOutput *s,uint32_t late,unsigned in_fade)
{
    unsigned shed=0;
    if(in_fade)s->fade_grace=FROST_LINE_FADE_GRACE;
    if(!s->opened){s->late_base=late;return 0;}
#ifdef FROST_LINE_LATE_RELEASE
    /* 31y14: an overload-only latch (no numeric/level/route/invalid flag)
     * releases after FROST_LINE_LATE_RELEASE blocks without a new late block
     * (the latched DSP sheds every block meanwhile); the output gain then
     * ramps back up. One route-change spike no longer means a reboot. */
    if(s->flags==FROST_LINE_LATE){
        if(late!=s->late_base){s->late_base=late;s->late_quiet=0;}
        else if(++s->late_quiet>=FROST_LINE_LATE_RELEASE){
            s->flags=0;s->late_recent=s->late_window=s->recovering=s->late_quiet=0;++s->late_releases;s->fade_grace=FROST_LINE_FADE_GRACE;
        }
        return 0;
    }
#endif
    if(late<s->late_base){frost_line_latch(s,FROST_LINE_LATE);return 0;}
    if(s->fade_grace){
        uint32_t d=late-s->late_base;s->late_in_fades+=d;s->late_base=late;
        if(in_fade)return 0;
        --s->fade_grace;
        if(d){++s->shed_blocks;return 1;}
        return 0;
    }
    /* One overrun is one event: while the DSP sheds to catch up with the DMA,
     * the shed blocks still report late (backlog). Count only the first late of
     * a consecutive run toward the burst limit; a run longer than ~1 s latches
     * as chronic overload. Revision 23h measured 4 edits -> 61 lates -> latch. */
    if(late!=s->late_base){
        uint32_t d=late-s->late_base;s->late_tolerated+=d;s->late_base=late;shed=1;
        if(!s->recovering)s->late_recent+=d;
        if(++s->recovering>FROST_LINE_RECOVERY_LIMIT){frost_line_latch(s,FROST_LINE_LATE);return 0;}
    }else s->recovering=0;
    if(s->late_recent>=FROST_LINE_LATE_BURST){frost_line_latch(s,FROST_LINE_LATE);return 0;}
    if(++s->late_window>=FROST_LINE_LATE_WINDOW){s->late_window=0;s->late_recent=0;}
    if(shed)++s->shed_blocks;
    return shed;
}
/* Main audio path only. Scratch words contain binary32, not Q31: apply master
 * before the sole PCM conversion, preserving peaks above unity at lower VOL.
 * Four Q16 steps/sample preserve the old ~372 ms full-range ramp. Validate the
 * original +/-16 chain envelope even at VOL=0; attenuation must not hide faults. */
static inline void frost_line_render(FrostLineOutput *s,FrostOutputMeter *meter,const int32_t *left,const int32_t *right,
                                    volatile uint32_t *out_left,volatile uint32_t *out_right,
                                    unsigned count,uint32_t requested)
{
    unsigned i;uint32_t target=0;s->requested=requested;
    if(requested>65536u)frost_line_latch(s,FROST_LINE_LEVEL);
    else target=requested; /* full-scale unity; no overflowing 65536*65536 */
    if(target && !s->flags)s->opened=1;
    for(i=0;i<count;++i){
        if(s->flags){out_left[i]=out_right[i]=0;continue;}
        float l=frost_output_unpack(left[i]),r=frost_output_unpack(right[i]),gain;
        if(s->gain<target)s->gain+=target-s->gain<4u?target-s->gain:4u;
        else if(s->gain>target)s->gain-=s->gain-target<4u?s->gain-target:4u;
        gain=(float)s->gain*(1.0f/65536.0f);
        if(!(l>=-16.0f && l<=16.0f) || !(r>=-16.0f && r<=16.0f)){
            frost_output_count(&meter->frames);frost_output_count(&meter->invalid);
            frost_line_latch(s,FROST_LINE_INVALID);out_left[i]=out_right[i]=0;continue;
        }
        out_left[i]=(uint32_t)frost_output_sample(meter,l*gain);
        out_right[i]=left[i]==right[i]?out_left[i]:(uint32_t)frost_output_sample(meter,r*gain);
    }
    /* A bad sample cuts the entire block, including any samples preceding it. */
    if(s->flags)for(i=0;i<count;++i)out_left[i]=out_right[i]=0;
}
#endif
