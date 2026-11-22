/* Host fixture for the stage 2c tuner (effects/tuner). */
#include "../effects/tuner/tuner.c"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static float mem[(FROST_TUNER_BYTES+3)/4];static FrostTuner tn;
API void tu_start(void){frost_tuner_start(&tn,mem,sizeof mem);}
API unsigned tu_bytes(void){return FROST_TUNER_BYTES;}
API void tu_block(const float *x,unsigned n){frost_tuner_block(&tn,x,n);}
API float tu_freq(void){return tn.freq;}
API float tu_clarity(void){return tn.clarity;}
API unsigned tu_seq(void){return tn.seq;}
