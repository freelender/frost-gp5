#ifndef FROST_NOISE_GATE_H
#define FROST_NOISE_GATE_H
#include <stdint.h>
float frost_gate_sample(float input,float threshold,float *envelope,float *gain,uint32_t *open);
#endif
