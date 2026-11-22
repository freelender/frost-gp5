#ifndef FROST_DYNAMICS_H
#define FROST_DYNAMICS_H
/* Dyna Comp-, Compressor Plus-, GE-7- and NS-2-inspired models. Per-instance
 * state only (no workspace). Parameters in library human units. */
#define FROST_GE7_BANDS 7u
typedef struct { float env,lp; } FrostDynaComp;
typedef struct { float env,tone,gain,shelf;unsigned n; } FrostCompPlus;
typedef struct { float s[FROST_GE7_BANDS][2],k[FROST_GE7_BANDS+1],db[FROST_GE7_BANDS+1]; } FrostGE7;
typedef struct { float hp,env,gain;unsigned open; } FrostNS2;
void frost_dynacomp_reset(FrostDynaComp *s);
void frost_compplus_reset(FrostCompPlus *s);
void frost_ge7_reset(FrostGE7 *s);
void frost_ns2_reset(FrostNS2 *s);
float frost_dynacomp_sample(FrostDynaComp *s,float x,const float *p);
float frost_compplus_sample(FrostCompPlus *s,float x,const float *p);
float frost_ge7_sample(FrostGE7 *s,float x,const float *p);
float frost_ns2_sample(FrostNS2 *s,float x,const float *p);
/* Band centres for tests/docs (Hz). */
extern const float frost_ge7_centres[FROST_GE7_BANDS];
#endif
