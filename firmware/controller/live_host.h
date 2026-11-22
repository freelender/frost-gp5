#ifndef FROST_LIVE_HOST_H
#define FROST_LIVE_HOST_H
/* Live editing from the website (web/live.js, docs/Build%20Firmware.md): USB host
 * commands handled by the controller, through the same editor queue as the
 * PARA knob, so the pedal screen, SAVE and the DSP stay consistent.
 *   7E 18             chain:   7E 18 ok types[6] order[6] enabled[6] revision u32 editable
 *   7E 14 slot        slot:    7E 14 ok slot type enabled position count values[8] (int16, UI units)
 *   7E 15 slot p v16  set parameter (clamped to the control's range)  -> 7E 15 ok
 *   7E 16 slot on     enable/bypass                                  -> 7E 16 ok
 *   7E 17 slot type   change the slot's effect type (255 = empty)    -> 7E 17 ok
 *   7E 19 slot        IR CAB: ok slot cab busy error exists name[16]
 *   7E 1A slot cab    set IR CAB 0..63 through the pedal asset client -> 7E 1A ok
 * ok = 1 accepted, 0 refused (busy, bad slot/parameter). Values use the
 * pedal's control integers (engine/control_catalog.h: value / scale + unit). */
static unsigned live_host(const uint8_t *q,unsigned n,uint8_t *pl,unsigned max)
{
    unsigned i,ok=0,slot=n>=3?q[2]:6u;
    if(n<2 || q[0]!=0x7e || q[1]<0x14 || q[1]>0x1a)return 0;
    pl[0]=0x7e;pl[1]=q[1];
    if(q[1]==0x18 && n==2 && max>=27u){
        pl[2]=editor.ready;
        for(i=0;i<6;++i){pl[3+i]=editor.types[i];pl[9+i]=editor.order[i];pl[15+i]=editor.desired_enabled[i];}
        for(i=0;i<4;++i)pl[21+i]=(uint8_t)(editor.revision>>(8*i));
        pl[25]=(uint8_t)(editor_editable(&editor) && fw_editor_ready());return 26;
    }
    if(slot>=6u){pl[2]=0;return 3;}
    if(q[1]==0x19 && n==3 && max>=25u){
        unsigned cab=ir_cabs[slot];uint32_t crc=0,rec=cab?store_ir_record(cab-1u,&crc):0;const char *name="BUILT IN";
        if(editor.types[slot]!=FROST_IR){pl[2]=0;return 3;}
        pl[2]=editor.ready;pl[3]=(uint8_t)slot;pl[4]=(uint8_t)cab;
        pl[5]=(uint8_t)((ir_file_client.state && ir_file_client.slot==slot) || ir_file_client.held_cab[slot]!=cab);
        pl[6]=(uint8_t)ir_file_client.error[slot];pl[7]=(uint8_t)(!cab || rec);
        if(cab)name=rec?(const char *)FROST_FILE_AT(rec):"EMPTY";
        for(i=0;i<16u;++i){pl[8+i]=(uint8_t)*name;if(*name)++name;}
        pl[24]=(uint8_t)globals.cab_override;return 25;
    }
    if(q[1]==0x1a && n==4){
        if(editor.types[slot]==FROST_IR && q[3]<64u && fw_editor_ready() && editor_editable(&editor)){
            ir_cabs[slot]=q[3];ir_file_client.held_cab[slot]=64;ir_file_client.next_check=0;changed=1;++generation;ok=1;
        }
        pl[2]=(uint8_t)ok;return 3;
    }
    if(q[1]==0x14 && n==3 && max>=24u){
        const FrostControlEffect *e=frost_control_find(editor.types[slot]);unsigned c=e?e->count:0u;
        if(c>8u)c=8u;
        pl[2]=editor.ready;pl[3]=(uint8_t)slot;pl[4]=editor.types[slot];pl[5]=editor.desired_enabled[slot];pl[6]=(uint8_t)editor_position(slot);pl[7]=(uint8_t)c;
        for(i=0;i<8u;++i){int v=i<c?editor_integer(&editor,slot,i):0;pl[8+2*i]=(uint8_t)v;pl[9+2*i]=(uint8_t)((unsigned)v>>8);}
        return 24;
    }
    if(q[1]==0x15 && n==6){
        const FrostControlEffect *e=frost_control_find(editor.types[slot]);unsigned p=q[3];int v=(int16_t)(q[4]|(q[5]<<8));
        if(e && p<e->count && editor_editable(&editor)){
            const Parameter *d=&e->p[p];if(v<d->minimum)v=d->minimum;if(v>d->maximum)v=d->maximum;
            ok=(unsigned)editor_queue_parameter(&editor,slot,p,v);if(ok){changed=1;++generation;}
        }
    }else if(q[1]==0x16 && n==4){
        if(editor_editable(&editor)){ok=(unsigned)editor_queue_enable(&editor,slot,q[3]?1u:0u);if(ok){changed=1;++generation;}}
    }else if(q[1]==0x17 && n==4){
        unsigned type=q[3];
        if((type<FROST_EFFECTS || type==FROST_EMPTY) && editor_editable(&editor)){
            uint8_t types[6],order[6];
            for(i=0;i<6;++i){types[i]=editor.types[i];order[i]=editor.order[i];}
            types[slot]=(uint8_t)type;ok=(unsigned)editor_queue_route(&editor,types,order,editor.pre);if(ok){changed=1;++generation;}
        }
    }else return 0;
    pl[2]=(uint8_t)ok;return 3;
}
#endif
