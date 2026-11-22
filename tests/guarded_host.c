#include "chain_host.c"
static unsigned amp_calls,amp_resets,fire_at=UINT32_MAX;
static float guarded_amp(void *context,float x)
{unsigned n=amp_calls++;(void)context;if(n==fire_at)return 0.0f/0.0f;return x*0.5f;}
static void guarded_reset(void *context){(void)context;++amp_resets;}
static const FrostAmpStage stage={guarded_amp,guarded_reset,0};
API void guarded_init(void){chain_init();amp_calls=amp_resets=0;fire_at=UINT32_MAX;}
API void guarded_fire(unsigned sample){fire_at=sample;amp_calls=amp_resets=0;}
API unsigned guarded_process(const float *x,float *y,unsigned n,unsigned amp)
{return (unsigned)frost_chain_process_guarded(&state,x,y,n,amp?&stage:0);}
API void guarded_legacy(const float *x,float *y,unsigned n,unsigned amp)
{frost_chain_process_with_amp(&state,x,y,n,amp?&stage:0);}
API unsigned guarded_calls(unsigned which){return which?amp_resets:amp_calls;}
API void guarded_saturate(void){state.faults=UINT32_MAX;}
API unsigned guarded_started(void){return state.started;}
API unsigned guarded_transition(void)
{unsigned types[6]={FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_EMPTY,FROST_EMPTY};
 unsigned order[6]={1,0,2,3,4,5};return frost_chain_request_route(&state,types,order,3);}
API unsigned guarded_position(void){return state.fade_position;}
