#include <stdint.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static uint8_t captured[404];static uint64_t micro,base;static uint32_t starts[51],begin_us[51],end_us[51],phase;
static unsigned captured_n,calls,sizes[51],fail_call;static unsigned want_active;
static uint32_t gp5_ms(void){uint32_t value=(uint32_t)(micro/1000u);micro+=37;return value;}
static uint32_t gp5_uart0_send(const uint8_t *p,uint32_t n,uint32_t timeout)
{unsigned i;(void)timeout;if(calls<51){starts[calls]=(uint32_t)(micro/1000u);begin_us[calls]=(uint32_t)(micro-base);sizes[calls]=n;}++calls;
 if(fail_call && calls==fail_call)return 0;
 for(i=0;i<n && captured_n<404;++i)captured[captured_n++]=p[i];micro+=n*22u;end_us[calls-1]=(uint32_t)(micro-base);return n;}
API unsigned paced_send(unsigned len,uint32_t start,unsigned fail)
{uint8_t tx[404];unsigned i;if(len>404)return 0;
 for(i=0;i<len;++i)tx[i]=(uint8_t)(i*37u+11u);
 captured_n=calls=0;fail_call=fail;base=micro=(uint64_t)start*1000u+phase;want_active=1;
#include "../firmware/controller/paced_uart.inc"
 return len;}
API void paced_phase(unsigned n){phase=n%1000u;}
API unsigned paced_value(unsigned field,unsigned i)
{return field==0?calls:field==1?captured_n:field==2?want_active:field==3?(i<51?sizes[i]:0):field==4?(i<404?captured[i]:0):field==5?(i<51?starts[i]:0):field==7?(i<51?begin_us[i]:0):field==8?(i<51?end_us[i]:0):(uint32_t)(micro/1000u);}
