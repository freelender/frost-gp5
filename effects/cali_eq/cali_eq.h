#ifndef FROST_CALI_EQ_H
#define FROST_CALI_EQ_H
/* CALI 5-BAND EQ: a five-slider graphic EQ in the style of the classic
 * California lead amp's EQ (80, 240, 750, 2200, 6600 Hz, +/-12 dB), plus an
 * output LEVEL (+/-12 dB). Parameters are in dB. State fits FrostEffectState. */
#define FROST_CALI_EQ_BANDS 5u
typedef struct { float s[FROST_CALI_EQ_BANDS][2],k[FROST_CALI_EQ_BANDS+1],db[FROST_CALI_EQ_BANDS+1]; } FrostCaliEq;
extern const float frost_cali_eq_centres[FROST_CALI_EQ_BANDS];
void frost_cali_eq_reset(FrostCaliEq *s);
float frost_cali_eq_sample(FrostCaliEq *s,float x,const float *p);
#endif
