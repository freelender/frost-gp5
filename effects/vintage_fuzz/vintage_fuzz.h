#ifndef FROST_VINTAGE_FUZZ_H
#define FROST_VINTAGE_FUZZ_H
typedef struct { float previous,lp,hp,bias,aa[2]; } FrostVintageFuzz;   /* aa: 32d ADAA states */
void frost_vintage_fuzz_reset(FrostVintageFuzz *s);
float frost_vintage_fuzz_sample(FrostVintageFuzz *s,float x,const float *p);
#endif
