#include <stdint.h>
#include "gp5_firmware.h"
static uint32_t GP5_CYCLES_LAST,GP5_CYCLES_WORST,GP5_LATE_BLOCKS;
#define GP5_BLOCK 64u
#define GP5_FULL_SCALE 2147483648.0f
static uint8_t response[80];static unsigned response_bytes,reply_count;
static uint32_t gp5_get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void gp5_ack(void){response_bytes=0;++reply_count;}
static void gp5_reply(const uint8_t *p,uint32_t n){unsigned i;response_bytes=n;++reply_count;for(i=0;i<n && i<80;++i)response[i]=p[i];}
#ifdef FROST_HOST_FAST_CONTROL
#define FROST_CONTROL_SCHEDULER 1
static void send_frame(const uint8_t *p,unsigned n){gp5_reply(p,n);}
#endif
/* 32h: stand-in for the pedal's overlay-pool admission (frost_ovl_fits, ARM
 * only): a route or preset containing host_full_type does not fit. */
static unsigned host_full_type=255u;
static int host_route_admit(const unsigned *t){unsigned i;for(i=0;i<6u;++i)if(host_full_type!=255u && t[i]==host_full_type)return 0;return 1;}
#define FROST_ROUTE_ADMIT(t) host_route_admit(t)
/* 32i: the real load table and estimate; the budget is a variable so other
 * tests keep their chains (default: unlimited). */
static uint32_t host_load_budget=UINT32_MAX;
#define FROST_LOAD_ADMIT 1
#define FROST_LOAD_BUDGET_CYCLES host_load_budget
#define fw_init dsp_init
#include "../firmware/dsp/firmware.h"
#undef fw_init
static uint32_t now;
static uint16_t pixels[160*80];static unsigned bad_blit;
uint32_t gp5_ms(void){return now;}
void gp5_text(unsigned row,const char *s){(void)row;(void)s;bad_blit=1;}
void gp5_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *p)
{unsigned i,j;if(x+w>160 || y+h>80){bad_blit=1;return;}for(j=0;j<h;++j)for(i=0;i<w;++i)pixels[(y+j)*160+x+i]=p[j*w+i];}
/* Controller flash files run A, simulated (fcm_files.h reads it memory-mapped). */
static uint8_t host_files[33u*4096u];static unsigned host_rx_resets,host_flash_ops,host_flash_cut,host_flash_cut_bytes;
/* Power-cut injection: after host_flash_cut operations (0 = never) flash stops changing;
 * the cutting operation applies only its first host_flash_cut_bytes bytes. */
#define FROST_FILE_AT(a) ((const volatile uint8_t *)(host_files+((a)-0x13f000u)))
int gp5_flash_erase(uint32_t a)
{unsigned i,n=4096u;if(a<0x13f000u || a>=0x160000u || (a&4095u))return -1;
 if(host_flash_cut){if(host_flash_ops>=host_flash_cut)return 0;if(host_flash_ops+1u==host_flash_cut)n=host_flash_cut_bytes<n?host_flash_cut_bytes:n;}
 for(i=0;i<n;++i)host_files[a-0x13f000u+i]=0xff;++host_flash_ops;return 0;}
int gp5_flash_program(uint32_t a,const uint32_t *src,unsigned n)
{unsigned i;const uint8_t *b=(const uint8_t *)src;if(a<0x13f000u || n>0x160000u-a || (n&3u) || (a&3u))return -1;
 if(host_flash_cut){if(host_flash_ops>=host_flash_cut)return 0;if(host_flash_ops+1u==host_flash_cut && host_flash_cut_bytes<n)n=host_flash_cut_bytes&~3u;}
 for(i=0;i<n;++i)host_files[a-0x13f000u+i]&=b[i];++host_flash_ops;return 0;}
void gp5_uart0_rx_err_clear(void){++host_rx_resets;}
void gp5_uart0_rx_int_clear(void){}
static unsigned host_backlight=100;void gp5_backlight_duty(unsigned percent){host_backlight=percent;}
#define FROST_SLOT_EDITOR 1
#include "../firmware/controller/firmware.c"
static unsigned drop_count,drop_mode,wire_calls,mutations,poison;
static unsigned dsp_link_raw(const uint8_t *p,unsigned n,uint8_t *out,unsigned max,unsigned wait,uint8_t *status)
{
    unsigned i,cmd=(unsigned)p[0]|((unsigned)p[1]<<8),mutation=cmd==FROST_ROUTE_WRITE_COMMAND ||
        (cmd==FROST_SLOT_COMMAND && frost_wire_get32(p+12)!=FROST_SLOT_GET) ||
        (cmd==FROST_PRESET_COMMAND && frost_wire_get32(p+12)!=FROST_PRESET_STATUS) ||
        (cmd==FROST_QUICK_COMMAND && n>12u) ||
        (cmd==FROST_POLICY_COMMAND && frost_wire_get32(p+12)==FROST_POLICY_SET);
    unsigned affected=mutation || cmd==FROST_QUICK_COMMAND || cmd==FROST_IR_COMMAND || cmd==FROST_PRESET_READ_COMMAND || cmd==FROST_POLICY_COMMAND;
    (void)wait;++wire_calls;if(mutation)++mutations;
#ifdef FROST_HOST_REQUEST_HOOK
    if(FROST_HOST_REQUEST_HOOK(p,n)){*status=0x14;return 0;}
#endif
    if(affected && drop_count && drop_mode==3){--drop_count;*status=0;return 0;}
    response_bytes=0;fw_command(cmd,p,n);
    if(poison && !mutation){
        if(poison==1 && cmd==FROST_ROUTE_STATUS_COMMAND){response[39]=response[38];poison=0;}
        else if(poison==2 && cmd==FROST_SLOT_COMMAND){response[32]^=1;poison=0;}
        else if(poison==3 && cmd==FROST_SLOT_COMMAND){frost_wire_put32(response+36,0x7fc00000u);poison=0;}
    }
    if(affected && drop_count){--drop_count;
        if(drop_mode==1){*status=0;return 0;}
        if(drop_mode==2 && response_bytes>=12)response[8]^=0x40;
    }
    for(i=0;i<response_bytes && i<max;++i)out[i]=response[i];*status=1;return response_bytes;
}
static uint32_t fx_next,fx_startup_acked;
#define DSP_CTL_DONE 1
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static void ge_dsp_boot_large(void){unsigned k;uint8_t *b=(uint8_t *)&frost_fcm_rx;for(k=0;k<sizeof(frost_fcm_rx);++k)b[k]=0;} /* LARGE is zeroed at DSP start-up */
API void ge_init(void){unsigned k;files_host_written=0;files_host_write_ms=0;for(k=0;k<sizeof(host_files);++k)host_files[k]=0xff;ge_dsp_boot_large();host_rx_resets=host_flash_ops=host_flash_cut=host_flash_cut_bytes=0;now=1000;bad_blit=drop_count=drop_mode=wire_calls=mutations=poison=0;fx_next=fx_startup_acked=0;dsp_init();fw_init();fw_dsp_ready();}
API void ge_tick(void){unsigned dsp_ctl_step=DSP_CTL_DONE;
#include "../firmware/controller/slot_transport.inc"
}
API void ge_audio(unsigned blocks){int32_t x[64]={0},l[64],r[64];while(blocks--)fw_process(x,l,r);}
API void ge_advance(unsigned ms){now+=ms;}
API void ge_pump(unsigned n){while(n--){ge_tick();ge_audio(1);now+=20;}}
API unsigned ge_ready(void){return fw_editor_ready();}
API void ge_midi(unsigned cin,unsigned status,unsigned d1,unsigned d2){uint8_t p[4]={(uint8_t)cin,(uint8_t)status,(uint8_t)d1,(uint8_t)d2};fw_midi_event(p);}
API unsigned ge_midi_bypass(void){return midi_bypass;}
API unsigned ge_midi_volume(void){return midi_volume;}
API unsigned ge_midi_tempo(void){return midi_tempo;}
API unsigned ge_midi_pending(void){return midi_preset;}
API unsigned ge_dsp_global_flags(void){return frost_global_mode;}
API float ge_bypass_sample(unsigned on,float dry,float wet){float d[64],w[64];unsigned i;frost_global_mode=on?2u:0u;for(i=0;i<64u;++i){d[i]=dry;w[i]=wet;}frost_bypass_blend(d,w);return w[63];}
API unsigned ge_editable(void){return editor_editable(&editor);}
API unsigned ge_selected(void){return selected;}
API unsigned ge_view(void){return view;}
API unsigned ge_parameter(void){return parameter;}
API unsigned ge_type(unsigned slot){return editor.types[slot];}
API unsigned ge_order(unsigned i){return editor.order[i];}
API unsigned ge_enabled(unsigned slot){return editor.desired_enabled[slot];}
API int ge_value(unsigned slot,unsigned p){return editor_integer(&editor,slot,p);}
/* Existing transport/preset tests use the original scaled DSP domain. Keep it
 * explicit and separate from the actual human-facing knob API above/below. */
static unsigned ge_dsp_scale(unsigned slot,unsigned p)
{unsigned type=editor.types[slot];return type==FROST_GATE?1000000u:type==FROST_DELAY && p==0?1u:1000u;}
API int ge_value_raw(unsigned slot,unsigned p)
{float x=editor_float(editor.desired[slot][p])*(float)ge_dsp_scale(slot,p);return (int)(x>=0.0f?x+.5f:x-.5f);}
API unsigned ge_raw(unsigned slot,unsigned p){return editor.desired[slot][p];}
API unsigned ge_actual_type(unsigned slot){return frost_chain.slots[slot].type;}
API unsigned ge_actual_enabled(unsigned slot){return (unsigned)frost_chain.slots[slot].wet_target;}
API float ge_actual_value(unsigned slot,unsigned p){return frost_chain.slots[slot].target[p];}
API unsigned ge_revision(void){return editor.revision;}
API unsigned ge_actual_revision(void){return slot_protocol.revision;}
API unsigned ge_error(void){return editor.error;}
API unsigned ge_route_state(void){return editor.route_state;}
API unsigned ge_calls(void){return wire_calls;}
API unsigned ge_mutations(void){return mutations;}
API unsigned ge_faults(void){return frost_chain.faults;}
API void ge_turn(int n){fw_para_turn(n);}
API void ge_press(void){now+=50;fw_para_push();now+=40;fw_para_release();}   /* a short press: down then up */
API void ge_long_press(void){now+=50;fw_para_push();now+=800;(void)fw_draw(0,0);fw_para_release();}
API void ge_foot(void){now+=50;fw_foot();if(!foot_held){now+=40;fw_foot_release();}}   /* a tap; hold-to-play modes stay held */
API void ge_foot_hold(void){now+=50;fw_foot();now+=800;(void)fw_draw(0,0);now+=40;fw_foot_release();}
API void ge_foot_release(void){now+=50;fw_foot_release();}
API void ge_foot_edge(unsigned down,unsigned ms){now+=ms;if(down)fw_foot();else fw_foot_release();}   /* raw switch edge after ms */
API unsigned ge_pick_category(void){return pick_category;}
API unsigned ge_pick_model(void){return pick_model;}
API unsigned ge_human_count(unsigned type){const FrostControlEffect *d=frost_control_find(type);return d?d->count:0;}
API unsigned ge_row_kind(unsigned row){return row_kind(frost_control_find(editor.types[selected]),row,0);}
API unsigned ge_row_total(void){return row_total(frost_control_find(editor.types[selected]));}
API unsigned ge_dirty(unsigned slot){return editor.dirty[slot];}
API void ge_drop(unsigned count,unsigned mode){drop_count=count;drop_mode=mode;}
API void ge_poison(unsigned mode){poison=mode;}
API void ge_reconnect(void){fw_dsp_ready();}
API void ge_dsp_reset(void){ge_dsp_boot_large();dsp_init();fw_dsp_ready();}
API unsigned ge_client_bytes(void){return sizeof(FrostEditor);}
API unsigned ge_paint(void){unsigned i;for(i=0;i<11;++i)if(!fw_draw(0,0))return bad_blit;return 2;}
API unsigned ge_pixel(unsigned i){return i<12800?pixels[i]:0;}
API int ge_queue(unsigned slot,unsigned p,int value){return editor_queue_parameter(&editor,slot,p,value);}
API int ge_queue_raw(unsigned slot,unsigned p,int value)
{if(slot>=6 || p>=3)return 0;return editor_queue_raw(&editor,slot,p,editor_bits((float)value/(float)ge_dsp_scale(slot,p)));}
API int ge_human_meta(unsigned type,unsigned p,unsigned field)
{const FrostControlEffect *d=frost_control_find(type);const FrostControlParameter *c;
 if(!d || p>=d->count)return -999;c=&d->p[p];
 {int v[]={c->minimum,c->maximum,c->step,c->initial,c->scale};return field<5?v[field]:-999;}}
API float ge_to_dsp(unsigned type,unsigned p,int value){return frost_control_to_dsp(type,p,value);}
API int ge_from_dsp(unsigned type,unsigned p,float value){return frost_control_from_dsp(type,p,value);}
API const char *ge_human_name(unsigned type,unsigned p){return frost_control_find(type)->p[p].name;}
API float ge_dsp_meta(unsigned type,unsigned p,unsigned field)
{const FrostParameter *d=&frost_control_find(type)->dsp[p];return field==0?d->minimum:field==1?d->maximum:d->initial;}
API const char *ge_human_hint(unsigned type,unsigned p){return frost_control_hint(type,p);}
API const char *ge_human_text(unsigned slot,unsigned p)
{static char out[32];const FrostControlEffect *d=frost_control_find(editor.types[slot]);formatted(out,&d->p[p],editor_integer(&editor,slot,p));return out;}
API int ge_type_change(unsigned slot,unsigned type){uint8_t types[6],order[6];unsigned i;
    for(i=0;i<6;++i){types[i]=editor.types[i];order[i]=editor.order[i];}types[slot]=(uint8_t)type;
    return editor_queue_route(&editor,types,order,editor.pre);}
API int ge_wire(unsigned cmd,const uint8_t *p,unsigned n){response_bytes=0;return fw_command(cmd,p,n);}
static uint8_t prepared[128];static unsigned prepared_bytes;
API unsigned ge_prepare(void){prepared_bytes=fw_editor_request(prepared);return prepared_bytes;}
API void ge_deliver(void){uint8_t r[48],status;unsigned n=dsp_link_raw(prepared,prepared_bytes,r,sizeof(r),100,&status);fw_editor_reply(status==1?r:0,status==1?n:0);}
API unsigned ge_prepared_word(unsigned i){return frost_wire_get32(prepared+4*i);}
API unsigned ge_preset_start(unsigned profile){return fw_preset_start(profile);}
API unsigned ge_preset_state(void){return preset_client.state;}
API unsigned ge_preset_error(void){return preset_client.error;}
API void ge_full_type(unsigned type){host_full_type=type;}
API void ge_load_budget(uint32_t cycles){host_load_budget=cycles;}
API uint32_t ge_load_default(void){return FROST_LOAD_BUDGET_DEFAULT;}
API uint32_t ge_load_estimate(const unsigned *types){return frost_load_estimate(types);}
API uint32_t ge_load_chain(void){unsigned i,t[6];for(i=0;i<6;++i)t[i]=frost_chain.slots[i].type;return frost_load_estimate(t);}
API unsigned ge_popup(void){return popup_kind;}
API const char *ge_message(void){const char *m=editor_message();return m?m:"";}
API unsigned ge_preset_next(void){return preset_client.next;}
API unsigned ge_preset_bytes(void){return sizeof(FrostPresetClient);}
API unsigned ge_bank_word(unsigned profile,unsigned i){return frost_bank_word(profile,i);}
API unsigned ge_bank_count(void){return FROST_BANK_COUNT;}
API unsigned ge_bank_choice(void){return preset_choice;}
API const char *ge_bank_name(unsigned profile){return frost_bank_name(profile);}
static char bank_label[24];
API const char *ge_bank_label(unsigned profile){frost_bank_label(profile,bank_label);return bank_label;}
API unsigned ge_transfer_state(void){return preset_transfer.state;}
API unsigned ge_transfer_next(void){return preset_transfer.next;}
API unsigned ge_commands(void){return commands;}
API unsigned ge_transition(void){return frost_chain.transition;}
API unsigned ge_policy(unsigned slot){return frost_chain.slots[slot].bypass_policy;}
API unsigned ge_ir_taps(unsigned slot){return frost_chain.slots[slot].state.ir->taps;}
API float ge_ir_coefficient(unsigned slot,unsigned i){return frost_chain.slots[slot].state.ir->coefficients[i];}
API unsigned ge_token(void){return editor.token;}
API void ge_set_token(unsigned token){editor.token=token;}
API int ge_reply(const uint8_t *p,unsigned n){return fw_editor_reply(p,n);}
API unsigned ge_ram_save(unsigned profile){return fw_ram_save(profile);}
API unsigned ge_ram_valid(unsigned profile){return ram_bank_valid(profile);}
API unsigned ge_ram_word(unsigned profile,unsigned index){return ram_bank_word(profile,index);}
API unsigned ge_capture_state(void){return capture_client.state;}
API unsigned ge_capture_error(void){return capture_client.error;}
API unsigned ge_capture_bytes(void){return sizeof(FrostCaptureClient);}
API unsigned ge_ram_bytes(void){return sizeof(FrostRamBank);}
API unsigned ge_changed(void){return changed;}
API unsigned ge_pre(void){return editor.pre;}
API unsigned ge_actual_pre(void){return frost_chain.pre_count;}
API unsigned ge_choice(void){return choice;}
API unsigned ge_suspended(unsigned slot){return frost_chain.slots[slot].suspended;}
API unsigned ge_response_bytes(void){return response_bytes;}
API unsigned ge_response_byte(unsigned i){return i<response_bytes?response[i]:0;}
API int ge_muted(void){int32_t x[64]={0},l[64],r[64];unsigned i,before=reply_count;fw_process(x,l,r);for(i=0;i<64;++i)if(l[i] || r[i])return 0;return before==reply_count && frost_bench_mute && fw_level()==0;}
/* AMP model files and transfer (fcm_files.h, fcm_client.h, fcm_transfer.c). */
API unsigned ge_host(const uint8_t *q,unsigned n,uint8_t *out)
{static uint8_t pl[100];unsigned r,i;pl[0]=0x7e;pl[1]=n>=2?q[1]:0;r=fw_files_host(q,n,pl,sizeof pl);for(i=0;i<r && i<100;++i)out[i]=pl[i];return r;}
API unsigned ge_files_byte(unsigned off){return off<sizeof(host_files)?host_files[off]:0;}
API unsigned ge_rx_resets(void){return host_rx_resets;}
API unsigned ge_fcm_client(unsigned field)
{FrostFcmClient *c=&fcm_client;unsigned v[]={c->state,c->known,c->held_index,c->held_crc,c->failures,c->index,c->crc};return field<7?v[field]:0;}
API unsigned ge_dsp_rx(unsigned field)
{FrostFcmRx *r=&frost_fcm_rx;unsigned v[]={r->state,r->index,r->crc,r->next,r->epoch};return field<5?v[field]:0;}
API unsigned ge_dsp_rx_byte(unsigned i){return i<sizeof(FrostFcmRecord)?((const uint8_t *)&frost_fcm_rx.rec)[i]:0;}
API unsigned ge_fcm_ws(unsigned slot,unsigned field)
{FrostFcmWorkspace *w;if(frost_chain.slots[slot].type!=FROST_FCM || !frost_chain.slots[slot].state.fcm.ring)return 0xffffffffu;
 w=(FrostFcmWorkspace *)(void *)&frost_chain.slots[slot].state.fcm.ring->line;
 {unsigned v[]={w->loaded,w->epoch,w->from_rx,frost_chain.slots[slot].state.fcm.ring->valid};return field<4?v[field]:0;}}
API float ge_fcm_ws_gain(unsigned slot)
{FrostFcmWorkspace *w=(FrostFcmWorkspace *)(void *)&frost_chain.slots[slot].state.fcm.ring->line;return w->model.gain;}
API const char *ge_fcm_label(int index){static char out[24];fcm_model_label(out,index);return out;}
API unsigned ge_flash_ops(void){return host_flash_ops;}
/* Stage 2 stored presets (preset_store.h). */
API unsigned ge_store_preset(unsigned i){return store_preset(i)!=0;}
API const char *ge_preset_label(unsigned i){static char out[24];preset_label(i,out);return out;}
API unsigned ge_flash_save(unsigned i){return fw_flash_save(i);}
API unsigned ge_flash_load(unsigned i){return fw_flash_load(i);}
API unsigned ge_preset_record(unsigned i,uint8_t *out){uint32_t r=store_preset(i);unsigned k;if(!r)return 0;for(k=0;k<FROST_PST_BYTES;++k)out[k]=FROST_FILE_AT(r)[k];return FROST_PST_BYTES;}
API unsigned ge_preset_choice(void){return preset_choice;}
/* power cycle: controller and DSP restart, controller flash kept */
API void ge_reboot(void){ge_dsp_boot_large();dsp_init();fw_init();fw_dsp_ready();}
API void ge_flash_cut(unsigned ops,unsigned bytes){host_flash_ops=0;host_flash_cut=ops;host_flash_cut_bytes=bytes;}
/* Stage 2b global settings */
API unsigned ge_backlight(void){return host_backlight;}
API unsigned ge_level(void){return fw_level();}
API int ge_global(unsigned field){int v[]={globals.in_gain_db,(int)globals.out_level,(int)globals.out_mode,(int)globals.cab_override,(int)globals.brightness,(int)globals.home,(int)globals.last_preset};return field<7?v[field]:0;}
API int ge_dsp_global(unsigned field){int v[]={(int)(frost_in_gain_target*1000.0f+0.5f),(int)frost_ir_bypass,(int)frost_global_mode,frost_global_gain_ddb};return field<4?v[field]:0;}
API unsigned ge_settings_row(void){return settings_row;}
API const char *ge_firmware_text(void){static char s[24];settings_value(SET_FIRMWARE,s);return s;}
API unsigned ge_current_preset(void){return current_preset;}
API void ge_tick_draw(void){(void)fw_draw(0,0);}
/* Stage 2c tuner */
API int ge_tuner(unsigned field)
{int v[]={(int)tuner_client.have,tuner_client.note,(int)(tuner_client.cents*100.0f),(int)frost_tuner.active,(int)globals.tuner_ref,(int)(tuner_client.freq*100.0f),(int)tuner_client.dsp_active};return field<7?v[field]:0;}
API unsigned ge_out_peak(unsigned blocks){int32_t x[64]={0},l[64],r[64];unsigned k,pk=0;while(blocks--){fw_process(x,l,r);for(k=0;k<64;++k){unsigned a=(unsigned)(l[k]<0?-l[k]:l[k]);if(a>pk)pk=a;}}return pk;}

API unsigned ge_set_order(void){return set_order;}
API unsigned ge_set_part(void){return set_part;}
API unsigned ge_set_count(void){setlist_refresh();return set_count;}
API unsigned ge_set_loaded(void){return set_loaded;}
API unsigned ge_set_error(void){return set_error;}
API unsigned ge_set_hold(void){return globals.setlist_hold;}
API int ge_set_global(unsigned field,unsigned value){if(field==0)globals.home=value;else globals.setlist_hold=value;return globals_save(now);}
API const char *ge_song_name(void){static char out[17];uint32_t s=setlist_song(set_order);setlist_name(s?s+2u:0,out);return out;}
API const char *ge_part_name(void){static char out[17];setlist_name(setlist_part(set_order,set_part),out);return out;}

API int ge_global_eq(unsigned band){return band<3?globals.eq_db[band]:0;}
API int ge_dsp_eq(unsigned band){return band<3?frost_global_eq.db[band]:0;}
API unsigned ge_settings_back_row(void){return SET_BACK;}
