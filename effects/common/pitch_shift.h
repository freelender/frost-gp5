#ifndef FROST_PITCH_SHIFT_H
#define FROST_PITCH_SHIFT_H
#include <stdint.h>
/* Reusable time-domain pitch-shifting primitives (DROP, OCTAVE).
 *
 * Overlay modules may not call code outside themselves, so the per-sample
 * functions are a template (pitch_shift_impl.h) that each effect instantiates
 * once, under its own name prefix and overlay section, with its own state;
 * this header holds the types and the small always-inlined helpers.
 *
 * FrostPsHistory  caller-owned 4608-byte history: the anti-aliased signal at
 *                 full rate (2048 x int16, +/-4.0 full scale) plus a /8
 *                 decimated, low-passed analysis copy (256 x int16, 46 ms)
 *                 used for the coarse correlation search.
 * FrostPsInput    per-slot input stage: optional anti-alias low-pass (for
 *                 reading faster than the input), the analysis decimator and
 *                 an onset (pick attack) detector.
 * FrostPsSplicer  one read head moving through the history at `ratio`
 *                 samples per sample (4-point Hermite interpolation). When it
 *                 drifts out of its band it jumps by J samples, chosen by a
 *                 normalized cross-correlation search (coarse on the decimated
 *                 copy, then per sample with a parabolic sub-sample peak), so
 *                 the waveform continues; the jump is crossfaded with
 *                 smoothstep gains normalized by the measured correlation
 *                 (amplitude-complementary for a perfect match,
 *                 power-complementary for none: no level dip or bump).
 *                 The search is spread over many samples (bounded work per
 *                 sample, no block spikes).
 * Splicing policy (latency/quality): candidates are scanned in increasing |J|
 * and the first clearly good match (correlation >= good, past its peak) is
 * taken, so latency stays near one period of the lowest note playing;
 * otherwise the best match over the whole range. */
#define FROST_PS_N 2048u
#define FROST_PS_MASK (FROST_PS_N-1u)
#define FROST_PS_DEC 8u
#define FROST_PS_ND 256u
#define FROST_PS_DMASK (FROST_PS_ND-1u)
#define FROST_PS_SCALE 8192.0f   /* int16 = value * 8192: +/-4.0, as frost_pcm16 */
/* Layout-compatible with FrostModulation (4608-byte line + 16 bytes), so a
 * slot's exclusive modulation workspace holds it. The decimated index is
 * pos/8 (no extra state). */
typedef struct { int16_t pcm[FROST_PS_N]; int16_t dec[FROST_PS_ND]; uint32_t pos,hold; float acc,spare; } FrostPsHistory;
#define FROST_PS_INLINE static inline __attribute__((always_inline))

/* ---- arithmetic helpers (no library calls) ---- */
FROST_PS_INLINE float frost_ps_rsqrt(float x)
{
    union { float f; uint32_t u; } v;float y;
    if(!(x>1e-30f))return 0.0f;
    v.f=x;v.u=0x5f3759dfu-(v.u>>1);y=v.f;
    y=y*(1.5f-0.5f*x*y*y);y=y*(1.5f-0.5f*x*y*y);return y;
}
FROST_PS_INLINE int16_t frost_ps_q(float v)
{v*=FROST_PS_SCALE;v=v>32767.0f?32767.0f:v<-32767.0f?-32767.0f:v;return (int16_t)(v>=0.0f?v+0.5f:v-0.5f);}
/* int64 -> float without a library call (overlay modules may not call out). */
FROST_PS_INLINE float frost_ps_l2f(int64_t x)
{uint64_t u=x<0?(uint64_t)0-(uint64_t)x:(uint64_t)x;float f=(float)(uint32_t)(u>>32)*4294967296.0f+(float)(uint32_t)u;return x<0?-f:f;}
/* Zero n bytes (a multiple of 4) with one small loop: reset functions run
 * from DSP code flash, which is full, so they stay compact. */
FROST_PS_INLINE void frost_ps_zero(void *p,unsigned n){uint32_t *w=(uint32_t *)p;for(;n>=4u;n-=4u)*w++=0u;}

/* ---- input stage ---- */
/* Per-slot filter state; write positions live in the history (h->pos =
 * next full-rate write index, h->hold = onset hold-off, h->acc = decimation sum). */
typedef struct { float aa[2],lp[2],fast,slow; } FrostPsInput;
FROST_PS_INLINE void frost_ps_input_reset(FrostPsInput *s,FrostPsHistory *h)
{
    unsigned i;for(i=0;i<FROST_PS_N;++i)h->pcm[i]=0;for(i=0;i<FROST_PS_ND;++i)h->dec[i]=0;
    h->pos=h->hold=0;h->acc=h->spare=0.0f;s->aa[0]=s->aa[1]=s->lp[0]=s->lp[1]=s->fast=s->slow=0.0f;
}
/* ---- splicer ---- */
enum { FROST_PS_IDLE, FROST_PS_COARSE, FROST_PS_FINE };
typedef struct {
    float d,d_old,score,left,right,last;   /* score: best correlation while searching; last: the splice's during its fade */
    uint16_t fade,fade_len,state,cand,best,lo,hi,right_pending;
} FrostPsSplicer;
typedef struct {
    float ratio;          /* read speed: < 1 lowers, > 1 raises the pitch */
    float dmin,dmax;      /* head distance band (dmax <= N - window - 16) */
    unsigned jmin,jmax;   /* jump range in samples (multiples of 8) */
    float good;           /* correlation taken as a clean splice */
    unsigned every;       /* coarse candidates: one every `every` samples (power of 2) */
    unsigned phase;       /* sample offset of this splicer's search slots (spreads two voices) */
    unsigned refresh;     /* lowering: re-measure the best jump on every other search slot */
    unsigned fade_min,fade_max;
} FrostPsConfig;
FROST_PS_INLINE void frost_ps_splicer_reset(FrostPsSplicer *s,float d)
{s->d=d;s->d_old=d;s->score=1.0f;s->left=s->right=s->last=0.0f;s->fade=s->fade_len=0;s->state=FROST_PS_IDLE;s->cand=s->best=s->lo=s->hi=0;s->right_pending=0;}
#ifndef FROST_PS_CW
#define FROST_PS_CW 40u   /* coarse: decimated points (even) */
#endif
#ifndef FROST_PS_FW
#define FROST_PS_FW 48u   /* fine: full-rate points */
#endif
#ifndef FROST_PS_FS
#define FROST_PS_FS 4u    /* fine: stride */
#endif
#ifndef FROST_PS_FINE_SPAN
#define FROST_PS_FINE_SPAN 5u   /* fine: coarse jump +/- 5 samples */
#endif
/* Immediate jump to distance d (e.g. to the minimum on a pick attack), with
 * a short power-complementary crossfade; cancels a running search. */
FROST_PS_INLINE void frost_ps_jump(FrostPsSplicer *s,float d,unsigned fade)
{s->d_old=s->d;s->d=d;s->last=0.0f;s->fade=s->fade_len=(uint16_t)fade;s->state=FROST_PS_IDLE;}
#endif
