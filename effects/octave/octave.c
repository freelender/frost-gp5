#include "octave.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
#define UP_START (2.0f+(float)FROST_OCTAVE_UP_GRAIN)
#define DOWN_START 2.0f
static void voice_reset(FrostOctaveVoice *v,float start,float rate,float grain)
{
    /* Tap 1 is half a grain into its life, with the matching distance. */
    v->ph[0]=0.0f;v->ph[1]=0.5f;v->d[0]=start;v->d[1]=start+rate*0.5f*grain;
}
void frost_octave_reset(FrostOctave *s)
{
    voice_reset(&s->up,UP_START,-1.0f,(float)FROST_OCTAVE_UP_GRAIN);
    voice_reset(&s->down,DOWN_START,0.5f,(float)FROST_OCTAVE_DOWN_GRAIN);
    s->sub_lp=s->up_hp=0.0f;if(s->ring){s->ring->position=s->ring->valid=0;}
}
#ifdef FROST_FCM_BENCH
/* EXPERIMENTAL FCM timing candidate (IIR engine): the POG slot runs the
 * Frost Capture Model from effects/fcm/fcm_iir with a baked model
 * (effects/fcm/model_bank.h, generated, private). frost_octave_reset() above
 * is unchanged; it zeroes ring->position/valid, which marks the FCM state for
 * lazy setup. The FCM state (about 2.2 KB) lives in the POG slot's own
 * modulation workspace (line storage). Per-sample entry with 64-sample
 * buffering: +64 samples latency in this benchmark build only.
 * Output = FCM(x) x (UP knob / 50); the other POG knobs are ignored. */
#include "../fcm/fcm_iir.h"
#if defined(__GNUC__) && !defined(__clang__)
#define FROST_FCM_BENCH_OPT __attribute__((optimize("no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_FCM_BENCH_OPT
#endif
#define FCMI_FN FROST_HOT FROST_OVL(pog) FROST_FCM_BENCH_OPT
#include "../fcm/model_bank.h"
#include "../fcm/fcm_iir.c"
typedef struct { FcmiState fcm; float in[FCMI_BLOCK],out[FCMI_BLOCK]; unsigned n; } FcmBenchWs;
typedef char fcm_bench_fits[sizeof(FcmBenchWs)<=sizeof(((FrostModulation *)0)->line)?1:-1];
FROST_HOT FROST_NOINLINE FROST_OVL(pog) FROST_FCM_BENCH_OPT float frost_octave_sample(FrostOctave *s,float x,const float *p)
{
    FrostModulation *r=s->ring;FcmBenchWs *w=(FcmBenchWs *)(void *)&r->line;float y;unsigned k;
    if(!r->valid){fcmi_reset(&w->fcm,&fcmi_model);for(k=0;k<FCMI_BLOCK;++k)w->out[k]=0.0f;w->n=0;r->valid=1u;}
    w->in[w->n]=x;y=w->out[w->n];
    if(++w->n==FCMI_BLOCK){fcmi_process(&w->fcm,w->in,w->out,FCMI_BLOCK);w->n=0;}
    return frost_rail(p[2]*0.02f*y,8.0f);
}
#else
FROST_HOT FROST_OVL(pog) static float sample_at(const FrostModulation *m,unsigned d)
{
    unsigned i;if(d>m->valid || !d || d>=FROST_MOD_PCM)return 0.0f;
    i=m->position>=d?m->position-d:m->position+FROST_MOD_PCM-d;return frost_unpcm16(m->line.pcm[i]);
}
/* 31o: when both reads d and d+1 are valid (the normal case), one validity
 * test and direct ring indexing replace two sample_at calls. The indices are
 * exactly sample_at's (j wraps to the ring end when i is 0), so the result is
 * bit-identical. */
FROST_HOT FROST_NOINLINE FROST_OVL(pog) static float octave_tap(const FrostModulation *m,float distance)
{
    unsigned d=(unsigned)distance,p=m->position;float f=distance-(float)d,a;
    if(d && d+1u<=m->valid && d+1u<FROST_MOD_PCM){
        unsigned i=p>=d?p-d:p+FROST_MOD_PCM-d,j=i?i-1u:FROST_MOD_PCM-1u;
        a=frost_unpcm16(m->line.pcm[i]);
        return a+f*(frost_unpcm16(m->line.pcm[j])-a);
    }
    a=sample_at(m,d);
    return a+f*(sample_at(m,d+1u)-a);
}
/* Choose the restart distance in [base, base+8*63] whose local waveform best
 * matches the other tap's current read position (16 points, stride 4).
 * 31n: the 16 reference values are read once and held as scalars (unrolled,
 * so they stay in FPU registers), pre-scaled by 2^-13. A candidate whose 16
 * reads are all in range and do not wrap (the normal case) reads raw int16
 * ring values directly; any other candidate (ring still filling, or window
 * straddling the ring start) uses a compact sample_at loop rescaled by 2^13.
 * Both scalings are exact powers of two, so every product, the j-order sum,
 * the strict-> first-best choice and the result match the original
 * sample_at(o)*sample_at(d) search bit for bit (tests/test_octave_search.py). */
#define POG16(X) X(0)X(1)X(2)X(3)X(4)X(5)X(6)X(7)X(8)X(9)X(10)X(11)X(12)X(13)X(14)X(15)
#define POG_REF(j) const float a##j=r[j];
#define POG_FAST(j) score+=a##j*(float)q[-4*(j)];
FROST_HOT FROST_NOINLINE FROST_OVL(pog) static float slow_score(const FrostModulation *m,unsigned d,const float *r)
{
    unsigned j;float score=0.0f;
    for(j=0;j<16u;++j)score+=r[j]*(8192.0f*sample_at(m,d+4u*j));
    return score;
}
/* Out of line and overlay-tagged: one copy, never in boot-adjacent .text. */
FROST_HOT FROST_NOINLINE FROST_OVL(pog) static float aligned_start(const FrostModulation *m,float base,float other)
{
    unsigned c,j,o=(unsigned)other,b0=(unsigned)base,best=0,p=m->position,v=m->valid;
    const int16_t *pcm=m->line.pcm;float best_score=-1e30f,r[16];
    for(j=0;j<16u;++j)r[j]=sample_at(m,o+4u*j)*(1.0f/8192.0f);
    POG16(POG_REF)
    for(c=0;c<FROST_OCTAVE_SEARCH;++c){
        unsigned d=b0+8u*c,i=p>=d?p-d:p+FROST_MOD_PCM-d;float score=0.0f;
        if(d && d+60u<=v && d+60u<FROST_MOD_PCM && i>=60u){
            const int16_t *q=pcm+i;
            POG16(POG_FAST)
        }else score=slow_score(m,d,r);
        if(score>best_score){best_score=score;best=c;}
    }
    return base+8.0f*(float)best;
}
#undef POG16
#undef POG_REF
#undef POG_FAST
/* 31o: an inline copy of frost_sin01 (effects/common/frost_dsp.c), the same
 * expression, so the Hann weight needs no far pointer call and wrapper. */
static inline __attribute__((always_inline)) float pog_sin01(float phase)
{
    float t=phase<0.25f?4.0f*phase:phase<0.75f?2.0f-4.0f*phase:4.0f*phase-4.0f,t2=t*t;
    return t*(1.57079633f+t2*(-0.64596409f+t2*(0.07969262f+t2*(-0.00468175f+t2*0.00015957f))));
}
/* 31o: the caller passes 1/grain; both grains are powers of two, so the
 * folded reciprocal equals the former per-sample 1.0f/grain division. */
FROST_HOT FROST_OVL(pog) static float octave_voice(const FrostModulation *m,FrostOctaveVoice *v,float rate,float inv_grain,float start)
{
    float y=0.0f;unsigned k;
    for(k=0;k<2u;++k){
        float w=pog_sin01(0.5f*v->ph[k]);y+=w*w*octave_tap(m,v->d[k]);
        v->d[k]+=rate;v->ph[k]+=inv_grain;
        if(v->ph[k]>=1.0f){v->ph[k]-=1.0f;v->d[k]=aligned_start(m,start,v->d[k^1u]);}
    }
    return y;
}
FROST_HOT FROST_NOINLINE FROST_OVL(pog) float frost_octave_sample(FrostOctave *s,float x,const float *p)
{
    FrostModulation *m=s->ring;float up,sub;
    m->line.pcm[m->position]=frost_pcm16(x);
    if(++m->position==FROST_MOD_PCM)m->position=0;
    if(m->valid<FROST_MOD_PCM)++m->valid;
    /* Up: read distance shrinks one sample per sample (ratio 2). Down: grows
     * half a sample per sample (ratio 0.5). */
    up=octave_voice(m,&s->up,-1.0f,1.0f/(float)FROST_OCTAVE_UP_GRAIN,UP_START);
    sub=octave_voice(m,&s->down,0.5f,1.0f/(float)FROST_OCTAVE_DOWN_GRAIN,DOWN_START);
    sub=frost_lp(&s->sub_lp,sub,frost_onepole(2500.0f));
    up=frost_hp(&s->up_hp,up,frost_onepole(150.0f));
    return frost_rail(p[0]*0.01f*x+p[1]*0.01f*sub+p[2]*0.01f*up,8.0f);
}
#endif /* FROST_FCM_BENCH */
#include "../../engine/overlay_far_off.h"
