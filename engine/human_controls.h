#ifndef FROST_HUMAN_CONTROLS_H
#define FROST_HUMAN_CONTROLS_H
/* Generic pedal UI only. Wire floats, legacy diagnostic integers and preset
 * schemas retain their existing units. Knob numbers are not amplitude percent. */
#define FROST_HUMAN_GAIN {{"GAIN","",0,100,1,25,1,0}}
#define FROST_HUMAN_GATE {{"THRESH","",0,100,1,50,1,0}}
#define FROST_HUMAN_EQ {{"BASS","",0,100,1,50,1,0},{"MID","",0,100,1,50,1,0},{"TREBLE","",0,100,1,50,1,0}}
#define FROST_HUMAN_IR {{"VOL","",0,100,1,80,1,0},{"LOW CUT","HZ",0,400,10,0,1,0},{"HIGH CUT","HZ",1000,20000,250,20000,1,0}}
#define FROST_HUMAN_OVERDRIVE {{"GAIN","",0,100,1,16,1,0},{"TONE","",0,100,1,50,1,0},{"VOL","",0,100,1,25,1,0}}
#define FROST_HUMAN_DELAY {{"TIME","MS",1,500,5,250,1,0},{"REPEAT","",0,100,1,37,1,0},{"MIX","",0,100,1,30,1,0}}
#define FROST_HUMAN_CHORUS {{"RATE","HZ",5,500,5,80,100,0},{"DEPTH","",0,100,1,50,1,0},{"MIX","",0,100,1,30,1,0}}
#define FROST_HUMAN_FLANGER {{"RATE","HZ",5,500,5,30,100,0},{"DEPTH","",0,100,1,50,1,0},{"RESO","",-100,100,1,56,1,0}}
#define FROST_HUMAN_REVERB {{"DECAY","",0,100,1,79,1,0},{"TONE","",0,100,1,50,1,0},{"MIX","",0,100,1,25,1,0}}
/* Threshold = 10^(-5 + 4 * knob/100). 0..100 spans -100..-20 dBFS
 * amplitude; one knob step is 0.8 dB. Constant data avoids controller libm. */
static const float frost_gate_knob[101]={
    1.000000000e-5f,1.096478196e-5f,1.202264435e-5f,1.318256739e-5f,1.445439771e-5f,
    1.584893192e-5f,1.737800829e-5f,1.905460718e-5f,2.089296131e-5f,2.290867653e-5f,
    2.511886432e-5f,2.754228703e-5f,3.019951720e-5f,3.311311215e-5f,3.630780548e-5f,
    3.981071706e-5f,4.365158322e-5f,4.786300923e-5f,5.248074602e-5f,5.754399373e-5f,
    6.309573445e-5f,6.918309709e-5f,7.585775750e-5f,8.317637711e-5f,9.120108394e-5f,
    1.000000000e-4f,1.096478196e-4f,1.202264435e-4f,1.318256739e-4f,1.445439771e-4f,
    1.584893192e-4f,1.737800829e-4f,1.905460718e-4f,2.089296131e-4f,2.290867653e-4f,
    2.511886432e-4f,2.754228703e-4f,3.019951720e-4f,3.311311215e-4f,3.630780548e-4f,
    3.981071706e-4f,4.365158322e-4f,4.786300923e-4f,5.248074602e-4f,5.754399373e-4f,
    6.309573445e-4f,6.918309709e-4f,7.585775750e-4f,8.317637711e-4f,9.120108394e-4f,
    1.000000000e-3f,1.096478196e-3f,1.202264435e-3f,1.318256739e-3f,1.445439771e-3f,
    1.584893192e-3f,1.737800829e-3f,1.905460718e-3f,2.089296131e-3f,2.290867653e-3f,
    2.511886432e-3f,2.754228703e-3f,3.019951720e-3f,3.311311215e-3f,3.630780548e-3f,
    3.981071706e-3f,4.365158322e-3f,4.786300923e-3f,5.248074602e-3f,5.754399373e-3f,
    6.309573445e-3f,6.918309709e-3f,7.585775750e-3f,8.317637711e-3f,9.120108394e-3f,
    1.000000000e-2f,1.096478196e-2f,1.202264435e-2f,1.318256739e-2f,1.445439771e-2f,
    1.584893192e-2f,1.737800829e-2f,1.905460718e-2f,2.089296131e-2f,2.290867653e-2f,
    2.511886432e-2f,2.754228703e-2f,3.019951720e-2f,3.311311215e-2f,3.630780548e-2f,
    3.981071706e-2f,4.365158322e-2f,4.786300923e-2f,5.248074602e-2f,5.754399373e-2f,
    6.309573445e-2f,6.918309709e-2f,7.585775750e-2f,8.317637711e-2f,9.120108394e-2f,
    1.000000000e-1f
};
static inline int frost_human_gate_value(float value)
{
    unsigned lo=0,hi=100;
    if(!(value>frost_gate_knob[0]))return 0;
    if(value>=frost_gate_knob[100])return 100;
    while(hi-lo>1){unsigned mid=(lo+hi)/2;if(value<frost_gate_knob[mid])hi=mid;else lo=mid;}
    return (int)((float)lo+(value-frost_gate_knob[lo])/(frost_gate_knob[hi]-frost_gate_knob[lo])+0.5f);
}
#endif
