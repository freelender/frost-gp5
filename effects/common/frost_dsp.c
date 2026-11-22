#include "frost_dsp.h"
#include "../../engine/hot_code.h"
/* Shared kernels used by many models; kept out of line to bound DSP flash
 * size (inlining them into every model overflowed the GP-5 DSP region). */
/* 26a: one-entry memo. frost_exp2 is pure, so an equal input returns the
 * identical result (equal floats differ only as +/-0, which both map to 1).
 * Per-sample callers with a settled knob (e.g. DS-1 DIST taper) hit it.
 * Initial key 0 -> exact exp2(0) = 1. NaN never compares equal. */
static float frost_exp2_key=0.0f,frost_exp2_value=1.0f;
FROST_HOT FROST_CORE FROST_NOINLINE FROST_AUDIO_RAM float frost_exp2(float x)
{
    union {float f;uint32_t u;} v;int n;float f,p,in=x;
    if(x==frost_exp2_key)return frost_exp2_value;
    x=frost_clampf(x,-60.0f,60.0f);n=(int)x;if((float)n>x)--n;f=x-(float)n;
    p=1.0f+f*(0.69314718f+f*(0.24022651f+f*(0.05550411f+f*(0.00961813f+f*0.00133336f))));
    v.u=(uint32_t)(n+127)<<23;
    frost_exp2_key=in;frost_exp2_value=p*v.f;return frost_exp2_value;
}
FROST_HOT FROST_NOINLINE FROST_LARGE_ONLY float frost_log2(float x)
{
    union {float f;uint32_t u;} v;float m,e;
    if(!(x>1e-18f))return -60.0f;
    v.f=x;e=(float)((int)((v.u>>23)&255u)-127);v.u=(v.u&0x007fffffu)|0x3f800000u;m=v.f;
    /* log2(m) on [1,2): (m-1)/(m+1) series */
    {float r=(m-1.0f)/(m+1.0f),r2=r*r;return e+2.88539008f*r*(1.0f+r2*(0.33333333f+r2*(0.2f+r2*0.14285714f)));}
}
FROST_HOT FROST_NOINLINE FROST_LARGE_ONLY float frost_sin01(float phase)
{
    float t=phase<0.25f?4.0f*phase:phase<0.75f?2.0f-4.0f*phase:4.0f*phase-4.0f,t2=t*t;
    return t*(1.57079633f+t2*(-0.64596409f+t2*(0.07969262f+t2*(-0.00468175f+t2*0.00015957f))));
}
FROST_HOT FROST_NOINLINE FROST_LARGE_ONLY float frost_allpass_coef(float hz)
{float w=FROST_PI*hz*(1.0f/FROST_FS);w=w*(1.0f+w*w*(0.33333333f+w*w*0.13333333f));return (w-1.0f)/(w+1.0f);}
FROST_HOT FROST_CORE FROST_NOINLINE FROST_AUDIO_RAM float frost_soft_clip(float x)
{
    if(x>3.0f)return 1.0f;if(x< -3.0f)return -1.0f;
    {float x2=x*x;return x*(27.0f+x2)/(27.0f+9.0f*x2);}
}



#ifdef FROST_OVERLAYS
/* 31i: absolute call targets for overlay modules (see engine/overlay_far_on.h). */
/* The pointers name LARGE forwarders, so the helpers' own addresses are never
 * taken (that reorders GCC's output near the boot-sensitive region). */
FROST_LARGE_ONLY float frost_ovl_soft_clip(float x);FROST_LARGE_ONLY float frost_ovl_soft_clip(float x){return frost_soft_clip(x);}
FROST_LARGE_ONLY float frost_ovl_exp2(float x);FROST_LARGE_ONLY float frost_ovl_exp2(float x){return frost_exp2(x);}
FROST_LARGE_ONLY float frost_ovl_log2(float x);FROST_LARGE_ONLY float frost_ovl_log2(float x){return frost_log2(x);}
FROST_LARGE_ONLY float frost_ovl_sin01(float x);FROST_LARGE_ONLY float frost_ovl_sin01(float x){return frost_sin01(x);}
FROST_LARGE_ONLY float frost_ovl_allpass_coef(float x);FROST_LARGE_ONLY float frost_ovl_allpass_coef(float x){return frost_allpass_coef(x);}
float (*volatile frost_far_soft_clip)(float)=frost_ovl_soft_clip;
float (*volatile frost_far_exp2)(float)=frost_ovl_exp2;
float (*volatile frost_far_log2)(float)=frost_ovl_log2;
float (*volatile frost_far_sin01)(float)=frost_ovl_sin01;
float (*volatile frost_far_allpass_coef)(float)=frost_ovl_allpass_coef;
#endif
