#include <stdint.h>
#define FROST_HOST_FAST_CONTROL 1
static unsigned qp_hook(const uint8_t *,unsigned);
#define FROST_HOST_REQUEST_HOOK qp_hook
#include "ir_files_host.c"
#include "../firmware/controller/factory_bank.h"
static uint8_t qp_last[FROST_QUICK_MAX_BYTES];
static unsigned qp_bytes,qp_quick,qp_status,qp_old,qp_ir,qp_hide;
static unsigned qp_hook(const uint8_t *p,unsigned n)
{
    unsigned cmd=(unsigned)p[0]|(unsigned)p[1]<<8,i;
    if(cmd==FROST_QUICK_COMMAND){
        if(n>12u){++qp_quick;qp_bytes=n;for(i=0;i<n;++i)qp_last[i]=p[i];}else ++qp_status;
        return qp_hide;
    }
    if(cmd==FROST_PRESET_COMMAND)++qp_old;
    if(cmd==FROST_IR_COMMAND)++qp_ir;
    return 0;
}
API void qp_reset_trace(void){qp_bytes=qp_quick=qp_status=qp_old=qp_ir=0;}
API unsigned qp_count(unsigned i){unsigned v[]={qp_quick,qp_status,qp_old,qp_ir};return i<4u?v[i]:0;}
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
