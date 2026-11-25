#include "cali_eq.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* Five-band graphic EQ at the slider frequencies of the classic California
 * lead amp (80, 240, 750, 2200, 6600 Hz). Same Regalia-Mitra sections as
 * EQ-7 (effects/dynamics): H = (1+K)/2 + (1-K)/2 * A(z) with a fixed
 * second-order lattice allpass A, so each band gives exactly K (the slider in
 * dB) at its centre when the others are flat and unity far away. The bands
 * are about 1.6 octaves apart, so each section is 1.6 octaves wide (EQ-7 uses
 * one octave): neighbouring sliders overlap like the original's broad
 * gyrator bands, and the famous "V" (lows and highs up, 750 down) needs no
 * extreme settings. k1 = -cos(w0), k2 = (1-tan(B/2))/(1+tan(B/2)),
 * B = w0*(2^0.8 - 2^-0.8), fs 44.1 kHz. Gains are recomputed only on change. */
const float frost_cali_eq_centres[FROST_CALI_EQ_BANDS]={80.0f,240.0f,750.0f,2200.0f,6600.0f};
static const float cali_eq_k[FROST_CALI_EQ_BANDS][2]={
    {-0.999935043f,0.986788932f},{-0.999415435f,0.960879042f},{-0.994296233f,0.882497131f},
    {-0.951276412f,0.687873287f},{-0.589512890f,0.241355240f}};
void frost_cali_eq_reset(FrostCaliEq *s)
{unsigned i;for(i=0;i<FROST_CALI_EQ_BANDS;++i)s->s[i][0]=s->s[i][1]=0.0f;for(i=0;i<=FROST_CALI_EQ_BANDS;++i){s->db[i]=0.0f;s->k[i]=1.0f;}}
FROST_HOT FROST_NOINLINE FROST_OVL(cali_eq) float frost_cali_eq_sample(FrostCaliEq *s,float x,const float *p)
{
    unsigned i;
    FROST_UNROLL(6)
    for(i=0;i<=FROST_CALI_EQ_BANDS;++i)if(p[i]!=s->db[i]){s->db[i]=p[i];s->k[i]=frost_db_to_gain(p[i]);}
    FROST_UNROLL(5)
    for(i=0;i<FROST_CALI_EQ_BANDS;++i){
        float k1=cali_eq_k[i][0],k2=cali_eq_k[i][1],v,u,a,K=s->k[i];
        v=x-k2*s->s[i][1];u=v-k1*s->s[i][0];
        a=k2*v+s->s[i][1];
        s->s[i][1]=k1*u+s->s[i][0];s->s[i][0]=u;
        x=0.5f*(1.0f+K)*x+0.5f*(1.0f-K)*a;
    }
    return frost_rail(x*s->k[FROST_CALI_EQ_BANDS],12.0f);
}
#include "../../engine/overlay_far_off.h"
