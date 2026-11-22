#ifndef FROST_OVERDRIVE_H
#define FROST_OVERDRIVE_H
/* Native 44.1 kHz baseline. Three caller-owned floats, no workspace/heap.
 * Parameters: drive multiplier [1,20], tone [0,1], output level [0,2]. */
typedef struct { float previous, highpass, tone; } FrostOverdrive;
void frost_overdrive_reset(FrostOverdrive *s);
float frost_overdrive_sample(FrostOverdrive *s,float input,const float *parameters);
#endif
