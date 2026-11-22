#ifndef FROST_AUTOWAH_H
#define FROST_AUTOWAH_H
typedef struct { float env,z[4],phase,g[2],damping; unsigned n; } FrostAutowah;
void frost_autowah_reset(FrostAutowah *s);
float frost_autowah_sample(FrostAutowah *s,float x,const float *p);
#endif
