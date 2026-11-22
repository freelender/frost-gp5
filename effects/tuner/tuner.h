#ifndef FROST_TUNER_H
#define FROST_TUNER_H
/* Stage 2c tuner (docs/User%20Guide.md): two-pass YIN pitch detection on the input.
 * Capture: one-pole low-pass, decimated to 22.05 kHz, 1846 samples (84 ms),
 * double buffered (one buffer fills while the other is analysed).
 * Pass 1 (coarse): the window averaged 4:1 to 5.51 kHz, YIN cumulative-mean-
 * normalized difference over lags 4..205 (1378 Hz down to 26.9 Hz), window 256:
 * picks the period without octave errors. Pass 2 (fine): the raw difference at
 * 22.05 kHz (window 1024) for the 9 lags around 4x the coarse period, minimum
 * plus parabolic interpolation. Work is bounded per audio block (a few
 * thousand multiply-adds), about 12 readings a second. Memory is BORROWED from
 * a caller buffer (the delay workspace: the tuner mutes the output; the delay
 * history is reset on exit). No allocation. */
#include <stdint.h>
#define FROST_TUNER_RATE 22050.0f
#define FROST_TUNER_W 1024u                 /* fine window, 22.05 kHz */
#define FROST_TUNER_N 1846u                 /* captured samples per reading */
#define FROST_TUNER_CW 256u                 /* coarse window, 5.51 kHz */
#define FROST_TUNER_CN (FROST_TUNER_N/4u)   /* 461 coarse samples */
#define FROST_TUNER_CTAU_MIN 4u
#define FROST_TUNER_CTAU_MAX 205u
#define FROST_TUNER_FINE 4                  /* +-4 lags around 4x the coarse period */
#define FROST_TUNER_COARSE_PER_BLOCK 20u
#define FROST_TUNER_FINE_PER_BLOCK 5u
#define FROST_TUNER_BYTES ((2u*FROST_TUNER_N+FROST_TUNER_CN+FROST_TUNER_CTAU_MAX+2u+2u*FROST_TUNER_FINE+1u)*4u)
typedef struct {
    float *buf[2],*c,*d,*fd;          /* borrowed: capture buffers, coarse samples, coarse d, fine d */
    unsigned active,fill,cap,ana,step,phase,odd,fine0;
    float lp,pair,level,coarse;
    float freq;                       /* last estimate, Hz (0 = none) */
    float clarity;                    /* 1 - normalized difference at the coarse lag (0..1) */
    uint32_t seq;                     /* increments with every finished analysis */
} FrostTuner;
/* 1 = started (mem large enough). */
int frost_tuner_start(FrostTuner *t,void *mem,unsigned bytes);
void frost_tuner_stop(FrostTuner *t);
/* One audio block of input: capture + a bounded slice of analysis. */
void frost_tuner_block(FrostTuner *t,const float *x,unsigned n);
#endif
