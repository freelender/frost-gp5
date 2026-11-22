#include "chorus.h"
#include "../../engine/hot_code.h"
FROST_HOT float frost_chorus_sample(FrostModulation *s,float input,const float *parameters)
{return frost_modulation_sample(s,input,parameters,0);}
