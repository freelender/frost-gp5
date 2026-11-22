#ifndef FROST_LOWPASS_H
#define FROST_LOWPASS_H
typedef struct { float z[4],c[5],last[2]; unsigned n; } FrostLowpass;
void frost_lowpass_reset(FrostLowpass *s);
float frost_lowpass_sample(FrostLowpass *s,float x,const float *p);
#endif
