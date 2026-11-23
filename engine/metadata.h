#ifndef FROST_METADATA_H
#define FROST_METADATA_H
/* Shared compile-time data only: safe for either processor, no DSP linkage.
 * Float and scaled integer representations are paired in each declaration.
 * Explicit literals preserve MSVC /fp:strict constant initialization. Host
 * contract tests verify that both representations describe the same bounds.
 * Step is an editor gesture size, not quantization of continuous DSP values. */
/* Stable effect IDs. 0..8 are the original generic types and keep their exact
 * behavior/presets. Pedal-inspired models are APPENDED (never reassigned). */
enum { FROST_GAIN, FROST_GATE, FROST_EQ, FROST_IR, FROST_OVERDRIVE, FROST_DELAY, FROST_CHORUS, FROST_FLANGER, FROST_REVERB,
       FROST_TS, FROST_BD2, FROST_KLON, FROST_BLUESBREAKER, FROST_OCD,
       FROST_DS1, FROST_RAT, FROST_DISTPLUS,
       FROST_DD3, FROST_DM2,
       FROST_RV6, FROST_SPRING,
       FROST_PHASE90, FROST_CE2, FROST_MISTRESS, FROST_M117, FROST_UNIVIBE, FROST_VB2, FROST_TR2, FROST_POG,
       FROST_DYNACOMP, FROST_COMPPLUS, FROST_GE7, FROST_NS2,
       FROST_FCM,   /* EXPERIMENTAL AMP: Frost Capture Model (converted NAM) */
       FROST_DROP,  /* stage 5: drop tune (effects/drop_tune) */
       FROST_NS1X,FROST_MULTIBAND,FROST_HIGHPASS,FROST_LOWPASS,FROST_AUTOWAH,FROST_TREBLE_BOOST,FROST_FOXX,FROST_TAPE,FROST_VINTAGE_FUZZ,FROST_ACOUSTIC,
       FROST_DELUXE,FROST_TWIN,FROST_AC30,FROST_JC120,FROST_JCM800,FROST_RECTIFIER,FROST_5150,
       FROST_EFFECTS,
       FROST_EMPTY = 255, FROST_PARAMETERS = 8 };
/* Original layout: nine types with at most three parameters. Version-1 preset
 * words and version-1 slot replies describe exactly this subset. */
#define FROST_LEGACY_EFFECTS 9u
#define FROST_LEGACY_PARAMETERS 3u
/* Parameter flags. STEPPED values (modes/switches) bypass smoothing so the
 * kernel never sees an intermediate mode. */
#define FROST_PARAM_STEPPED 1u
#define FROST_SLOT_COUNT 6u
#define FROST_DELAY_WORKSPACES 1u
#ifndef FROST_MOD_WORKSPACES
#ifdef FROST_IR_LINK
/* Stage 5 (31y16): the DSP RAM freed by moving the IR to the controller holds
 * a third modulation workspace, so AMP + CE-2 + DROP (or POG) fit together. */
#define FROST_MOD_WORKSPACES 3u
#else
#define FROST_MOD_WORKSPACES 2u
#endif
#endif
#define FROST_REVERB_WORKSPACES 1u
/* Exclusive large-workspace classes. A model in a class needs one free object
 * of that class; models without a class keep all state in the slot. */
enum { FROST_WORKSPACE_NONE, FROST_WORKSPACE_IR, FROST_WORKSPACE_DELAY, FROST_WORKSPACE_MODULATION, FROST_WORKSPACE_REVERB };
/* 31b, DSP target only: always inlined, so size-optimized (FROST_COLD) callers
 * never emit out-of-line copies into the boot-adjacent flash region. Other
 * builds (host, controller) are unchanged. */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#define FROST_META_INLINE static inline __attribute__((always_inline))
#else
#define FROST_META_INLINE static inline
#endif
FROST_META_INLINE unsigned frost_type_workspace(unsigned type)
{
    switch(type){
    case FROST_IR:return FROST_WORKSPACE_IR;
    case FROST_DELAY:case FROST_DD3:case FROST_DM2:case FROST_TAPE:return FROST_WORKSPACE_DELAY;
    case FROST_CHORUS:case FROST_FLANGER:case FROST_CE2:case FROST_MISTRESS:case FROST_M117:case FROST_VB2:case FROST_POG:case FROST_FCM:case FROST_DROP:
        return FROST_WORKSPACE_MODULATION;
    case FROST_REVERB:case FROST_RV6:case FROST_SPRING:return FROST_WORKSPACE_REVERB;
    default:return FROST_WORKSPACE_NONE;
    }
}
FROST_META_INLINE unsigned frost_type_is_modulation(unsigned type)
{return frost_type_workspace(type)==FROST_WORKSPACE_MODULATION;}
FROST_META_INLINE unsigned frost_type_is_delay(unsigned type)
{return frost_type_workspace(type)==FROST_WORKSPACE_DELAY;}
FROST_META_INLINE unsigned frost_type_is_reverb(unsigned type)
{return frost_type_workspace(type)==FROST_WORKSPACE_REVERB;}
/* Defaults are a preset contract, not the number of registered effect types. */
static inline unsigned frost_default_type(unsigned slot)
{
    static const unsigned types[FROST_SLOT_COUNT]={FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_EMPTY,FROST_EMPTY};
    return slot<FROST_SLOT_COUNT?types[slot]:FROST_EMPTY;
}
typedef struct {
    const char *name;
    float minimum, maximum, initial;
    unsigned flags;
} FrostParameter;
typedef struct {
    const char *name, *unit;
    int minimum, maximum, step, initial, scale;
    const char *const *labels; /* optional names for STEPPED integer values */
} FrostControlParameter;

#define FROST_PARAM_GAIN(X) X("MULTIPLIER","GAIN","X",0,4000,25,1000,1000,0.0f,4.0f,1.0f)
#define FROST_PARAM_GATE(X) X("THRESHOLD","THRESH","",10,100000,100,1000,1000000,0.00001f,0.1f,0.001f)
#define FROST_PARAM_EQ_LOW(X) X("LOW","LOW","X",0,2000,25,1000,1000,0.0f,2.0f,1.0f)
#define FROST_PARAM_EQ_MID(X) X("MID","MID","X",0,2000,25,1000,1000,0.0f,2.0f,1.0f)
#define FROST_PARAM_EQ_HIGH(X) X("HIGH","HIGH","X",0,2000,25,1000,1000,0.0f,2.0f,1.0f)
#define FROST_PARAM_IR_LEVEL(X) X("LEVEL","LEVEL","X",0,2000,25,1600,1000,0.0f,2.0f,1.6f)
/* 32c: cabinet LOW CUT / HIGH CUT in Hz. 0 (and LOW CUT below 20, HIGH CUT
 * below 1000 or at 20000) is off, so older presets, which store 0 or omit
 * these parameters, sound exactly as before. */
#define FROST_PARAM_IR_LOW_CUT(X) X("LOW CUT","LOW CUT","HZ",0,400,10,0,1,0.0f,400.0f,0.0f)
#define FROST_PARAM_IR_HIGH_CUT(X) X("HIGH CUT","HIGH CUT","HZ",1000,20000,250,20000,1,0.0f,20000.0f,20000.0f)
#define FROST_PARAM_OD_DRIVE(X) X("DRIVE","DRIVE","X",1000,20000,250,4000,1000,1.0f,20.0f,4.0f)
#define FROST_PARAM_OD_TONE(X) X("TONE","TONE","",0,1000,25,500,1000,0.0f,1.0f,0.5f)
#define FROST_PARAM_OD_LEVEL(X) X("LEVEL","LEVEL","X",0,2000,25,500,1000,0.0f,2.0f,0.5f)
#define FROST_PARAM_DELAY_TIME(X) X("TIME","TIME","MS",1,500,5,250,1,1.0f,500.0f,250.0f)
#define FROST_PARAM_DELAY_FEEDBACK(X) X("FEEDBACK","FEEDBACK","",0,950,25,350,1000,0.0f,0.95f,0.35f)
#define FROST_PARAM_DELAY_MIX(X) X("MIX","MIX","",0,1000,25,300,1000,0.0f,1.0f,0.3f)
#define FROST_PARAM_CHORUS_RATE(X) X("RATE","RATE","HZ",50,5000,50,800,1000,0.05f,5.0f,0.8f)
#define FROST_PARAM_FLANGER_RATE(X) X("RATE","RATE","HZ",50,5000,50,300,1000,0.05f,5.0f,0.3f)
#define FROST_PARAM_MOD_DEPTH(X) X("DEPTH","DEPTH","",0,1000,25,500,1000,0.0f,1.0f,0.5f)
#define FROST_PARAM_CHORUS_MIX(X) X("MIX","MIX","",0,1000,25,300,1000,0.0f,1.0f,0.3f)
#define FROST_PARAM_FLANGER_REGEN(X) X("FEEDBACK","FEEDBACK","",-900,900,25,500,1000,-0.9f,0.9f,0.5f)
#define FROST_PARAM_REVERB_DECAY(X) X("DECAY","DECAY","",0,950,25,750,1000,0.0f,0.95f,0.75f)
#define FROST_PARAM_REVERB_DAMP(X) X("DAMP","DAMP","",0,1000,25,500,1000,0.0f,1.0f,0.5f)
#define FROST_PARAM_REVERB_MIX(X) X("MIX","MIX","",0,1000,25,250,1000,0.0f,1.0f,0.25f)
/* Diagnostic asset control: not an IR DSP parameter or a cabinet selector. */
#define FROST_DIAGNOSTIC_TAPS(X) X("TAPS","TAPS","",16,512,16,128,1,16.0f,512.0f,128.0f)
#define FROST_PARAM_ENABLE(X) X("ENABLE","ENABLE","",0,1,1,1,1,0.0f,1.0f,1.0f)

#define FROST_GAIN_PARAMETERS(X) FROST_PARAM_GAIN(X)
#define FROST_GATE_PARAMETERS(X) FROST_PARAM_GATE(X)
#define FROST_EQ_PARAMETERS(X) FROST_PARAM_EQ_LOW(X),FROST_PARAM_EQ_MID(X),FROST_PARAM_EQ_HIGH(X)
#define FROST_IR_PARAMETERS(X) FROST_PARAM_IR_LEVEL(X),FROST_PARAM_IR_LOW_CUT(X),FROST_PARAM_IR_HIGH_CUT(X)
#define FROST_OVERDRIVE_PARAMETERS(X) FROST_PARAM_OD_DRIVE(X),FROST_PARAM_OD_TONE(X),FROST_PARAM_OD_LEVEL(X)
#define FROST_DELAY_PARAMETERS(X) FROST_PARAM_DELAY_TIME(X),FROST_PARAM_DELAY_FEEDBACK(X),FROST_PARAM_DELAY_MIX(X)
#define FROST_CHORUS_PARAMETERS(X) FROST_PARAM_CHORUS_RATE(X),FROST_PARAM_MOD_DEPTH(X),FROST_PARAM_CHORUS_MIX(X)
#define FROST_FLANGER_PARAMETERS(X) FROST_PARAM_FLANGER_RATE(X),FROST_PARAM_MOD_DEPTH(X),FROST_PARAM_FLANGER_REGEN(X)
#define FROST_REVERB_PARAMETERS(X) FROST_PARAM_REVERB_DECAY(X),FROST_PARAM_REVERB_DAMP(X),FROST_PARAM_REVERB_MIX(X)
#define FROST_PARAMETER_DSP(name,label,unit,lo,hi,step,initial,scale,dlo,dhi,dinitial) \
    {name,dlo,dhi,dinitial,0u}
#define FROST_PARAMETER_CONTROL(name,label,unit,lo,hi,step,initial,scale,dlo,dhi,dinitial) \
    {label,unit,lo,hi,step,initial,scale,0}
#define FROST_GAIN_NAME "GAIN"
#define FROST_GATE_NAME "GATE"
#define FROST_EQ_NAME "EQ"
#define FROST_IR_NAME "IR"
#define FROST_OVERDRIVE_NAME "DRIVE"
#define FROST_DELAY_NAME "DELAY"
#define FROST_CHORUS_NAME "CHORUS"
#define FROST_FLANGER_NAME "FLANGE"
#define FROST_REVERB_NAME "REVERB"
#define FROST_GAIN_TITLE "INPUT GAIN"
#define FROST_GATE_TITLE "NOISE GATE"
#define FROST_EQ_TITLE "THREE BAND EQ"
#define FROST_IR_TITLE "CABINET IR"
#define FROST_OVERDRIVE_TITLE "OVERDRIVE"
#define FROST_DELAY_TITLE "MONO DELAY"
#define FROST_CHORUS_TITLE "MONO CHORUS"
#define FROST_FLANGER_TITLE "MONO FLANGER"
#define FROST_REVERB_TITLE "MONO REVERB"
#define FROST_GAIN_PARAMETER_COUNT 1u
#define FROST_GATE_PARAMETER_COUNT 1u
#define FROST_EQ_PARAMETER_COUNT 3u
#define FROST_IR_PARAMETER_COUNT 3u
#define FROST_OVERDRIVE_PARAMETER_COUNT 3u
#define FROST_DELAY_PARAMETER_COUNT 3u
#define FROST_CHORUS_PARAMETER_COUNT 3u
#define FROST_FLANGER_PARAMETER_COUNT 3u
#define FROST_REVERB_PARAMETER_COUNT 3u
#endif
