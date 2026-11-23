#ifdef FROST_SLOT_EDITOR
#include "slot_editor.h"
#else
/* Frost controller: muted diagnostic controls or controller-only concept demo.
 * fw_level()==0 adds platform mute; the DSP also has its hard mute.
 */
#include <stdint.h>
#include "gp5_firmware.h"
#include "hal/tick.h"
#include "font5x7.h"

#include "display_color.h"
#include "engine/metadata.h"
#ifdef FROST_CHAIN_BENCH
#include "firmware/protocol/settings.h"
#include "firmware/protocol/sync_wire.h"
#endif
#define BG COLOR(6,12,24)
#define INK COLOR(231,241,255)
#define FOCUS COLOR(40,156,255)
#define SELECTION COLOR(12,36,76)
#define WIRE COLOR(79,113,158)
#ifdef FROST_BOOT_SPLASH
#include "boot_logo.h"
static unsigned boot_band,boot_complete,boot_waiting;
static uint32_t boot_mark;
#endif
enum { CHAIN, PARAMETERS, VALUE };
typedef FrostControlParameter Parameter;
typedef struct { const char *short_name,*name;unsigned count;Parameter p[3]; } Effect;
static const Effect effects[6]={
#ifdef FROST_CHAIN_BENCH
    {FROST_GAIN_NAME,FROST_GAIN_TITLE,FROST_GAIN_PARAMETER_COUNT,{FROST_GAIN_PARAMETERS(FROST_PARAMETER_CONTROL)}},
    {FROST_GATE_NAME,FROST_GATE_TITLE,FROST_GATE_PARAMETER_COUNT,{FROST_GATE_PARAMETERS(FROST_PARAMETER_CONTROL)}},
    {FROST_EQ_NAME,FROST_EQ_TITLE,FROST_EQ_PARAMETER_COUNT,{FROST_EQ_PARAMETERS(FROST_PARAMETER_CONTROL)}},
    {"NAM","NAM NOT RUNNING",0,{{0},{0},{0}}},
    {FROST_IR_NAME,"IR TEST FILTER",2u,{FROST_DIAGNOSTIC_TAPS(FROST_PARAMETER_CONTROL),FROST_DIAGNOSTIC_IR_LEVEL(FROST_PARAMETER_CONTROL)}},
    {"REV","REV NOT LOADED",0,{{0},{0},{0}}}
#else
    {"DROP","DROP TUNE",2,{{"SEMITONE","ST",-12,0,1,-2,1,0},{"MIX","%",0,100,1,100,1,0},{0}}},
    {"NAM","NEURAL AMP",2,{{"GAIN","X",0,4000,25,1000,1000,0},{"LEVEL","X",0,2000,25,500,1000,0},{0}}},
    {"IR","CABINET IR",2,{{"CAB","",1,8,1,1,1,0},{"LEVEL","%",0,100,1,80,1,0},{0}}},
    {"MOD","CHORUS",3,{{"RATE","HZ",1,100,1,12,10,0},{"DEPTH","%",0,100,1,40,1,0},{"MIX","%",0,100,1,30,1,0}}},
    {"DLY","DELAY",3,{{"TIME","MS",10,300,10,180,1,0},{"FEEDBACK","%",0,90,1,35,1,0},{"MIX","%",0,100,1,25,1,0}}},
    {"REV","REVERB",3,{{"DECAY","%",0,100,1,45,1,0},{"TONE","%",0,100,1,50,1,0},{"MIX","%",0,100,1,20,1,0}}}
#endif
};
#ifdef FROST_CHAIN_BENCH
static uint32_t pending;
static uint32_t sync_revision,sync_token,sync_route_token,sync_setting_token,sync_id,sync_sent,sync_expected_revision,sync_next_poll;
static unsigned sync_state,sync_error,sync_kind,sync_retry,sync_resync;
static void queue_effect(unsigned effect)
{ unsigned i;sync_error=0;for(i=0;i<FROST_SETTING_COUNT;++i)if(frost_settings[i].row==effect)pending|=1u<<i; }
/* Platform requeues a setting when its DSP transaction gets no ACK.
 * Sending again uses the latest UI value, coalescing rapid edits. */
void fw_retry(uint32_t id){if(id<FROST_SETTING_COUNT)pending|=1u<<id;}
#endif
static unsigned selected,view,parameter,enabled[6],changed,generation,draw_generation,band;
static int values[6][3];
static uint32_t last_foot,last_para;
static uint16_t framebuffer[160*80];

#include "draw.h"
#ifdef FROST_CHAIN_BENCH
static const char *sync_message(void)
{
    return sync_state==FROST_SYNC_BUSY?"SYNC BUSY":sync_state==FROST_SYNC_LAYOUT?"SYNC ROUTE":
        sync_state==FROST_SYNC_ERROR || sync_error?"DSP ERROR":sync_state==FROST_SYNC_UNKNOWN?"SYNC WAIT":0;
}
#endif
static void render(void)
{
    static const unsigned xy[6][2]={{5,16},{59,16},{113,16},{113,46},{59,46},{5,46}};
    unsigned i;const Effect *e=effects+selected;
    rect(0,0,160,80,BG);
    if(view==CHAIN){
        char count[8]="0/6 ON";unsigned on=0;
        for(i=0;i<6;++i)on+=enabled[i];count[0]=(char)('0'+on);
#ifdef FROST_CHAIN_BENCH
        text(5,3,sync_message()?sync_message():changed?"MUTED FX*":"MUTED FX",INK);
#else
        text(5,3,changed?"FROST DEMO*":"FROST DEMO",INK);
#endif
        text(119,3,count,INK);
        arrow(47,27,58,27);arrow(101,27,112,27);arrow(134,38,134,45);arrow(113,57,102,57);arrow(59,57,48,57);
        for(i=0;i<6;++i){
            int x=(int)xy[i][0],y=(int)xy[i][1];uint16_t color=i==selected?FOCUS:WIRE;
            const char *status=enabled[i]?"ON":"OFF";
            rect(x,y,42,22,i==selected?SELECTION:BG);
            line(x,y,x+41,y,color);line(x,y+21,x+41,y+21,color);line(x,y,x,y+21,color);line(x+41,y,x+41,y+21,color);
            if(i==selected){line(x+1,y+1,x+40,y+1,color);line(x+1,y+20,x+40,y+20,color);line(x+1,y+1,x+1,y+20,color);line(x+40,y+1,x+40,y+20,color);}
            text(x+(42-(int)(length(effects[i].short_name)*6-1))/2,y+4,effects[i].short_name,INK);
            text(x+(42-(int)(length(status)*6-1))/2,y+13,status,enabled[i]?FOCUS:INK);
        }
        text(5,72,e->name,INK);text(11+(int)length(e->name)*6,72,enabled[selected]?"ON":"OFF",INK);
    }else{
#ifdef FROST_CHAIN_BENCH
        text(5,3,"MUTE",INK);
#else
        text(5,3,"DEMO",INK);
#endif
        text(35,3,e->short_name,INK);text(65,3,enabled[selected]?"ON":"OFF",INK);
        text(125,3,view==VALUE?"VALUE":"EDIT",INK);line(4,12,155,12,WIRE);
        for(i=0;i<=e->count;++i){int y=18+(int)i*12;
            if(i==parameter){rect(3,y-2,154,11,SELECTION);text(5,y,">",FOCUS);}
            if(i==e->count)text(15,y,"BACK",INK);
            else {char value[20];formatted(value,e->p+i,values[selected][i]);text(15,y,e->p[i].name,INK);
                text(153-(int)length(value)*6,y,value,i==parameter && view==VALUE?FOCUS:INK);}
        }
#ifdef FROST_CHAIN_BENCH
        text(5,72,sync_message()?sync_message():view==VALUE?"PRESS TO CONFIRM":"PARA: SELECT / EDIT",INK);
#else
        text(5,72,view==VALUE?"PRESS TO CONFIRM":"PARA: SELECT / EDIT",INK);
#endif
    }
}

void fw_init(void)
{
    unsigned i,j;selected=1;view=CHAIN;parameter=0;changed=0;generation=1;draw_generation=0;band=80;
    for(i=0;i<6;++i){enabled[i]=1;for(j=0;j<3;++j)values[i][j]=effects[i].p[j].initial;}
#ifdef FROST_CHAIN_BENCH
    for(i=0;i<6;++i)enabled[i]=effects[i].count!=0;
    for(i=0;i<FROST_SETTING_COUNT;++i)if(frost_settings[i].kind==FROST_CONTROL_ENABLE)
        enabled[frost_settings[i].row]=(unsigned)frost_settings[i].control.initial;
    pending=FROST_SETTINGS_PENDING;
    sync_revision=sync_token=sync_route_token=sync_setting_token=sync_id=sync_sent=sync_expected_revision=0;
    sync_state=FROST_SYNC_UNKNOWN;sync_error=sync_kind=sync_retry=sync_resync=0;
    sync_next_poll=gp5_ms();
#endif
#ifdef FROST_BOOT_SPLASH
    boot_band=boot_complete=boot_waiting=0;boot_mark=0;
#endif
    last_foot=last_para=gp5_ms()-50u;
}
void fw_dsp_ready(void){++generation;
#ifdef FROST_CHAIN_BENCH
    pending=FROST_SETTINGS_PENDING;
    sync_state=FROST_SYNC_UNKNOWN;sync_kind=sync_retry=sync_resync=0;
    sync_next_poll=gp5_ms();
#endif
}
unsigned fw_setting_count(void){
#ifdef FROST_CHAIN_BENCH
    return FROST_SETTING_COUNT;
#else
    return 0;
#endif
}
unsigned fw_level(void){return 0;}
unsigned fw_active(void){return enabled[selected];}
int fw_take(uint32_t *id,uint32_t *value){
#ifdef FROST_CHAIN_BENCH
    unsigned i,j;
    if(sync_state!=FROST_SYNC_READY || sync_kind)return 0;
    for(j=0;j<FROST_SETTING_COUNT;++j){
        i=sync_retry && sync_id<FROST_SETTING_COUNT && (pending&(1u<<sync_id))?sync_id:j;
        if(pending&(1u<<i)){
        const FrostSetting *d=&frost_settings[i];
        pending&=~(1u<<i);*id=i;
        *value=d->kind==FROST_CONTROL_ENABLE?enabled[d->row]:(uint32_t)values[d->row][d->ui_parameter];
        return 1;
        }
    }
    return 0;
#else
    (void)id;(void)value;return 0;
#endif
}
#ifdef FROST_CHAIN_BENCH
unsigned fw_sync_ready(void){return sync_state==FROST_SYNC_READY;}
unsigned fw_sync_poll_due(void)
{
    uint32_t now=gp5_ms();if((int32_t)(now-sync_next_poll)<0)return 0;
    sync_next_poll=now+100u;return 1;
}
static uint32_t sync_next_token(void)
{
    if(sync_token==UINT32_MAX){sync_state=FROST_SYNC_ERROR;++generation;return 0;}
    return ++sync_token;
}
unsigned fw_sync_route_request(uint8_t *p)
{
    if(sync_kind || !(sync_route_token=sync_next_token()))return 0;
    p[0]=0x41;p[1]=0x7f;p[2]=FROST_ROUTE_STATUS_REQUEST_BYTES;p[3]=0;
    frost_wire_put32(p+4,FROST_SLOT_VERSION);frost_wire_put32(p+8,sync_route_token);
    sync_kind=1;return FROST_ROUTE_STATUS_REQUEST_BYTES;
}
int fw_sync_route_reply(const uint8_t *p,unsigned n)
{
    unsigned i,mask=0,layout=1,phase,position,status;
    if(sync_kind!=1)return 0;
    sync_kind=0;
    if(!p || n!=FROST_ROUTE_STATUS_REPLY_BYTES || p[0]!=0x41 || p[1]!=0x7f ||
       p[2]!=n || p[3] || frost_wire_get32(p+4)!=FROST_SLOT_VERSION ||
       frost_wire_get32(p+8)!=sync_route_token)goto invalid;
    status=frost_wire_get32(p+12);phase=frost_wire_get32(p+20);position=frost_wire_get32(p+24);
    if((status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_BUSY) || phase>2 || position>=128 ||
       (!phase && position) || ((status==FROST_SLOT_BUSY)!=(phase!=0)) ||
       frost_wire_get32(p+28)>6 || !frost_wire_get32(p+16))goto invalid;
    if(frost_wire_get32(p+28)!=3)layout=0;
    for(i=0;i<6;++i){
        unsigned order=p[38+i];
        if(order>=6 || (mask&(1u<<order)))goto invalid;
        mask|=1u<<order;
        if(order!=i || p[32+i]!=frost_default_type(i))layout=0;
    }
    sync_revision=frost_wire_get32(p+16);
    sync_state=sync_revision==UINT32_MAX?FROST_SYNC_ERROR:phase?FROST_SYNC_BUSY:
        layout?FROST_SYNC_READY:FROST_SYNC_LAYOUT;
    if(sync_state==FROST_SYNC_BUSY || sync_state==FROST_SYNC_LAYOUT)sync_resync=1;
    if(sync_state==FROST_SYNC_READY && sync_resync){pending|=FROST_SETTINGS_PENDING;sync_resync=0;}
    ++generation;return 1;
invalid:
    sync_state=FROST_SYNC_ERROR;++generation;return 0;
}
unsigned fw_sync_setting_request(uint32_t id,uint32_t value,uint8_t *p)
{
    if(sync_kind || sync_state!=FROST_SYNC_READY || id>=FROST_SETTING_COUNT)return 0;
    if(!(sync_retry && id==sync_id && value==sync_sent)){
        sync_setting_token=sync_next_token();if(!sync_setting_token)return 0;
        sync_id=id;sync_sent=value;sync_expected_revision=sync_revision;
    }
    p[0]=0x11;p[1]=0x7f;p[2]=FROST_SETTING_REQUEST_BYTES;p[3]=0;
    frost_wire_put32(p+4,FROST_SLOT_VERSION);frost_wire_put32(p+8,sync_setting_token);
    frost_wire_put32(p+12,sync_expected_revision);frost_wire_put32(p+16,id);frost_wire_put32(p+20,value);
    sync_kind=2;return FROST_SETTING_REQUEST_BYTES;
}
static uint32_t sync_desired(unsigned id)
{
    const FrostSetting *d=&frost_settings[id];
    return d->kind==FROST_CONTROL_ENABLE?enabled[d->row]:(uint32_t)values[d->row][d->ui_parameter];
}
int fw_sync_setting_reply(const uint8_t *p,unsigned n)
{
    uint32_t status,revision,actual;const FrostSetting *d;
    if(sync_kind!=2)return 0;
    sync_kind=0;
    if(!p || n!=FROST_SETTING_REPLY_BYTES || p[0]!=0x11 || p[1]!=0x7f || p[2]!=n || p[3] ||
       frost_wire_get32(p+4)!=FROST_SLOT_VERSION || frost_wire_get32(p+8)!=sync_setting_token ||
       frost_wire_get32(p+20)!=sync_id || frost_wire_get32(p+24)!=sync_sent)goto retry;
    status=frost_wire_get32(p+12);revision=frost_wire_get32(p+16);actual=frost_wire_get32(p+28);
    if(!revision)goto retry;
    if(status==FROST_SLOT_APPLIED || status==FROST_SLOT_REPLAYED){
        if(sync_expected_revision==UINT32_MAX || revision!=sync_expected_revision+1 || actual!=sync_sent)goto retry;
        sync_revision=revision;sync_retry=0;sync_error=0;
        if(sync_desired(sync_id)==sync_sent)pending&=~(1u<<sync_id);else fw_retry(sync_id);
        sync_state=revision==UINT32_MAX?FROST_SYNC_ERROR:FROST_SYNC_READY;
        ++generation;return 1;
    }
    sync_retry=0;fw_retry(sync_id);++generation;
    if(status==FROST_SLOT_BUSY){sync_state=FROST_SYNC_BUSY;sync_resync=1;return 0;}
    if(status==FROST_SLOT_STALE_REVISION){sync_state=FROST_SYNC_UNKNOWN;sync_resync=1;return 0;}
    if(status==FROST_SLOT_WRONG_TYPE){sync_state=FROST_SYNC_LAYOUT;sync_resync=1;return 0;}
    d=&frost_settings[sync_id];
    if(status==FROST_SLOT_BAD_VALUE && revision==sync_expected_revision && actual>=(uint32_t)d->control.minimum && actual<=(uint32_t)d->control.maximum &&
       (d->kind!=FROST_CONTROL_IR_TAPS || actual%(unsigned)d->control.step==0)){
        sync_revision=revision;sync_error=1;
        if(sync_desired(sync_id)==sync_sent){
            if(d->kind==FROST_CONTROL_ENABLE)enabled[d->row]=actual;
            else values[d->row][d->ui_parameter]=(int)actual;
            pending&=~(1u<<sync_id);
        }
        return 0;
    }
    sync_state=FROST_SYNC_ERROR;return 0;
retry:
    sync_retry=1;fw_retry(sync_id);sync_state=FROST_SYNC_UNKNOWN;++generation;return 0;
}
#endif
void fw_knob(unsigned knob){(void)knob;}
void fw_foot(void)
{
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    uint32_t now=gp5_ms();if(now-last_foot<35u)return;last_foot=now;
#ifdef FROST_CHAIN_BENCH
    if(sync_state!=FROST_SYNC_READY || effects[selected].count==0)return;
#endif
    enabled[selected]=!enabled[selected];changed=1;++generation;
#ifdef FROST_CHAIN_BENCH
    queue_effect(selected);
#endif
}
void fw_para_push(void)
{
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    uint32_t now=gp5_ms();if(now-last_para<35u)return;last_para=now;
    if(view==CHAIN){view=PARAMETERS;parameter=0;}
    else if(view==VALUE)view=PARAMETERS;
    else if(parameter==effects[selected].count)view=CHAIN;
    else view=VALUE;
    ++generation;
}
void fw_para_turn(int detents)
{
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    if(!detents)return;
    if(view==CHAIN)selected=(unsigned)(((int)selected+detents%6+6)%6);
    else if(view==PARAMETERS){int count=(int)effects[selected].count+1;parameter=(unsigned)(((int)parameter+detents%count+count)%count);}
    else {
#ifdef FROST_CHAIN_BENCH
        if(sync_state!=FROST_SYNC_READY)return;
#endif
        const Parameter *p=effects[selected].p+parameter;int64_t v=(int64_t)values[selected][parameter]+(int64_t)detents*p->step;
        if(v<p->minimum)v=p->minimum;if(v>p->maximum)v=p->maximum;
        if(values[selected][parameter]!=(int)v){values[selected][parameter]=(int)v;changed=1;
#ifdef FROST_CHAIN_BENCH
            queue_effect(selected);
#endif
        }
    }
    ++generation;
}
int fw_draw(unsigned knob,int force)
{
    (void)knob;
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete){
        if(boot_band<80){gp5_blit(0,boot_band,160,8,frost_boot_pixels+boot_band*160);boot_band+=8;return 1;}
        if(!boot_waiting){boot_mark=gp5_ms();boot_waiting=1;}
        if(gp5_ms()-boot_mark<1500u)return 1;
        boot_complete=1;force=1;
    }
#endif
    if(force || draw_generation!=generation){render();draw_generation=generation;band=0;}
    if(band>=80)return 0;
    gp5_blit(0,band,160,8,framebuffer+band*160);band+=8;
    return band<80;
}

#endif /* FROST_SLOT_EDITOR */
