#ifndef FROST_MIDI_CONTROL_H
#define FROST_MIDI_CONTROL_H
/* USB-MIDI 1.0 packets, cable 0, channel 1. Called only on the serialized
 * controller loop. No UART IO, allocation or flash writes in this callback.
 * Performance edits use the same coalescing queue as PARA and the website. */
static unsigned midi_expression_slot,midi_expression_param;
static unsigned midi_preset=255,midi_clock_enabled,midi_clock_ticks,midi_clock_running,midi_tempo_pending;
static uint32_t midi_tap_mark,midi_clock_mark,midi_clock_last,midi_tempo;
static void fsw_reset(void);
static void midi_init(void)
{
    midi_volume=127;midi_expression_slot=midi_expression_param=midi_bypass=0;
    midi_preset=255;midi_clock_enabled=midi_clock_ticks=midi_clock_running=midi_tempo_pending=0;
    midi_tap_mark=midi_clock_mark=midi_clock_last=midi_tempo=0;
    midi_tap_tempo=midi_tap_note=midi_tap_target=255u;
    fsw_reset();
}
static int midi_editable(void)
{
    return boot_load_done && editor_editable(&editor) && !editor.recover &&
        !preset_client.state && !capture_client.state && !ir_file_client.state &&
        !ir_file_client.recover && !fcm_client.state && !global_client.state &&
        !(files_host_written && (uint32_t)(gp5_ms()-files_host_write_ms)<FROST_STORE_GUARD_MS);
}
/* MIDI addresses positions in the visible chain, not internal slot identities. */
static int midi_parameter(unsigned position,unsigned parameter,unsigned value)
{
    unsigned slot;const FrostControlEffect *e;const Parameter *p;int v;
    if(position>=6u || !midi_editable())return 0;
    slot=editor.order[position];e=frost_control_find(editor.types[slot]);
    if(!e || parameter>=e->count)return 0;
    p=&e->p[parameter];v=p->minimum+(int)(((unsigned)(p->maximum-p->minimum)*value+63u)/127u);
    if(value!=127u && p->step>1)v=p->minimum+((v-p->minimum+p->step/2)/p->step)*p->step;
    if(v>p->maximum)v=p->maximum;
    if(!editor_queue_parameter(&editor,slot,parameter,v))return 0;
    changed=1;++generation;return 1;
}
/* MIDI tap and clock are the global tap tempo: `ms` is the beat; every tempo
 * block in the chain follows it through GLOBAL SETTINGS -> TAP TEMPO -> TAP
 * NOTE (tap_apply_all: delay TIME in ms, modulation rate in Hz, clamped). */
static void midi_tempo_apply(unsigned ms)
{
    if(ms<200u || ms>2000u)return;midi_tempo=ms;midi_tempo_pending=1;
    if(!midi_editable())return;
    midi_tempo_pending=0;tap_apply_all(ms,gp5_ms());++generation;
}
static void midi_clock(unsigned status)
{
    uint32_t now=gp5_ms();
    if(!midi_clock_enabled)return;
    if(status==0xfcu){midi_clock_running=0;midi_clock_ticks=0;return;}
    if(status==0xfau || status==0xfbu){midi_clock_running=1;midi_clock_ticks=0;midi_clock_mark=now;midi_clock_last=now;return;}
    if(status!=0xf8u || !midi_clock_running)return;
    if(!midi_clock_ticks || now-midi_clock_last>500u){midi_clock_mark=now;midi_clock_ticks=1;}
    else if(++midi_clock_ticks==25u){unsigned ms=now-midi_clock_mark;midi_clock_ticks=1;midi_clock_mark=now;
        if(ms>=200u && ms<=2000u && (!midi_tempo || ms+2u<midi_tempo || ms>midi_tempo+2u))midi_tempo_apply(ms);
    }
    midi_clock_last=now;
}
static int midi_load(unsigned preset)
{
    if(preset>=FROST_PRESETS || !store_preset(preset))return 0;
    midi_preset=preset;return 1; /* one bounded latest-wins recall mailbox */
}
/* External footswitches with three gestures each: tap, double press and hold (2 s). A controller (the M-VAVE
 * Chocolate Plus, set up by the website's Controller page) sends on MIDI channel 16, just before each press, the
 * twelve bytes that say what that footswitch does, so the setup lives in the controller and nothing is stored here:
 *   CC 16..27 = definition bytes: tap, double, hold, each as kind, a, b, c
 *   CC 28 = footswitch n pressed, CC 29 = footswitch n released (value n = 0..15)
 * kinds: 0 nothing; 1 CC a = b; 2 PC a; 3 toggle CC a by the pedal's state (86 tuner, 90 bypass all, 20..25 the
 * effect at a chain position; other CCs alternate 127 and 0); 4 CC a alternating b and c; 5 the current preset's CTL
 * effects, each switched the other way (as the GP-5's own footswitch does in FOOTSWITCH CTL).
 * The actions run as channel-1 messages (docs/Midi Command.md). A tap runs on the press when the footswitch has
 * neither a double nor a hold action; otherwise on the release, 400 ms later when it has a double action. A tap
 * tempo (CC 85) tap always runs on the press so its timing is exact; such a footswitch has no double press.
 * Two neighbouring footswitches pressed together (n and n+1, within 100 ms) run a fourth action instead of their own.
 * Footswitch n sends that pair's action with its definition, before its press:
 *   CC 30 = b, CC 31 = c (left out when 0), then CC 32+kind = a (kind 1..5 as above)
 *   CC 38 = footswitch n pressed, and it is part of a pair (with n-1 or n+1), in place of CC 28.
 * Such a footswitch acts 100 ms after its press (when no neighbour joined it), so a tap tempo keeps its spacing. */
#define FROST_FSW 16u
#define FROST_FSW_HOLD_MS 2000u
#define FROST_FSW_DOUBLE_MS 400u
#define FROST_FSW_PAIR_MS 100u
/* def: tap, double, hold, then the pair with the next footswitch (g = 3), each as kind, a, b, c.
 * joins: part of a pair; wait: the press is held back while a neighbour may still join it. */
typedef struct { uint8_t def[16],have,down,done,tapped,pending,alt,later,joins,wait; uint32_t down_ms,up_ms; } FrostFsw;
static FrostFsw fsw[FROST_FSW];
static uint8_t fsw_def[16];
static unsigned fsw_got;                           /* bit i: definition byte i received since the last press (bit 12: the pair) */
static unsigned led_dirty;static uint32_t led_changed_ms;   /* LED colours from the website, not saved yet */
void fw_midi_event(const uint8_t *packet);
static void fsw_reset(void)
{unsigned n,i;for(n=0;n<FROST_FSW;++n){for(i=0;i<16u;++i)fsw[n].def[i]=0;fsw[n].have=fsw[n].down=fsw[n].done=fsw[n].tapped=fsw[n].pending=fsw[n].alt=fsw[n].later=fsw[n].joins=fsw[n].wait=0;fsw[n].down_ms=fsw[n].up_ms=0;}for(i=0;i<16u;++i)fsw_def[i]=0;fsw_got=0;led_dirty=0;}
static unsigned fsw_has(const FrostFsw *s,unsigned g){return s->def[4u*g]!=0u && s->def[4u*g]<=5u;}
static unsigned fsw_tap_tempo(const FrostFsw *s){return s->def[0]==1u && s->def[1]==85u;}
/* CCs that edit the chain or the view: like PARA, they wait while the pedal is busy (a preset load, a settings
 * sync after a bypass...) instead of being lost; fsw_service runs them as soon as it is idle. */
static unsigned fsw_waits(unsigned kind,unsigned a){return kind==5u || (kind!=2u && (a==86u || a==89u || (a>=20u && a<=25u) || (a>=32u && a<=79u)));}
/* kind 5: the CTL group of the preset playing now (foot_release in slot_editor.h does the same for the GP-5's own footswitch). */
static void fsw_ctl(void)
{
    unsigned i,mask=0,targets=0;
    ctl_sync();
    for(i=0;i<6u;++i)if(editor.types[i]!=FROST_EMPTY){if(editor.desired_enabled[i])mask|=1u<<i;targets|=ctl_mask&(1u<<i);}
    if(targets && editor_queue_mask(&editor,mask^targets)){changed=1;++generation;}
}
static void fsw_fire(FrostFsw *s,unsigned g)       /* g: 0 tap, 1 double, 2 hold, 3 the pair with the next footswitch */
{
    const uint8_t *d=s->def+4u*g;unsigned kind=d[0],a=d[1],b=d[2],c=d[3],v;uint8_t p[4];
    if(!fsw_has(s,g) || a>127u)return;
    /* Busy: run it later. A switch (kinds 3..5) keeps its parity, so pressing it twice during a preset load leaves
     * the effect as it was instead of switching it once; a fixed value (kind 1) runs once. */
    if(fsw_waits(kind,a) && !midi_editable()){if(kind>=3u)s->later^=(uint8_t)(1u<<g);else s->later|=(uint8_t)(1u<<g);return;}
    if(kind==5u){fsw_ctl();return;}
    if(kind==2u){p[0]=0x0cu;p[1]=0xc0u;p[2]=(uint8_t)a;p[3]=0;fw_midi_event(p);return;}
    if(kind==3u){
        if(a==86u)v=view==TUNER?0u:127u;
        else if(a==90u)v=midi_bypass?0u:127u;
        else if(a>=20u && a<=25u)v=editor.desired_enabled[editor.order[a-20u]]?0u:127u;
        else{s->alt^=(uint8_t)(1u<<g);v=(s->alt>>g)&1u?127u:0u;}
    }else if(kind==4u){s->alt^=(uint8_t)(1u<<g);v=(s->alt>>g)&1u?b:c;}
    else v=b;
    p[0]=0x0bu;p[1]=0xb0u;p[2]=(uint8_t)a;p[3]=(uint8_t)(v&127u);fw_midi_event(p);
}
/* The press itself, once no neighbour can join it any more. */
static void fsw_go(FrostFsw *s)
{
    s->wait=0;
    if(fsw_tap_tempo(s)){s->tapped=1;fsw_fire(s,0);}
    else if(!fsw_has(s,1) && !fsw_has(s,2)){s->done=1;fsw_fire(s,0);}
}
/* Footswitch n is pressed: a neighbour pressed less than 100 ms ago and still waiting makes it a pair. */
static unsigned fsw_pair(unsigned n,uint32_t now)
{
    unsigned k;
    for(k=0;k<2u;++k){
        unsigned m=k?n+1u:n-1u;FrostFsw *o,*low;
        if((k==0u && n==0u) || m>=FROST_FSW)continue;
        o=&fsw[m];low=&fsw[m<n?m:n];
        if(!o->down || !o->wait || !fsw_has(low,3) || now-o->down_ms>FROST_FSW_PAIR_MS)continue;
        o->wait=0;o->done=1;o->tapped=0;                 /* neither footswitch acts on its own any more */
        fsw[n].down=1;fsw[n].done=1;fsw[n].tapped=0;fsw[n].wait=0;fsw[n].down_ms=now;
        fsw_fire(low,3);return 1;
    }
    return 0;
}
static void fsw_press(unsigned n,uint32_t now,unsigned joins)
{
    FrostFsw *s=&fsw[n];unsigned i;
    if((fsw_got&0xfffu)==0xfffu){
        for(i=0;i<12u;++i)s->def[i]=fsw_def[i];
        for(i=12u;i<16u;++i)s->def[i]=(fsw_got&0x1000u)?fsw_def[i]:0u;
        s->have=1;s->joins=(uint8_t)joins;
    }
    fsw_got=0;for(i=12u;i<16u;++i)fsw_def[i]=0;
    if(!s->have)return;
    if(s->pending && fsw_has(s,1) && !fsw_tap_tempo(s) && now-s->up_ms<=FROST_FSW_DOUBLE_MS){
        s->pending=0;s->down=1;s->done=1;s->tapped=0;s->wait=0;s->down_ms=now;fsw_fire(s,1);return;
    }
    if(s->pending){s->pending=0;fsw_fire(s,0);}    /* a late second press: the first one was a tap */
    if(s->joins && fsw_pair(n,now))return;
    s->down=1;s->done=0;s->tapped=0;s->down_ms=now;
    if(s->joins){s->wait=1;return;}                 /* fsw_service goes on in 100 ms unless a neighbour joins */
    fsw_go(s);
}
static void fsw_release(unsigned n,uint32_t now)
{
    FrostFsw *s=&fsw[n];
    if(!s->down)return;
    if(s->wait && fsw_tap_tempo(s)){s->down=0;return;}   /* the tap still runs 100 ms after the press, like every tap */
    if(s->wait)fsw_go(s);                           /* released within 100 ms: it was a press on its own */
    s->down=0;
    if(s->done || s->tapped){s->done=s->tapped=0;return;}
    if(fsw_has(s,1) && !fsw_tap_tempo(s)){s->pending=1;s->up_ms=now;return;}
    fsw_fire(s,0);
}
static void fsw_service(uint32_t now)
{
    unsigned n;
    for(n=0;n<FROST_FSW;++n){
        FrostFsw *s=&fsw[n];unsigned g;
        if(s->later && midi_editable())for(g=0;g<4u;++g)if(s->later&(1u<<g)){s->later&=(uint8_t)~(1u<<g);fsw_fire(s,g);}
        if(s->wait && now-s->down_ms>=FROST_FSW_PAIR_MS){fsw_go(s);if(!s->down)s->done=s->tapped=0;}
        if(s->down && !s->done && fsw_has(s,2) && now-s->down_ms>=FROST_FSW_HOLD_MS){s->done=1;s->tapped=0;fsw_fire(s,2);}
        if(s->pending && now-s->up_ms>FROST_FSW_DOUBLE_MS){s->pending=0;fsw_fire(s,0);}
    }
}
/* The website's colour picker, also on channel 16: CC 40 = LED ON colour (0..10), CC 41 = LED OFF colour (0..4), in the
 * order of GLOBAL SETTINGS -> DISPLAY. The footswitch shows it at once; the global settings are saved 1.5 s after the
 * last change, so trying colours does not write the storage at every click. */
#define FROST_LED_SAVE_MS 1500u
static void led_service(uint32_t now)
{
    if(!led_dirty || !boot_load_done || !fw_editor_ready() || (uint32_t)(now-led_changed_ms)<FROST_LED_SAVE_MS)return;
    if(globals_save(now))led_dirty=0;else led_changed_ms=now;   /* storage busy (a host write): try again later */
}
static void fsw_midi(unsigned cc,unsigned value,uint32_t now)
{
    if(cc==40u || cc==41u){
        if(cc==40u && value<FROST_LED_ON_CHOICES)globals.led_on=value;
        else if(cc==41u && value<FROST_LED_OFF_CHOICES)globals.led_off=value;
        else return;
        led_dirty=1;led_changed_ms=now;++generation;return;
    }
    if(cc==42u){unsigned v=value<=6u?value:0u;if(v!=loop_state)++generation;loop_state=v;loop_state_ms=now;return;}   /* the website's looper */
    if(cc==43u){if(value>127u)value=127u;if(144u*value/127u!=144u*loop_pos/127u)++generation;loop_pos=value;return;}  /* its position: redraw when the bar moves */
    if(cc==44u){if(value!=loop_info)++generation;loop_info=value;return;}                                            /* its track and section */
    if(cc>=16u && cc<=27u){fsw_def[cc-16u]=(uint8_t)value;fsw_got|=1u<<(cc-16u);return;}
    if(cc==30u || cc==31u){fsw_def[cc-16u]=(uint8_t)value;return;}               /* the pair's b and c */
    if(cc>=33u && cc<=37u){fsw_def[12]=(uint8_t)(cc-32u);fsw_def[13]=(uint8_t)value;fsw_got|=0x1000u;return;}
    if((cc==28u || cc==38u) && value<FROST_FSW){fsw_press(value,now,cc==38u);return;}
    if(cc==29u && value<FROST_FSW)fsw_release(value,now);
}
static void midi_service(void)
{
    fsw_service(gp5_ms());led_service(gp5_ms());
    /* Finish a short CLEAR/restore handshake instead of the obsolete upload.
     * The one-slot mailbox remains latest-wins while cancellation runs. */
    if(midi_preset<FROST_PRESETS && midi_preset!=current_preset && !preset_client.state &&
       !capture_client.state && ir_file_client.state)(void)ir_file_cancel(&editor);
    if(midi_preset<FROST_PRESETS && midi_editable() && fw_editor_ready() && !tuner_client.dsp_active && view!=TUNER){
        if(fw_flash_load(midi_preset)){
            midi_preset=255;boot_load_done=1;changed=0;   /* last_preset_service saves it */
            midi_tempo_pending=midi_clock_enabled && midi_tempo;
            if(view==SETLIST_HOME)view=CHAIN;
            ++generation;
        }
    }
    if(midi_preset==255u && midi_tempo_pending && midi_editable())midi_tempo_apply(midi_tempo);
}
void fw_midi_event(const uint8_t *packet)
{
    unsigned cin=packet[0],status=packet[1],cc=packet[2],value=packet[3],i;
    uint32_t now=gp5_ms();
    if(cin==0x0fu && status>=0xf8u){midi_clock(status);return;}
    if(cin==0x0bu && status==0xbfu && cc<128u && value<128u){fsw_midi(cc,value,now);return;}   /* channel 16: footswitch gestures */
    if(cin==0x0cu && status==0xc0u && cc<128u){(void)midi_load(cc);return;}
    if(cin!=0x0bu || status!=0xb0u || cc>=128u || value>=128u)return;
    if(cc==7u){midi_volume=value;return;}
    if(cc==80u){if(value<6u)midi_expression_slot=value;return;}
    if(cc==81u){if(value<8u)midi_expression_param=value;return;}
    if(cc==11u){(void)midi_parameter(midi_expression_slot,midi_expression_param,value);return;}
    if(cc>=32u && cc<=79u){(void)midi_parameter((cc-32u)/8u,(cc-32u)%8u,value);return;}
    if(cc==91u){midi_clock_enabled=value>=64u;midi_clock_running=midi_clock_enabled;midi_clock_ticks=0;return;}
    /* Tap tempo settings: volatile overrides, like every MIDI command never saved. */
    if(cc==92u){midi_tap_tempo=value>=64u;if(!midi_tap_tempo)tap_mode=0;++generation;return;}
    if(cc==93u){if(value<3u){midi_tap_note=value;if(midi_clock_enabled && midi_tempo)midi_tempo_pending=1;++generation;}return;}
    if(cc==94u){midi_tap_target=value>=64u;++generation;return;}
    if(cc==85u && value>=64u){
        unsigned ms=now-midi_tap_mark;if(midi_tap_mark && ms>=200u && ms<=2000u)midi_tempo_apply(ms);
        midi_tap_mark=now;return;
    }
    if((cc==87u || cc==88u) && value>=64u){
        unsigned from=midi_preset<FROST_PRESETS?midi_preset:current_preset;
        unsigned next=preset_step(from,cc==87u?1:-1);if(next!=255u)(void)midi_load(next);return;
    }
    if(cc==90u){midi_bypass=value>=64u;global_client.next_check=now;++generation;return;}
    if(!midi_editable())return;
    if(cc>=20u && cc<=25u){
        if(editor_queue_enable(&editor,editor.order[cc-20u],value>=64u)){changed=1;++generation;}return;
    }
    if(cc==89u && value<64u){
        unsigned mask=0;for(i=0;i<6u;++i)if(value&(1u<<i))mask|=1u<<editor.order[i];
        if(editor_queue_mask(&editor,mask)){changed=1;++generation;}return;
    }
    if(cc==86u){
        if(value>=64u && view!=TUNER){tuner_home=is_home_view(view)?view:home_view();view=TUNER;tuner_client.have=0;tuner_ref_unlocked=0;}
        else if(value<64u && view==TUNER)view=tuner_home;
        tuner_client.want=view==TUNER;tuner_client.next_poll=now;++generation;
    }
}
#endif
