#ifndef FROST_FCM_H
#define FROST_FCM_H
/* EXPERIMENTAL Frost Capture Model (FCM): SnapTone-class amp model fitted
 * per NAM file by the offline/web converter (docs/Build%20Firmware.md).
 * A converted model is an APPROXIMATION of its source NAM.
 * Signal: x -> pre FIR -> gain -> [ envelope bias -> asymmetric exponential
 * saturation (oversampled) -> mid FIR ] x stages -> post FIR.
 * Saturation: y = Ap*(1-exp(-Kp*u)) for u>0, An*(exp(Kn*u)-1) otherwise.
 * Block processing, caller-owned state, no heap. */
#include <stdint.h>
#define FCM_BLOCK 64u
#define FCM_MAX_STAGES 2u
#define FCM_MAX_PRE 128u
#define FCM_MAX_MID 32u
#define FCM_MAX_POST 512u
#define FCM_MAX_AA 40u
#ifndef FCM_EXP_EXACT
#define FCM_EXP_EXACT 0
#endif
/* 0: (1+z/1024)^1024 by ten squarings; 1: libm expf (host checks) */
typedef struct {
    unsigned stages,n_pre,n_mid,n_post,os,n_aa;
    const float *pre,*post,*aa;              /* FIR taps, h[0] applies to the newest sample */
    const float *mid[FCM_MAX_STAGES];        /* stages-1 mid FIRs */
    float gain,Ap[FCM_MAX_STAGES],An[FCM_MAX_STAGES],Kp[FCM_MAX_STAGES],Kn[FCM_MAX_STAGES];
    float env_a,env_k[FCM_MAX_STAGES];       /* env_a = 0 disables the envelope */
} FcmModel;
typedef struct {
    const FcmModel *m;
    float xh[FCM_MAX_PRE+FCM_BLOCK];                       /* pre FIR history */
    float uh[FCM_MAX_STAGES][FCM_MAX_AA/2u+FCM_BLOCK];     /* upsampler history (input rate) */
    float vh[FCM_MAX_STAGES][FCM_MAX_AA+4u*FCM_BLOCK];     /* decimator history (oversampled rate) */
    float mh[FCM_MAX_STAGES][FCM_MAX_MID+FCM_BLOCK];       /* mid FIR history */
    float ph[FCM_MAX_POST+FCM_BLOCK];                      /* post FIR history */
    float env;
} FcmState;
void fcm_reset(FcmState *s,const FcmModel *m);
void fcm_process(FcmState *s,const float *x,float *y,unsigned n);
#endif
