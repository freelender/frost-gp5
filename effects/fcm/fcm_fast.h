#ifndef FROST_FCM_FAST_H
#define FROST_FCM_FAST_H
/* EXPERIMENTAL fast FCM path: FIRs by uniformly partitioned FFT convolution
 * (64-sample partitions, 128-point real FFT, the same algorithm as the
 * cabinet IR's FFT path, no added latency) and a division-free polyphase
 * oversampler. Requires blocks of exactly FCM_BLOCK samples. Numerically a
 * close match to fcm_process() (float rounding differs), not bit-identical. */
#include "fcm.h"
#define FCM_FFT 128u
#define FCM_MAX_PARTS (FCM_MAX_POST/FCM_BLOCK)
typedef struct {
    unsigned parts,head;
    float spec[FCM_MAX_PARTS][FCM_FFT];  /* packed half spectra x 1/128 */
    float fdl[FCM_MAX_PARTS][FCM_FFT];   /* frequency-domain delay line */
    float frame[FCM_FFT];                /* previous 64 inputs, current 64 */
} FcmConv;
typedef struct {
    const FcmModel *m;
    FcmConv pre,mid[FCM_MAX_STAGES],post;
    float twiddle[FCM_FFT],scratch[FCM_FFT];
    float poly[FCM_MAX_AA];              /* aa taps regrouped by phase: [p][j] = aa[p + j*os] */
    float aa[FCM_MAX_AA];                /* RAM copy of the anti-alias taps */
    float uh[FCM_MAX_STAGES][FCM_MAX_AA/2u+FCM_BLOCK];
    float vh[FCM_MAX_STAGES][FCM_MAX_AA+4u*FCM_BLOCK];
    float env;
} FcmFast;
void fcm_fast_reset(FcmFast *s,const FcmModel *m);
void fcm_fast_process(FcmFast *s,const float *x,float *y); /* FCM_BLOCK samples */
#endif
