#ifndef FROST_TONE_H
#define FROST_TONE_H
/* Complementary first-order bands, 44.1 kHz. Gain 1 is neutral.
 * Header-only so capture overlay code never branches outside its module. */
#if defined(__GNUC__)
#define FROST_TONE_INLINE static inline __attribute__((always_inline))
#else
#define FROST_TONE_INLINE static inline
#endif
FROST_TONE_INLINE float frost_tone_sample(float x,const float *gain,float *z)
{
    z[0]+=0.034994f*(x-z[0]);z[1]+=0.29966f*(x-z[1]);
    if(gain[0]==1.0f && gain[1]==1.0f && gain[2]==1.0f)return x;
    return x+z[0]*(gain[0]-1.0f)+(z[1]-z[0])*(gain[1]-1.0f)+(x-z[1])*(gain[2]-1.0f);
}
#endif
