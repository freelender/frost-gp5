#ifndef FROST_GLOBAL_SETTINGS_H
#define FROST_GLOBAL_SETTINGS_H
/* Stage 2b global settings (docs/User%20Guide.md): one 64-byte record in
 * the group file glb-0 (firmware FGLB) on controller flash area A, written
 * copy-on-write like presets (preset_store.h). Input gain, cabinet override and
 * output mode go to the DSP (firmware/protocol/global_wire.h) through a small
 * reconcile client; output level scales the volume knob's level; brightness
 * drives the backlight. Main-loop code only. */
#include "preset_store.h"
#include "firmware/protocol/global_wire.h"
#include "firmware/version.h"
#define FROST_FIRMWARE_TEXT FROST_VERSION_DISPLAY
#define FROST_GLB_FIRM 0x424c4746u            /* "FGLB" */
#define FROST_GLB_BYTES 64u
enum { HOME_CHAIN, HOME_PRESET, HOME_STOMP, HOME_SETLIST, HOMES };
#define FROST_GLOBAL_GROUP_BYTES FROST_GROUP_DATA
/* Setlists share the unused tail of glb-0. No extra storage sector. */
static uint32_t globals_record(void)
{uint32_t d=store_group(FROST_GLB_FIRM,"glb-0",FROST_GLOBAL_GROUP_BYTES);return d?d:store_group(FROST_GLB_FIRM,"glb-0",FROST_GLB_BYTES);}
typedef struct {
    int eq_db[3];                    /* output BASS/MIDDLE/TREBLE, -12..12 dB */
    int in_gain_db;                  /* -20..+20 */
    unsigned out_level;              /* 0..100 % */
    unsigned out_mode;               /* 0 mono, 1 stereo */
    unsigned cab_override;           /* 0 off, 1 on: every IR block bypassed */
    unsigned brightness;             /* 0..100 %, steps of 10 */
    unsigned home;                   /* HOME_* */
    unsigned last_preset;            /* 0..59, 255 = none */
    unsigned tuner_ref;              /* tuner reference A4, 430..450 Hz */
    unsigned setlist_hold;           /* 0 tuner, 1 previous part; Setlist only */
    unsigned factory_state;          /* 0 fresh, 1 initialized/skipped, 2 seeding */
    unsigned tap_note;               /* 32t: 0 quarter, 1 dotted eighth, 2 eighth */
    unsigned tempo_light;            /* 32t: 1 = footswitch LED flashes the tempo */
    unsigned tap_tempo;              /* 32u: 1 = footswitch hold on a tempo effect taps; 0 (default) = as before */
} FrostGlobals;
static FrostGlobals globals;
/* Performance controls are volatile: MIDI never saves global preferences. */
static unsigned midi_bypass;
static unsigned global_output_flags(void){return globals.out_mode | (midi_bypass?FROST_GLOBAL_BYPASS:0u);}
void gp5_backlight_duty(unsigned percent);
static void globals_defaults(FrostGlobals *g)
{g->eq_db[0]=g->eq_db[1]=g->eq_db[2]=0;g->in_gain_db=0;g->out_level=100;g->out_mode=0;g->cab_override=0;g->brightness=100;g->home=HOME_CHAIN;g->last_preset=255;g->tuner_ref=440;g->setlist_hold=1;g->factory_state=0;g->tap_note=0;g->tempo_light=1;g->tap_tempo=0;}
static int globals_valid(const FrostGlobals *g)
{return g->eq_db[0]>=-12 && g->eq_db[0]<=12 && g->eq_db[1]>=-12 && g->eq_db[1]<=12 && g->eq_db[2]>=-12 && g->eq_db[2]<=12 && g->in_gain_db>=-20 && g->in_gain_db<=20 && g->out_level<=100 && g->out_mode<=1 && g->cab_override<=1 &&
        g->brightness<=100 && g->brightness%10u==0 && g->home<HOMES && g->setlist_hold<=1u && (g->last_preset<FROST_PRESETS || g->last_preset==255) && g->tuner_ref>=430u && g->tuner_ref<=450u && g->tap_note<=2u && g->tempo_light<=1u && g->tap_tempo<=1u;}
/* record: 'FG' 1 0 | in_gain(i8) out_level out_mode cab brightness home last_preset 0 | 0... | crc32 @60 */
static void globals_load(void)
{
    char name[8];uint32_t d;FrostGlobals g;
    globals_defaults(&globals);
    store_group_name(name,'g','l','b',0);
    if(!(d=globals_record()))return;
    if(FROST_FILE_AT(d)[0]!=0x46 || FROST_FILE_AT(d)[1]!=0x47 || FROST_FILE_AT(d)[2]!=1u ||
       files_crc(d,60)!=files_word(d+60))return;
    g.in_gain_db=(int)(int8_t)FROST_FILE_AT(d)[4];g.out_level=FROST_FILE_AT(d)[5];g.out_mode=FROST_FILE_AT(d)[6];
    g.cab_override=FROST_FILE_AT(d)[7];g.brightness=FROST_FILE_AT(d)[8];g.home=FROST_FILE_AT(d)[9];g.last_preset=FROST_FILE_AT(d)[10];
    g.tuner_ref=FROST_FILE_AT(d)[11]?400u+FROST_FILE_AT(d)[11]:440u;   /* 0 (a 31v record) = 440 */
    g.setlist_hold=FROST_FILE_AT(d)[12]?FROST_FILE_AT(d)[12]-1u:1u;
    g.factory_state=FROST_FILE_AT(d)[13]<=2u?FROST_FILE_AT(d)[13]:1u;
    for(unsigned i=0;i<3u;++i)g.eq_db[i]=(int)(int8_t)FROST_FILE_AT(d)[14u+i];
    g.tap_note=FROST_FILE_AT(d)[17]?FROST_FILE_AT(d)[17]-1u:0u;       /* 32t; 0 (older record) = quarter */
    g.tempo_light=FROST_FILE_AT(d)[18]?FROST_FILE_AT(d)[18]-1u:1u;    /* 32t; 0 (older record) = on */
    g.tap_tempo=FROST_FILE_AT(d)[19]?FROST_FILE_AT(d)[19]-1u:0u;      /* 32u; 0 (older record) = off */
    if(globals_valid(&g))globals=g;
}
/* 1 = written (or nothing to do). */
static int globals_save(uint32_t now)
{
    char name[8];unsigned i,bytes=FROST_GLB_BYTES;uint32_t old;
    if(files_host_written && (uint32_t)(now-files_host_write_ms)<FROST_STORE_GUARD_MS)return 0;
    old=store_group(FROST_GLB_FIRM,"glb-0",FROST_GLOBAL_GROUP_BYTES);
    if(old){bytes=FROST_GLOBAL_GROUP_BYTES;for(i=64u;i<bytes;++i)store_buf[i]=FROST_FILE_AT(old)[i];}
    for(i=0;i<FROST_GLB_BYTES;++i)store_buf[i]=0;
    store_buf[0]=0x46;store_buf[1]=0x47;store_buf[2]=1;
    store_buf[4]=(uint8_t)(int8_t)globals.in_gain_db;store_buf[5]=(uint8_t)globals.out_level;store_buf[6]=(uint8_t)globals.out_mode;
    store_buf[7]=(uint8_t)globals.cab_override;store_buf[8]=(uint8_t)globals.brightness;store_buf[9]=(uint8_t)globals.home;
    store_buf[10]=(uint8_t)globals.last_preset;store_buf[11]=(uint8_t)(globals.tuner_ref-400u);
    store_buf[12]=(uint8_t)(globals.setlist_hold+1u);
    store_buf[13]=(uint8_t)globals.factory_state;
    for(i=0;i<3u;++i)store_buf[14u+i]=(uint8_t)(int8_t)globals.eq_db[i];
    store_buf[17]=(uint8_t)(globals.tap_note+1u);store_buf[18]=(uint8_t)(globals.tempo_light+1u);store_buf[19]=(uint8_t)(globals.tap_tempo+1u);
    frost_wire_put32(store_buf+60,store_crc_ram(store_buf,60));
    store_group_name(name,'g','l','b',0);
    return store_write_group(FROST_GLB_FIRM,name,bytes);
}
/* 0 % keeps a faint backlight (1 %) so the screen can still be read in the dark. */
static void globals_apply_backlight(void){gp5_backlight_duty(globals.brightness?globals.brightness:1u);}
static inline unsigned globals_level(unsigned knob_level){return knob_level*globals.out_level/100u;}
/* Factory reset: erase the whole storage area (presets, AMP models, IRs, settings). */
static void globals_factory_erase(void)
{
    unsigned s;
    for(s=0;s<FROST_FILES_SECTORS;++s)if(!store_sector_blank(s))(void)frost_flash_erase(FROST_FILES_BASE+s*FROST_FILES_SECTOR);
    gp5_uart0_rx_err_clear();gp5_uart0_rx_int_clear();++files_epoch;
    globals_defaults(&globals);
}

/* ---- DSP sync (reconcile, like fcm_client.h) ---- */
enum { GLB_IDLE, GLB_SENT };
typedef struct {
    unsigned state,awaiting,known,retries;
    unsigned eq_known;int dsp_eq[3];
    int dsp_gain_ddb;unsigned dsp_cab,dsp_mode;
    uint32_t next_check;uint8_t saved[FROST_GLOBAL_REQUEST_BYTES];
} FrostGlobalClient;
static void global_client_init(FrostGlobalClient *c,uint32_t now)
{c->eq_known=0;c->dsp_eq[0]=c->dsp_eq[1]=c->dsp_eq[2]=0;c->state=c->awaiting=c->known=c->retries=0;c->dsp_gain_ddb=0;c->dsp_cab=c->dsp_mode=0;c->next_check=now;}
static int global_client_base_wanted(const FrostGlobalClient *c)
{return !c->known || c->dsp_gain_ddb!=globals.in_gain_db*10 || c->dsp_cab!=globals.cab_override || c->dsp_mode!=global_output_flags();}
static int global_client_wanted(const FrostGlobalClient *c)
{return global_client_base_wanted(c) || !c->eq_known || c->dsp_eq[0]!=globals.eq_db[0] || c->dsp_eq[1]!=globals.eq_db[1] || c->dsp_eq[2]!=globals.eq_db[2];}
static int global_client_due(const FrostGlobalClient *c,const FrostEditor *e,uint32_t now)
{
    /* Bypass must not starve behind a continuous expression stream. It may
     * precede unsent parameter edits, never an immutable in-flight retry or route. */
    unsigned bypass_changed=midi_bypass!=((c->dsp_mode&FROST_GLOBAL_BYPASS)!=0);
    unsigned idle=!editor_pending(e) || (bypass_changed && !e->saved_len && !e->awaiting && !e->route_state);
    return c->state!=GLB_IDLE || (editor_editable(e) && idle && (int32_t)(now-c->next_check)>=0 && global_client_wanted(c));
}
static unsigned global_client_next(FrostGlobalClient *c,FrostEditor *e,uint8_t *p,uint32_t now)
{
    unsigned i;uint32_t token;
    if(c->awaiting)return 0;
    if(c->state==GLB_IDLE){
        if(!global_client_wanted(c))return 0;
        if(!(token=editor_token(e)))return 0;
        if(global_client_base_wanted(c)){
        editor_header(c->saved,FROST_GLOBAL_COMMAND,FROST_GLOBAL_REQUEST_BYTES,token);
        frost_wire_put32(c->saved+12,FROST_GLOBAL_SET);frost_wire_put32(c->saved+16,(uint32_t)(globals.in_gain_db*10));
        frost_wire_put32(c->saved+20,globals.cab_override);frost_wire_put32(c->saved+24,global_output_flags());
        }else{
            editor_header(c->saved,FROST_GLOBAL_EQ_COMMAND,FROST_GLOBAL_REQUEST_BYTES,token);
            frost_wire_put32(c->saved+12,FROST_GLOBAL_SET);
            for(i=0;i<3u;++i)frost_wire_put32(c->saved+16+4*i,(uint32_t)globals.eq_db[i]);
        }
        c->state=GLB_SENT;c->retries=0;
    }else if(++c->retries>4u){c->state=GLB_IDLE;c->known=c->eq_known=0;c->next_check=now+1000u;return 0;}
    for(i=0;i<FROST_GLOBAL_REQUEST_BYTES;++i)p[i]=c->saved[i];
    return FROST_GLOBAL_REQUEST_BYTES;
}
static int global_client_reply(FrostGlobalClient *c,const uint8_t *p,unsigned n,uint32_t now)
{
    uint32_t status;
    c->awaiting=0;
    if(!p || n!=FROST_GLOBAL_REPLY_BYTES || p[0]!=c->saved[0] || p[1]!=0x7f || p[2]!=n || p[3] ||
       frost_wire_get32(p+4)!=1u || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;   /* resend */
    status=frost_wire_get32(p+12);c->state=GLB_IDLE;
    if(c->saved[0]==0x50){
        c->eq_known=status==FROST_SLOT_APPLIED;
        if(c->eq_known)for(unsigned i=0;i<3u;++i)c->dsp_eq[i]=(int)(int32_t)frost_wire_get32(p+16+4*i);
        c->next_check=now+(c->eq_known?0u:1000u);return c->eq_known;
    }
    c->known=1;
    c->dsp_gain_ddb=(int)(int32_t)frost_wire_get32(p+16);c->dsp_cab=frost_wire_get32(p+20);c->dsp_mode=frost_wire_get32(p+24);
    if(status!=FROST_SLOT_APPLIED){c->known=status==FROST_SLOT_BUSY;c->next_check=now+(status==FROST_SLOT_BUSY?100u:1000u);if(status!=FROST_SLOT_BUSY)c->known=0;return 0;}
    c->next_check=now;return 1;
}
#endif
