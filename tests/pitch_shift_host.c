/* Host fixture for the pitch-shifting primitives (effects/common/pitch_shift*.h)
 * and the OCTAVE kernel built on them. */
#include "../effects/octave/octave.c"
#include "../effects/common/frost_dsp.c"
#define FROST_PS_PREFIX t
#define FROST_PS_FN static
#include "../effects/common/pitch_shift_impl.h"
#undef FROST_PS_PREFIX
#undef FROST_PS_FN
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static FrostModulation ring;
static FrostPsInput in;
static FrostPsSplicer head;
API unsigned ps_history_bytes(void){return (unsigned)sizeof(FrostPsHistory);}
API unsigned ps_modulation_bytes(void){return (unsigned)sizeof(FrostModulation);}
API unsigned ps_octave_bytes(void){return (unsigned)sizeof(FrostOctave);}
API float ps_rsqrt(float x){return frost_ps_rsqrt(x);}
API float ps_l2f(int64_t x){return frost_ps_l2f(x);}
/* Normalized correlation of two windows of an int16 ring. */
API float ps_ncc(const int16_t *r,unsigned mask,unsigned ia,unsigned ib,unsigned n,unsigned s){return t_ps_ncc(r,mask,ia,ib,n,s);}
/* Writes x into a fresh history and reads it back at fractional distance d. */
API float ps_read_after(const float *x,unsigned n,float d)
{
    FrostPsHistory *h=(FrostPsHistory *)(void *)&ring;unsigned i;
    frost_ps_input_reset(&in,h);for(i=0;i<n;++i)(void)t_ps_input(&in,h,x[i],0u,1100u);
    return t_ps_read(h,h->pos,d);
}
/* One splicer at an arbitrary ratio over x; records the head distance range,
 * the number of splices and the number of pick attacks detected. */
static float dmin_seen,dmax_seen;static unsigned splices,onsets;
API void ps_run(const float *x,float *y,unsigned n,float ratio,float dmin,float dmax,unsigned jmin,unsigned jmax,unsigned every,unsigned fade_max,float start)
{
    FrostPsHistory *h=(FrostPsHistory *)(void *)&ring;FrostPsConfig c;unsigned i;
    c.ratio=ratio;c.dmin=dmin;c.dmax=dmax;c.jmin=jmin;c.jmax=jmax;c.good=0.97f;c.every=every;c.phase=0u;c.refresh=ratio<1.0f;c.fade_min=64u;c.fade_max=fade_max;
    frost_ps_input_reset(&in,h);frost_ps_splicer_reset(&head,start);dmin_seen=1e9f;dmax_seen=0.0f;splices=onsets=0;
    for(i=0;i<n;++i){
        unsigned f=head.fade;
        onsets+=t_ps_input(&in,h,x[i],ratio>1.0f,1100u);
        y[i]=t_ps_splice(&head,h,&c,1u);
        if(!f && head.fade)++splices;
        if(head.d<dmin_seen)dmin_seen=head.d;if(head.d>dmax_seen)dmax_seen=head.d;
    }
}
API float ps_dmin(void){return dmin_seen;}
API float ps_dmax(void){return dmax_seen;}
API unsigned ps_splices(void){return splices;}
API unsigned ps_onsets(void){return onsets;}
/* The OCTAVE kernel. */
static FrostOctave oct;
API void ps_octave(const float *x,float *y,unsigned n,float dry,float sub,float up)
{
    float p[3];unsigned i;p[0]=dry;p[1]=sub;p[2]=up;
    for(i=0;i<sizeof ring;++i)((unsigned char *)&ring)[i]=0xa5;
    oct.ring=&ring;frost_octave_reset(&oct);
    for(i=0;i<n;++i)y[i]=frost_octave_sample(&oct,x[i],p);
}
API float ps_octave_up_distance(void){return oct.up.d;}
API float ps_octave_sub_distance(void){return oct.down.d;}
