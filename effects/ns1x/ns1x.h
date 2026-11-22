#ifndef FROST_NS1X_H
#define FROST_NS1X_H
typedef struct { float split[2],env[3],gain[3],thr,floor,release; unsigned n; } FrostNS1X;
void frost_ns1x_reset(FrostNS1X *s);
float frost_ns1x_sample(FrostNS1X *s,float x,const float *p);
#endif
