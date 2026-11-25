#ifndef FROST_CONTROL_CATALOG_H
#define FROST_CONTROL_CATALOG_H
#include "metadata.h"
#include "human_controls.h"
#include "library.h"
/* Shared UI catalog contains data only, never DSP state or kernel pointers. */
typedef struct {
    const char *short_name,*name;
    unsigned count;
    FrostControlParameter p[FROST_PARAMETERS];
    FrostParameter dsp[FROST_PARAMETERS];
} FrostControlEffect;
static const FrostControlEffect frost_control_effects[FROST_EFFECTS]={
    {FROST_GAIN_NAME,FROST_GAIN_TITLE,FROST_GAIN_PARAMETER_COUNT,
        FROST_HUMAN_GAIN,{FROST_GAIN_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_GATE_NAME,FROST_GATE_TITLE,FROST_GATE_PARAMETER_COUNT,
        FROST_HUMAN_GATE,{FROST_GATE_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_EQ_NAME,FROST_EQ_TITLE,FROST_EQ_PARAMETER_COUNT,
        FROST_HUMAN_EQ,{FROST_EQ_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_IR_NAME,FROST_IR_TITLE,FROST_IR_PARAMETER_COUNT,
        FROST_HUMAN_IR,{FROST_IR_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_OVERDRIVE_NAME,FROST_OVERDRIVE_TITLE,FROST_OVERDRIVE_PARAMETER_COUNT,
        FROST_HUMAN_OVERDRIVE,{FROST_OVERDRIVE_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_DELAY_NAME,FROST_DELAY_TITLE,FROST_DELAY_PARAMETER_COUNT,
        FROST_HUMAN_DELAY,{FROST_DELAY_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_CHORUS_NAME,FROST_CHORUS_TITLE,FROST_CHORUS_PARAMETER_COUNT,
        FROST_HUMAN_CHORUS,{FROST_CHORUS_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_FLANGER_NAME,FROST_FLANGER_TITLE,FROST_FLANGER_PARAMETER_COUNT,
        FROST_HUMAN_FLANGER,{FROST_FLANGER_PARAMETERS(FROST_PARAMETER_DSP)}},
    {FROST_REVERB_NAME,FROST_REVERB_TITLE,FROST_REVERB_PARAMETER_COUNT,
        FROST_HUMAN_REVERB,{FROST_REVERB_PARAMETERS(FROST_PARAMETER_DSP)}},
#define FROST_LIB_CONTROL_ENTRY(id,short_name,title,LIST,category) \
    {short_name,title,FROST_LIB_COUNT(LIST),{LIST(FROST_LIB_CONTROL)},{LIST(FROST_LIB_DSP)}},
    FROST_LIBRARY_MODELS(FROST_LIB_CONTROL_ENTRY)
#undef FROST_LIB_CONTROL_ENTRY
};
static const FrostControlEffect frost_empty_control={"EMPTY","EMPTY SLOT",0,{{0,0,0,0,0,0,0,0}},{{0,0.0f,0.0f,0.0f,0u}}};
static inline const FrostControlEffect *frost_control_find(unsigned type)
{return type<FROST_EFFECTS?&frost_control_effects[type]:type==FROST_EMPTY?&frost_empty_control:0;}
/* Category-then-model picker order. Original generic types stay selectable
 * (listed last in their category) so existing presets/routes keep working. */
static const unsigned char frost_picker_types[]={
    FROST_TS,FROST_BD2,FROST_KLON,FROST_BLUESBREAKER,FROST_OCD,FROST_OVERDRIVE,FROST_TREBLE_BOOST,FROST_BOOST,
    FROST_DS1,FROST_RAT,FROST_DISTPLUS,FROST_VINTAGE_FUZZ,FROST_MUFF,FROST_CRUSHER,
    FROST_DD3,FROST_DM2,FROST_DELAY,FROST_TAPE,FROST_PINGPONG,
    FROST_RV6,FROST_SPRING,FROST_REVERB,
    FROST_PHASE90,FROST_CE2,FROST_MISTRESS,FROST_M117,FROST_UNIVIBE,FROST_VB2,FROST_TR2,FROST_HARMTREM,FROST_ROTARY,FROST_RINGMOD,FROST_CHORUS,FROST_FLANGER,FROST_AUTOWAH,FROST_FOXX,
    FROST_DYNACOMP,FROST_COMPPLUS,FROST_GE7,FROST_CALI_EQ,FROST_NS2,FROST_NS1X,FROST_MULTIBAND,FROST_SWELL,
    FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_EMPTY,FROST_HIGHPASS,FROST_LOWPASS,FROST_ACOUSTIC,
    FROST_DELUXE,FROST_TWIN,FROST_AC30,FROST_JC120,FROST_JCM800,FROST_RECTIFIER,FROST_5150,FROST_SL100,FROST_FCM,
    FROST_DROP,FROST_POG};
static const unsigned char frost_picker_start[FROST_CATEGORIES+1]={0,8,14,19,22,36,44,52,61,63};
static const char *const frost_category_names[FROST_CATEGORIES]={
    "OVERDRIVE","DISTORTION","DELAY","REVERB","MODULATION","DYNAMICS / EQ","UTILITY / CAB","AMP / CAPTURE","PITCH"};
/* RGB888 shared by the controller display and generated website catalog. */
static const unsigned long frost_category_rgb[FROST_CATEGORIES]={
    0xF07830,0xFF525A,0x48D878,0xAD85FF,0x42DDE5,0xF0D452,0xC5D0DE,0xE8B85A,0xB8E34B};
/* UI mapping only. DSP floats retain their original precise values until an
 * actual knob edit; displaying or bypassing must never quantize a snapshot. */
static inline float frost_control_to_dsp(unsigned type,unsigned p,int knob)
{
    const FrostControlEffect *d=frost_control_find(type);const FrostControlParameter *c;
    float t,value;
    if(!d || p>=d->count)return 0.0f;
    c=&d->p[p];if(knob<c->minimum)knob=c->minimum;if(knob>c->maximum)knob=c->maximum;
    /* Appended models store human units directly: exact, invertible. */
    if(type>=FROST_LEGACY_EFFECTS)return (float)knob/(float)c->scale;
    if(type==FROST_GATE)return frost_gate_knob[knob];
    if(type==FROST_DELAY && p==0)return (float)knob;
    if(type==FROST_IR && p)return (float)knob;       /* LOW/HIGH CUT: Hz */
    if((type==FROST_CHORUS || type==FROST_FLANGER) && p==0)return (float)knob/100.0f;
    t=(float)(knob-c->minimum)/(float)(c->maximum-c->minimum);
    if(type==FROST_REVERB && p==1)t=1.0f-t; /* tone clockwise = brighter */
    value=d->dsp[p].minimum+t*(d->dsp[p].maximum-d->dsp[p].minimum);
    if(value<d->dsp[p].minimum)value=d->dsp[p].minimum;
    if(value>d->dsp[p].maximum)value=d->dsp[p].maximum;
    return value;
}
static inline int frost_control_from_dsp(unsigned type,unsigned p,float value)
{
    const FrostControlEffect *d=frost_control_find(type);const FrostControlParameter *c;float t,x;
    if(!d || p>=d->count)return 0;
    c=&d->p[p];if(type==FROST_GATE)return frost_human_gate_value(value);
    /* HIGH CUT stored as 0 (older presets) or below 1 kHz is off: show 20000. */
    if(type==FROST_IR && p==2 && !(value>=1000.0f))return c->maximum;
    if(type>=FROST_LEGACY_EFFECTS || (type==FROST_IR && p)){
        x=value*(float)c->scale;
        if(!(x>c->minimum))return c->minimum;
        if(x>=c->maximum)return c->maximum;
        return (int)(x>=0.0f?x+.5f:x-.5f);
    }
    t=(value-d->dsp[p].minimum)/(d->dsp[p].maximum-d->dsp[p].minimum);
    if(type==FROST_REVERB && p==1)t=1.0f-t;
    x=(float)c->minimum+t*(float)(c->maximum-c->minimum);
    if(!(x>c->minimum))return c->minimum;
    if(x>=c->maximum)return c->maximum;
    return (int)(x>=0.0f?x+.5f:x-.5f);
}
/* Contextual hints for appended models; NULL falls back to generic text. */
static inline const char *frost_library_hint(unsigned type,unsigned p)
{
    const FrostControlEffect *d=frost_control_find(type);const char *name;
    if(!d || p>=d->count)return 0;name=d->p[p].name;
#define FROST_IS(a) (name[0]==(a)[0] && name[1]==(a)[1] && name[2]==(a)[2])
    if(FROST_IS("TAI") && name[3]=='L' && !name[4])return "ON: RINGS OUT WHEN OFF";   /* beta.10 */
    if(type==FROST_RAT && p==1)return "CW BRIGHTER: RAT REVERSED";
    if(type==FROST_DD3 && p==3)return "FREEZE LOOP OF LAST TIME";
    if(type==FROST_DD3 && p==4)return "HOLD = MOMENTARY FOOT";
    if(type==FROST_VB2 && p==3)return "UNLATCH: ON WHILE HELD";
    if(type==FROST_VB2 && p==2)return "TIME TO FULL DEPTH";
    if(type==FROST_NS2 && p==2)return "MUTE: BLOCK ON = SILENCE";
    if(type==FROST_NS2 && p==1)return "HIGHER = SLOWER RELEASE";
    if(type==FROST_MISTRESS && p==3)return "ON: RANGE IS MANUAL SWEEP";
    if(type==FROST_MISTRESS && p==2)return "FEEDBACK / RESONANCE";
    if(type==FROST_M117 && p==3)return "FEEDBACK / RESONANCE";
    if(type==FROST_RV6 && p==3)return "MODE CHANGE RESTARTS TAIL";
    if(type==FROST_OCD && p==3)return "HP: MORE GAIN AND MIDS";
    if(type==FROST_TS && p==3)return "TS9: HOTTER BRIGHTER OUT";
    if(type==FROST_PHASE90 && p==1)return "BLOCK ADDS FEEDBACK";
    if(type==FROST_UNIVIBE && p==2)return "VIBRATO = WET ONLY";
    if(type==FROST_COMPPLUS && p==4)return "ATTACK/RELEASE BY PICKUP";
    if(type==FROST_POG)return p==0?"DRY LEVEL":p==1?"SUB OCTAVE / EXPERIMENTAL":"OCTAVE UP / EXPERIMENTAL";
    if(type==FROST_DROP)return p==0?"DOWN BY HALF STEPS":"100 = DROPPED ONLY";
    if(type==FROST_HIGHPASS || type==FROST_LOWPASS)return p==0?"CUTOFF FREQUENCY HZ":p==1?"0.71 = BUTTERWORTH":"DB PER OCTAVE";
    if(type==FROST_MULTIBAND)return p<3?"250 / 2500 HZ CROSSOVERS":p==3?"1 = NO COMPRESSION":p==4?"ATTACK TIME MS":p==5?"RELEASE TIME MS":p==6?"0 DRY / 100 COMPRESSED":"50 = UNITY";
    if(type==FROST_NS1X)return p==2?"MAXIMUM NOISE ATTENUATION":p==3?"FROST MULTIBAND / NOT MDP":"HIGHER = MORE SUPPRESSION";
    if(type==FROST_AUTOWAH)return p==6?"TEMPO MODE RATE HZ":p==4 || p==5?"HUMAN MODE VOWEL":p==2?"ENVELOPE RELEASE MS":p==7?"0 DRY / 100 WAH":"ENVELOPE DRIVEN WAH";
    if(type==FROST_FOXX)return p==3?"NUMBER OF PHASE STAGES":p==4?"FIXED POSITION IN MANUAL":"VINTAGE FOOT PHASER STYLE";
    if(type==FROST_SL100)return p==0?"CHANNEL / BRT = BRIGHT CAP":p==1?"PREAMP DRIVE":p==5?"UPPER AMP RESPONSE":p==6?"POWER DRIVE AND VOLUME":p==7?"OUTPUT LEVEL":"TONE STACK";
    if(type==FROST_PINGPONG)return p==0?"TIME BETWEEN ECHOES MS":p==3?"STEREO SPREAD / MONO OUT = DELAY":p==4?"ECHOES RING OUT WHEN OFF":"ECHOES ALTERNATE L / R";
    if(type==FROST_TAPE)return p==3?"WOW AND FLUTTER DEPTH":p==4?"HIGHER = DARKER REPEATS":p==5?"RECORDING SATURATION":p==0?"DELAY TIME MS":"MONO TAPE ECHO";
    if(type==FROST_TREBLE_BOOST && p==1)return "HIGHER = MORE BASS CUT";
    if(type==FROST_ROTARY)return p==0?"ROTOR SPEED / GLIDES":p==2?"0 DRUM / 100 HORN":p==1?"MODULATION DEPTH":"50 = UNITY / NOMINAL";
    if(type==FROST_RINGMOD && p==0)return "CARRIER FREQUENCY HZ";
    if(type==FROST_CRUSHER)return p==0?"FEWER = HARSHER":p==1?"SAMPLE RATE KHZ":"0 DRY / 100 CRUSHED";
    if(type==FROST_MUFF && p==1)return "MIDDLE = SCOOPED MIDS";
    if(type==FROST_BOOST)return p==0?"CLEAN GAIN DB":"TREBLE LIFT";
    if(type==FROST_SWELL)return p==0?"FADE-IN TIME MS":p==1?"HIGHER = QUIETER NOTES SWELL":"50 = UNITY / NOMINAL";
    if(type==FROST_ACOUSTIC)return p==0?"BODY RESONANCE":p==1?"PICK ATTACK / UPPER HARMONICS":p==2?"BODY MODEL / CLEAN INPUT":p==3?"0 DRY / 100 ACOUSTIC":"50 = UNITY / NOMINAL";
    if(type>=FROST_DELUXE && type<=FROST_5150)return p==0?"PREAMP DRIVE":p==4?(type==FROST_AC30?"HIGHER = DARKER POWER TONE":"UPPER AMP RESPONSE"):p==5?"POWER DRIVE AND VOLUME":p==6?"OUTPUT TRIM / 50 = NOMINAL":"AMP HEAD ONLY / ADD IR CAB";
    if(type==FROST_GE7)return p==7?"OUTPUT LEVEL DB":"BAND GAIN DB / 0 = FLAT";
    if(type==FROST_CALI_EQ)return p==5?"OUTPUT LEVEL DB":"BAND GAIN DB / 0 = FLAT";
    if(type==FROST_FCM)return p==0?"CAPTURE SLOT / 0 = DEFAULT":p==1?"INPUT DRIVE DB / 0 = AS CAPTURED":"50 = UNITY / APPROX OF NAM";
    if(FROST_IS("TIM") || (FROST_IS("RAT") && d->p[p].unit[0]=='M'))return "DELAY TIME IN MS";
    if(d->p[p].unit[0]=='H')return "SPEED IN HZ";
    if(FROST_IS("LEV") || FROST_IS("VOL") || FROST_IS("OUT"))return "50 = UNITY / NOMINAL";
    if(FROST_IS("E.L") || FROST_IS("ECH"))return "EFFECT LEVEL / DRY STAYS";
    if(FROST_IS("MIX"))return "0 DRY / HIGHER ADDS WET";
    if(FROST_IS("TON") || FROST_IS("TRE") || FROST_IS("FIL"))return "HIGHER = BRIGHTER";
    if(FROST_IS("F.B") || (FROST_IS("INT") && type==FROST_DM2))return "HIGHER = MORE REPEATS";
    if(FROST_IS("DRI") || FROST_IS("GAI") || FROST_IS("DIS"))return "HIGHER = MORE DRIVE";
    if(FROST_IS("SUS") || FROST_IS("SEN"))return "HIGHER = MORE SQUASH";
    if(FROST_IS("BLE"))return "0 DRY / 100 COMPRESSED";
    if(FROST_IS("THR"))return "HIGHER = MORE SUPPRESSION";
    if(FROST_IS("WAV"))return "0 TRIANGLE / 100 SQUARE";
    if(FROST_IS("DEP") || FROST_IS("WID") || FROST_IS("RAN") || FROST_IS("INT"))return "HIGHER = DEEPER";
    if(FROST_IS("MAN"))return "SWEEP CENTRE POSITION";
    if(FROST_IS("REG"))return "FEEDBACK / RESONANCE";
    if(FROST_IS("DWE"))return "SEND INTO SPRINGS";
    return "PRESS TO CONFIRM";
#undef FROST_IS
}
static inline const char *frost_control_hint(unsigned type,unsigned p)
{
    if(type>=FROST_LEGACY_EFFECTS && type<FROST_EFFECTS)return frost_library_hint(type,p);
    if(type==FROST_GAIN)return "25 = UNITY GAIN";
    if(type==FROST_GATE)return "HIGHER = MORE GATING";
    if(type==FROST_EQ)return "50 = FLAT BAND LEVEL";
    if(type==FROST_IR || (type==FROST_OVERDRIVE && p==2))return "50 = UNITY VOLUME";
    if(type==FROST_OVERDRIVE)return p==0?"HIGHER = MORE DRIVE":"HIGHER = BRIGHTER";
    if((type==FROST_DELAY || type==FROST_CHORUS || type==FROST_REVERB) && p==2)return "0 DRY / 100 WET";
    if(type==FROST_DELAY)return p==0?"DELAY TIME IN MS":"HIGHER = MORE REPEATS";
    if((type==FROST_CHORUS || type==FROST_FLANGER) && p==0)return "MODULATION SPEED IN HZ";
    if(type==FROST_FLANGER && p==2)return "SIGNED RESONANCE";
    if(type==FROST_REVERB)return p==0?"HIGHER = LONGER TAIL":"HIGHER = BRIGHTER";
    return "HIGHER = DEEPER SWEEP";
}
#endif
