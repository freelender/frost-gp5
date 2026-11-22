#ifndef FROST_HIGHPASS_H
#define FROST_HIGHPASS_H
typedef struct { float z[4],c[5],last[2]; unsigned n; } FrostHighpass;
void frost_highpass_reset(FrostHighpass *s);
float frost_highpass_sample(FrostHighpass *s,float x,const float *p);
#endif
