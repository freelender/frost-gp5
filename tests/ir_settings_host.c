/* Real settings-save path with both IR halves and simulated flash. */
#define FROST_IR_LINK 1
#define FROST_IRL_HOST 1
#define REG(a) 0u
#include "slot_editor_host.c"
API void is_init(void)
{
    irl.started=0;irl_flash_depth=irl_flash_writes=0;
    irl_host_pause_fail=irl_host_resume_fail=irl_host_controls=irl_host_pause_misses=0;
    ge_init();
}
API void is_home(unsigned home){globals.home=home;settings_dirty=1;settings_leave();}
API void is_fail_pause(unsigned on){irl_host_pause_fail=on;}
API void is_miss_pause(unsigned n){irl_host_pause_misses=n;}
API unsigned is_state(unsigned i)
{
    unsigned v[]={FROST_IRL->quiet,irl_flash_depth,irl_flash_writes,irl_host_controls,settings_dirty};
    return i<5?v[i]:0;
}
/* IR input the main loop sees: streams mask and a constant Q23 level. */
API void is_ir_input(unsigned streams,int level)
{
    unsigned n;irl.streams=streams;
    for(n=0;n<FROST_IRL_STREAMS*FROST_IRL_BLOCK;++n)irl_in[FROST_IRL_W_AUDIO+n]=(n&1u)?-level:level;
}
