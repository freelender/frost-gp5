#include <stdint.h>
#define FROST_HOST_FAST_CONTROL 1
static unsigned qp_hook(const uint8_t *,unsigned);
#define FROST_HOST_REQUEST_HOOK qp_hook
#include "ir_files_host.c"
#include "../firmware/controller/factory_bank.h"
static uint8_t qp_last[FROST_QUICK_MAX_BYTES];
static unsigned qp_bytes,qp_quick,qp_status,qp_old,qp_ir,qp_hide,qp_ir_write,qp_ir_clear,qp_prepare;
static unsigned qp_hook(const uint8_t *p,unsigned n)
{
    unsigned cmd=(unsigned)p[0]|(unsigned)p[1]<<8,i;
    if(cmd==FROST_QUICK_COMMAND){
        if(n==16u){++qp_prepare;return 0;}
        if(n>16u){if(qp_hide==2u){drop_count=2;drop_mode=1;qp_hide=0;}++qp_quick;qp_bytes=n;for(i=0;i<n;++i)qp_last[i]=p[i];}else ++qp_status;
        return qp_hide;
    }
    if(cmd==FROST_PRESET_COMMAND)++qp_old;
    if(cmd==FROST_IR_COMMAND){++qp_ir;if(frost_wire_get32(p+12)==FROST_IR_WRITE)++qp_ir_write;if(frost_wire_get32(p+12)==FROST_IR_CLEAR)++qp_ir_clear;}
    return 0;
}
API void qp_reset_trace(void){qp_bytes=qp_quick=qp_status=qp_old=qp_ir=qp_ir_write=qp_ir_clear=qp_prepare=0;}
API unsigned qp_count(unsigned i){unsigned v[]={qp_quick,qp_status,qp_old,qp_ir,qp_ir_write,qp_ir_clear,qp_prepare};return i<7u?v[i]:0;}
API unsigned qp_packet(uint8_t *p){unsigned i;for(i=0;i<qp_bytes;++i)p[i]=qp_last[i];return qp_bytes;}
API unsigned qp_dispatch(const uint8_t *p,unsigned n,uint8_t *r)
{frost_quick_preset_command(&preset_transfer,&slot_protocol,&frost_chain,p,n,r,frost_quick_builtin);return 32;}
API unsigned qp_fast_dispatch(const uint8_t *p,unsigned n,uint8_t *r)
{unsigned i;response_bytes=0;frost_slot_command_ram(p,n);for(i=0;i<response_bytes;++i)r[i]=response[i];return response_bytes;}
API void qp_poison_pending_assets(void)
{unsigned i,j;for(i=0;i<2u;++i){frost_chain.pending_preset.ir_taps[i]=512u;for(j=0;j<FROST_IR_MAX;++j)frost_chain.pending_preset.ir_coefficients[i][j]=9.0f;}}
API unsigned qp_unused_assets_zero(void)
{unsigned i,j,used=0;for(i=0;i<6u;++i)used+=frost_chain.pending_preset.types[i]==FROST_IR;
 for(i=used;i<2u;++i){if(frost_chain.pending_preset.ir_taps[i])return 0;for(j=0;j<FROST_IR_MAX;++j)if(frost_chain.pending_preset.ir_coefficients[i][j]!=0.0f)return 0;}return 1;}
API void qp_drop_mutation(unsigned count){drop_count=0;qp_hide=count?2u:0u;}
API void qp_hide_quick(unsigned hide){qp_hide=hide;preset_client.quick_known=0;}
API void qp_force_legacy(void){preset_client.quick_known=2;}
API void qp_step(void){ge_tick();ge_audio(1);++now;ge_tick_draw();}
API unsigned qp_ir_generation(void){return frost_ir_generation;}
API unsigned qp_interval(void){return fw_editor_interval();}
API unsigned qp_rejected(void){return rejected;}
API unsigned qp_record(unsigned i,uint8_t *p){unsigned k;if(i>=20u)return 0;for(k=0;k<264u;++k)p[k]=frost_factory_records[i][k];return 264;}
API unsigned qp_state(void){return preset_client.state;}
API unsigned qp_factory(void)
{
    unsigned g,i,k;char name[8];
    for(g=0;g<2u;++g){
        for(k=0;k<3960u;++k)store_buf[k]=0;
        for(i=g*15u;i<20u && i<(g+1u)*15u;++i)for(k=0;k<264u;++k)store_buf[(i%15u)*264u+k]=frost_factory_records[i][k];
        store_group_name(name,'p','s','t',g);if(!store_write_group(FROST_PST_FIRM,name,3960))return 0;
    }
    return 1;
}

API void qp_old_capabilities(void){preset_client.quick_known=1;preset_client.quick_caps=0;}
API void qp_probe_again(void){preset_client.quick_known=0;}

API void qp_old_probe_reply(void)
{
    uint8_t r[32]={0};r[0]=0x4d;r[1]=0x7f;r[2]=32;
    frost_wire_put32(r+4,1);frost_wire_put32(r+16,editor.revision);
    preset_client.awaiting=1;(void)fw_editor_reply(r,32);
}

API unsigned qp_ir_phase(void){return frost_ir_rx.state;}
API unsigned qp_ir_next(void){return frost_ir_rx.next;}
API unsigned qp_pending_preset(void){return midi_preset;}
API void qp_preset_home(void){globals.home=HOME_PRESET;view=PRESET_HOME;}

API unsigned qp_phase(void){return frost_chain.transition;}
API unsigned qp_owner(void){return frost_chain.recall_token;}
API float qp_gate(unsigned phase,unsigned position,unsigned sample,unsigned stereo)
{float w[64],s[64];unsigned i;for(i=0;i<64;++i){w[i]=1.0f;s[i]=0.5f;}frost_recall_gate(w,s,64,phase,position,stereo);return stereo?s[sample]:w[sample];}

API unsigned qp_gate_invalid(unsigned stereo)
{union {uint32_t u;float f;} v;float w[1],s[1];v.u=0x7fc00000u;w[0]=s[0]=v.f;
 frost_recall_gate(w,s,1,FROST_ROUTE_QUIET,0,stereo);return !(w[0]==w[0]) && (!stereo || !(s[0]==s[0]));}
