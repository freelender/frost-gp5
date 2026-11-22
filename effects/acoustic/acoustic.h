#ifndef FROST_ACOUSTIC_H
#define FROST_ACOUSTIC_H
/* Local histories only: no cabinet/delay workspace or shared instance state. */
typedef struct {
    float z[8],hp,top,air,excite_lp,excite_hp,excite_out,fast,slow;
} FrostAcoustic;
void frost_acoustic_reset(FrostAcoustic *s);
float frost_acoustic_sample(FrostAcoustic *s,float x,const float *p);
#endif
