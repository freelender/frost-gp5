#include "effect.h"
#include "hot_code.h"
#include "../effects/gain/gain.h"
#include "../effects/noise_gate/noise_gate.h"
#include "../effects/eq/eq.h"
#include "../effects/chorus/chorus.h"
#include "../effects/flanger/flanger.h"

static void gain_reset(FrostEffectState *s) { (void)s; }
static void gate_reset(FrostEffectState *s)
{ s->gate.envelope=s->gate.gain=0.0f;s->gate.open=0; }
static void eq_reset(FrostEffectState *s)
{ s->eq.low=s->eq.high_low=0.0f; }
static void ir_reset(FrostEffectState *s)
{ unsigned i;for(i=0;i<s->ir->taps;++i)s->ir->history[i]=0.0f; }
FROST_HOT static float gain_sample(FrostEffectState *s,float x,const float *p)
{ (void)s;return frost_gain_sample(x,p[0]); }
FROST_HOT static float gate_sample(FrostEffectState *s,float x,const float *p)
{ return frost_gate_sample(x,p[0],&s->gate.envelope,&s->gate.gain,&s->gate.open); }
FROST_HOT static float eq_sample(FrostEffectState *s,float x,const float *p)
{ return frost_eq_sample(x,p,&s->eq.low,&s->eq.high_low); }
FROST_HOT FROST_COLD static float ir_sample(FrostEffectState *s,float x,const float *p)
{ return frost_ir_sample(s->ir,x)*p[0]; }
static void overdrive_reset(FrostEffectState *s)
{ frost_overdrive_reset(&s->overdrive); }
FROST_HOT FROST_LARGE_ONLY static float overdrive_sample(FrostEffectState *s,float x,const float *p)
{ return frost_overdrive_sample(&s->overdrive,x,p); }
static void delay_reset(FrostEffectState *s){frost_delay_reset(s->delay);}
FROST_HOT FROST_LARGE_ONLY static float delay_sample(FrostEffectState *s,float x,const float *p)
{return frost_delay_sample(s->delay,x,p);}
static void modulation_reset(FrostEffectState *s){frost_modulation_reset(s->modulation);}
FROST_HOT FROST_LARGE_ONLY static float chorus_sample(FrostEffectState *s,float x,const float *p)
{return frost_chorus_sample(s->modulation,x,p);}
FROST_HOT FROST_LARGE_ONLY static float flanger_sample(FrostEffectState *s,float x,const float *p)
{return frost_flanger_sample(s->modulation,x,p);}
static void reverb_reset(FrostEffectState *s){frost_active_reverb_reset(s->reverb);}
FROST_HOT static float reverb_sample(FrostEffectState *s,float x,const float *p)
{return frost_active_reverb_sample(s->reverb,x,p);}

/* ---- appended pedal-inspired models ---- */
#define FROST_WRAP(name,member,kernel,resetfn) static void name##_reset(FrostEffectState *s){resetfn(&s->member);} FROST_HOT static float name##_sample(FrostEffectState *s,float x,const float *p){return kernel(&s->member,x,p);}
#ifdef FROST_OVERLAYS
/* 31i: overlay kernels are reached through the loaded-copy table; a type whose
 * module is not loaded passes its input (the route admission sizes the pool so
 * every chain fits; see frost_ovl_load). */
FrostOvlKernel frost_ovl_kernel[FROST_EFFECTS];
#define FROST_WRAP_OVL(name,member,kernel,resetfn,type) static void name##_reset(FrostEffectState *s){resetfn(&s->member);} FROST_HOT FROST_LARGE_ONLY static float name##_sample(FrostEffectState *s,float x,const float *p){return frost_ovl_call(&s->member,x,p,type);}
#else
#define FROST_WRAP_OVL(name,member,kernel,resetfn,type) FROST_WRAP(name,member,kernel,resetfn)
#endif
FROST_WRAP_OVL(ts,drive,frost_ts_sample,frost_drive_reset,FROST_TS)
FROST_WRAP_OVL(bd2,drive,frost_bd2_sample,frost_drive_reset,FROST_BD2)
FROST_WRAP_OVL(klon,drive,frost_klon_sample,frost_drive_reset,FROST_KLON)
FROST_WRAP_OVL(bluesbreaker,drive,frost_bluesbreaker_sample,frost_drive_reset,FROST_BLUESBREAKER)
FROST_WRAP_OVL(ocd,drive,frost_ocd_sample,frost_drive_reset,FROST_OCD)
FROST_WRAP(ds1,drive,frost_ds1_sample,frost_drive_reset)
FROST_WRAP_OVL(rat,drive,frost_rat_sample,frost_drive_reset,FROST_RAT)
FROST_WRAP_OVL(distplus,drive,frost_distplus_sample,frost_drive_reset,FROST_DISTPLUS)
FROST_WRAP_OVL(dd3,dd3,frost_dd3_sample,frost_dd3_reset,FROST_DD3)
FROST_WRAP_OVL(dm2,dm2,frost_dm2_sample,frost_dm2_reset,FROST_DM2)
FROST_WRAP(rv6,reverb_model,frost_active_rv6_sample,frost_active_rv6_reset)
FROST_WRAP(spring,reverb_model,frost_active_spring_sample,frost_active_spring_reset)
FROST_WRAP_OVL(phase90,phase90,frost_phase90_sample,frost_phase90_reset,FROST_PHASE90)
FROST_WRAP(ce2,mod,frost_ce2_sample,frost_modstate_reset)
FROST_WRAP_OVL(mistress,mod,frost_mistress_sample,frost_modstate_reset,FROST_MISTRESS)
FROST_WRAP_OVL(m117,mod,frost_m117_sample,frost_modstate_reset,FROST_M117)
FROST_WRAP_OVL(univibe,univibe,frost_univibe_sample,frost_univibe_reset,FROST_UNIVIBE)
FROST_WRAP_OVL(vb2,mod,frost_vb2_sample,frost_modstate_reset,FROST_VB2)
FROST_WRAP_OVL(tr2,tr2,frost_tr2_sample,frost_tr2_reset,FROST_TR2)
FROST_WRAP_OVL(pog,octave,frost_octave_sample,frost_octave_reset,FROST_POG)
FROST_WRAP_OVL(drop,drop,frost_drop_sample,frost_drop_reset,FROST_DROP)
FROST_WRAP_OVL(dynacomp,dynacomp,frost_dynacomp_sample,frost_dynacomp_reset,FROST_DYNACOMP)
FROST_WRAP_OVL(compplus,compplus,frost_compplus_sample,frost_compplus_reset,FROST_COMPPLUS)
FROST_WRAP_OVL(ge7,ge7,frost_ge7_sample,frost_ge7_reset,FROST_GE7)
FROST_WRAP_OVL(ns2,ns2,frost_ns2_sample,frost_ns2_reset,FROST_NS2)
/* EXPERIMENTAL AMP (FCM). External wrappers (never static: boot-region rule).
 * Settled blocks use the block entry (no latency) via fcm_settled below. */
void frost_fcm_state_reset(FrostEffectState *s);
void frost_fcm_state_reset(FrostEffectState *s){frost_fcm_reset(&s->fcm);}
FROST_HOT FROST_LARGE_ONLY float frost_fcm_state_sample(FrostEffectState *s,float x,const float *p);
#ifdef FROST_OVERLAYS
FROST_HOT FROST_LARGE_ONLY float frost_fcm_state_sample(FrostEffectState *s,float x,const float *p){return frost_ovl_call(&s->fcm,x,p,FROST_FCM);}
#else
FROST_HOT FROST_LARGE_ONLY float frost_fcm_state_sample(FrostEffectState *s,float x,const float *p){return frost_fcm_sample(&s->fcm,x,p);}
#endif
FROST_XCODE void frost_new_ns1x_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_ns1x_reset(FrostEffectState *s){frost_ns1x_reset(&s->ns1x);}
FROST_LARGE_ONLY float frost_new_ns1x_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_ns1x_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->ns1x,x,p,FROST_NS1X);
#else
return frost_ns1x_sample(&s->ns1x,x,p);
#endif
}
FROST_XCODE void frost_new_multiband_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_multiband_reset(FrostEffectState *s){frost_multiband_reset(&s->multiband);}
FROST_LARGE_ONLY float frost_new_multiband_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_multiband_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->multiband,x,p,FROST_MULTIBAND);
#else
return frost_multiband_sample(&s->multiband,x,p);
#endif
}
FROST_XCODE void frost_new_highpass_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_highpass_reset(FrostEffectState *s){frost_highpass_reset(&s->highpass);}
FROST_LARGE_ONLY float frost_new_highpass_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_highpass_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->highpass,x,p,FROST_HIGHPASS);
#else
return frost_highpass_sample(&s->highpass,x,p);
#endif
}
FROST_XCODE void frost_new_lowpass_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_lowpass_reset(FrostEffectState *s){frost_lowpass_reset(&s->lowpass);}
FROST_LARGE_ONLY float frost_new_lowpass_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_lowpass_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->lowpass,x,p,FROST_LOWPASS);
#else
return frost_lowpass_sample(&s->lowpass,x,p);
#endif
}
FROST_XCODE void frost_new_autowah_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_autowah_reset(FrostEffectState *s){frost_autowah_reset(&s->autowah);}
FROST_LARGE_ONLY float frost_new_autowah_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_autowah_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->autowah,x,p,FROST_AUTOWAH);
#else
return frost_autowah_sample(&s->autowah,x,p);
#endif
}
FROST_XCODE void frost_new_treble_boost_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_treble_boost_reset(FrostEffectState *s){frost_treble_boost_reset(&s->treble_boost);}
FROST_LARGE_ONLY float frost_new_treble_boost_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_treble_boost_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->treble_boost,x,p,FROST_TREBLE_BOOST);
#else
return frost_treble_boost_sample(&s->treble_boost,x,p);
#endif
}
FROST_XCODE void frost_new_foxx_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_foxx_reset(FrostEffectState *s){frost_foxx_reset(&s->foxx);}
FROST_LARGE_ONLY float frost_new_foxx_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_foxx_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->foxx,x,p,FROST_FOXX);
#else
return frost_foxx_sample(&s->foxx,x,p);
#endif
}
FROST_XCODE void frost_new_tape_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_tape_reset(FrostEffectState *s){frost_tape_reset(&s->tape);}
FROST_LARGE_ONLY float frost_new_tape_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_tape_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->tape,x,p,FROST_TAPE);
#else
return frost_tape_sample(&s->tape,x,p);
#endif
}
FROST_XCODE void frost_new_vintage_fuzz_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_vintage_fuzz_reset(FrostEffectState *s){frost_vintage_fuzz_reset(&s->vintage_fuzz);}
FROST_LARGE_ONLY float frost_new_vintage_fuzz_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_vintage_fuzz_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->vintage_fuzz,x,p,FROST_VINTAGE_FUZZ);
#else
return frost_vintage_fuzz_sample(&s->vintage_fuzz,x,p);
#endif
}
FROST_XCODE void frost_new_acoustic_reset(FrostEffectState *s);   /* 32c: cold, flash */
FROST_XCODE void frost_new_acoustic_reset(FrostEffectState *s){frost_acoustic_reset(&s->acoustic);}
FROST_LARGE_ONLY float frost_new_acoustic_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_new_acoustic_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->acoustic,x,p,FROST_ACOUSTIC);
#else
return frost_acoustic_sample(&s->acoustic,x,p);
#endif
}
#undef FROST_WRAP
#define FROST_AMP_RESET(name,voice) FROST_LARGE_ONLY void name(FrostEffectState *s); FROST_LARGE_ONLY void name(FrostEffectState *s){frost_amphead_reset(&s->amphead,voice);}
FROST_AMP_RESET(frost_deluxe_reset,0)
FROST_AMP_RESET(frost_twin_reset,1)
FROST_AMP_RESET(frost_ac30_reset,2)
FROST_AMP_RESET(frost_jc120_reset,3)
FROST_AMP_RESET(frost_jcm800_reset,4)
FROST_AMP_RESET(frost_rectifier_reset,5)
FROST_AMP_RESET(frost_5150_reset,6)
#undef FROST_AMP_RESET
FROST_LARGE_ONLY float frost_ampheads_sample(FrostEffectState *s,float x,const float *p);
FROST_LARGE_ONLY float frost_ampheads_sample(FrostEffectState *s,float x,const float *p){
#ifdef FROST_OVERLAYS
return frost_ovl_call(&s->amphead,x,p,FROST_DELUXE);
#else
return frost_amphead_sample(&s->amphead,x,p);
#endif
}
#define FROST_LIB_ENTRY(id,short_name,title,LIST,category,stem,bytes,ws)     {short_name,FROST_LIB_COUNT(LIST),frost_params_##id,bytes,ws,stem##_reset,stem##_sample}

static const FrostParameter frost_params_gain[]={FROST_GAIN_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_gate[]={FROST_GATE_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_eq[]={FROST_EQ_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_ir[]={FROST_IR_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_overdrive[]={FROST_OVERDRIVE_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_delay[]={FROST_DELAY_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_chorus[]={FROST_CHORUS_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_flanger[]={FROST_FLANGER_PARAMETERS(FROST_PARAMETER_DSP)};
static const FrostParameter frost_params_reverb[]={FROST_REVERB_PARAMETERS(FROST_PARAMETER_DSP)};
#define FROST_LIB_PARAMS(id,short_name,title,LIST,category) static const FrostParameter frost_params_##id[]={LIST(FROST_LIB_DSP)};
FROST_LIBRARY_MODELS(FROST_LIB_PARAMS)
#undef FROST_LIB_PARAMS
const FrostEffect frost_effects[FROST_EFFECTS] = {
    {FROST_GAIN_NAME,FROST_GAIN_PARAMETER_COUNT,frost_params_gain,0,0,gain_reset,gain_sample},
    {FROST_GATE_NAME,FROST_GATE_PARAMETER_COUNT,frost_params_gate,
        sizeof(((FrostEffectState *)0)->gate),0,gate_reset,gate_sample},
    {FROST_EQ_NAME,FROST_EQ_PARAMETER_COUNT,frost_params_eq,
        sizeof(((FrostEffectState *)0)->eq),0,eq_reset,eq_sample},
    {FROST_IR_NAME,FROST_IR_PARAMETER_COUNT,frost_params_ir,sizeof(FrostIR *),sizeof(FrostIR),ir_reset,ir_sample},
    {FROST_OVERDRIVE_NAME,FROST_OVERDRIVE_PARAMETER_COUNT,frost_params_overdrive,
        sizeof(FrostOverdrive),0,overdrive_reset,overdrive_sample},
    {FROST_DELAY_NAME,FROST_DELAY_PARAMETER_COUNT,frost_params_delay,
        sizeof(FrostDelay *),sizeof(FrostDelay),delay_reset,delay_sample},
    {FROST_CHORUS_NAME,FROST_CHORUS_PARAMETER_COUNT,frost_params_chorus,
        sizeof(FrostModulation *),sizeof(FrostModulation),modulation_reset,chorus_sample},
    {FROST_FLANGER_NAME,FROST_FLANGER_PARAMETER_COUNT,frost_params_flanger,
        sizeof(FrostModulation *),sizeof(FrostModulation),modulation_reset,flanger_sample},
    {FROST_REVERB_NAME,FROST_REVERB_PARAMETER_COUNT,frost_params_reverb,
        sizeof(FrostReverbWorkspace *),sizeof(FrostReverbWorkspace),reverb_reset,reverb_sample},
    FROST_LIB_ENTRY(FROST_TS,"TS",0,FROST_LIB_TS,0,ts,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_BD2,"BD-2",0,FROST_LIB_BD2,0,bd2,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_KLON,"KLON",0,FROST_LIB_KLON,0,klon,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_BLUESBREAKER,"BLUESB",0,FROST_LIB_BLUESBREAKER,0,bluesbreaker,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_OCD,"OCD",0,FROST_LIB_OCD,0,ocd,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_DS1,"DS-1",0,FROST_LIB_DS1,0,ds1,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_RAT,"RAT",0,FROST_LIB_RAT,0,rat,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_DISTPLUS,"DIST+",0,FROST_LIB_DISTPLUS,0,distplus,sizeof(FrostDrive),0),
    FROST_LIB_ENTRY(FROST_DD3,"DD-3",0,FROST_LIB_DD3,0,dd3,sizeof(FrostDD3),sizeof(FrostDelay)),
    FROST_LIB_ENTRY(FROST_DM2,"DM-2",0,FROST_LIB_DM2,0,dm2,sizeof(FrostDM2),sizeof(FrostDelay)),
    FROST_LIB_ENTRY(FROST_RV6,"RV-6",0,FROST_LIB_RV6,0,rv6,sizeof(FrostReverbActiveModel),sizeof(FrostReverbWorkspace)),
    FROST_LIB_ENTRY(FROST_SPRING,"SPRING",0,FROST_LIB_SPRING,0,spring,sizeof(FrostReverbActiveModel),sizeof(FrostReverbWorkspace)),
    FROST_LIB_ENTRY(FROST_PHASE90,"PH-90",0,FROST_LIB_PHASE90,0,phase90,sizeof(FrostPhase90),0),
    FROST_LIB_ENTRY(FROST_CE2,"CE-2",0,FROST_LIB_CE2,0,ce2,sizeof(FrostModState),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_MISTRESS,"MISTRS",0,FROST_LIB_MISTRESS,0,mistress,sizeof(FrostModState),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_M117,"M117",0,FROST_LIB_M117,0,m117,sizeof(FrostModState),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_UNIVIBE,"VIBE",0,FROST_LIB_UNIVIBE,0,univibe,sizeof(FrostUniVibe),0),
    FROST_LIB_ENTRY(FROST_VB2,"VB-2",0,FROST_LIB_VB2,0,vb2,sizeof(FrostModState),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_TR2,"TR-2",0,FROST_LIB_TR2,0,tr2,sizeof(FrostTR2),0),
    FROST_LIB_ENTRY(FROST_POG,"POG",0,FROST_LIB_POG,0,pog,sizeof(FrostOctave),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_DYNACOMP,"DYNA",0,FROST_LIB_DYNACOMP,0,dynacomp,sizeof(FrostDynaComp),0),
    FROST_LIB_ENTRY(FROST_COMPPLUS,"COMP+",0,FROST_LIB_COMPPLUS,0,compplus,sizeof(FrostCompPlus),0),
    FROST_LIB_ENTRY(FROST_GE7,"GE-7",0,FROST_LIB_GE7,0,ge7,sizeof(FrostGE7),0),
    FROST_LIB_ENTRY(FROST_NS2,"NS-2",0,FROST_LIB_NS2,0,ns2,sizeof(FrostNS2),0),
    {"AMP",FROST_LIB_COUNT(FROST_LIB_FCM),frost_params_FROST_FCM,sizeof(FrostFcm),sizeof(FrostModulation),frost_fcm_state_reset,frost_fcm_state_sample},
    FROST_LIB_ENTRY(FROST_DROP,"DROP",0,FROST_LIB_DROP,0,drop,sizeof(FrostDropTune),sizeof(FrostModulation)),
    FROST_LIB_ENTRY(FROST_NS1X,"NS-1X",0,FROST_LIB_NS1X,0,frost_new_ns1x,sizeof(FrostNS1X),0),
    FROST_LIB_ENTRY(FROST_MULTIBAND,"MB-CMP",0,FROST_LIB_MULTIBAND,0,frost_new_multiband,sizeof(FrostMultiband),0),
    FROST_LIB_ENTRY(FROST_HIGHPASS,"HPF",0,FROST_LIB_HIGHPASS,0,frost_new_highpass,sizeof(FrostHighpass),0),
    FROST_LIB_ENTRY(FROST_LOWPASS,"LPF",0,FROST_LIB_LOWPASS,0,frost_new_lowpass,sizeof(FrostLowpass),0),
    FROST_LIB_ENTRY(FROST_AUTOWAH,"AW-3",0,FROST_LIB_AUTOWAH,0,frost_new_autowah,sizeof(FrostAutowah),0),
    FROST_LIB_ENTRY(FROST_TREBLE_BOOST,"TREBLE",0,FROST_LIB_TREBLE_BOOST,0,frost_new_treble_boost,sizeof(FrostTrebleBoost),0),
    FROST_LIB_ENTRY(FROST_FOXX,"FOXX",0,FROST_LIB_FOXX,0,frost_new_foxx,sizeof(FrostFoxx),0),
    FROST_LIB_ENTRY(FROST_TAPE,"TAPE",0,FROST_LIB_TAPE,0,frost_new_tape,sizeof(FrostTape),sizeof(FrostDelay)),
    FROST_LIB_ENTRY(FROST_VINTAGE_FUZZ,"FUZZ",0,FROST_LIB_VINTAGE_FUZZ,0,frost_new_vintage_fuzz,sizeof(FrostVintageFuzz),0),
    FROST_LIB_ENTRY(FROST_ACOUSTIC,"ACOUST",0,FROST_LIB_ACOUSTIC,0,frost_new_acoustic,sizeof(FrostAcoustic),0),
#define FROST_AMP_ENTRY(id,name,LIST,reset) {name,FROST_LIB_COUNT(LIST),frost_params_##id,sizeof(FrostAmpHead),0,reset,frost_ampheads_sample}
    FROST_AMP_ENTRY(FROST_DELUXE,"DELUXE",FROST_LIB_DELUXE,frost_deluxe_reset),
    FROST_AMP_ENTRY(FROST_TWIN,"TWIN",FROST_LIB_TWIN,frost_twin_reset),
    FROST_AMP_ENTRY(FROST_AC30,"AC30",FROST_LIB_AC30,frost_ac30_reset),
    FROST_AMP_ENTRY(FROST_JC120,"JC-120",FROST_LIB_JC120,frost_jc120_reset),
    FROST_AMP_ENTRY(FROST_JCM800,"JCM800",FROST_LIB_JCM800,frost_jcm800_reset),
    FROST_AMP_ENTRY(FROST_RECTIFIER,"RECTO",FROST_LIB_RECTIFIER,frost_rectifier_reset),
    FROST_AMP_ENTRY(FROST_5150,"5150",FROST_LIB_5150,frost_5150_reset)
#undef FROST_AMP_ENTRY
};
FROST_CTL_RAM const FrostEffect *frost_effect_find(unsigned type)
{ return type<FROST_EFFECTS?&frost_effects[type]:0; }
void frost_instance_init(FrostInstance *s,unsigned type,FrostIR *ir_workspace)
{frost_instance_init_workspace(s,type,type==FROST_IR?ir_workspace:0);}
FROST_COLD void frost_instance_init_workspace(FrostInstance *s,unsigned type,void *workspace)
{
    unsigned j;const FrostEffect *d=frost_effect_find(type);
    if(frost_type_workspace(type)!=FROST_WORKSPACE_NONE && !workspace)d=0;
    s->type=d?type:FROST_EMPTY;s->wet=s->wet_target=0.0f;
    s->bypass_policy=FROST_BYPASS_KEEP_HISTORY;s->suspended=0;
    for(j=0;j<FROST_PARAMETERS;++j)
        s->target[j]=s->current[j]=d && j<d->parameter_count?d->parameters[j].initial:0.0f;
    s->current[FROST_PARAMETERS]=0.0f;
    if(d && type==FROST_IR){const float identity=1.0f;s->state.ir=(FrostIR *)workspace;(void)frost_ir_load(s->state.ir,&identity,1);}
    else if(d && type==FROST_DELAY){s->state.delay=(FrostDelay *)workspace;d->reset(&s->state);}
    else if(d && (type==FROST_CHORUS || type==FROST_FLANGER)){s->state.modulation=(FrostModulation *)workspace;d->reset(&s->state);}
    else if(d && type==FROST_REVERB){s->state.reverb=(FrostReverbWorkspace *)workspace;d->reset(&s->state);}
    else if(d && (type==FROST_DD3)){s->state.dd3.ring=(FrostDelay *)workspace;d->reset(&s->state);}
    else if(d && type==FROST_TAPE){s->state.tape.ring=(FrostDelay *)workspace;d->reset(&s->state);}
    else if(d && (type==FROST_DM2)){s->state.dm2.ring=(FrostDelay *)workspace;d->reset(&s->state);}
    else if(d && (type==FROST_RV6 || type==FROST_SPRING)){s->state.reverb_model.ws=(FrostReverbWorkspace *)workspace;d->reset(&s->state);}
    else if(d && type==FROST_POG){s->state.octave.ring=(FrostModulation *)workspace;d->reset(&s->state);}
    else if(d && type==FROST_DROP){s->state.drop.ring=(FrostModulation *)workspace;d->reset(&s->state);}
    else if(d && type==FROST_FCM){s->state.fcm.ring=(FrostModulation *)workspace;d->reset(&s->state);}
    else if(d && frost_type_is_modulation(type) && type!=FROST_CHORUS && type!=FROST_FLANGER){s->state.mod.ring=(FrostModulation *)workspace;d->reset(&s->state);}
    else if(d)d->reset(&s->state);
}
const void *frost_instance_workspace(const FrostInstance *s)
{
    switch(s->type){
    case FROST_IR:return s->state.ir;
    case FROST_DELAY:return s->state.delay;
    case FROST_DD3:return s->state.dd3.ring;
    case FROST_DM2:return s->state.dm2.ring;
    case FROST_TAPE:return s->state.tape.ring;
    case FROST_CHORUS:case FROST_FLANGER:return s->state.modulation;
    case FROST_POG:return s->state.octave.ring;
    case FROST_DROP:return s->state.drop.ring;
    case FROST_FCM:return s->state.fcm.ring;
    case FROST_CE2:case FROST_MISTRESS:case FROST_M117:case FROST_VB2:return s->state.mod.ring;
    case FROST_REVERB:return s->state.reverb;
    case FROST_RV6:case FROST_SPRING:return s->state.reverb_model.ws;
    default:return 0;
    }
}
void frost_instance_reset_history(FrostInstance *s)
{
    const FrostEffect *d=frost_effect_find(s->type);if(d)d->reset(&s->state);
    /* 31a: the descriptor reset clears the IR's time ring; its FFT histories too. */
    if(d && s->type==FROST_IR)frost_ir_fft_reset(s->state.ir);
}
FROST_HOT static float effect_smooth(float current,float target)
{ return current+0.004525f*(target-current); }
/* 27a: one out-of-line copy (was inlined at four sites) bounds DSP flash;
 * arithmetic is entirely inside this function, so results are unchanged. */
FROST_HOT FROST_CORE FROST_NOINLINE static float instance_step(FrostInstance *s,const FrostEffect *d,float input)
{
    unsigned j;float y;
    if(s->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND){
        const float step=1.0f/(float)FROST_BYPASS_FADE;
        if(s->wet<s->wet_target){s->wet+=step;if(s->wet>s->wet_target)s->wet=s->wet_target;}
        else if(s->wet>s->wet_target){s->wet-=step;if(s->wet<s->wet_target)s->wet=s->wet_target;}
    }else s->wet=effect_smooth(s->wet,s->wet_target);
    for(j=0;j<d->parameter_count;++j)
        s->current[j]=d->parameters[j].flags&FROST_PARAM_STEPPED?s->target[j]:effect_smooth(s->current[j],s->target[j]);
    s->current[FROST_PARAMETERS]=s->wet_target;
    /* Parameter smoothing stays live even when expensive sample work stops.
     * Reset only at exact dry output, never mid-fade. This policy cuts tails. */
    if(s->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND && s->wet==0.0f && s->wet_target==0.0f){
        if(!s->suspended){d->reset(&s->state);s->suspended=1;}
        return input;
    }
    s->suspended=0;
    y=d->sample(&s->state,input,s->current);
    return input+s->wet*(y-input);
}
FROST_NOINLINE float frost_instance_sample(FrostInstance *s,float input)
{
    const FrostEffect *d=frost_effect_find(s->type);
    if(!d)return input;
    return instance_step(s,d,input);
}
/* One effect's code stays hot for the whole block instead of every slot's code
 * being cycled through per sample (flash-resident code on the pedal). */
/* Reverb with the default KEEP policy: same per-sample smoothing and mix as
 * instance_step, but the network runs line-by-line over chunks of samples. */
FROST_HOT FROST_CORE static int instance_settled(const FrostInstance *s,const FrostEffect *d);
FROST_HOT static unsigned reverb_process(FrostInstance *s,const FrostEffect *d,float *x,unsigned count)
{
    float p[FROST_REVERB_CHUNK][3],w[FROST_REVERB_CHUNK],y[FROST_REVERB_CHUNK];unsigned base,k,j,n;
    /* 26a: settled (fixed point at block start) -> stage constants; exact. */
    int settled=instance_settled(s,d);
    for(base=0;base<count;base+=n){
        n=count-base;if(n>FROST_REVERB_CHUNK)n=FROST_REVERB_CHUNK;
        if(settled)for(k=0;k<n;++k){p[k][0]=s->current[0];p[k][1]=s->current[1];p[k][2]=s->current[2];w[k]=s->wet;}
        else for(k=0;k<n;++k){
            s->wet=effect_smooth(s->wet,s->wet_target);
            for(j=0;j<d->parameter_count;++j)s->current[j]=effect_smooth(s->current[j],s->target[j]);
            p[k][0]=s->current[0];p[k][1]=s->current[1];p[k][2]=s->current[2];w[k]=s->wet;
        }
        s->suspended=0;
        frost_active_reverb_block(s->state.reverb,x+base,y,n,(const float (*)[3])p);
        for(k=0;k<n;++k){float in=x[base+k];x[base+k]=in+w[k]*(y[k]-in);if(!frost_signal_valid(x[base+k]))return base+k;}
    }
    return count;
}
#if !(defined(__arm__) && defined(GP5_FIRMWARE)) && !defined(FROST_IR_LINK)
FROST_HOT static unsigned instance_ir_process_pairs(FrostInstance *s,const FrostEffect *d,float *x,unsigned count)
{
    unsigned n=0;
    /* Validate the first output through the existing scalar path. A corrupt
     * IR still performs one kernel call before the guarded fault reset/latch.
     * Parameters/wet advance only when their individual output is checked. */
    if(count){x[0]=instance_step(s,d,x[0]);if(!frost_signal_valid(x[0]))return 0;n=1;}
    for(;n+1<count;n+=2){
        float in0=x[n],in1=x[n+1],a,b;
        frost_ir_pair(s->state.ir,in0,in1,&a,&b);
        s->wet=effect_smooth(s->wet,s->wet_target);
        s->current[0]=effect_smooth(s->current[0],s->target[0]);
        x[n]=in0+s->wet*(a*s->current[0]-in0);
        if(!frost_signal_valid(x[n]))return n;
        s->wet=effect_smooth(s->wet,s->wet_target);
        s->current[0]=effect_smooth(s->current[0],s->target[0]);
        x[n+1]=in1+s->wet*(b*s->current[0]-in1);
        if(!frost_signal_valid(x[n+1]))return n+1;
    }
    if(n<count){x[n]=instance_step(s,d,x[n]);if(!frost_signal_valid(x[n]))return n;}
    return count;
}
#endif
/* 25a: whole-block FIR. Sample 0 still takes the scalar instance path (so a
 * corrupt IR evaluates exactly one kernel sample before the guarded fault);
 * the remaining outputs come from one register-blocked block FIR, then the
 * identical per-sample smoothing/mix/validity sequence of the pair path. A
 * fault discards the block and resets histories, so FIR state computed past
 * the first invalid output is never used. */
/* Platform block is 64 samples; larger host chunks use the pair path. */
#define FROST_IR_BLOCK_MAX 64u
/* 31a: the direct block FIR is host/reference only (DSP flash for the FFT). */
#if !(defined(__arm__) && defined(GP5_FIRMWARE)) && !defined(FROST_IR_LINK)
FROST_NOINLINE FROST_HOT FROST_CORE static unsigned instance_ir_process_block(FrostInstance *s,float *x,unsigned count)
{
    float y[FROST_IR_BLOCK_MAX];unsigned n;
    const FrostEffect *d=&frost_effects[FROST_IR];
    x[0]=instance_step(s,d,x[0]);if(!frost_signal_valid(x[0]))return 0;
    frost_ir_block(s->state.ir,x+1,y,count-1);
    /* 28a: at a smoothing fixed point every later step returns the same wet and
     * level (targets change only between blocks), so skipping it is exact. */
    if(instance_settled(s,d)){
        const float wet=s->wet,level=s->current[0];
        for(n=1;n<count;++n){
            float in=x[n];
            x[n]=in+wet*(y[n-1]*level-in);
            if(!frost_signal_valid(x[n]))return n;
        }
        return count;
    }
    for(n=1;n<count;++n){
        float in=x[n];
        s->wet=effect_smooth(s->wet,s->wet_target);
        s->current[0]=effect_smooth(s->current[0],s->target[0]);
        x[n]=in+s->wet*(y[n-1]*s->current[0]-in);
        if(!frost_signal_valid(x[n]))return n;
    }
    return count;
}
#endif
/* 31a: one FFT block (64 samples, window aligned). Settled KEEP blocks mix
 * with constant wet/level as before. Otherwise (smoothing, CUT ramps) the
 * unchanged instance_step and ir_sample run per sample while frost_ir_sample
 * replays the block's precomputed raw FIR outputs (frost_ir_fft_replay), so
 * every smoothing/ramp/suspension step is the per-sample path's own; FFT
 * histories are cleared after a suspension, as ir_reset clears the time ring.
 * The kernel cannot produce a non-finite value from finite input and
 * validated coefficients; validity is still checked per output. */
unsigned frost_instance_ir_fft(FrostInstance *s,const FrostEffect *d,float *x);
#ifdef FROST_IR_LINK
/* Stage 4: the DSP-side IR is the fallback; its mix step moves to flash to
 * make LARGE room for the remote branch (the FFT kernels stay in LARGE). */
FROST_NOINLINE FROST_HOT FROST_CORE unsigned frost_instance_ir_fft(FrostInstance *s,const FrostEffect *d,float *x)
#else
FROST_NOINLINE FROST_HOT FROST_CORE FROST_AUDIO_RAM unsigned frost_instance_ir_fft(FrostInstance *s,const FrostEffect *d,float *x)
#endif
{
    float y[FROST_IR_FFT_BLOCK];unsigned n;FrostIR *ir=s->state.ir;
    if(instance_settled(s,d)){
        const float wet=s->wet,level=s->current[0];
        frost_ir_fft_block(ir,x,y);
        (void)frost_ir_cut_block(s->state.ir_cut.cut,s->current[1],s->current[2],y,y);   /* 32c LOW/HIGH CUT */
        s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
        for(n=0;n<FROST_IR_FFT_BLOCK;++n){
            float in=x[n];
            x[n]=in+wet*(y[n]*level-in);
            if(!frost_signal_valid(x[n]))return n;
        }
        return FROST_IR_FFT_BLOCK;
    }
    frost_ir_fft_block(ir,x,y);
    (void)frost_ir_cut_block(s->state.ir_cut.cut,s->current[1],s->current[2],y,y);
    frost_ir_fft_replay=y;
    for(n=0;n<FROST_IR_FFT_BLOCK;++n){
        x[n]=instance_step(s,d,x[n]);
        if(!frost_signal_valid(x[n])){frost_ir_fft_replay=0;return n;}
    }
    frost_ir_fft_replay=0;
    if(s->suspended)frost_ir_fft_reset(ir);
    return FROST_IR_FFT_BLOCK;
}
#ifdef FROST_IR_LINK
/* Stage 4 (docs/Build%20Firmware.md): remote mode. The IR block's raw FIR sums come
 * from the controller (frost_ir_remote_fn: the input of two blocks back,
 * faded while unavailable); smoothing, level, bypass ramp and validity are
 * exactly the FFT block path's, through the same replay mechanism. A
 * workspace the link does not serve falls back to the local paths. Stage 4b:
 * in LARGE (the DSP-side FFT kernels moved to flash instead). */
unsigned frost_ir_remote;
const float *(*frost_ir_remote_fn)(FrostIR *ir,const float *x);
uint32_t frost_ir_remote_cycles;   /* DSP cycles in remote IR blocks this block (firmware clock) */
#ifndef FROST_IRL_CLOCK
#define FROST_IRL_CLOCK() 0u
#endif
unsigned frost_instance_ir_remote(FrostInstance *s,const FrostEffect *d,float *x);
FROST_NOINLINE FROST_HOT FROST_AUDIO_RAM unsigned frost_instance_ir_remote(FrostInstance *s,const FrostEffect *d,float *x)
{
    const float *y=frost_ir_remote_fn?frost_ir_remote_fn(s->state.ir,x):0;unsigned n;float cut[FROST_IR_FFT_BLOCK];
    if(!y){
        FrostIRFFT *f=frost_ir_fft_of(s->state.ir);
        if(f && f->parts){if(s->suspended && f->dirty)frost_ir_fft_reset(s->state.ir);if(!f->fill)return frost_instance_ir_fft(s,d,x);}
        for(n=0;n<FROST_IR_FFT_BLOCK;++n){x[n]=instance_step(s,d,x[n]);if(!frost_signal_valid(x[n]))return n;}
        return FROST_IR_FFT_BLOCK;
    }
    y=frost_ir_cut_block(s->state.ir_cut.cut,s->current[1],s->current[2],y,cut);   /* 32c LOW/HIGH CUT */
    if(instance_settled(s,d)){
        const float wet=s->wet,level=s->current[0];
        s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
        for(n=0;n<FROST_IR_FFT_BLOCK;++n){
            float in=x[n];
            x[n]=in+wet*(y[n]*level-in);
            if(!frost_signal_valid(x[n]))return n;
        }
        return FROST_IR_FFT_BLOCK;
    }
    frost_ir_fft_replay=y;
    for(n=0;n<FROST_IR_FFT_BLOCK;++n){
        x[n]=instance_step(s,d,x[n]);
        if(!frost_signal_valid(x[n])){frost_ir_fft_replay=0;return n;}
    }
    frost_ir_fft_replay=0;
    return FROST_IR_FFT_BLOCK;
}
#endif
/* 25a: exact fast path. If, at block start, one smoothing step leaves wet and
 * every parameter unchanged (a floating-point fixed point), every later step
 * in this block does too: targets only change between blocks. Skipping the
 * per-sample smoothing therefore yields bit-identical outputs and state. */
FROST_HOT FROST_CORE static int instance_settled(const FrostInstance *s,const FrostEffect *d)
{
    unsigned j;
    if(s->bypass_policy!=FROST_BYPASS_KEEP_HISTORY)return 0;
    if(effect_smooth(s->wet,s->wet_target)!=s->wet)return 0;
    for(j=0;j<d->parameter_count;++j){
        if(d->parameters[j].flags&FROST_PARAM_STEPPED){if(s->current[j]!=s->target[j])return 0;}
        else if(effect_smooth(s->current[j],s->target[j])!=s->current[j])return 0;
    }
    return 1;
}
/* 27a: settled RV-6 runs the exact block body (parameters constant over the
 * block); the mix/validity sequence is the generic settled loop's. Like the
 * reverb chunk path, a fault discards the block and resets histories. */
FROST_NOINLINE FROST_HOT FROST_CORE FROST_COLD static unsigned rv6_process_settled(FrostInstance *s,float *x,unsigned count,float wet)
{
    float y[FROST_IR_BLOCK_MAX];unsigned base,m,n;
    for(base=0;base<count;base+=m){
        m=count-base;if(m>FROST_IR_BLOCK_MAX)m=FROST_IR_BLOCK_MAX;
        frost_active_rv6_block(&s->state.reverb_model,x+base,y,m,s->current);
        for(n=0;n<m;++n){float in=x[base+n];x[base+n]=in+wet*(y[n]-in);if(!frost_signal_valid(x[base+n]))return base+n;}
    }
    return count;
}
#ifdef frost_active_spring_block
/* 32e: settled SPRING through its block body; mix and validity as the
 * generic settled loop. External linkage (DSP boot layout rule); the short
 * per-block loop runs from dsp-data flash (LARGE is full), the block body
 * from LARGE. */
unsigned frost_spring_settled(FrostInstance *s,float *x,unsigned count,float wet);
FROST_NOINLINE FROST_HOT FROST_XCODE unsigned frost_spring_settled(FrostInstance *s,float *x,unsigned count,float wet)
{
    float y[64];unsigned base,m,n;
    for(base=0;base<count;base+=m){
        m=count-base;if(m>64u)m=64u;
        frost_active_spring_block(&s->state.reverb_model,x+base,y,m,s->current);
        for(n=0;n<m;++n){float in=x[base+n];x[base+n]=in+wet*(y[n]-in);if(!frost_signal_valid(x[base+n]))return base+n;}
    }
    return count;
}
#endif
/* 28a: settled loops for the simplest/common kernels. Same per-sample
 * arithmetic and order as the generic settled loop (kernel, then
 * in+wet*(y-in), then validity), but the kernel is compiled into the loop
 * instead of an indirect call per sample. FROST_OPAQUE keeps y's rounding
 * exactly as returned by a call. */
/* 31o: the settled loops' validity test on the IEEE bits: |x| <= 16 with x
 * finite and not NaN is exactly (bits & 0x7fffffff) <= bits(16.0f), since
 * NaN/Inf/|x|>16 all compare above it. Same pass/stop decision as
 * frost_signal_valid with one integer compare instead of FPU compares. Used
 * only by these LARGE-resident loops; frost_signal_valid is unchanged. */
static inline __attribute__((always_inline)) int settled_valid(float x)
{union {float f;uint32_t u;} v;v.f=x;return (v.u&0x7fffffffu)<=0x41800000u;}
#define FROST_SETTLED_LOOP(name,expr) \
FROST_NOINLINE FROST_HOT FROST_CORE FROST_AUDIO_RAM static unsigned name##_settled(FrostInstance *s,float *x,unsigned count,float wet) \
{ \
    unsigned n;FrostEffectState *st=&s->state;const float *p=s->current; \
    for(n=0;n<count;++n){float in=x[n],y=(expr);FROST_OPAQUE(y);x[n]=in+wet*(y-in);if(!settled_valid(x[n]))return n;} \
    return count; \
}
FROST_SETTLED_LOOP(gain,gain_sample(st,in,p))
FROST_SETTLED_LOOP(gate,gate_sample(st,in,p))
FROST_SETTLED_LOOP(eq,eq_sample(st,in,p))
FROST_SETTLED_LOOP(ds1,frost_ds1_sample(&st->drive,in,p))
/* 31h: CE-2 and DELAY settle in LARGE without the indirect table call. */
FROST_SETTLED_LOOP(ce2,frost_ce2_sample(&st->mod,in,p))
FROST_SETTLED_LOOP(delay,frost_delay_sample(st->delay,in,p))
#undef FROST_SETTLED_LOOP
/* EXPERIMENTAL AMP (FCM): settled blocks through the block kernel, then the
 * generic wet mix and validity sequence. No added latency. */
FROST_NOINLINE FROST_HOT FROST_AUDIO_RAM FROST_COLD static unsigned fcm_settled(FrostInstance *s,float *x,unsigned count,float wet,FrostBlockFn kernel)
{
    float y[64];unsigned base,m,n;
    for(base=0;base<count;base+=m){
        m=count-base;if(m>64u)m=64u;
        kernel(&s->state,x+base,y,m,s->current,0);
        for(n=0;n<m;++n){float in=x[base+n];x[base+n]=in+wet*(y[n]-in);if(!settled_valid(x[base+n]))return base+n;}
    }
    return count;
}
/* 31t: AMP while GAIN/LEVEL/wet are still smoothing (knob moves). The
 * per-sample path would call the oversampled model once per sample (about
 * twice the settled cost, measured AMP+IR 60k -> 101k instructions/block,
 * crackle with a DSP command on top). Same smoothing recurrences as
 * instance_step, advanced per sample; the model runs once per <= 64 samples
 * with GAIN and LEVEL ramped linearly from the chunk's start values (an
 * approximation of the per-sample exponential glide). */
/* Flash-resident (LARGE is full): only runs while a knob moves; the model work
 * itself is the loaded overlay kernel. */
/* External linkage keeps it out of the boot-adjacent region (DSP boot layout rule). */
unsigned frost_fcm_ramped(FrostInstance *s,const FrostEffect *d,float *x,unsigned count);
FROST_NOINLINE FROST_HOT unsigned frost_fcm_ramped(FrostInstance *s,const FrostEffect *d,float *x,unsigned count)
{
    float y[64],wet[64],p0[FROST_PARAMETERS+1];unsigned base,m,n,j;
    FrostBlockFn kernel=frost_fcm_block_kernel;
    for(base=0;base<count;base+=m){
        m=count-base;if(m>64u)m=64u;
        for(j=0;j<=FROST_PARAMETERS;++j)p0[j]=s->current[j];
        for(n=0;n<m;++n){s->wet=effect_smooth(s->wet,s->wet_target);wet[n]=s->wet;}
        /* Independent recurrences: retain the exact m smoothing steps and
         * final values, reading each parameter's flash metadata only once. */
        for(j=0;j<d->parameter_count;++j){
            float value=s->current[j],target=s->target[j];
            if(d->parameters[j].flags&FROST_PARAM_STEPPED)value=target;
            else for(n=0;n<m;++n)value=effect_smooth(value,target);
            s->current[j]=value;
        }
        s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
        kernel(&s->state,x+base,y,m,s->current,p0);
        for(n=0;n<m;++n){float in=x[base+n];x[base+n]=in+wet[n]*(y[n]-in);if(!frost_signal_valid(x[base+n]))return base+n;}
    }
    return count;
}
#ifdef FROST_EDIT_BLOCKS
/* Edit blocks (31y9): while a knob moves (parameters or wet still smoothing),
 * most effects took the per-sample path: instance_step from flash and the
 * kernel through its wrapper, every sample. RV-6 even rebuilt its whole
 * set-up per sample. Measured in the emulator on the heavy chain: RV-6 edits
 * +50..70%, GE-7 +13%, OCD +6%, which is the editing overload on the pedal.
 * Here the smoothing recurrences run per sample exactly as instance_step's
 * (same end state), the wet ramp stays per sample, and the kernel runs with
 * the chunk's end parameters: the RV-6 block kernel, the resident overlay
 * kernel, or the plain sample function. Parameters move in 64-sample steps
 * (1.45 ms) along the same glide, like AMP's frost_fcm_ramped. KEEP policy
 * only; CUT slots keep the per-sample path (suspension). */
unsigned frost_instance_edit(FrostInstance *s,const FrostEffect *d,float *x,unsigned count);
FROST_NOINLINE FROST_HOT FROST_AUDIO_RAM unsigned frost_instance_edit(FrostInstance *s,const FrostEffect *d,float *x,unsigned count)
{
    float y[64],wet[64];unsigned base,m,n,j;
    for(base=0;base<count;base+=m){
        m=count-base;if(m>64u)m=64u;
        for(n=0;n<m;++n){s->wet=effect_smooth(s->wet,s->wet_target);wet[n]=s->wet;}
        /* Independent recurrences: retain the exact m smoothing steps and
         * final values, reading each parameter's flash metadata only once. */
        for(j=0;j<d->parameter_count;++j){
            float value=s->current[j],target=s->target[j];
            if(d->parameters[j].flags&FROST_PARAM_STEPPED)value=target;
            else for(n=0;n<m;++n)value=effect_smooth(value,target);
            s->current[j]=value;
        }
        s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
        if(s->type>=FROST_DELUXE && s->type<=FROST_5150 && frost_amphead_block_kernel)frost_amphead_block_kernel(&s->state,x+base,y,m,s->current,0);
        else if(s->type==FROST_RV6)frost_active_rv6_block(&s->state.reverb_model,x+base,y,m,s->current);
#ifdef frost_active_spring_block
        else if(s->type==FROST_SPRING)frost_active_spring_block(&s->state.reverb_model,x+base,y,m,s->current);
#endif
        else{
#ifdef FROST_OVERLAYS
            FrostOvlKernel k=s->type<FROST_EFFECTS?frost_ovl_kernel[s->type]:0;
            if(k){for(n=0;n<m;++n)y[n]=k(&s->state,x[base+n],s->current);}
            else
#endif
            for(n=0;n<m;++n)y[n]=d->sample(&s->state,x[base+n],s->current);
        }
        for(n=0;n<m;++n){float in=x[base+n];x[base+n]=in+wet[n]*(y[n]-in);if(!frost_signal_valid(x[base+n]))return base+n;}
    }
    return count;
}
#endif
#ifdef FROST_OVERLAYS
/* 31i: settled overlay kernels, called directly at their loaded LARGE copy
 * (same arithmetic as the generic settled loop through the wrapper). */
FROST_NOINLINE FROST_HOT FROST_AUDIO_RAM static unsigned ovl_settled(FrostInstance *s,float *x,unsigned count,float wet,FrostOvlKernel k)
{
    unsigned n;const float *p=s->current;
    for(n=0;n<count;++n){float in=x[n],y=k(&s->state,in,p);x[n]=in+wet*(y-in);if(!settled_valid(x[n]))return n;}
    return count;
}
#endif
/* Stage 2b Cabinet Override: while set, every IR block passes its input (changed
 * only at a silent route commit, engine/chain.c frost_chain_request_ir_bypass). */
unsigned frost_ir_bypass;
FROST_HOT FROST_CORE FROST_AUDIO_RAM unsigned frost_instance_process(FrostInstance *s,float *x,unsigned count)
{
    unsigned n;const FrostEffect *d=frost_effect_find(s->type);
    if(!d || (frost_ir_bypass && s->type==FROST_IR))return count;
#ifdef FROST_IR_LINK
    if(frost_ir_remote && s->type==FROST_IR && count==FROST_IR_FFT_BLOCK && d->parameter_count==FROST_IR_PARAMETER_COUNT &&
       !(s->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND && s->wet==0.0f && s->wet_target==0.0f))
        {uint32_t t0=FROST_IRL_CLOCK();unsigned r=frost_instance_ir_remote(s,d,x);frost_ir_remote_cycles+=FROST_IRL_CLOCK()-t0;return r;}
#endif
    /* 31a: with an active FFT workspace only the FFT block path and the
     * per-sample path (which feeds the FFT window) keep both histories in
     * step; the direct block/pair kernels are not used. A CUT slot resting dry
     * takes the per-sample path (no FIR work); one resuming from suspension
     * first drops FFT histories left from before its reset. */
    if(s->type==FROST_IR && d->parameter_count==FROST_IR_PARAMETER_COUNT){
        FrostIRFFT *f=frost_ir_fft_of(s->state.ir);
        if(f && f->parts){
            if(s->suspended && f->dirty)frost_ir_fft_reset(s->state.ir);
            if(count==FROST_IR_FFT_BLOCK && !f->fill &&
               !(s->bypass_policy==FROST_BYPASS_CUT_AND_SUSPEND && s->wet==0.0f && s->wet_target==0.0f))
                return frost_instance_ir_fft(s,d,x);
            goto per_sample;
        }
    }
    if(s->type==FROST_IR && s->bypass_policy==FROST_BYPASS_KEEP_HISTORY && d->parameter_count==FROST_IR_PARAMETER_COUNT){
#if (defined(__arm__) && defined(GP5_FIRMWARE)) || defined(FROST_IR_LINK)
        /* 26a/31a target: an IR without the FFT workspace (a second IR) runs
         * the scalar path (bit-identical to the block/pair kernels); the block
         * FIR is host-only since 31a to free flash for the FFT. */
        goto per_sample;
#else
        if(count>=2 && count-1<=FROST_IR_BLOCK_MAX && frost_ir_block_fits(s->state.ir,count-1))
            return instance_ir_process_block(s,x,count);
        return instance_ir_process_pairs(s,d,x,count);
#endif
    }
    if(s->type==FROST_REVERB && s->bypass_policy==FROST_BYPASS_KEEP_HISTORY && d->parameter_count==3)
        return reverb_process(s,d,x,count);
    if(count && instance_settled(s,d)){
        float (*sample)(FrostEffectState *,float,const float *)=d->sample;float wet=s->wet;
        s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
        switch(s->type){
        case FROST_RV6:return rv6_process_settled(s,x,count,wet);
#ifdef frost_active_spring_block
        case FROST_SPRING:return frost_spring_settled(s,x,count,wet);
#endif
        case FROST_GAIN:return gain_settled(s,x,count,wet);
        case FROST_GATE:return gate_settled(s,x,count,wet);
        case FROST_EQ:return eq_settled(s,x,count,wet);
        case FROST_DS1:return ds1_settled(s,x,count,wet);
        case FROST_CE2:return ce2_settled(s,x,count,wet);
        case FROST_DELAY:return delay_settled(s,x,count,wet);
        case FROST_FCM:if(frost_fcm_block_kernel)return fcm_settled(s,x,count,wet,frost_fcm_block_kernel);break;
        case FROST_DELUXE:case FROST_TWIN:case FROST_AC30:case FROST_JC120:case FROST_JCM800:case FROST_RECTIFIER:case FROST_5150:
            if(frost_amphead_block_kernel)return fcm_settled(s,x,count,wet,frost_amphead_block_kernel);break;
        default:
#ifdef FROST_OVERLAYS
            if(s->type<FROST_EFFECTS && frost_ovl_kernel[s->type])return ovl_settled(s,x,count,wet,frost_ovl_kernel[s->type]);
#endif
            break;
        }
        for(n=0;n<count;++n){
            float in=x[n],y=sample(&s->state,in,s->current);
            x[n]=in+wet*(y-in);if(!frost_signal_valid(x[n]))return n;
        }
        return count;
    }
    if(s->type==FROST_FCM && frost_fcm_block_kernel && s->bypass_policy==FROST_BYPASS_KEEP_HISTORY)return frost_fcm_ramped(s,d,x,count);
#ifdef FROST_EDIT_BLOCKS
    if(count && s->bypass_policy==FROST_BYPASS_KEEP_HISTORY && s->type!=FROST_FCM && s->type!=FROST_IR && s->type!=FROST_REVERB)
        return frost_instance_edit(s,d,x,count);
#endif
per_sample:
    for(n=0;n<count;++n){x[n]=instance_step(s,d,x[n]);if(!frost_signal_valid(x[n]))return n;}
    return count;
}
