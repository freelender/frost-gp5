#ifndef FROST_FCM_IIR_H
#define FROST_FCM_IIR_H
/* EXPERIMENTAL FCM with IIR filters: pre/mid/post are cascades of biquads
 * (transposed direct form II) fitted to the trained FIR responses
 * (Steiglitz-McBride, build/nam-distill/iirfit.py). The saturation stages are
 * the 2x/20-tap oversampled stage of fcm_fast. Blocks of any length up to
 * FCM_BLOCK; no added latency. A converted model is an APPROXIMATION of its
 * source NAM. */
#include <stdint.h>
#define FCMI_BLOCK 64u
#define FCMI_MAX_SOS 4u
#define FCMI_STAGES 2u
#define FCMI_AA 20u
typedef struct { float b0,b1,b2,a1,a2; } FcmiBiquad;        /* y = (b0+b1 z^-1+b2 z^-2)/(1+a1 z^-1+a2 z^-2) */
typedef struct {
    unsigned n_pre,n_mid,n_post;
    FcmiBiquad pre[FCMI_MAX_SOS],mid[FCMI_MAX_SOS],post[FCMI_MAX_SOS];
    float gain,Ap[FCMI_STAGES],An[FCMI_STAGES],Kp[FCMI_STAGES],Kn[FCMI_STAGES];
    float env_a,env_k[FCMI_STAGES];
    float aa[FCMI_AA];                                       /* symmetric 2x anti-alias FIR */
} FcmiModel;
typedef struct {
    const FcmiModel *m;
    float zpre[FCMI_MAX_SOS][2],zmid[FCMI_MAX_SOS][2],zpost[FCMI_MAX_SOS][2];
    float uh[FCMI_STAGES][FCMI_AA/2u-1u+FCMI_BLOCK];        /* upsampler history (input rate) */
    float vh[FCMI_STAGES][FCMI_AA-1u+2u*FCMI_BLOCK];        /* decimator history (2x rate) */
    float env;
} FcmiState;
void fcmi_reset(FcmiState *s,const FcmiModel *m);
void fcmi_process(FcmiState *s,const float *x,float *y,unsigned n);
#endif
