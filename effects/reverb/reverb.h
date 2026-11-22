#ifndef FROST_REVERB_H
#define FROST_REVERB_H
#define FROST_REVERB_LINES 6u
#define FROST_REVERB_COMBS 4u
#define FROST_REVERB_SAMPLES 5234u
/* Fixed 44.1 kHz mono network. Caller-owned storage, never bulk-cleared in
 * processing. First four lines are damped combs, last two are allpasses. */
/* Mutable network state for the appended RV-6/spring-style models. It lives
 * with the exclusive workspace (one reverb-class instance at a time) instead
 * of in every slot. Generic reverb uses z[0..5] for wet-send and bass damping. */
#define FROST_REVERB_MODEL_LINES 12u
typedef struct {
    unsigned position[FROST_REVERB_MODEL_LINES],valid[FROST_REVERB_MODEL_LINES],mode,index;
    float z[32];
} FrostReverbModelState;
typedef struct {
    float samples[FROST_REVERB_SAMPLES];
    unsigned position[FROST_REVERB_LINES],valid[FROST_REVERB_LINES];
    float damped[FROST_REVERB_COMBS];
    FrostReverbModelState model;
} FrostReverb;
extern const unsigned frost_reverb_lengths[FROST_REVERB_LINES];
void frost_reverb_reset(FrostReverb *s);
/* Valid smoothed parameters: normalized decay[0,.95], damp[0,1], mix[0,1]. */
float frost_reverb_sample(FrostReverb *s,float input,const float *parameters);
/* Block form: out[k] equals frost_reverb_sample(s,in[k],p[k]) for k=0..n-1 in
 * order, bit for bit (same per-sample arithmetic and operation order), but each
 * line runs over the whole block: combs in parallel accumulate into wet[], then
 * each allpass in series. n<=FROST_REVERB_CHUNK; in and out may alias. */
/* The 64-sample target experiment saved only about 1.2k cycles while using
 * 1152 extra stack bytes. Retain the smaller qualified scratch size. */
#define FROST_REVERB_CHUNK 16u
void frost_reverb_block(FrostReverb *s,const float *in,float *out,unsigned n,const float (*p)[3]);
#endif
