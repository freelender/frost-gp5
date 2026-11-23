/* Generic six-slot editor implementation, selected by FROST_SLOT_EDITOR.
 * No DSP kernels, allocation, asset loading, storage writes or audio enable. */
#include <stdint.h>
#include "gp5_firmware.h"
#include "hal/tick.h"
#include "font5x7.h"
#include "display_color.h"
#include "editor_client.h"
#include "preset_client.h"
#include "capture_client.h"
#include "bank_read.h"
#include "fcm_client.h"
#include "ir_file_client.h"
#include "global_settings.h"
#ifdef FROST_FACTORY_PRESETS
#include "factory_store.h"
#endif
#include "setlist.h"
#include "tuner_client.h"
#ifdef FROST_IR_LINK
#include "ir_link.h"
#endif
#ifdef FROST_DSP_USAGE
#include "dsp_usage.h"
static FrostDSPUsage dsp_usage;
static unsigned dsp_usage_dirty,dsp_usage_band;
void fw_dsp_report(const uint8_t *p,unsigned n)
{frost_dsp_usage_report(&dsp_usage,p,n,gp5_ms());}
#endif
#ifdef FROST_LINE_TEST
#include "line_control.h"
static FrostLineControl line_control;
#endif
#define BG COLOR(6,12,24)
#define INK COLOR(231,241,255)
#define FOCUS COLOR(40,156,255)
#define SELECTION COLOR(12,36,76)
#define WIRE COLOR(79,113,158)
#define MUTED COLOR(139,157,179)
static uint16_t effect_color(unsigned type)
{
    unsigned long rgb=frost_category_rgb[frost_type_category(type)];
    return COLOR((rgb>>16)&255,(rgb>>8)&255,rgb&255);
}
typedef FrostControlParameter Parameter;
enum { CHAIN, PARAMETERS, VALUE, PRESETS, PRESET_CONFIRM, VIEW_RETIRED_5, VIEW_RETIRED_6 /* IR SOURCE picker, removed; numbers kept */, TYPE_CATEGORY, TYPE_MODEL,
       SETTINGS, SETTINGS_VALUE, SETTINGS_CONFIRM, PRESET_HOME, STOMP, TUNER, SETLIST_HOME };
static FrostEditor editor;
static FrostPresetClient preset_client;
static FrostCaptureClient capture_client;
static FrostFcmClient fcm_client;
static FrostGlobalClient global_client;
static FrostTunerClient tuner_client;
/* Stage 2c: footswitch hold on a home screen opens the tuner (docs/User%20Guide.md). */
static unsigned foot_down,foot_long,foot_toggled,tuner_ref_dirty,tuner_ref_unlocked;
static uint32_t foot_down_ms;
static unsigned tuner_home;                      /* the home view to return to */
/* Stage 2b: global settings menu, home screens, long press (docs/User%20Guide.md). */
enum { SET_IN_GAIN, SET_OUT_LEVEL, SET_OUT_MODE, SET_CAB, SET_BRIGHT, SET_HOME, SET_SETLIST_HOLD, SET_FIRMWARE, SET_RESET, SET_FACTORY, SET_EQ_BASS, SET_EQ_MIDDLE, SET_EQ_TREBLE, SET_BACK, SET_ROWS };
#define FROST_LONG_PRESS_MS 700u
#define FROST_HOME_RETURN_MS 15000u
/* 32k: Global settings is a menu tree. Entries are SET_* rows (leaves),
 * SET_BACK (one level up; saves and leaves at the top) or a submenu. The
 * highlighted leaf stays in settings_row, so editing is unchanged. */
enum { SM_TOP, SM_AUDIO, SM_DISPLAY, SM_SYSTEM, SM_EQ, SM_MENUS };
enum { SE_AUDIO=32, SE_DISPLAY, SE_SYSTEM, SE_EQ };
static const uint8_t settings_menus[SM_MENUS][7]={
    {SE_AUDIO,SE_DISPLAY,SE_SYSTEM,SET_BACK,255,255,255},
    {SET_IN_GAIN,SET_OUT_LEVEL,SET_OUT_MODE,SET_CAB,SE_EQ,SET_BACK,255},
    {SET_HOME,SET_BRIGHT,SET_SETLIST_HOLD,SET_BACK,255,255,255},
    {SET_FIRMWARE,SET_RESET,SET_FACTORY,SET_BACK,255,255,255},
    {SET_EQ_BASS,SET_EQ_MIDDLE,SET_EQ_TREBLE,SET_BACK,255,255,255}};
static unsigned settings_menu,settings_pos;
static unsigned factory_stage,settings_row,settings_dirty,settings_confirm,para_down,para_long,current_preset,last_preset_dirty,boot_load_done;
static uint32_t para_down_ms,last_input_ms;
static uint16_t recall_old_cabs[6];
static unsigned recall_pending,recall_previous;
static unsigned editor_transport_status;
static unsigned midi_volume=127;
static void midi_init(void);
static unsigned home_view(void){return globals.home==HOME_SETLIST?SETLIST_HOME:globals.home==HOME_PRESET?PRESET_HOME:globals.home==HOME_STOMP?STOMP:CHAIN;}
static int is_home_view(unsigned v){return v==CHAIN || v==PRESET_HOME || v==STOMP || v==SETLIST_HOME;}
static unsigned preset_choice,preset_action;
static void preset_label(unsigned i,char out[24]);
static unsigned selected,view,parameter,choice,changed,generation,draw_generation,band;
static unsigned pick_category,pick_model,foot_held;
static uint32_t last_para,foot_edge_ms,foot_raw_ms;
static unsigned foot_raw,foot_state;   /* footswitch debounce (foot_debounce) */
static uint16_t framebuffer[160*80];
#include "draw.h"
#ifdef FROST_DSP_USAGE
void frost_render_dsp_usage(void)
{
    char value[12];frost_dsp_usage_text(&dsp_usage,value);
    rect(108,0,48,12,BG);text(155-(int)(length(value)*6u-1u),3,value,INK);
}
#endif
#ifdef FROST_BOOT_SPLASH
#include "boot_logo.h"
static unsigned boot_band,boot_complete,boot_waiting;
static uint32_t boot_mark;
#endif
static unsigned picker_count(unsigned category)
{return (unsigned)(frost_picker_start[category+1]-frost_picker_start[category]);}
static unsigned picker_type(unsigned category,unsigned index)
{return frost_picker_types[frost_picker_start[category]+index];}
/* Model-specific footswitch modes are explicit parameters of the selected
 * block: DD-3 FOOT=HOLD (momentary HOLD) and VB-2 MODE=UNLATCH (enabled only
 * while held). Every other block keeps the selected-only bypass toggle. */
static int foot_parameter(unsigned slot,unsigned type,unsigned p)
{return frost_control_from_dsp(type,p,editor_float(editor.desired[slot][p]));}
static unsigned foot_mode(void)
{
    unsigned type=editor.types[selected];
    if(type==FROST_DD3 && foot_parameter(selected,type,4)==1)return 1;
    if(type==FROST_VB2 && foot_parameter(selected,type,3)==1)return 2;
    return 0;
}
static unsigned editor_position(unsigned slot)
{unsigned i;for(i=0;i<6;++i)if(editor.order[i]==slot)return i;return 0;}
/* PARA menu rows of the selected block, top to bottom: CAB (IR only), its
 * parameters, TYPE, POSITION, REMOVE EFFECT (any non-empty block), BACK, PRESETS.
 * PRESETS stays last (one turn back from the first row). */
enum { ROW_PARAM, ROW_CAB, ROW_TYPE, ROW_POSITION, ROW_REMOVE, ROW_BACK, ROW_PRESETS };
static unsigned row_has_cab(void){return editor.types[selected]==FROST_IR;}
static unsigned row_has_remove(void){return editor.types[selected]!=FROST_EMPTY;}
static unsigned row_total(const FrostControlEffect *e){return e->count+4u+row_has_cab()+row_has_remove();}
static unsigned row_kind(const FrostControlEffect *e,unsigned row,unsigned *p)
{
    if(row_has_cab()){if(!row)return ROW_CAB;--row;}
    if(row<e->count){if(p)*p=row;return ROW_PARAM;}
    row-=e->count;
    if(row==0)return ROW_TYPE;
    if(row==1)return ROW_POSITION;
    row-=2;
    if(row_has_remove()){if(!row)return ROW_REMOVE;--row;}
    return row?ROW_PRESETS:ROW_BACK;
}
static const char *editor_message(void)
{
#ifdef FROST_FACTORY_PRESETS
    if(factory_error)return "FACTORY SETUP FAILED";
#endif
    if(capture_client.state)return "SAVING / WAIT";
    if(capture_client.error)return "SAVE FAILED";
    if(preset_client.state)return "LOADING / WAIT";
    if(preset_client.error==2)return "CHAIN TOO BIG";
    if(preset_client.error==3)return "DSP LIMIT";
    if(preset_client.error)return "LOAD FAILED";
    if(editor.error==1)return "DSP ERROR";
    if(editor.busy)return "SYNC BUSY";
    if(editor.route_state)return "SYNC ROUTE";
    if(!editor.ready)return "SYNC WAIT";
    if(editor.error==2)return "ROUTE CHANGED";
    if(editor.error==3)return "EDIT REJECTED";
    if(editor.error==4)return "CHAIN TOO BIG";
    if(editor.error==5)return "DSP LIMIT";
    if(editor.revision==UINT32_MAX)return "DSP READ ONLY";
    return 0;
}
/* 32i: a refused chain (effect memory or DSP load) also shows a popup over
 * the screen for FROST_POPUP_MS. 1 = CHAIN TOO BIG, 2 = DSP LIMIT. */
#ifndef FROST_POPUP_MS
#define FROST_POPUP_MS 2500u
#endif
static unsigned popup_kind,popup_seen;   /* popup_seen: editor.refusals already shown */
static uint32_t popup_until;
static unsigned refusal_code(void)
{return editor.error==4 || preset_client.error==2?1u:editor.error==5 || preset_client.error==3?2u:0u;}
static void popup_tick(uint32_t now)
{
    unsigned code=refusal_code();
    if(editor.refusals!=popup_seen && code){popup_kind=code;popup_until=now+FROST_POPUP_MS;++generation;}
    popup_seen=editor.refusals;
    if(popup_kind && (int32_t)(now-popup_until)>=0){popup_kind=0;++generation;}
}
/* MODEL row of an AMP block: "NN NAME" from its file, "NN DEFAULT" without one.
 * Uppercase; characters the font lacks become '*'. */
static void fcm_model_label(char out[24],int index)
{
    char name[17];unsigned i,n=0;
    out[n++]=(char)('0'+(unsigned)index/10u%10u);out[n++]=(char)('0'+(unsigned)index%10u);out[n++]=' ';
    if(index<0 || !fcm_file_label((unsigned)index,name)){const char *d="DEFAULT";for(i=0;d[i];++i)name[i]=d[i];name[i]=0;}
    for(i=0;name[i] && n<14u;++i){
        char ch=name[i];
        if(ch>='a' && ch<='z')ch=(char)(ch-'a'+'A');
        if(!((ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch==' ' || ch=='-' || ch=='.' || ch=='/' || ch==':' || ch=='+'))ch='*';
        out[n++]=ch;
    }
    out[n]=0;
}
/* Font scaled by an integer factor (5x7 glyphs, 6-pixel advance). */
static void text_big(int x,int y,const char *str,uint16_t color,int scale)
{
    for(;*str;++str,x+=6*scale){unsigned c=(unsigned char)*str,col,row;int a,b;if(c>='a' && c<='z')c-=32;if(c<32 || c>90)continue;
        for(col=0;col<5;++col)for(row=0;row<7;++row)if(frost_font[c-32][col]&(1u<<row))
            for(a=0;a<scale;++a)for(b=0;b<scale;++b)pixel(x+(int)col*scale+a,y+(int)row*scale+b,color);}
}
static void text_bold(int x,int y,const char *str,uint16_t color)
{
    for(;*str;++str,x+=7){unsigned c=(unsigned char)*str,col,row;
        if(c>='a' && c<='z')c-=32;if(c<32 || c>90)continue;
        for(col=0;col<5;++col)for(row=0;row<7;++row)if(frost_font[c-32][col]&(1u<<row)){
            pixel(x+(int)col,y+(int)row,color);pixel(x+(int)col+1,y+(int)row,color);
        }
    }
}
static void text_center(int y,const char *str,uint16_t color,int scale)
{text_big((160-(int)length(str)*6*scale+scale)/2,y,str,color,scale);}
static void settings_value(unsigned row,char out[24])
{
    char *q=out;unsigned n;
    switch(row){
    case SET_IN_GAIN:n=(unsigned)(globals.in_gain_db<0?-globals.in_gain_db:globals.in_gain_db);*q++=globals.in_gain_db<0?'-':'+';q=number(q,n);*q++=' ';*q++='D';*q++='B';*q=0;break;
    case SET_EQ_BASS:case SET_EQ_MIDDLE:case SET_EQ_TREBLE:{int db=globals.eq_db[row-SET_EQ_BASS];*q++=db<0?'-':'+';q=number(q,(unsigned)(db<0?-db:db));*q++=' ';*q++='D';*q++='B';*q=0;break;}
    case SET_OUT_LEVEL:q=number(q,globals.out_level);*q++='%';*q=0;break;
    case SET_OUT_MODE:{const char *t=globals.out_mode?"STEREO":"MONO";while(*t)*q++=*t++;*q=0;break;}
    case SET_CAB:{const char *t=globals.cab_override?"ON":"OFF";while(*t)*q++=*t++;*q=0;break;}
    case SET_BRIGHT:q=number(q,globals.brightness);*q++='%';*q=0;break;
    case SET_HOME:{const char *t=globals.home==HOME_SETLIST?"SETLIST":globals.home==HOME_PRESET?"PRESET":globals.home==HOME_STOMP?"STOMP":"CHAIN";while(*t)*q++=*t++;*q=0;break;}
    case SET_SETLIST_HOLD:{const char *t=globals.setlist_hold?"PREV PART":"TUNER";while(*t)*q++=*t++;*q=0;break;}
    case SET_FIRMWARE:{const char *t=FROST_FIRMWARE_TEXT;while(*t)*q++=*t++;*q=0;break;}
    default:*q=0;
    }
}
static unsigned settings_count(unsigned menu)
{unsigned n=0;while(n<7u && settings_menus[menu][n]!=255u)++n;return n;}
static unsigned settings_entry(void){return settings_menus[settings_menu][settings_pos];}
static void settings_select(unsigned menu,unsigned pos)
{settings_menu=menu;settings_pos=pos;settings_row=settings_entry()<SET_ROWS?settings_entry():SET_BACK;}
static void render_stage2b(const char *message)
{
    static const char *const labels[SET_ROWS]={"INPUT GAIN","OUTPUT LEVEL","OUTPUT MODE","CAB OVERRIDE","BRIGHTNESS","HOME SCREEN","SETLIST HOLD","FIRMWARE","RESET SETTINGS","FACTORY RESET","GLOBAL BASS","GLOBAL MIDDLE","GLOBAL TREBLE","BACK"};
    static const char *const menus[]={"AUDIO","DISPLAY","SYSTEM","GLOBAL EQ"};
    static const char *const titles[SM_MENUS]={"GLOBAL SETTINGS","AUDIO","DISPLAY","SYSTEM","AUDIO / GLOBAL EQ"};
    unsigned i;char value[24];
    if(view==SETTINGS || view==SETTINGS_VALUE){
        unsigned count=settings_count(settings_menu),first=settings_pos>=4?settings_pos-3:0,entry=settings_entry();
        text(5,3,titles[settings_menu],INK);text(125,3,view==SETTINGS_VALUE?"VALUE":"",INK);line(4,12,155,12,WIRE);
        for(i=first;i<count && i<first+4;++i){
            unsigned e=settings_menus[settings_menu][i];int y=18+(int)(i-first)*12;
            if(i==settings_pos){rect(3,y-2,154,11,SELECTION);text(5,y,">",FOCUS);}
            if(e>=SE_AUDIO){text(15,y,menus[e-SE_AUDIO],INK);text(147,y,">",INK);continue;}
            text(15,y,labels[e],INK);settings_value(e,value);
            text(153-(int)length(value)*6,y,value,i==settings_pos && view==SETTINGS_VALUE?FOCUS:INK);
        }
        text(5,72,message?message:entry>=SE_AUDIO?"PRESS TO OPEN":entry==SET_SETLIST_HOLD?"SETLIST VIEW ONLY":
             entry==SET_BACK?(settings_menu==SM_TOP?"PRESS TO SAVE AND EXIT":"PRESS TO GO BACK"):"",INK);
    }else if(view==SETTINGS_CONFIRM){
        unsigned factory=settings_row==SET_FACTORY;
        text(5,3,factory?"FACTORY RESET":"RESET SETTINGS",INK);line(4,12,155,12,WIRE);
        text(5,18,factory?(factory_stage?"REALLY ERASE PRESETS":"ERASE EVERYTHING"):"SETTINGS TO DEFAULTS",INK);
        if(factory && factory_stage)text(5,28,"AMP MODELS AND IRS",INK);
        rect(3,38+(int)settings_confirm*11,154,10,SELECTION);text(5,40+(int)settings_confirm*11,">",FOCUS);
        text(15,40,"CANCEL",INK);text(15,51,factory?(factory_stage?"YES: ERASE ALL":"YES"):"YES: RESET",INK);
        text(5,72,message?message:factory?"CANNOT BE UNDONE":"PRESETS ARE KEPT",INK);
    }else if(view==PRESET_HOME){
        char num[8]="PRESET ";char name[17];unsigned n;
        if(current_preset<FROST_PRESETS && store_preset(current_preset)){
            num[0]=(char)('0'+(current_preset+1)/10);num[1]=(char)('0'+(current_preset+1)%10);num[2]=0;
            text(5,3,num,FOCUS);text(23,3,changed?"*":"",INK);
            store_preset_name(current_preset,name);
            for(n=0;name[n];++n){char c=name[n];if(c>='a' && c<='z')c=(char)(c-32);if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || c==' ' || c=='-' || c=='.' || c=='/' || c==':' || c=='+'))c='*';name[n]=c;}
            if(n<=8)text_center(24,name,INK,3);
            else if(n<=13)text_center(28,name,INK,2);
            else {char a[14],b[14];unsigned k=13,m;while(k && name[k]!=' ')--k;if(!k)k=13;
                for(m=0;m<k;++m)a[m]=name[m];a[m]=0;
                {unsigned j=k+(name[k]==' ');for(m=0;name[j] && m<13;++m,++j)b[m]=name[j];b[m]=0;}
                text_center(18,a,INK,2);text_center(38,b,INK,2);}
        }else {text(5,3,"PRESETS",FOCUS);text_center(28,"NO PRESET",WIRE,2);}
        text(5,72,message?message:"PARA / FOOT: NEXT PRESET",INK);
    }else if(view==SETLIST_HOME){
        char name[24],pos[16],*q;uint32_t song=setlist_song(set_order),part=setlist_part(set_order,set_part);
        setlist_name(set_addr?set_addr+8u:0,name);text(5,3,name[0]?name:"SETLIST",FOCUS);
        if(song && part){
            setlist_name(song+2u,name);text_center(19,name,INK,length(name)<=13?2:1);
            setlist_name(part,name);text_center(39,name,FOCUS,length(name)<=13?2:1);
            preset_label(FROST_FILE_AT(part)[16],name);text(5,59,name,INK);
            q=number(pos,set_order+1u);*q++='/';q=number(q,set_count);*q=0;text(155-(int)length(pos)*6,3,pos,WIRE);
        }else if(setlist_updating()){text_center(26,"UPDATING",FOCUS,2);text_center(48,"PLEASE WAIT",INK,1);}
        else {text_center(26,"NO SETLIST",WIRE,2);text_center(48,"ADD SONGS ON WEBSITE",INK,1);}
        text(5,72,message?message:set_error?"PRESET MISSING / LOAD FAIL":set_pending?"LOADING / WAIT":"TAP NEXT / PARA SONG",INK);
    }else if(view==TUNER){
        static const char *const names[12]={"C","C","D","D","E","F","F","G","G","A","A","B"};
        static const unsigned sharp[12]={0,1,0,1,0,0,1,0,1,0,1,0};
        const uint16_t good=COLOR(72,216,120),near=COLOR(240,212,82);
        unsigned have=tuner_client.have;int cents=0,target=0,k;
        char ref[8];char *q=number(ref,globals.tuner_ref);*q++='H';*q++='Z';*q=0;
        text(124,2,ref,tuner_ref_unlocked?FOCUS:INK);
        if(tuner_ref_unlocked){line(123,0,155,0,FOCUS);line(123,10,155,10,FOCUS);}
        else { /* Small closed lock; pressing PARA unlocks reference editing. */
            line(115,3,115,6,WIRE);line(119,3,119,6,WIRE);line(115,2,119,2,WIRE);rect(114,6,7,4,WIRE);
        }
        if(have){
            int idx=((tuner_client.note%12)+12)%12,oct=tuner_client.note/12-1;
            unsigned in_tune;char o[2]={0,0},cs[8];unsigned n=0;
            cents=(int)(tuner_client.cents+(tuner_client.cents>=0.0f?0.5f:-0.5f));
            in_tune=cents>=-2 && cents<=2;
            text_big(66,13,names[idx],in_tune?good:INK,5);
            if(sharp[idx]){int x=96,y=14;line(x+2,y,x+2,y+12,INK);line(x+6,y,x+6,y+12,INK);line(x,y+4,x+8,y+3,INK);line(x,y+9,x+8,y+8,INK);}
            if(oct>=0 && oct<=9){o[0]=(char)('0'+oct);text_big(97,34,o,INK,2);}
            if(cents>0)cs[n++]='+';else if(cents<0)cs[n++]='-';
            q=number(cs+n,(unsigned)(cents<0?-cents:cents));*q=0;
            text_big(4,23,cs,in_tune?good:INK,2);text(15,40,"CT",MUTED);
            /* Arrows point toward the note: flat -> tighten, sharp -> loosen. */
            for(k=0;k<9;++k){
                line(49+k,24+k/2,49+k,32-k/2,(cents< -2 || in_tune)?good:SELECTION);
                line(121-k,24+k/2,121-k,32-k/2,(cents>2 || in_tune)?good:SELECTION);
            }
            target=cents*9/50;if(target< -9)target=-9;if(target>9)target=9;
            if(!in_tune && !target)target=cents<0?-1:1;
        }else text_big(66,15,"-",MUTED,5);
        /* Nineteen segments arc upward toward the in-tune centre. Only the
         * measured side lights; silence clears every segment and both arrows. */
        for(k=-9;k<=9;++k){
            int distance=k<0?-k:k,x=4+(k+9)*8,y=54+distance;
            unsigned lit=have && ((k>=0 && k<=target) || (k<=0 && k>=target));
            rect(x,y,6,k==0?17:14,lit?(distance<=1?good:distance<=5?near:COLOR(240,120,48)):SELECTION);
        }
        for(k=0;k<4;++k)line(79-k,73+k,79+k,73+k,have?INK:WIRE);
    }else{                                      /* STOMP */
        const FrostControlEffect *d=frost_control_find(editor.types[selected]);char pos[8]="0/6";unsigned on=editor.desired_enabled[selected];
        pos[0]=(char)('1'+editor_position(selected));text(5,3,pos,INK);text(125,3,"STOMP",INK);
        if(editor.types[selected]!=FROST_EMPTY){
            uint16_t color=effect_color(editor.types[selected]);
            if(on)rect(4,15,152,54,color);
            else {line(4,15,155,15,color);line(4,68,155,68,color);line(4,15,4,68,color);line(155,15,155,68,color);}
        }
        {int scale=length(d->short_name)<=8?3:2;
            int top=15+(54-(7*scale+6+7))/2;
            text_center(top,d->short_name,editor.types[selected]==FROST_EMPTY?WIRE:on?BG:MUTED,scale);
            {char nm[27];unsigned k;for(k=0;d->name[k] && k<26;++k)nm[k]=d->name[k];nm[k]=0;text_center(top+7*scale+6,nm,on && editor.types[selected]!=FROST_EMPTY?BG:MUTED,1);}
        }
        text(5,72,message?message:"PARA: FX  FOOT: BYPASS",INK);
    }
}
static void render_popup(void)
{
    const char *title=popup_kind==1?"CHAIN TOO BIG":"DSP LIMIT",*hint="REMOVE AN EFFECT";
    if(!popup_kind)return;
    rect(8,20,144,40,FOCUS);rect(10,22,140,36,BG);
    text(80-(int)length(title)*3,29,title,FOCUS);text(80-(int)length(hint)*3,43,hint,INK);
}
static void render(void)
{
    /* 32j: 46 px blocks (was 42) so a 6-letter bold name (41 px) clears the frame;
     * 8 px arrow gaps, 3 px left for the selection ring at both screen edges. */
    static const unsigned xy[6][2]={{3,16},{57,16},{111,16},{111,46},{57,46},{3,46}};
    const FrostControlEffect *e=frost_control_find(editor.types[selected]);
    unsigned i;const char *message=editor_message();
    rect(0,0,160,80,BG);
    if(view>=SETTINGS){render_stage2b(message);return;}
    if(view==CHAIN){
#ifndef FROST_DSP_USAGE
        char count[8]="0/6";unsigned on=0;
        for(i=0;i<6;++i)on+=editor.desired_enabled[i];count[0]=(char)('0'+on);
#endif
        {char name[17];unsigned n;
            if(current_preset<FROST_PRESETS && store_preset(current_preset))store_preset_name(current_preset,name);
            else {const char *t="UNSAVED";for(n=0;t[n];++n)name[n]=t[n];name[n]=0;}
            for(n=0;name[n];++n){unsigned c=(unsigned char)name[n];if(c>='a' && c<='z')c-=32;name[n]=(char)(c>=32 && c<=90?c:'*');}
            text(5,3,name,INK);
        }
#ifdef FROST_DSP_USAGE
        frost_render_dsp_usage();
#else
        text(119,3,count,INK);
#endif
        arrow(49,27,56,27);arrow(103,27,110,27);arrow(134,38,134,45);arrow(110,57,103,57);arrow(56,57,49,57);
        for(i=0;i<6;++i){
            unsigned slot=editor.order[i];const FrostControlEffect *d=frost_control_find(editor.types[slot]);
            unsigned empty=editor.types[slot]==FROST_EMPTY,on=!empty && editor.desired_enabled[slot];
            int x=(int)xy[i][0],y=(int)xy[i][1];uint16_t color=empty?WIRE:effect_color(editor.types[slot]);
            rect(x,y,46,22,on?color:BG);
            line(x,y,x+45,y,color);line(x,y+21,x+45,y+21,color);line(x,y,x,y+21,color);line(x+45,y,x+45,y+21,color);
            if(slot==selected){int r;for(r=2;r<=3;++r){
                line(x-r,y-r,x+45+r,y-r,FOCUS);line(x-r,y+21+r,x+45+r,y+21+r,FOCUS);
                line(x-r,y-r,x-r,y+21+r,FOCUS);line(x+45+r,y-r,x+45+r,y+21+r,FOCUS);
            }}
            text_bold(x+(46-(int)length(d->short_name)*7+1)/2,y+7,d->short_name,on?BG:MUTED);
        }
        text(5,72,message?message:e->name,INK);
    }else if(view==TYPE_CATEGORY || view==TYPE_MODEL){
        unsigned count=view==TYPE_CATEGORY?FROST_CATEGORIES:picker_count(pick_category);
        unsigned cursor=view==TYPE_CATEGORY?pick_category:pick_model,first=cursor>=4?cursor-3:0;
        text(5,3,view==TYPE_CATEGORY?"TYPE: CATEGORY":frost_category_names[pick_category],INK);line(4,12,155,12,WIRE);
        for(i=first;i<count+1 && i<first+4;++i){
            int y=18+(int)(i-first)*12;const char *label;char row[24];unsigned k=0;
            if(i==cursor){rect(3,y-2,154,11,SELECTION);text(5,y,">",FOCUS);}
            if(i==count)label="BACK";
            else if(view==TYPE_CATEGORY)label=frost_category_names[i];
            else label=frost_control_find(picker_type(pick_category,i))->name;
            while(label[k] && k<21){row[k]=label[k];++k;}row[k]=0;
            text(15,y,row,INK);
            if(view==TYPE_MODEL && i<count && picker_type(pick_category,i)==editor.types[selected])text(147,y,"*",FOCUS);
        }
        text(5,72,message?message:view==TYPE_CATEGORY?"PARA: CATEGORY / OPEN":"PARA: SELECT MODEL",INK);
    }else if(view==PRESETS || view==PRESET_CONFIRM){
        char label[24];
        text(5,3,"PRESETS",INK);line(4,12,155,12,WIRE);
        if(view==PRESETS){
            unsigned first=preset_choice>=4?preset_choice-3:0;
            for(i=first;i<FROST_PRESETS+1 && i<first+4;++i){
                int y=18+(int)(i-first)*12;
                if(i==preset_choice){rect(3,y-2,154,11,SELECTION);text(5,y,">",FOCUS);}
                if(i<FROST_PRESETS)preset_label(i,label);
                text(15,y,i==FROST_PRESETS?"BACK":label,i<FROST_PRESETS && !store_preset(i)?WIRE:INK);
            }
            text(5,72,message?message:"PARA: SELECT / OPEN",INK);
        }else{
            unsigned stored=store_preset(preset_choice)!=0;
            preset_label(preset_choice,label);text(5,18,label,INK);
            text(5,28,preset_action==2?(stored?"OVERWRITE THIS PRESET?":"SAVE CURRENT SOUND?"):stored?"REPLACE ALL EDITS?":"EMPTY PRESET",INK);
            rect(3,38+(int)preset_action*11,154,10,SELECTION);
            text(5,40+(int)preset_action*11,">",FOCUS);
            text(15,40,"CANCEL",INK);text(15,51,"LOAD",stored?INK:WIRE);text(15,62,"SAVE",INK);
            text(5,72,message?message:"SAVED IN THE PEDAL",INK);
        }
    }else{
        unsigned first=parameter>=4?parameter-3:0,total=row_total(e),pk=0,kind=row_kind(e,parameter,&pk);
#ifdef FROST_LINE_TEST
        text(5,3,"FX",INK);
#else
        text(5,3,"MUTE",INK);
#endif
        text(35,3,e->short_name,INK);text(75,3,editor.desired_enabled[selected]?"ON":"OFF",INK);
        text(125,3,view==VALUE?"VALUE":"EDIT",INK);line(4,12,155,12,WIRE);
        for(i=first;i<total && i<first+4;++i){
            int y=18+(int)(i-first)*12;char value[24];const char *label,*shown=value;unsigned ip=0,k=row_kind(e,i,&ip);
            if(i==parameter){rect(3,y-2,154,11,SELECTION);text(5,y,">",FOCUS);}
            if(k==ROW_PARAM){label=e->p[ip].name;formatted(value,&e->p[ip],editor_integer(&editor,selected,ip));
                if(editor.types[selected]==FROST_FCM && !ip)fcm_model_label(value,editor_integer(&editor,selected,0));}
            else if(k==ROW_TYPE){
                unsigned type=view==VALUE && parameter==i?choice:editor.types[selected];
                label="TYPE";shown=frost_control_find(type)->short_name;
            }else if(k==ROW_POSITION){
                static const Parameter position={"POSITION","",1,6,1,1,1,0};
                label="POSITION";formatted(value,&position,(int)(view==VALUE && parameter==i?choice:editor_position(selected))+1);
            }else if(k==ROW_REMOVE){
                label="REMOVE EFFECT";shown=view==VALUE && parameter==i?"CONFIRM?":"";
            }else if(k==ROW_CAB){
                unsigned cab=view==VALUE && parameter==i?choice:ir_cabs[selected];uint32_t crc=0,rec=cab?store_ir_record(cab-1,&crc):0;
                label="CAB";
                if(!cab)shown="BUILT IN";
                else {char *q=value;unsigned k;*q++=(char)('0'+cab/10);*q++=(char)('0'+cab%10);*q++=' ';
                    if(ir_file_client.error[selected]){const char *t="UNAVAILABLE";while(*t)*q++=*t++;}
                    else if(rec)for(k=0;k<15 && FROST_FILE_AT(rec)[k];++k)*q++=(char)FROST_FILE_AT(rec)[k];
                    else {const char *t="EMPTY";while(*t)*q++=*t++;}*q=0;}
            }else {label=k==ROW_BACK?"BACK":"PRESETS";shown="";}
            text(15,y,label,INK);text(153-(int)length(shown)*6,y,shown,i==parameter && view==VALUE?FOCUS:INK);
        }
        text(5,72,message?message:
            view==VALUE?(kind==ROW_PARAM?frost_control_hint(editor.types[selected],pk):kind==ROW_REMOVE?"PRESS: REMOVE / TURN: NO":"PRESS TO CONFIRM"):
            kind==ROW_TYPE?frost_category_names[frost_type_category(editor.types[selected])]:"PARA: SELECT / EDIT",INK);
    }
}
void fw_init(void)
{
    midi_init();
#ifdef FROST_FACTORY_PRESETS
    factory_error=factory_checked=0;
#endif
#ifdef FROST_LINE_TEST
    frost_line_control_init(&line_control);
#endif
    globals_load();setlist_init();global_client_init(&global_client,gp5_ms());globals_apply_backlight();
    settings_menu=settings_pos=0;settings_row=settings_dirty=settings_confirm=para_down=para_long=last_preset_dirty=boot_load_done=0;current_preset=globals.last_preset;
    para_down_ms=last_input_ms=gp5_ms();recall_pending=editor_transport_status=0;
    tuner_client_init(&tuner_client,gp5_ms());foot_down=foot_long=foot_toggled=tuner_ref_dirty=tuner_ref_unlocked=0;foot_down_ms=gp5_ms();tuner_home=CHAIN;
    editor_init(&editor,gp5_ms());selected=1;view=home_view();parameter=choice=changed=0;pick_category=pick_model=foot_held=0;
    preset_client_init(&preset_client);preset_choice=preset_action=0;
    ram_bank_init();capture_client_init(&capture_client);
    {unsigned i;for(i=0;i<6;++i)ir_cabs[i]=0;}ir_file_client_init(&editor);
    fcm_client_init(&fcm_client,gp5_ms());
    generation=1;draw_generation=0;band=80;last_para=gp5_ms()-50u;foot_raw=foot_state=0;foot_edge_ms=foot_raw_ms=gp5_ms()-50u;popup_kind=0;popup_seen=editor.refusals;popup_until=0;
#ifdef FROST_IR_LINK
    if(!irl.started)irl_start();
#endif
#ifdef FROST_DSP_USAGE
    frost_dsp_usage_init(&dsp_usage,gp5_ms());dsp_usage_dirty=0;dsp_usage_band=16;
#endif
#ifdef FROST_BOOT_SPLASH
    boot_band=boot_complete=boot_waiting=0;boot_mark=0;
#endif
}
void fw_dsp_ready(void){
    midi_init();
#ifdef FROST_DSP_USAGE
frost_dsp_usage_init(&dsp_usage,gp5_ms());dsp_usage_dirty=0;dsp_usage_band=16;
#endif
#ifdef FROST_LINE_TEST
frost_line_control_init(&line_control);
#endif
preset_client_init(&preset_client);capture_client_init(&capture_client);ir_file_client_init(&editor);fcm_client_init(&fcm_client,gp5_ms());global_client_init(&global_client,gp5_ms());tuner_client_init(&tuner_client,gp5_ms());foot_down=0;editor_reconnect(&editor,gp5_ms());view=home_view();parameter=0;boot_load_done=0;++generation;}
/* One coherent editor-state unit drives the platform startup indicator. */
unsigned fw_setting_count(void){return 1;}
unsigned fw_level(void){
#ifdef FROST_LINE_TEST
return globals_level(line_control.level)*midi_volume/127u;
#else
return 0;
#endif
}
unsigned fw_active(void){return editor.desired_enabled[selected];}
int fw_take(uint32_t *id,uint32_t *value){(void)id;(void)value;return 0;}
void fw_retry(uint32_t id){(void)id;}
void fw_knob(unsigned knob){
#ifdef FROST_LINE_TEST
frost_line_control_knob(&line_control,knob);
#else
(void)knob;
#endif
}
unsigned fw_editor_ready(void){return !ir_file_client.state && !ir_file_client.recover && !global_client_wanted(&global_client) && !capture_client.state && !preset_client.state && editor.ready && !editor_pending(&editor);}
unsigned fw_editor_interval(void)
{return preset_client.state || capture_client.state || ir_file_client.state || fcm_client.state?0u:20u;}
void fw_editor_transport_status(unsigned status){editor_transport_status=status;}
static void fw_recall_finish(void)
{
    unsigned i;if(!recall_pending || preset_client.state)return;
    if(preset_client.error){
        for(i=0;i<6u;++i){ir_cabs[i]=recall_old_cabs[i];ir_file_client.held_cab[i]=64;}
        current_preset=recall_previous;ir_file_client.next_check=0;
    }else changed=0;
    recall_pending=0;
}
/* Volatile RAM bank save (pre-stage-2 path; kept for the IR SOURCE picker's
 * RAM sources and bank export; the PRESETS menu now saves to flash). */
__attribute__((unused)) static int fw_ram_save(unsigned profile)
{
    if(ram_bank.epoch==UINT32_MAX || preset_client.state || !capture_client_start(&capture_client,&editor,profile))return 0;
    preset_client.error=0;return 1;
}
/* Stored presets (preset_store.h): SAVE captures the compact body and writes
 * it to flash; LOAD uploads a stored record through the atomic recall. */
static int capture_client_store(unsigned preset,const uint32_t *words)
{
    unsigned i,b=0;store_ir_refs[0]=store_ir_refs[1]=0xffffu;
    for(i=0;i<6 && b<2;++i)if(words[3+i]==FROST_IR)store_ir_refs[b++]=ir_cabs[i]?(uint16_t)(ir_cabs[i]-1u):0xffffu;
    if(!store_save_preset(preset,words,gp5_ms()))return 0;
    current_preset=preset;
    if(globals.last_preset!=preset){globals.last_preset=preset;last_preset_dirty=globals_save(gp5_ms())?0u:1u;}
    else last_preset_dirty=0; /* Explicit SAVE only; no duplicate global write. */
    return 1;
}
/* Next/previous STORED preset from `from` (255 = none yet); 255 if none stored. */
static unsigned preset_step(unsigned from,int dir)
{
    unsigned k,i=from<FROST_PRESETS?from:(dir>0?FROST_PRESETS-1u:0u);
    for(k=0;k<FROST_PRESETS;++k){i=(unsigned)(((int)i+dir+(int)FROST_PRESETS)%(int)FROST_PRESETS);if(store_preset(i))return i;}
    return 255;
}
static int fw_flash_save(unsigned preset)
{
    if(preset>=FROST_PRESETS || ir_file_client.state || preset_client.state || !capture_client_start_compact(&capture_client,&editor,preset))return 0;
    preset_client.error=0;return 1;
}
static uint32_t load_words[FROST_PRESET_COMPACT_WORDS];
static int fw_flash_load(unsigned preset)
{
    uint8_t rec[FROST_PST_BYTES];uint32_t r=store_preset(preset);unsigned k,builtin=1,b=0;
    fw_recall_finish();
    if(!r || ir_file_client.state || capture_client.state || preset_client.state)return 0;
    for(k=0;k<FROST_PST_BYTES;++k)rec[k]=FROST_FILE_AT(r)[k];
    if(!store_record_to_words(rec,load_words))return 0;
    for(k=0;k<6u;++k)if(load_words[3+k]==FROST_IR && b<2u){unsigned ref=rec[248+2*b]|rec[249+2*b]<<8;++b;if(ref<63u)builtin=0;}
    if(!(builtin?preset_client_start_quick(&preset_client,&editor,load_words,editor.revision):
                  preset_client_start_snapshot(&preset_client,&editor,load_words,editor.revision)))return 0;
    recall_pending=1;recall_previous=current_preset;for(k=0;k<6u;++k)recall_old_cabs[k]=ir_cabs[k];
    {unsigned b=0;for(k=0;k<6;++k){
        unsigned ref=0xffffu;
        if(load_words[3+k]==FROST_IR && b<2){ref=rec[248+2*b]|rec[249+2*b]<<8;++b;}
        ir_cabs[k]=ref<63?ref+1u:0;
        ir_file_client.types[k]=load_words[3+k];ir_file_client.held_cab[k]=builtin && preset_client.quick?0u:64u;ir_file_client.held_crc[k]=0;
    }}
    ir_file_client.next_check=0;
    capture_client.error=0;
    if(current_preset!=preset){current_preset=preset;last_preset_dirty=1;}
    return 1;
}
#include "midi_control.h"
/* Setlist selection only commits after the preset/asset clients settle. */
static int setlist_start(unsigned order,unsigned part)
{
    uint32_t p;setlist_refresh();p=setlist_part(order,part);
    if(!p || !fw_editor_ready() || !editor_editable(&editor) || set_pending)return 0;
    if(!store_preset(FROST_FILE_AT(p)[16])){set_error=1;++generation;return 0;}
    if(!fw_flash_load(FROST_FILE_AT(p)[16]))return 0;
    set_prev_order=set_order;set_prev_part=set_part;set_order=order;set_part=part;set_pending=1;set_error=0;++generation;return 1;
}
static void setlist_step(int dir,int song_only)
{
    unsigned o,p;uint32_t song;
    setlist_refresh();o=set_order;p=set_part;
    if(!set_count || set_pending)return;
    if(!set_loaded){(void)setlist_start(o,p);return;}
    if(song_only){if(dir>0){if(o+1u>=set_count)return;++o;}else {if(!o)return;--o;}p=0;}
    else if(dir>0){song=setlist_song(o);if(!song)return;if(p+1u<FROST_FILE_AT(song)[1])++p;else {if(o+1u>=set_count)return;++o;p=0;}}
    else {if(p)--p;else {if(!o)return;--o;song=setlist_song(o);if(!song)return;p=FROST_FILE_AT(song)[1]-1u;}}
    (void)setlist_start(o,p);
}
/* "NN NAME" or "NN ----" (uppercase; characters the font lacks become '*'). */
static void preset_label(unsigned i,char out[24])
{
    char name[17];unsigned k,n=0;
    out[n++]=(char)('0'+(i+1)/10);out[n++]=(char)('0'+(i+1)%10);out[n++]=' ';
    store_preset_name(i,name);
    if(!store_preset(i)){out[n++]='-';out[n++]='-';out[n++]='-';out[n++]='-';}
    for(k=0;name[k] && n<22u;++k){
        char ch=name[k];if(ch>='a' && ch<='z')ch=(char)(ch-'a'+'A');
        if(!((ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch==' ' || ch=='-' || ch=='.' || ch=='/' || ch==':' || ch=='+'))ch='*';
        out[n++]=ch;
    }
    out[n]=0;
}
__attribute__((unused)) static int fw_preset_start(unsigned profile)
{
    if(capture_client.state || !preset_client_start(&preset_client,&editor,profile))return 0;
    capture_client.error=0;return 1;
}
unsigned fw_editor_request(uint8_t *p)
{
    midi_service();
    unsigned old=preset_client.state,saving=capture_client.state;
    unsigned n;
    fw_recall_finish();
    if(!saving && !old && !fcm_client.state && !global_client.state && !tuner_client.awaiting){
        n=ir_file_next(&editor,p,gp5_ms());
        if(n){ir_file_client.awaiting=1;return n;}
        if(ir_file_client.state)return 0;
    }
    /* The tuner first (while open, or until the DSP has stopped it). */
    if(!saving && !old && !fcm_client.state && !global_client.state && (view==TUNER || tuner_client.dsp_active || !tuner_client.known) &&
       tuner_client_due(&tuner_client,gp5_ms())){
        tuner_client.want=view==TUNER;
        n=tuner_client_next(&tuner_client,&editor,p,gp5_ms());
        if(n){tuner_client.awaiting=1;return n;}
    }
    /* Global audio settings first, then AMP model sync, only while nothing else is in flight. */
    if(!saving && !old && !fcm_client.state && global_client_due(&global_client,&editor,gp5_ms())){
        n=global_client_next(&global_client,&editor,p,gp5_ms());
        if(n){global_client.awaiting=1;return n;}
    }
    if(!saving && !old && fcm_client_due(&fcm_client,&editor,gp5_ms())){
        n=fcm_client_next(&fcm_client,&editor,p,gp5_ms());
        if(n){fcm_client.awaiting=1;return n;}
    }
    n=saving?capture_client_next(&capture_client,&editor,p,gp5_ms()):preset_client_next(&preset_client,&editor,p,gp5_ms());
    if(saving && !capture_client.state && !capture_client.error)changed=editor.revision!=capture_client.revision;
    if(old && !preset_client.state && !preset_client.error)changed=editor.revision!=preset_client.revision+1u;
    if(old!=preset_client.state || saving!=capture_client.state)++generation;return n;
}
unsigned fw_bank_read(const uint8_t *request,unsigned bytes,uint8_t *reply)
{return bank_read_command(request,bytes,reply);}
#include "live_host.h"
unsigned fw_files_host(const uint8_t *request,unsigned bytes,uint8_t *reply,unsigned max)
{
    if(bytes==2u && request[0]==0x7e && request[1]==0x1e){
        static const char identity[]="FROST_FW:" FROST_VERSION;unsigned i;
        if(max<3u+sizeof identity)return 0;
        reply[0]=0x7e;reply[1]=0x1e;reply[2]=1;
        for(i=0;i<sizeof identity;++i)reply[3+i]=(uint8_t)identity[i];return 3u+sizeof identity;
    }
    /* Website recall uses the same mailbox as MIDI PC; no flash writes. */
    if(bytes>=2u && request[0]==0x7e && (request[1]==0x1c || request[1]==0x1d) && max>=10u){
        reply[0]=0x7e;reply[1]=request[1];reply[2]=0;
        if(request[1]==0x1d && bytes==3u){reply[2]=(uint8_t)midi_load(request[2]);return 3;}
        if(request[1]==0x1c && bytes==2u){
            reply[2]=1;reply[3]=(uint8_t)current_preset;reply[4]=(uint8_t)midi_preset;
            reply[5]=(uint8_t)(preset_client.state || ir_file_client.state || ir_file_client.recover);
            reply[6]=(uint8_t)preset_client.error;reply[7]=(uint8_t)midi_bypass;reply[8]=(uint8_t)midi_volume;reply[9]=(uint8_t)fw_editor_ready();return 10;
        }
        return 3;
    }
    if(bytes==2u && request[0]==0x7e && request[1]==0x1b && max>=8u){
        setlist_refresh();reply[0]=0x7e;reply[1]=0x1b;reply[2]=1;reply[3]=1;
        reply[4]=(uint8_t)set_count;reply[5]=(uint8_t)set_order;reply[6]=(uint8_t)set_part;reply[7]=(uint8_t)set_pending;return 8;
    }
    {unsigned n=live_host(request,bytes,reply,max);if(n)return n;}   /* 7E 14..18: live editing (website) */
#ifdef FROST_IR_LINK
    if(bytes==2 && request[0]==0x7e && request[1]==0x13)return irl_host_status(reply,max);   /* read-only */
#endif
    {unsigned n=frost_files_host(request,bytes,reply,max);if(n)++generation;return n;}
}
int fw_editor_reply(const uint8_t *p,unsigned n)
{
    unsigned old_type=editor.types[selected],had_snapshot=editor.initialized,quick=preset_client.quick;uint32_t old_revision=editor.revision;
    int result;
    if(ir_file_client.awaiting){result=ir_file_reply(&editor,p,n);++generation;return result;}
    if(tuner_client.awaiting){result=tuner_client_reply(&tuner_client,p,n,globals.tuner_ref);if(result || view==TUNER)++generation;return result;}
    if(global_client.awaiting){result=global_client_reply(&global_client,p,n,gp5_ms());++generation;return result;}
    if(fcm_client.awaiting){result=fcm_client_reply(&fcm_client,&editor,p,n,gp5_ms());++generation;return result;}
    if(preset_client.state==PRESET_QUICK_PROBE && editor_transport_status==0x14u){
        /* An explicit old-firmware ACK means this endpoint is unsupported.
         * A timeout alone must not disable fast recall for this whole boot. */
        preset_client.quick_known=2;preset_client.quick=0;preset_client.state=PRESET_DISCOVER;
        preset_client.pending=preset_client.awaiting=0;
    }
    result=capture_client.state?capture_client_reply(&capture_client,&editor,p,n,gp5_ms()):preset_client_reply(&preset_client,&editor,p,n,gp5_ms());
    if(quick && !preset_client.quick){unsigned i;for(i=0;i<6u;++i)ir_file_client.held_cab[i]=64;ir_file_client.next_check=0;}
    if(had_snapshot && editor.ready && (view==PARAMETERS || view==VALUE) && old_type!=editor.types[selected]){view=PARAMETERS;parameter=0;}
    if(had_snapshot && editor.ready && editor.revision!=old_revision)changed=1;
    ++generation;return result;
}
static void settings_leave(void);static void settings_up(void);
/* Footswitch outside the home screens and tuner: one level back, like the
 * BACK rows. A parameter value is already applied while turning; a pending
 * TYPE/POSITION/CAB/REMOVE choice or a confirmation is cancelled. Leaving
 * Global settings saves them, as its BACK row does. */
static void foot_back(void)
{
    switch(view){
    case SETTINGS:settings_up();break;
    case SETTINGS_VALUE:case SETTINGS_CONFIRM:view=SETTINGS;settings_confirm=0;factory_stage=0;break;
    case VALUE:case TYPE_CATEGORY:case PRESETS:view=PARAMETERS;break;
    case TYPE_MODEL:view=TYPE_CATEGORY;break;
    case PRESET_CONFIRM:view=PRESETS;break;
    default:view=home_view();break;                        /* PARAMETERS */
    }
}
static void foot_press(uint32_t now)
{
    unsigned mode;
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    if(ir_file_client.state || preset_client.state || capture_client.state)return;
    last_input_ms=now;
    if(view==TUNER){view=tuner_home;tuner_client.want=0;if(tuner_ref_dirty && globals_save(now))tuner_ref_dirty=0;++generation;return;}
    if(!is_home_view(view)){foot_back();++generation;return;}
    if(view==SETLIST_HOME){foot_down=1;foot_long=0;foot_down_ms=now;foot_toggled=0;return;}
    mode=foot_mode();foot_held=0;
    if(is_home_view(view) && !(view!=PRESET_HOME && mode)){        /* hold opens the tuner */
        foot_down=1;foot_long=0;foot_down_ms=now;foot_toggled=0;
        if(view==PRESET_HOME)return;                                  /* next preset on release */
        if(editor_queue_enable(&editor,selected,!editor.desired_enabled[selected])){foot_toggled=1+selected;changed=1;++generation;}
        return;
    }
    if(mode==1){if(editor_queue_parameter(&editor,selected,3,1)){foot_held=1+selected;changed=1;++generation;}return;}
    if(mode==2){if(editor_queue_enable(&editor,selected,1)){foot_held=1+selected;changed=1;++generation;}return;}
    if(editor_queue_enable(&editor,selected,!editor.desired_enabled[selected])){changed=1;++generation;}
}
/* Release only ends a momentary action started by fw_foot on the same block. */
static void foot_release(void)
{
    unsigned slot;
    if(foot_down){
        foot_down=0;
        if(!foot_long && view==SETLIST_HOME)setlist_step(1,0);
        if(!foot_long && view==PRESET_HOME){unsigned next=preset_step(current_preset,1);if(next!=255u && fw_flash_load(next))++generation;}
        foot_long=0;return;
    }
    if(!foot_held)return;
    slot=foot_held-1u;foot_held=0;
    if(editor.types[slot]==FROST_DD3)(void)editor_queue_parameter(&editor,slot,3,0);
    else if(editor.types[slot]==FROST_VB2)(void)editor_queue_enable(&editor,slot,0);
    changed=1;++generation;
}
/* Footswitch debounce. The platform reports every raw level change of the
 * switch, contact bounce included. The old filter only spaced presses 35 ms
 * apart, so a bounce on RELEASE (press, hold > 35 ms, release, bounce-press)
 * was a second press: the block toggled twice and went back to its previous
 * state, mostly with fast repeated presses. Now each edge acts at once, then
 * the switch is locked for FROST_FOOT_DEBOUNCE_MS; a level that differs when
 * the lock ends (a very short tap's release) is applied from fw_draw. */
#ifndef FROST_FOOT_DEBOUNCE_MS
#define FROST_FOOT_DEBOUNCE_MS 35u
#endif
static void foot_debounce(uint32_t now)
{
    uint32_t open;
    if(foot_raw==foot_state || (uint32_t)(now-foot_edge_ms)<FROST_FOOT_DEBOUNCE_MS)return;
    open=foot_edge_ms+FROST_FOOT_DEBOUNCE_MS;                   /* a held-back edge counts from when it could act */
    foot_state=foot_raw;foot_edge_ms=(int32_t)(foot_raw_ms-open)>0?foot_raw_ms:open;
    if(foot_state)foot_press(now);else foot_release();
}
void fw_foot(void)
{
    uint32_t now=gp5_ms();
    foot_debounce(now);                                             /* a held-back edge first */
    if(foot_raw && foot_state){foot_state=0;foot_release();}   /* no release reported (host benches) */
    foot_raw=1;foot_raw_ms=now;foot_debounce(now);
}
void fw_foot_release(void){uint32_t now=gp5_ms();foot_debounce(now);foot_raw=0;foot_raw_ms=now;foot_debounce(now);}
static void settings_open(void){view=SETTINGS;settings_select(SM_TOP,0);settings_confirm=0;factory_stage=0;}
/* One level up: a submenu returns to its entry in the parent; the top saves and leaves. */
static void settings_up(void)
{
    unsigned parent=settings_menu==SM_EQ?SM_AUDIO:SM_TOP,i,want=settings_menu==SM_EQ?SE_EQ:SE_AUDIO+settings_menu-SM_AUDIO;
    if(settings_menu==SM_TOP){settings_leave();return;}
    for(i=0;i<settings_count(parent);++i)if(settings_menus[parent][i]==want)break;
    settings_select(parent,i);
}
static void settings_leave(void)
{
    if(settings_dirty || last_preset_dirty){globals.last_preset=current_preset;if(globals_save(gp5_ms()))settings_dirty=last_preset_dirty=0;}
    view=home_view();
}
static void settings_press(void)
{
    if(view==SETTINGS){
        unsigned entry=settings_entry();
        if(entry>=SE_AUDIO)settings_select(entry==SE_EQ?SM_EQ:SM_AUDIO+entry-SE_AUDIO,0);
        else if(settings_row==SET_BACK)settings_up();
        else if(settings_row==SET_FIRMWARE){}
        else if(settings_row==SET_RESET || settings_row==SET_FACTORY){view=SETTINGS_CONFIRM;settings_confirm=0;factory_stage=0;}
        else view=SETTINGS_VALUE;
    }else if(view==SETTINGS_VALUE)view=SETTINGS;
    else {                                     /* SETTINGS_CONFIRM: choice 0 = CANCEL */
        if(!settings_confirm){view=SETTINGS;return;}
        if(settings_row==SET_FACTORY && !factory_stage){factory_stage=1;settings_confirm=0;return;}   /* asks twice */
        if(settings_row==SET_FACTORY){globals_factory_erase();current_preset=255;
#ifdef FROST_FACTORY_PRESETS
            factory_error=factory_checked=0;boot_load_done=0;
#endif
        }
        else {unsigned keep=globals.last_preset,factory=globals.factory_state;globals_defaults(&globals);globals.last_preset=keep;globals.factory_state=factory;}
        globals_apply_backlight();settings_dirty=1;(void)globals_save(gp5_ms());settings_dirty=0;
        view=SETTINGS;settings_confirm=0;factory_stage=0;
    }
}
static void settings_turn(int d)
{
    if(view==SETTINGS){int n=(int)settings_count(settings_menu);settings_select(settings_menu,(unsigned)(((int)settings_pos+d%n+n)%n));return;}
    if(view==SETTINGS_CONFIRM){settings_confirm=(unsigned)(((int)settings_confirm+d%2+2)%2);return;}
    switch(settings_row){
    case SET_EQ_BASS:case SET_EQ_MIDDLE:case SET_EQ_TREBLE:{int v=globals.eq_db[settings_row-SET_EQ_BASS]+d;globals.eq_db[settings_row-SET_EQ_BASS]=v<-12?-12:v>12?12:v;break;}
    case SET_IN_GAIN:{int v=globals.in_gain_db+d;globals.in_gain_db=v<-20?-20:v>20?20:v;break;}
    case SET_OUT_LEVEL:{int v=(int)globals.out_level+d;globals.out_level=(unsigned)(v<0?0:v>100?100:v);break;}
    case SET_OUT_MODE:globals.out_mode=(unsigned)(((int)globals.out_mode+d%2+2)%2);break;
    case SET_CAB:globals.cab_override=(unsigned)(((int)globals.cab_override+d%2+2)%2);break;
    case SET_BRIGHT:{int v=(int)globals.brightness+10*d;globals.brightness=(unsigned)(v<0?0:v>100?100:v);globals_apply_backlight();break;}
    case SET_HOME:globals.home=(unsigned)(((int)globals.home+d%HOMES+HOMES)%HOMES);break;
    case SET_SETLIST_HOLD:globals.setlist_hold=(unsigned)(((int)globals.setlist_hold+d%2+2)%2);break;
    default:return;
    }
    settings_dirty=1;
}
void fw_para_release(void)
{
    if(!para_down)return;
    para_down=0;
    if(para_long || !is_home_view(view)){para_long=0;return;}
    if(view==PRESET_HOME || view==SETLIST_HOME)view=CHAIN;                    /* edit the current sound */
    else {view=PARAMETERS;parameter=0;}                 /* chain and stomp: the selected effect */
    ++generation;
}
void fw_para_push(void)
{
    const FrostControlEffect *e=frost_control_find(editor.types[selected]);
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    uint32_t now=gp5_ms();if(now-last_para<35u)return;last_para=now;last_input_ms=now;
    if(ir_file_client.state || preset_client.state || capture_client.state)return;
    if(view==TUNER){tuner_ref_unlocked=!tuner_ref_unlocked;++generation;return;}
    if(is_home_view(view)){para_down=1;para_long=0;para_down_ms=now;return;}   /* decided on release / after the hold time */
    if(view==SETTINGS || view==SETTINGS_VALUE || view==SETTINGS_CONFIRM){settings_press();++generation;return;}
    if(view==CHAIN){view=PARAMETERS;parameter=0;}
    else if(view==PRESETS){
        if(preset_choice==FROST_PRESETS)view=PARAMETERS;
        else {view=PRESET_CONFIRM;preset_action=0;}
    }else if(view==PRESET_CONFIRM){
        if(!preset_action)view=PRESETS;
        else if(preset_action==1){
            if(fw_flash_load(preset_choice)){view=home_view();changed=1;}
        }else if(fw_flash_save(preset_choice)){view=home_view();}
    }
    else if(view==TYPE_CATEGORY){
        if(pick_category==FROST_CATEGORIES)view=PARAMETERS;
        else {unsigned i,n=picker_count(pick_category);view=TYPE_MODEL;pick_model=0;
            for(i=0;i<n;++i)if(picker_type(pick_category,i)==editor.types[selected])pick_model=i;}
    }else if(view==TYPE_MODEL){
        if(pick_model==picker_count(pick_category))view=TYPE_CATEGORY;
        else {
            if(editor_editable(&editor) && picker_type(pick_category,pick_model)!=editor.types[selected]){
                uint8_t types[6],order[6];unsigned i;
                for(i=0;i<6;++i){types[i]=editor.types[i];order[i]=editor.order[i];}
                types[selected]=(uint8_t)picker_type(pick_category,pick_model);
                if(editor_queue_route(&editor,types,order,editor.pre))changed=1;
            }
            view=PARAMETERS;
        }
    }
    else if(view==PARAMETERS){
        unsigned kind=row_kind(e,parameter,0);
        if(kind==ROW_BACK)view=home_view();
        else if(kind==ROW_TYPE && editor_editable(&editor)){view=TYPE_CATEGORY;pick_category=frost_type_category(editor.types[selected]);}
        else if(kind==ROW_CAB){choice=ir_cabs[selected];view=VALUE;}
        else if(kind==ROW_PRESETS){view=PRESETS;preset_choice=0;}
        else if(kind==ROW_REMOVE){if(editor_editable(&editor))view=VALUE;}  /* asks first: CONFIRM? */
        else if(editor_editable(&editor)){
            choice=kind==ROW_TYPE?editor.types[selected]:editor_position(selected);view=VALUE;
        }
    }else if(row_kind(e,parameter,0)==ROW_CAB){
        ir_cabs[selected]=(uint16_t)choice;ir_file_client.held_cab[selected]=64;ir_file_client.next_check=0;changed=1;view=PARAMETERS;
    }else{
        unsigned kind=row_kind(e,parameter,0);
        if(editor_editable(&editor) && kind!=ROW_PARAM){
            uint8_t types[6],order[6];unsigned i,pos=editor_position(selected);
            for(i=0;i<6;++i){types[i]=editor.types[i];order[i]=editor.order[i];}
            if(kind==ROW_TYPE)types[selected]=(uint8_t)choice;
            else if(kind==ROW_REMOVE){types[selected]=(uint8_t)FROST_EMPTY;parameter=0;}
            else if(kind==ROW_POSITION){
                if(choice>pos){for(i=pos;i<choice;++i)order[i]=order[i+1];order[choice]=(uint8_t)selected;}
                else {for(i=pos;i>choice;--i)order[i]=order[i-1];order[choice]=(uint8_t)selected;}
            }
            if(editor_queue_route(&editor,types,order,editor.pre))changed=1;
        }
        view=PARAMETERS;
    }
    ++generation;
}
void fw_para_turn(int detents)
{
    const FrostControlEffect *e=frost_control_find(editor.types[selected]);
#ifdef FROST_BOOT_SPLASH
    if(!boot_complete)return;
#endif
    if(!detents)return;
    last_input_ms=gp5_ms();
    if(view==SETTINGS || view==SETTINGS_VALUE || view==SETTINGS_CONFIRM){settings_turn(detents);++generation;return;}
    if(view==TUNER){int r;if(!tuner_ref_unlocked)return;r=detents>20?450:detents< -20?430:(int)globals.tuner_ref+detents;globals.tuner_ref=(unsigned)(r<430?430:r>450?450:r);tuner_ref_dirty=1;tuner_client_update(&tuner_client,globals.tuner_ref);++generation;return;}
    if(view==SETLIST_HOME){setlist_step(detents>0?1:-1,1);++generation;return;}
    if(view==PRESET_HOME){
        if(!(preset_client.state || capture_client.state)){
            unsigned next=preset_step(current_preset,detents>0?1:-1);if(next!=255u)(void)fw_flash_load(next);
        }
        ++generation;return;
    }
    if(view==CHAIN || view==STOMP){int pos=(int)editor_position(selected);selected=editor.order[(pos+detents%6+6)%6];}
    else if(ir_file_client.state || preset_client.state || capture_client.state)return;
    else if(view==PRESETS){int count=(int)FROST_PRESETS+1;preset_choice=(unsigned)(((int)preset_choice+detents%count+count)%count);}
    else if(view==PRESET_CONFIRM)preset_action=(unsigned)(((int)preset_action+detents%3+3)%3);
    else if(view==TYPE_CATEGORY){int count=FROST_CATEGORIES+1;pick_category=(unsigned)(((int)pick_category+detents%count+count)%count);}
    else if(view==TYPE_MODEL){int count=(int)picker_count(pick_category)+1;pick_model=(unsigned)(((int)pick_model+detents%count+count)%count);}
    else if(view==PARAMETERS){int count=(int)row_total(e);parameter=(unsigned)(((int)parameter+detents%count+count)%count);}
    else if(row_kind(e,parameter,0)==ROW_REMOVE)view=PARAMETERS;          /* turning cancels the removal */
    else if(row_kind(e,parameter,0)==ROW_CAB){choice=(unsigned)(((int)choice+detents%64+64)%64);}
    else if(editor_editable(&editor)){
        unsigned pk=0,kind=row_kind(e,parameter,&pk);
        if(kind==ROW_PARAM){
            const Parameter *p=&e->p[pk];int64_t value=(int64_t)editor_integer(&editor,selected,pk)+(int64_t)detents*p->step;
            if(value<p->minimum)value=p->minimum;if(value>p->maximum)value=p->maximum;
            if(editor_queue_parameter(&editor,selected,pk,(int)value))changed=1;
        }else if(kind==ROW_TYPE){
            int index=choice==FROST_EMPTY?FROST_EFFECTS:(int)choice,count=FROST_EFFECTS+1;
            index=(index+detents%count+count)%count;choice=index==FROST_EFFECTS?FROST_EMPTY:(unsigned)index;
        }else choice=(unsigned)(((int)choice+detents%6+6)%6);
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
    {
        uint32_t now=gp5_ms();
#ifdef FROST_IR_LINK
        irl_poll(now);
#endif
#ifdef FROST_FACTORY_PRESETS
        if(!factory_checked && fw_editor_ready()){
            if(factory_store_tick(now)){++generation;return 1;}
            factory_checked=1;
        }
#endif
        foot_debounce(now);
        if(para_down && !para_long && is_home_view(view) && (uint32_t)(now-para_down_ms)>=FROST_LONG_PRESS_MS){para_long=1;settings_open();++generation;}
        if(foot_down && !foot_long && is_home_view(view) && (uint32_t)(now-foot_down_ms)>=FROST_LONG_PRESS_MS){
            foot_long=1;
            if(foot_toggled){unsigned s=foot_toggled-1u;(void)editor_queue_enable(&editor,s,!editor.desired_enabled[s]);foot_toggled=0;}   /* undo the press */
            if(view==SETLIST_HOME && globals.setlist_hold){setlist_step(-1,0);}
            else {tuner_home=view;view=TUNER;tuner_client.want=1;tuner_client.have=0;tuner_ref_unlocked=0;}++generation;
        }
        if(view==CHAIN && globals.home!=HOME_CHAIN && !para_down && !foot_down && (uint32_t)(now-last_input_ms)>=FROST_HOME_RETURN_MS && editor_editable(&editor)){view=home_view();++generation;}
        /* Preset selection is RAM-only. Background flash writes stop the IR
         * audio IRQ; persist the boot selection only on explicit SAVE/settings. */
        {uint32_t old_crc=set_crc,old_addr=set_addr,old_epoch=set_epoch;unsigned old_count=set_count;setlist_refresh();
         if(old_crc!=set_crc || old_count!=set_count || old_addr!=set_addr ||
            (old_epoch==UINT32_MAX)!=(set_epoch==UINT32_MAX))++generation;}
        if(set_pending && !preset_client.state && !ir_file_client.state && editor.ready){
            if(preset_client.error){set_order=set_prev_order;set_part=set_prev_part;set_loaded=0;set_error=1;}
            else if(fw_editor_ready()){set_loaded=1;set_error=0;}
            if(preset_client.error || fw_editor_ready()){set_pending=0;++generation;}
        }
        if(view==SETLIST_HOME && set_count)boot_load_done=1;
        if(view==SETLIST_HOME && !set_loaded && !set_pending && !set_error && set_count && fw_editor_ready())(void)setlist_start(set_order,set_part);
        if(!boot_load_done && fw_editor_ready()){
            boot_load_done=1;
            if(view==SETLIST_HOME && set_count){(void)setlist_start(set_order,set_part);last_preset_dirty=0;}
            else if(globals.last_preset<FROST_PRESETS && store_preset(globals.last_preset) && fw_flash_load(globals.last_preset)){last_preset_dirty=0;++generation;}
#ifdef FROST_EMPTY_BOOT_CHAIN
            /* 2026-11-18 (user): without a saved preset to recall, start with an
             * empty chain (all six slots empty) instead of the built-in demo chain. */
            else{uint8_t types[6],order[6];unsigned i;for(i=0;i<6;++i){types[i]=(uint8_t)FROST_EMPTY;order[i]=(uint8_t)i;}
                if(editor_queue_route(&editor,types,order,editor.pre))++generation;}
#endif
        }
    }
#ifdef FROST_DSP_USAGE
    if(frost_dsp_usage_tick(&dsp_usage,gp5_ms()))dsp_usage_dirty=1;
#endif
    popup_tick(gp5_ms());
    if(force || draw_generation!=generation){render();render_popup();draw_generation=generation;band=0;
#ifdef FROST_DSP_USAGE
        dsp_usage_dirty=0;dsp_usage_band=16;
#endif
    }
    if(band>=80){
#ifdef FROST_DSP_USAGE
        if(view==CHAIN && dsp_usage_dirty){frost_render_dsp_usage();dsp_usage_dirty=0;dsp_usage_band=0;}
        if(view==CHAIN && dsp_usage_band<16){
            gp5_blit(0,dsp_usage_band,160,8,framebuffer+dsp_usage_band*160);
            dsp_usage_band+=8;return dsp_usage_band<16;
        }
#endif
        return 0;
    }
    gp5_blit(0,band,160,8,framebuffer+band*160);band+=8;return band<80;
}
