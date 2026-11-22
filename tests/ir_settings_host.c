/* Real settings-save path with both IR halves and simulated flash. */
#define FROST_IR_LINK 1
#define FROST_IRL_HOST 1
#define REG(a) 0u
#include "slot_editor_host.c"
API void is_init(void)
{
    irl.started=0;irl_flash_depth=irl_flash_writes=0;
    irl_host_pause_fail=irl_host_resume_fail=irl_host_controls=0;
    ge_init();
}
API void is_home(unsigned home){globals.home=home;settings_dirty=1;settings_leave();}
API void is_fail_pause(unsigned on){irl_host_pause_fail=on;}
API unsigned is_state(unsigned i)
{
    unsigned v[]={FROST_IRL->quiet,irl_flash_depth,irl_flash_writes,irl_host_controls,settings_dirty};
    return i<5?v[i]:0;
}
