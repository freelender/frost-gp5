#include <stdint.h>
static uint32_t fake_cycle;
static uint32_t profile_clock(void){uint32_t c=fake_cycle;fake_cycle+=100;return c;}
#define FROST_PROFILE_CLOCK() profile_clock()
#include "timing_host.c"
API void slot_note(unsigned slot,unsigned type,uint32_t begin,uint32_t end)
{frost_slot_load_note(slot,type,begin,end);}
API void slot_profile_block(unsigned fallback)
{FROST_PROFILE_BLOCK(fallback);}
/* 31a: an aligned route fade runs stage-major (profiled); a fade phase ending
 * inside the block (here: the last fade-in sample) still takes the fallback. */
API void slot_guarded(unsigned transition)
{float x[64]={0},y[64];if(transition){frost_chain.transition=FROST_ROUTE_IN;frost_chain.fade_position=FROST_ROUTE_FADE-1u;}
 frost_chain_process_guarded(&frost_chain,x,y,64,0);}
API void slot_clock(uint32_t c){fake_cycle=c;}
API void slot_reverse(void)
{unsigned i;for(i=0;i<6;++i)frost_chain.order[i]=5-i;}
API void slot_inplace(uint8_t *p,unsigned n){frost_slot_load_read(&frost_load,p,n,p);}
