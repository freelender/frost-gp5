#ifndef FROST_TREBLE_BOOST_H
#define FROST_TREBLE_BOOST_H
typedef struct { float hp,dc,lp; } FrostTrebleBoost;
void frost_treble_boost_reset(FrostTrebleBoost *s);
float frost_treble_boost_sample(FrostTrebleBoost *s,float x,const float *p);
#endif
