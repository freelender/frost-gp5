#include <stdint.h>
#include "gp5_firmware.h"
static uint32_t GP5_CYCLES_LAST,GP5_CYCLES_WORST,GP5_LATE_BLOCKS;
#define GP5_BLOCK 64u
#define GP5_FULL_SCALE 2147483648.0f
static uint8_t reply[80];static unsigned reply_length,acks;
static uint32_t gp5_get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void gp5_ack(void){++acks;}
static void gp5_reply(const uint8_t *p,uint32_t n){unsigned i;reply_length=n;for(i=0;i<n && i<80;++i)reply[i]=p[i];}
#define fw_init dsp_init
#include "../firmware/dsp/firmware.h"
#undef fw_init
static uint32_t now;
uint32_t gp5_ms(void){return now;}
void gp5_text(unsigned row,const char *s){(void)row;(void)s;}
static uint16_t connected_pixels[160*80];
void gp5_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *p)
{ unsigned r,c;for(r=0;r<h;++r)for(c=0;c<w;++c)if(x+c<160 && y+r<80)connected_pixels[(y+r)*160+x+c]=p[r*w+c]; }
#define FROST_CHAIN_BENCH
#include "../firmware/controller/firmware.c"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static void connected_poll_internal(void)
{uint8_t p[12];unsigned n=fw_sync_route_request(p);if(n){fw_command(FROST_ROUTE_STATUS_COMMAND,p,n);fw_sync_route_reply(reply,reply_length);}}
static unsigned link_mode;
static uint32_t fx_next,fx_startup_acked;
static unsigned dsp_link_raw(const uint8_t *p,unsigned n,uint8_t *r,unsigned max,unsigned wait,uint8_t *status)
{
    unsigned i;uint32_t cmd=(uint32_t)p[0]|((uint32_t)p[1]<<8);(void)wait;
    reply_length=0;fw_command(cmd,p,n);
    if(cmd==FROST_SETTING_COMMAND && link_mode){
        *status=link_mode==2?0x14:0;link_mode=0;return 0;
    }
    for(i=0;i<reply_length && i<max;++i)r[i]=reply[i];
    *status=1;return reply_length;
}
API void connected_init(void){now=1000;acks=reply_length=link_mode=0;fx_next=fx_startup_acked=0;dsp_init();fw_init();fw_dsp_ready();connected_poll_internal();}
API int connected_set(uint32_t id,uint32_t value){
    uint8_t p[12]={0x10,0x7f,12,0};unsigned b;
    for(b=0;b<4;++b){p[4+b]=(uint8_t)(id>>(8*b));p[8+b]=(uint8_t)(value>>(8*b));}
    return fw_command(0x7f10,p,12);
}
API unsigned connected_drain(void){
    uint32_t id,v;unsigned n=0;
    while(fw_take(&id,&v)){
        uint8_t p[24],r[32],status;unsigned bytes=fw_sync_setting_request(id,v,p),received;
        if(!bytes){fw_retry(id);break;}
        received=dsp_link_raw(p,bytes,r,sizeof(r),100,&status);
        fw_sync_setting_reply(status==1?r:0,status==1?received:0);
        ++n;if(n>11)break;
    }
    return n;
}
API void connected_turn(int d){fw_para_turn(d);}
API void connected_press(void){now+=50;fw_para_push();}
API void connected_foot(void){now+=50;fw_foot();}
API void connected_ready(void){fw_dsp_ready();connected_poll_internal();}
API void connected_retry(unsigned id){fw_retry(id);}
API unsigned connected_next(uint32_t *id,uint32_t *v){return fw_take(id,v);}
API unsigned connected_meta(unsigned id,unsigned field){
    const FrostSetting *d=&frost_settings[id];
    unsigned v[]={d->row,d->ui_parameter,d->kind,d->slot,d->type,d->parameter,
        (unsigned)d->control.minimum,(unsigned)d->control.maximum,(unsigned)d->control.step,
        (unsigned)d->control.initial,(unsigned)d->control.scale};
    return field<11?v[field]:0;
}
API unsigned connected_ui_meta(unsigned id,unsigned field){
    const FrostSetting *d=&frost_settings[id];
    const Parameter *p=&effects[d->row].p[d->ui_parameter];
    unsigned v[]={(unsigned)p->minimum,(unsigned)p->maximum,(unsigned)p->step,(unsigned)p->initial,(unsigned)p->scale};
    return field<5?v[field]:0;
}
API float connected_registry_meta(unsigned id,unsigned field){
    const FrostSetting *d=&frost_settings[id];
    const FrostParameter *p=&frost_effects[d->type].parameters[d->parameter];
    return field==0?p->minimum:field==1?p->maximum:p->initial;
}
API float connected_target(unsigned id){
    const FrostSetting *d=&frost_settings[id];const FrostInstance *s=&frost_chain.slots[d->slot];
    return d->kind==FROST_CONTROL_ENABLE?s->wet_target:
        d->kind==FROST_CONTROL_IR_TAPS?(float)s->state.ir->taps:s->target[d->parameter];
}
API void connected_paint(void){unsigned i;for(i=0;i<10;++i)fw_draw(0,0);}
API unsigned connected_pixel(unsigned i){return i<160*80?connected_pixels[i]:0;}
API unsigned connected_get(unsigned id){uint8_t p[4]={0x31,0x7f,4,0};fw_command(0x7f31,p,4);return id<11 && reply_length==48?gp5_get32(reply+4+4*id):0xffffffffu;}
API unsigned connected_metric(unsigned id){uint8_t p[4]={0x30,0x7f,4,0};fw_command(0x7f30,p,4);return id<10 && reply_length==44?gp5_get32(reply+4+4*id):0xffffffffu;}
API int connected_muted(void){
    int32_t x[64],l[64],r[64];unsigned i,b;
    for(i=0;i<64;++i)x[i]=2147483647;
    for(b=0;b<100;++b){fw_process(x,l,r);for(i=0;i<64;++i)if(l[i] || r[i])return 0;}
    return fw_level()==0 && frost_bench_mute==1;
}
API void connected_poll(void){connected_poll_internal();}
API unsigned connected_sync_state(void){return sync_state;}
API unsigned connected_pending(void){return pending;}
API unsigned connected_revision(void){return slot_protocol.revision;}
API void connected_drop(unsigned mode){link_mode=mode;}
API void connected_disconnect(void){fw_dsp_ready();}
API void connected_advance(unsigned ms){now+=ms;}
#define DSP_CTL_DONE 1
API void connected_tick(void){unsigned dsp_ctl_step=DSP_CTL_DONE;
#include "../firmware/controller/transport.inc"
}
API unsigned connected_confirmed(void){return fx_startup_acked;}
API void connected_audio(unsigned blocks){int32_t x[64]={0},l[64],r[64];while(blocks--)fw_process(x,l,r);}
API unsigned connected_route(const unsigned *types,const unsigned *order,unsigned pre)
{return frost_slots_request_route(&slot_protocol,&frost_chain,types,order,pre,slot_protocol.revision);}
API int connected_wire(uint32_t cmd,const uint8_t *p,unsigned n){reply_length=0;return fw_command(cmd,p,n);}
API unsigned connected_reply_bytes(void){return reply_length;}
API unsigned connected_reply_byte(unsigned i){return i<reply_length?reply[i]:0;}
API void connected_force_value(unsigned id,unsigned value){
    const FrostSetting *d=&frost_settings[id];
    if(d->kind==FROST_CONTROL_ENABLE)enabled[d->row]=value;else values[d->row][d->ui_parameter]=(int)value;
    pending|=1u<<id;
}
API unsigned connected_ui_value(unsigned id){return sync_desired(id);}
static uint8_t prepared[24];
API unsigned connected_prepare(void){uint32_t id,v;if(!fw_take(&id,&v))return 0;return fw_sync_setting_request(id,v,prepared);}
API void connected_deliver(void){fw_command(FROST_SETTING_COMMAND,prepared,sizeof(prepared));fw_sync_setting_reply(reply,reply_length);}
API void connected_deliver_lost(unsigned mode){uint8_t r[32],status;unsigned n;link_mode=mode;
    n=dsp_link_raw(prepared,sizeof(prepared),r,sizeof(r),100,&status);
    fw_sync_setting_reply(status==1?r:0,status==1?n:0);}
API unsigned connected_prepared_word(unsigned i){return frost_wire_get32(prepared+4*i);}
API int connected_route_reply(const uint8_t *p,unsigned n){return fw_sync_route_reply(p,n);}
API unsigned connected_begin_poll(void){uint8_t p[12];fw_sync_route_request(p);return frost_wire_get32(p+8);}
API unsigned connected_ir_position(void){return frost_chain.slots[3].state.ir->position;}
/* 25a: the block FIR leaves position 0 after every 64-sample block, so a
 * reload (frost_ir_load zeroes history and position) is detected with a
 * history sentinel written after audio instead. */
API void connected_ir_mark(void){frost_chain.slots[3].state.ir->history[0]=0.25f;}
API unsigned connected_ir_marked(void){return frost_chain.slots[3].state.ir->history[0]==0.25f;}
API void connected_dsp_reset(void){dsp_init();fw_dsp_ready();}
API void connected_force_revision(uint32_t value){slot_protocol.revision=value;slot_protocol.cached=0;}
API void connected_route_inplace(uint8_t *p,unsigned n){frost_route_command(&slot_protocol,&frost_chain,p,n,p);}
