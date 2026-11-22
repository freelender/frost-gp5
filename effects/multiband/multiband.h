#ifndef FROST_MULTIBAND_H
#define FROST_MULTIBAND_H
typedef struct { float split[2],env[3],gain[3],target[3],attack,release; unsigned n; } FrostMultiband;
void frost_multiband_reset(FrostMultiband *s);
float frost_multiband_sample(FrostMultiband *s,float x,const float *p);
#endif
