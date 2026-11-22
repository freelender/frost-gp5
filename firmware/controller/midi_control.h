#ifndef FROST_MIDI_CONTROL_H
#define FROST_MIDI_CONTROL_H
/* USB-MIDI 1.0 packets, cable 0, channel 1. Called only on the serialized
 * controller loop. No UART IO, allocation or flash writes in this callback.
 * Performance edits use the same coalescing queue as PARA and the website. */
static unsigned midi_expression_slot,midi_expression_param;
static unsigned midi_preset=255,midi_clock_enabled,midi_clock_ticks,midi_clock_running,midi_tempo_pending;
static uint32_t midi_tap_mark,midi_clock_mark,midi_clock_last,midi_tempo;
static void midi_init(void)
{
    midi_volume=127;midi_expression_slot=midi_expression_param=midi_bypass=0;
    midi_preset=255;midi_clock_enabled=midi_clock_ticks=midi_clock_running=midi_tempo_pending=0;
    midi_tap_mark=midi_clock_mark=midi_clock_last=midi_tempo=0;
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
static void midi_tempo_apply(unsigned ms)
{
    unsigned i,j;if(ms<200u || ms>2000u)return;midi_tempo=ms;midi_tempo_pending=1;
    if(!midi_editable())return;
    midi_tempo_pending=0;
    for(i=0;i<6u;++i){
        unsigned slot=editor.order[i],type=editor.types[slot];const FrostControlEffect *e=frost_control_find(type);
        for(j=0;e && j<e->count;++j){
            const Parameter *p=&e->p[j];int v;
            /* Only delay time (not reverb decay/envelopes) and explicit Hz
             * modulation rates follow a quarter note. Clamp to effect limits. */
            if(frost_type_is_delay(type) && p->unit[0]=='M' && p->unit[1]=='S')v=(int)ms;
            else if(frost_type_is_modulation(type) && p->unit[0]=='H' && p->unit[1]=='Z')v=(int)((1000u*(unsigned)p->scale+ms/2u)/ms);
            else continue;
            if(v<p->minimum)v=p->minimum;if(v>p->maximum)v=p->maximum;
            if(editor_queue_parameter(&editor,slot,j,v)){changed=1;++generation;}
        }
    }
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
static void midi_service(void)
{
    if(midi_preset<FROST_PRESETS && midi_editable() && fw_editor_ready() && !tuner_client.dsp_active && view!=TUNER){
        if(fw_flash_load(midi_preset)){
            midi_preset=255;boot_load_done=1;changed=0;last_preset_dirty=0;
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
    if(cin==0x0cu && status==0xc0u && cc<128u){(void)midi_load(cc);return;}
    if(cin!=0x0bu || status!=0xb0u || cc>=128u || value>=128u)return;
    if(cc==7u){midi_volume=value;return;}
    if(cc==80u){if(value<6u)midi_expression_slot=value;return;}
    if(cc==81u){if(value<8u)midi_expression_param=value;return;}
    if(cc==11u){(void)midi_parameter(midi_expression_slot,midi_expression_param,value);return;}
    if(cc>=32u && cc<=79u){(void)midi_parameter((cc-32u)/8u,(cc-32u)%8u,value);return;}
    if(cc==91u){midi_clock_enabled=value>=64u;midi_clock_running=midi_clock_enabled;midi_clock_ticks=0;return;}
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
