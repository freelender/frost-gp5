/* Per-sample pitch-shifting functions (see pitch_shift.h), instantiated once
 * per including effect. Before including, define:
 *   FROST_PS_PREFIX  name prefix, e.g. drop -> drop_ps_input, drop_ps_splice
 *   FROST_PS_FN      storage/placement, e.g. FROST_HOT FROST_NOINLINE FROST_OVL(drop) static
 *   FROST_PS_COARSE_W, FROST_PS_FINE_W/_S (optional) search window points and stride
 *   FROST_PS_LINEAR  (optional) 1: linear instead of Hermite interpolation
 *   FROST_PS_LOWER_ONLY (optional) 1 when every ratio is < 1: the raising
 *                    paths compile out
 *   FROST_PS_SMALL   (optional) extra attribute for the control-heavy functions,
 *                    e.g. FROST_COLD (size-optimized: bounds the overlay module)
 * Deliberately no include guard: every module gets its own copy in its own
 * overlay section. Integer and float arithmetic only, no library calls. */
#include "pitch_shift.h"
#if !defined(FROST_PS_PREFIX) || !defined(FROST_PS_FN)
#error define FROST_PS_PREFIX and FROST_PS_FN before including pitch_shift_impl.h
#endif
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#endif
#ifndef FROST_PS_SMALL
#define FROST_PS_SMALL
#endif
#ifndef FROST_PS_COARSE_W
#define FROST_PS_COARSE_W FROST_PS_CW   /* coarse window: decimated points (even) */
#endif
#ifndef FROST_PS_FINE_W
#define FROST_PS_FINE_W FROST_PS_FW   /* fine window: points */
#endif
#ifndef FROST_PS_FINE_S
#define FROST_PS_FINE_S FROST_PS_FS   /* fine window: stride (1 = contiguous, SMLALD on the M4) */
#endif
#define FROST_PS_CAT2(a,b) a##b
#define FROST_PS_CAT(a,b) FROST_PS_CAT2(a,b)
#define FROST_PSF(x) FROST_PS_CAT(FROST_PS_PREFIX,x)

/* Read at fractional distance d (2 <= d <= N-3) behind the newest sample
 * (pos = next write): 4-point Hermite, or linear with FROST_PS_LINEAR (for
 * voices that are low-passed afterwards or read at exactly 2x / 0.5x). */
FROST_PS_FN float FROST_PSF(_ps_read)(const FrostPsHistory *h,unsigned pos,float d)
{
#if defined(FROST_PS_LINEAR) && FROST_PS_LINEAR
    unsigned n=(unsigned)d,i=(pos-n)&FROST_PS_MASK;float f=d-(float)n,b=(float)h->pcm[i];
    return (b+f*((float)h->pcm[(i-1u)&FROST_PS_MASK]-b))*(1.0f/FROST_PS_SCALE);
#endif
    {
    unsigned n=(unsigned)d,i=(pos-n)&FROST_PS_MASK;float u=1.0f-(d-(float)n);
    float a0=(float)h->pcm[(i-2u)&FROST_PS_MASK],a=(float)h->pcm[(i-1u)&FROST_PS_MASK],b=(float)h->pcm[i],b1=(float)h->pcm[(i+1u)&FROST_PS_MASK];
    float c1=0.5f*(b-a0),c2=a0-2.5f*a+2.0f*b-0.5f*b1,c3=0.5f*(b1-a0)+1.5f*(a-b);
    return (((c3*u+c2)*u+c1)*u+a)*(1.0f/FROST_PS_SCALE);
    }
}
/* Normalized correlation of n points (step s) of two windows of a ring
 * (mask m) ending at indices ia and ib (inclusive, going back in time).
 * Windows that do not wrap are read directly; on the Cortex-M4 contiguous
 * ones (s = 1, n even) use dual 16-bit multiply-accumulates (SMLALD). */
FROST_PS_FN float FROST_PSF(_ps_ncc)(const int16_t *r,unsigned m,unsigned ia,unsigned ib,unsigned n,unsigned s)
{
    int64_t num=0,ea=0,eb=0;unsigned k,span=(n-1u)*s;
    ia&=m;ib&=m;
    if(ia>=span && ib>=span){
        const int16_t *pa=r+ia-span,*pb=r+ib-span;
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__ARM_FEATURE_DSP)
        if(s==1u && !(n&1u)){
            for(k=0;k<n;k+=2u){
                int32_t wa,wb;__builtin_memcpy(&wa,pa+k,4);__builtin_memcpy(&wb,pb+k,4);
                num=(int64_t)__smlald(wa,wb,num);ea=(int64_t)__smlald(wa,wa,ea);eb=(int64_t)__smlald(wb,wb,eb);
            }
        }else
#endif
        for(k=0;k<=span;k+=s){int32_t a=pa[k],b=pb[k];num+=(int64_t)(a*b);ea+=(int64_t)(a*a);eb+=(int64_t)(b*b);}
    }else for(k=0;k<n;++k){int32_t a=r[ia&m],b=r[ib&m];num+=(int64_t)(a*b);ea+=(int64_t)(a*a);eb+=(int64_t)(b*b);ia-=s;ib-=s;}
    if(ea<=0 || eb<=0)return 0.0f;
    return frost_ps_l2f(num)*frost_ps_rsqrt(frost_ps_l2f(ea)*frost_ps_l2f(eb));
}
/* Writes x (optionally anti-aliased: 2nd-order Butterworth ~10 kHz) and the
 * analysis copy (two one-poles ~1.6 kHz, then the mean of 8). Returns 1 on a
 * pick attack (energy above ~1.6 kHz: 2 ms average > 8 x the 45 ms average,
 * above -57 dBFS), at most once per hold samples. */
FROST_PS_FN unsigned FROST_PSF(_ps_input)(FrostPsInput *s,FrostPsHistory *h,float x,unsigned antialias,unsigned hold)
{
    float v=x,a;unsigned onset=0;
    if(antialias){
        /* Direct form II transposed, fc = 10 kHz, Q = 0.7071, fs = 44.1 kHz. */
        const float b0=0.31261277f,b1=0.62522554f,a1=-0.03440978f,a2=0.28486087f;
        float y=b0*x+s->aa[0];s->aa[0]=b1*x-a1*y+s->aa[1];s->aa[1]=b0*x-a2*y;v=y;
    }
    h->pcm[h->pos]=frost_ps_q(v);
    s->lp[0]+=0.204f*(v-s->lp[0]);s->lp[1]+=0.204f*(s->lp[0]-s->lp[1]);h->acc+=s->lp[1];
    if((h->pos&(FROST_PS_DEC-1u))==FROST_PS_DEC-1u){h->dec[h->pos/FROST_PS_DEC]=frost_ps_q(h->acc*(1.0f/(float)FROST_PS_DEC));h->acc=0.0f;}
    h->pos=(h->pos+1u)&FROST_PS_MASK;
    /* Pick attacks are broadband: track the energy above ~1.6 kHz (the input
     * minus the analysis low-pass), 2 ms against 45 ms. Sustained low notes
     * and their waveform ripple do not trigger it. */
    a=v-s->lp[1];a*=a;
    s->fast+=0.0113f*(a-s->fast);s->slow+=0.0005f*(a-s->slow);
    if(h->hold)--h->hold;
    else if(s->fast>8.0f*s->slow+2e-6f){onset=1u;h->hold=hold;}
    return onset;
}
/* Coarse score of jump j*8 (dec units) at head distance d: FROST_PS_CW
 * decimated points ending up to 160 samples ahead of the heads. Lowering:
 * the new head is j*8 newer (smaller distance); raising: older. */
/* fine: 0 = coarse window, 1 = fine window, 2 = long coarse window (768
 * samples, clipped to the history) used once to confirm a match. */
FROST_PS_FN FROST_PS_SMALL float FROST_PSF(_ps_score)(const FrostPsHistory *h,float d,unsigned J,int up,unsigned fine)
{
    unsigned near=up?(unsigned)d:(unsigned)d-J,lim=fine==1u?128u:160u,ahead=near>lim+16u?lim:near>16u?near-16u:0u;
    unsigned a=(unsigned)d-ahead,b=up?a+J:a-J,w=FROST_PS_COARSE_W;
    if(fine==1u){unsigned newest=h->pos-1u;return FROST_PSF(_ps_ncc)(h->pcm,FROST_PS_MASK,newest-a,newest-b,FROST_PS_FINE_W,FROST_PS_FINE_S);}
    if(fine==2u){unsigned old=up?b:a,room=old<FROST_PS_N-24u?(FROST_PS_N-24u-old)/8u:0u;w=room<96u?room&~1u:96u;if(w<FROST_PS_COARSE_W)w=FROST_PS_COARSE_W;}
    {unsigned newest=h->pos/FROST_PS_DEC-1u;return FROST_PSF(_ps_ncc)(h->dec,FROST_PS_DMASK,newest-a/8u,newest-b/8u,w,1u);}
}
FROST_PS_INLINE void FROST_PSF(_ps_to_fine)(FrostPsSplicer *s,const FrostPsConfig *c)
{
    unsigned J=8u*s->best;
    s->lo=(uint16_t)(J>c->jmin+FROST_PS_FINE_SPAN?J-FROST_PS_FINE_SPAN:c->jmin);s->hi=(uint16_t)(J+FROST_PS_FINE_SPAN);
    s->cand=s->lo;s->best=s->lo;s->score=-2.0f;s->right_pending=0;s->state=FROST_PS_FINE;
}
/* A new coarse scan starts at the minimum jump, or (raising, tracking) at
 * half the previous jump: each raising grain is only one jump long, too short
 * to rescan a low note's period from the minimum, while starting at half lets
 * a jump of several periods come back down to one. best = 0 (reset, pick
 * attack) restarts from the minimum. */
FROST_PS_INLINE void FROST_PSF(_ps_to_coarse)(FrostPsSplicer *s,const FrostPsConfig *c,unsigned track)
{
    unsigned start=c->jmin/8u;
    if(track && s->best/16u>start+4u)start=s->best/16u-4u;
    s->state=FROST_PS_COARSE;s->cand=(uint16_t)start;s->best=s->cand;s->score=-2.0f;s->hi=0;
}
/* The search, one step per sample while a splice is being prepared (see
 * _ps_splice); size-optimized: it runs at most once per sample. */
FROST_PS_FN FROST_PS_SMALL void FROST_PSF(_ps_search)(FrostPsSplicer *s,const FrostPsHistory *h,const FrostPsConfig *c,int up)
{
    const unsigned slot=h->pos+c->phase;
    if(s->state==FROST_PS_COARSE){
        float room=up?(float)c->dmax-s->d:s->d-c->dmin;unsigned cap=room<8.0f?0u:(unsigned)room/8u;
        if(cap>c->jmax/8u)cap=c->jmax/8u;
        /* Lowering: the scan waits for candidates to become valid as the head
         * drifts, so it can last thousands of samples, and a chord's
         * correlation drifts meanwhile: every other slot re-measures the best
         * jump so far, so the jump finally taken still matches. */
        if(!(slot&(c->every-1u)) && !up && c->refresh && s->score>-2.0f && (s->hi^=1u))s->score=FROST_PSF(_ps_score)(h,s->d,8u*s->best,up,0u);
        else if(!(slot&(c->every-1u)) && s->cand<=cap){
            float sc=FROST_PSF(_ps_score)(h,s->d,8u*s->cand,up,0u);
            unsigned past=s->score>=c->good && sc<s->score-0.05f;
            if(sc>s->score){s->score=sc;s->best=s->cand;}
            /* Confirm a clear match over a long window before taking it: a
             * short window can match a chord at a lag that is not a common
             * period. A failed match is dropped and the scan goes on. */
            if(past && FROST_PSF(_ps_score)(h,s->d,8u*s->best,up,2u)<c->good-0.1f){past=0;s->score=sc;s->best=s->cand;}
            if(past || ++s->cand>c->jmax/8u)FROST_PSF(_ps_to_fine)(s,c);
        }
        /* forced: the head reached its band limit before a clear match */
        if(s->state==FROST_PS_COARSE && (up?s->d<=c->dmin+(float)(c->fade_max+24u):s->d>=c->dmax))FROST_PSF(_ps_to_fine)(s,c);
        return;
    }
    /* Raising: hold the coarse result until the head is near its limit, so
     * each grain plays as long (and the delay stays as short) as possible. */
    if(up && s->d>c->dmin+(float)(c->fade_max+24u))return;
    /* fine: one candidate every 2nd sample (every 4th with a slow coarse scan) */
    if(slot&(c->every>=4u?3u:1u))return;
    {
        unsigned J=s->cand,ok;float sc,off=0.0f;
        /* Raising: never land closer than one minimum grain above the limit
         * (after a forced or late splice), so the grains cannot collapse. */
        if(up){float need=c->dmin+(float)(c->fade_max+24u+c->jmin)-s->d;if(need>(float)J){s->cand=(uint16_t)(J=(unsigned)need);if(s->hi<J){s->hi=(uint16_t)J;s->lo=(uint16_t)J;}}}
        ok=up?s->d+(float)J<=(float)(FROST_PS_N-FROST_PS_FINE_W*FROST_PS_FINE_S-140u):s->d-(float)J>=2.0f;
        sc=ok?FROST_PSF(_ps_score)(h,s->d,J,up,1u):-2.0f;
        if(sc>s->score){s->left=J>s->lo?s->last:sc;s->score=sc;s->best=(uint16_t)J;s->right_pending=1;}
        else if(s->right_pending){s->right=sc;s->right_pending=0;}
        s->last=sc;
        if(++s->cand>s->hi){
            unsigned F;
            if(!s->right_pending && s->best>s->lo){   /* sub-sample peak: parabola through the neighbours */
                float den=s->left-2.0f*s->score+s->right;
                if(den<-1e-6f){off=0.5f*(s->left-s->right)/den;off=off>0.5f?0.5f:off<-0.5f?-0.5f:off;}
            }
            s->d_old=s->d;s->d=up?s->d+(float)s->best+off:s->d-(float)s->best-off;
            if(s->d<2.0f)s->d=2.0f;
            F=s->best;F=F<c->fade_min?c->fade_min:F>c->fade_max?c->fade_max:F;
            if(up && F+32u>s->best)F=s->best>64u?s->best-32u:32u;   /* raising: the fade ends before the next grain's search */
            s->fade=s->fade_len=(uint16_t)F;s->last=s->score;s->state=FROST_PS_IDLE;
            if(up)FROST_PSF(_ps_to_coarse)(s,c,1u);
        }
    }
}
/* One sample (after _ps_input): read (and crossfade), advance, and run the
 * search; a new search starts only when allow is non-zero. */
FROST_PS_FN float FROST_PSF(_ps_splice)(FrostPsSplicer *s,const FrostPsHistory *h,const FrostPsConfig *c,unsigned allow)
{
#if defined(FROST_PS_LOWER_ONLY) && FROST_PS_LOWER_ONLY
    const int up=0;
#else
    const int up=c->ratio>1.0f;
#endif
    const float inc=1.0f-c->ratio;float y;
    y=FROST_PSF(_ps_read)(h,h->pos,s->d);
    if(s->fade){
        float t=1.0f-(float)s->fade/(float)s->fade_len,b=t*t*(3.0f-2.0f*t),a=1.0f-b,r=s->last<0.0f?0.0f:s->last;
        y=(a*FROST_PSF(_ps_read)(h,h->pos,s->d_old)+b*y)*frost_ps_rsqrt(a*a+b*b+2.0f*r*a*b);
        s->d_old+=inc;if(s->d_old<2.0f)s->d_old=2.0f;if(s->d_old>(float)(FROST_PS_N-4u))s->d_old=(float)(FROST_PS_N-4u);
        --s->fade;
    }
    s->d+=inc;
    if(s->d<2.0f)s->d=2.0f;
    if(s->d>(float)(FROST_PS_N-4u))s->d=(float)(FROST_PS_N-4u);
    /* Lowering: the search starts after the crossfade, once a minimum jump
     * fits. Raising: each grain lasts only J samples, so the search starts
     * at once (also during the crossfade) and its result waits. */
    if(s->fade && (!up || s->state!=FROST_PS_COARSE))return y;
    if(s->state==FROST_PS_IDLE){
        if(allow && (up || s->d>=c->dmin+(float)c->jmin))FROST_PSF(_ps_to_coarse)(s,c,0u);
        return y;
    }
    FROST_PSF(_ps_search)(s,h,c,up);
    return y;
}
#undef FROST_PSF
#undef FROST_PS_SMALL
#undef FROST_PS_FINE_W
#undef FROST_PS_COARSE_W
#undef FROST_PS_FINE_S
#undef FROST_PS_CAT
#undef FROST_PS_CAT2
