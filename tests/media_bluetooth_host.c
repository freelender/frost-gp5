/* BLE register stand-ins: exercise the real bounded stop/resume adapter. */
#include <stdint.h>
enum {BLE_OFF,BLE_ADVERTISING,BLE_CONNECTED};
#define BLE 0u
#define BT 32u
static uint32_t regs[64];
#define R32(a) regs[(a)/4u]
#define BIT_SET(a,mask,v) (R32(a)=(R32(a)&~(mask))|(v))
static struct {unsigned state,disconnects,last_reason,midi_notify;} ble;
static struct {unsigned pending,armed;} ev;
static struct {unsigned rxi;} cn;
static unsigned frost_ble_ready,irq_on,core_calls,adv_calls,schedules,tx_calls;
static unsigned portq_head,portq_n,sxn,in_sx;
static uint8_t cccd[2];
static unsigned gp5_irq_global_state(void){return irq_on;}
static void gp5_irq_global(int on){irq_on=(unsigned)on;}
static void clear_entry(void){ev.armed=0;}
static void tx_reset(void){++tx_calls;}
static void core_setup(void){++core_calls;R32(BLE+0x0C)=0x33A;}
static void adv_setup(void){++adv_calls;}
static unsigned slot_now(void){return 42;}
static void adv_schedule(unsigned slot){(void)slot;++schedules;ev.pending=1;}
#include "firmware/controller/media_bluetooth.inc"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void init(unsigned ready,unsigned state,unsigned irq)
{
    unsigned i;for(i=0;i<64;++i)regs[i]=0;
    frost_ble_ready=ready;ble.state=state;irq_on=irq;
    ble.disconnects=ble.last_reason=0;ble.midi_notify=cccd[0]=cccd[1]=1;
    core_calls=adv_calls=schedules=tx_calls=0;
    portq_head=portq_n=sxn=in_sx=ev.armed=ev.pending=1;
    R32(BLE)=1u<<8;R32(BLE+0x0C)=0x33A;R32(BT+0x0C)=0x200;
}
API void enable(int on){gp5_bt_set_enabled(on);}
API unsigned get(unsigned i)
{
    unsigned v[]={ble.state,ble.disconnects,ble.last_reason,irq_on,core_calls,adv_calls,schedules,tx_calls,
        portq_head|portq_n|sxn|in_sx|cccd[0]|cccd[1]|ble.midi_notify,ev.pending,ev.armed,
        R32(BLE),R32(BLE+0x0C),R32(BT+0x0C)};
    return i<sizeof(v)/sizeof(v[0])?v[i]:0;
}
