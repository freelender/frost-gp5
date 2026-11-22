#include "eq.h"
#include "../../engine/hot_code.h"
/* Complementary one-pole split at approximately 250 / 2500 Hz, 44.1 kHz. */
FROST_HOT FROST_CORE float frost_eq_sample(float input,const float gains[3],float *low,float *high_low)
{
    *low+=0.034994f*(input-*low);
    *high_low+=0.29966f*(input-*high_low);
    return *low*gains[0]+(*high_low-*low)*gains[1]+(input-*high_low)*gains[2];
}
