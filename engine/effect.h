#ifndef FROST_EFFECT_H
#define FROST_EFFECT_H
#include <stdint.h>
#include "../effects/cabinet_ir/ir.h"
#include "../effects/overdrive/overdrive.h"
#include "../effects/delay/delay.h"
#include "../effects/modulation/modulation.h"
#include "../effects/reverb/reverb.h"
#include "../effects/drives/drives.h"
#include "../effects/dynamics/dynamics.h"
#include "../effects/modulators/modulators.h"
#include "../effects/octave/octave.h"
#include "../effects/drop_tune/drop_tune.h"
#include "../effects/delays/delays.h"
#include "../effects/reverbs/reverbs.h"
#include "../effects/fcm/fcm_effect.h"
#include "../effects/reverb/storage.h"
#include "../effects/ns1x/ns1x.h"
#include "../effects/multiband/multiband.h"
#include "../effects/highpass/highpass.h"
#include "../effects/lowpass/lowpass.h"
#include "../effects/autowah/autowah.h"
#include "../effects/treble_boost/treble_boost.h"
#include "../effects/extras/extras.h"
#include "../effects/foxx/foxx.h"
#include "../effects/tape/tape.h"
#include "../effects/vintage_fuzz/vintage_fuzz.h"
#include "../effects/acoustic/acoustic.h"
#include "../effects/amp_heads/amp_heads.h"
#include "metadata.h"
#include "library.h"
#define FROST_BYPASS_FADE 128u
enum { FROST_BYPASS_KEEP_HISTORY, FROST_BYPASS_CUT_AND_SUSPEND };
typedef union {
    struct { float envelope, gain; uint32_t open; } gate;
    struct { float low, high_low; } eq;
    FrostOverdrive overdrive;
    FrostIR *ir; /* caller-owned workspace, unique per IR instance */
    /* 32c: the IR block's LOW/HIGH CUT filter state follows the same
     * pointer (first member). Fits the union; no size or layout change. */
    struct { FrostIR *ir; float cut[FROST_IR_CUT_STATE]; } ir_cut;
    FrostDelay *delay; /* separate caller-owned large workspace */
    FrostModulation *modulation;
    FrostReverbWorkspace *reverb;
    /* Appended pedal-inspired models. Large histories stay in the exclusive
     * class workspaces; only filters/LFOs/envelopes live per slot. */
    FrostDrive drive;
    FrostDynaComp dynacomp;
    FrostCompPlus compplus;
    FrostGE7 ge7;
    FrostNS2 ns2;
    FrostPhase90 phase90;
    FrostUniVibe univibe;
    FrostTR2 tr2;
    FrostModState mod;
    FrostOctave octave;
    FrostDropTune drop;
    FrostDD3 dd3;
    FrostDM2 dm2;
    FrostReverbActiveModel reverb_model;
    FrostFcm fcm;
    FrostNS1X ns1x;
    FrostMultiband multiband;
    FrostHighpass highpass;
    FrostLowpass lowpass;
    FrostAutowah autowah;
    FrostTrebleBoost treble_boost;
    FrostFoxx foxx;
    FrostTape tape;
    FrostVintageFuzz vintage_fuzz;
    FrostAcoustic acoustic;
    FrostAmpHead amphead;
    /* 32l: effects/extras (each within the union's existing size) */
    FrostRotary rotary;
    FrostRingMod ringmod;
    FrostCrusher crusher;
    FrostHarmTrem harmtrem;
    FrostMuff muff;
    FrostBoost boost;
    FrostSwell swell;

} FrostEffectState;
typedef struct {
    const char *name;
    unsigned parameter_count;
    const FrostParameter *parameters; /* exactly parameter_count entries (<= FROST_PARAMETERS) */
    unsigned state_bytes;
    unsigned workspace_bytes;
    void (*reset)(FrostEffectState *state);
    float (*sample)(FrostEffectState *state, float input, const float *parameters);
} FrostEffect;
/* current[FROST_PARAMETERS] is an engine-provided ENGAGE input equal to the
 * slot's enable target, so a kernel (e.g. VB-2 RISE) can react to engagement.
 * It is never a stored/preset parameter. */
typedef struct {
    unsigned type;
    float target[FROST_PARAMETERS], current[FROST_PARAMETERS+1];
    float wet, wet_target;
    FrostEffectState state;
    unsigned bypass_policy, suspended;
} FrostInstance;
extern const FrostEffect frost_effects[FROST_EFFECTS];
#ifdef FROST_OVERLAYS
/* 31i: loaded overlay kernel per type (NULL = not loaded); see firmware frost_ovl_load. */
typedef float (*FrostOvlKernel)(void *state,float input,const float *parameters);
extern FrostOvlKernel frost_ovl_kernel[FROST_EFFECTS];
/* Per-sample entry for a loaded overlay kernel; input passes if not loaded.
 * Defined late in the unit (firmware) so table wrappers stay tiny tail calls. */
float frost_ovl_call(void *state,float x,const float *p,unsigned type);
#endif
const FrostEffect *frost_effect_find(unsigned type);
/* Stage 2b Cabinet Override (see frost_chain_request_ir_bypass). */
extern unsigned frost_ir_bypass;
void frost_instance_init(FrostInstance *s, unsigned type, FrostIR *ir_workspace);
/* Workspace type must match effect; IR compatibility initializer above stays. */
void frost_instance_init_workspace(FrostInstance *s,unsigned type,void *workspace);
void frost_instance_reset_history(FrostInstance *s);
/* The exclusive class workspace an instance uses, or NULL. */
const void *frost_instance_workspace(const FrostInstance *s);
float frost_instance_sample(FrostInstance *s, float input);
/* Signal admitted between stages: finite and within +/-FROST_SIGNAL_LIMIT. */
#define FROST_SIGNAL_LIMIT 16.0f
static inline int frost_signal_valid(float x)
{union {float f;uint32_t u;} v;v.f=x;
 return (v.u&0x7f800000u)!=0x7f800000u && x<=FROST_SIGNAL_LIMIT && x>=-FROST_SIGNAL_LIMIT;}
/* Block form of frost_instance_sample: identical per-sample arithmetic and
 * state sequence, applied in place to x[0..count). Stops after the first
 * sample whose output is not frost_signal_valid and returns its index;
 * returns count when every output is valid. Empty slots pass through. */
unsigned frost_instance_process(FrostInstance *s, float *x, unsigned count);
#endif
