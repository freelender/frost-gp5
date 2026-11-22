#include "noise_gate.h"
#include "../../engine/hot_code.h"
/* Fixed 44.1 kHz; hysteretic envelope gate. State owned by the caller. */
FROST_HOT FROST_CORE float frost_gate_sample(float input,float threshold,float *envelope,float *gain,uint32_t *open)
{
    float absolute=input<0.0f?-input:input,target;
    *envelope+=(absolute>*envelope?0.022421f:0.0004534f)*(absolute-*envelope);
    if(*envelope>threshold)*open=1;
    else if(*envelope<threshold*0.5f)*open=0;
    target=(float)*open;
    *gain+=(target>*gain?0.011274f:0.0011331f)*(target-*gain);
    return input * *gain;
}
