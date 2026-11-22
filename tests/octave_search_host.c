/* Differential fixture: the optimized POG aligned_start against the original
 * (pre-31n) search, verbatim apart from its name. Host arithmetic only; the
 * ARM build is checked separately by emulator output identity. */
#include "../effects/octave/octave.c"
#include "../effects/common/frost_dsp.c"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static float reference_start(const FrostModulation *m,float base,float other)
{
    unsigned c,j,o=(unsigned)other,best=0;float best_score=-1e30f;
    for(c=0;c<FROST_OCTAVE_SEARCH;++c){
        unsigned d=(unsigned)base+8u*c;float score=0.0f;
        for(j=0;j<16u;++j)score+=sample_at(m,o+4u*j)*sample_at(m,d+4u*j);
        if(score>best_score){best_score=score;best=c;}
    }
    return base+8.0f*(float)best;
}
static FrostModulation ring;
API void os_set(unsigned position,unsigned valid,const int16_t *pcm)
{unsigned i;for(i=0;i<FROST_MOD_PCM;++i)ring.line.pcm[i]=pcm[i];ring.position=position;ring.valid=valid;}
/* Returns the number of (base, other) pairs whose bit patterns differ. */
API unsigned os_compare(const float *bases,const float *others,unsigned n,float *fast,float *ref)
{
    unsigned i,bad=0;
    for(i=0;i<n;++i){
        union {float f;unsigned u;} a,b;
        a.f=aligned_start(&ring,bases[i],others[i]);b.f=reference_start(&ring,bases[i],others[i]);
        fast[i]=a.f;ref[i]=b.f;bad+=a.u!=b.u;
    }
    return bad;
}
/* Whole-effect parity: run n samples through the real kernel and record state. */
static FrostModulation effect_ring;
static FrostOctave effect;
API void os_effect_reset(void){effect.ring=&effect_ring;frost_octave_reset(&effect);}
API void os_effect_run(const float *x,unsigned n,const float *p,float *y)
{unsigned i;for(i=0;i<n;++i)y[i]=frost_octave_sample(&effect,x[i],p);}
