#ifndef FROST_FOXX_H
#define FROST_FOXX_H
typedef struct { float ap[10],phase,feedback,coef; unsigned n; } FrostFoxx;
void frost_foxx_reset(FrostFoxx *s);
float frost_foxx_sample(FrostFoxx *s,float x,const float *p);
#endif
