#ifndef FROST_REVERB_VOICING_H
#define FROST_REVERB_VOICING_H
#include "../common/frost_dsp.h"
/* The compact target retains unreachable float kernels for boot layout.
 * Keep their generated instructions unchanged; host/float builds use the
 * same voicing as the active compact backend. No workspace layout changes. */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(FROST_REVERB_INT16)
#define FROST_REVERB_VOICING 0
#else
#define FROST_REVERB_VOICING 1
#endif
#define FROST_RV6_STATE_COUNT (FROST_REVERB_VOICING ? 22u : 16u)
#if defined(__GNUC__)
#define FROST_VOICE_INLINE static inline __attribute__((always_inline))
#else
#define FROST_VOICE_INLINE static inline
#endif
/* Wet excitation only: two 180 Hz poles. Dry audio never enters this filter. */
FROST_VOICE_INLINE float frost_reverb_send(float *z,float x)
{return frost_hp(z+1,frost_hp(z,x,frost_onepole(180.0f)),frost_onepole(180.0f));}
/* Feedback low shelf below 300 Hz. amount=1-g gives DC loop gain g*g:
 * about half the main decay time, with unity shelf gain at high frequency. */
FROST_VOICE_INLINE float frost_reverb_bass(float *z,float x,float amount)
{return x-amount*frost_lp(z,x,frost_onepole(300.0f));}
#endif
