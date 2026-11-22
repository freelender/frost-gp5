#include "slot_protocol_host.c"
static struct {uint32_t before,words[1024],after;} painted;
API void stack_init(void)
{unsigned i;protocol_init();painted.before=0x12345678;painted.after=0xabcdef01;
 for(i=0;i<1024;++i)painted.words[i]=FROST_STACK_PATTERN;}
API void stack_touch(unsigned word){if(word<1024)painted.words[word]=0;}
API unsigned stack_word(unsigned word){return word<1024?painted.words[word]:0;}
API unsigned stack_guard(void){return painted.before==0x12345678 && painted.after==0xabcdef01;}
API void stack_read(uint32_t sp,const uint8_t *p,unsigned n,uint8_t *reply)
{frost_stack_read(painted.words,sp,1,2,p,n,reply);}
API void stack_inplace(uint32_t sp,uint8_t *p,unsigned n)
{frost_stack_read(painted.words,sp,1,2,p,n,p);}
