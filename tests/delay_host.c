#include "framework_host.c"
static struct {unsigned before;FrostDelay delay;unsigned after;} memory[2];
API int dl_bind(unsigned c){return frost_chain_bind_delay(&chains[c],&memory[c].delay);}
API int dl_unbind(unsigned c){return frost_chain_bind_delay(&chains[c],0);}
API void dl_poison(unsigned c)
{unsigned i;memory[c].before=0x12345678u;memory[c].after=0x87654321u;
 for(i=0;i<FROST_DELAY_SAMPLES;++i)memory[c].delay.line.half[i]=0x7e00u;}
API int dl_guard(unsigned c){return memory[c].before==0x12345678u && memory[c].after==0x87654321u;}
API void dl_reset(unsigned c){frost_delay_reset(&memory[c].delay);}
API float dl_sample(unsigned c,float x,float time,float feedback,float mix)
{float p[3]={time,feedback,mix};return frost_delay_sample(&memory[c].delay,x,p);}
API unsigned dl_valid(unsigned c){return memory[c].delay.valid;}
API unsigned dl_position(unsigned c){return memory[c].delay.position;}
API float dl_distance(unsigned c){return memory[c].delay.distance;}
API unsigned dl_bytes(void){return sizeof(FrostDelay);}
API unsigned dl_capacity(void){return FROST_DELAY_SAMPLES;}
API unsigned dl_word(unsigned c,unsigned index)
{return memory[c].delay.line.half[index];}
API unsigned dl_half_store(float x){return frost_half_store(x);}
API float dl_half_load(unsigned h){return frost_half_load((uint16_t)h);}
API int dl_policy(unsigned c,unsigned slot,unsigned policy){return frost_chain_set_bypass_policy(&chains[c],slot,policy);}
