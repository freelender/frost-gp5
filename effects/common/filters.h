#ifndef FROST_FILTER_HELPERS_H
#define FROST_FILTER_HELPERS_H
#include "frost_dsp.h"
/* Inlined into each relocated module; no pointers to another module. */
#define FROST_FINLINE static inline __attribute__((always_inline))
FROST_FINLINE float frost_time_ms(float ms){return 1.0f/(1.0f+0.001f*FROST_FS*ms);}
FROST_FINLINE void frost_split3(float *z,float x,float *b)
{b[0]=frost_lp(z,x,frost_onepole(250.0f));x-=b[0];b[1]=frost_lp(z+1,x,frost_onepole(2500.0f));b[2]=x-b[1];}
/* Stable topology-preserving state variable filter, fc <= 8 kHz. */
FROST_FINLINE float frost_svf_g(float hz)
{float w=FROST_PI*hz/FROST_FS,w2=w*w;return w*(1.0f+w2*(0.33333333f+w2*(0.13333333f+w2*0.05396825f)));}
FROST_FINLINE float frost_svf(float *z,float x,float g,float damping)
{float v1=(z[0]+g*(x-z[1]))/(1.0f+g*(g+damping)),v2=z[1]+g*v1;z[0]=2.0f*v1-z[0];z[1]=2.0f*v2-z[1];return v1;}
FROST_FINLINE float frost_biquad(float *z,float x,const float *c)
{float y=c[0]*x+z[0];z[0]=c[1]*x-c[3]*y+z[1];z[1]=c[2]*x-c[4]*y;return y;}
#endif
