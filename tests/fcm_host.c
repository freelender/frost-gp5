/* Host fixture for the EXPERIMENTAL AMP/FCM block: received-record transfer
 * (fcm_transfer.c), CRC, default fallback, model switching and
 * block/per-sample identity. */
#include "../effects/fcm/fcm_effect.c"
#include "../effects/fcm/fcm_transfer.c"
#include "../effects/common/frost_dsp.c"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static FrostModulation ws;static FrostFcm st;
API unsigned fcm_record_bytes(void){return (unsigned)sizeof(FrostFcmRecord);}
API unsigned fcm_model_bytes(void){return (unsigned)sizeof(FcmiModel);}
API void fcm_rx_reset(void){unsigned i;uint8_t *b=(uint8_t *)&frost_fcm_rx;for(i=0;i<sizeof(frost_fcm_rx);++i)b[i]=0;}
API void fcm_command(const uint8_t *p,unsigned n,uint8_t *reply){frost_fcm_command(p,n,reply);}
API void fcm_reset_state(void){st.ring=&ws;frost_fcm_reset(&st);}
API unsigned fcm_loaded(void){return ((const uint32_t *)(const void *)&ws.line)[0];}
API unsigned fcm_from_rx(void){return ((const uint32_t *)(const void *)&ws.line)[2];}
API void fcm_block(const float *x,float *y,unsigned n,unsigned chunk,const float *p)
{unsigned i;for(i=0;i<n;i+=chunk){unsigned m=n-i<chunk?n-i:chunk;frost_fcm_block(&st,x+i,y+i,m,p,0);}}
API void fcm_block_ramp(const float *x,float *y,unsigned n,const float *p,const float *p0)
{unsigned i;for(i=0;i<n;i+=64){unsigned m=n-i<64?n-i:64;frost_fcm_block(&st,x+i,y+i,m,p,p0);}}
API void fcm_samples(const float *x,float *y,unsigned n,const float *p)
{unsigned i;for(i=0;i<n;++i)y[i]=frost_fcm_sample(&st,x[i],p);}
API uint32_t fcm_crc(const void *d,unsigned n){return frost_fcm_crc32(d,n);}
