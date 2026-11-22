#include "../effects/common/frost_dsp.c"
#include "../firmware/dsp/global_eq.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API void eq_init(void){frost_global_eq_init();}
API void eq_command(const uint8_t *q,unsigned n,uint8_t *reply){frost_global_eq_command(q,n,reply);}
API int eq_process(float *x,unsigned n){unsigned i;for(i=0;i<n;i+=64){unsigned m=n-i<64?n-i:64;if(!frost_global_eq_block(x+i,m))return 0;}return 1;}
API unsigned eq_active(void){return frost_global_eq.active;}
API unsigned eq_state_bytes(void){return sizeof(FrostGlobalEq);}
