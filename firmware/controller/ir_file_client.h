#ifndef FROST_IR_FILE_CLIENT_H
#define FROST_IR_FILE_CLIENT_H
#include "preset_store.h"
#include "firmware/protocol/ir_wire.h"
#include "ir_normalize.h"
/* CAB numbers are controller asset references, not DSP parameters. */
static uint16_t ir_cabs[6];
typedef struct {
    unsigned state,awaiting,slot,next,count,op,recover,retries,dsp_state;
    uint32_t crc,record,next_check,epoch,held_crc[6],source_crc,normal_scale;
    unsigned held_cab[6],types[6],error[6];
    uint8_t saved[112];unsigned bytes;
} FrostIrFileClient;
static FrostIrFileClient ir_file_client;
static void ir_file_client_init(const FrostEditor *e)
{unsigned i;FrostIrFileClient *c=&ir_file_client;c->state=c->awaiting=c->next_check=0;c->recover=1;c->retries=0;c->dsp_state=FROST_IR_IDLE;c->epoch=files_epoch;for(i=0;i<6;++i){c->error[i]=0;c->held_cab[i]=65;c->held_crc[i]=0;c->types[i]=e->types[i];}}
/* Virtual transfer record: adjust scale/CRC in RAM, leaving stored IR files
 * and backups intact. Old L1-normalized uploads gain the same calibration. */
static uint32_t ir_file_word(const FrostIrFileClient *c,unsigned index)
{return index==4u?c->normal_scale:files_word(c->record+4u*index);}
static void ir_file_normal_record(FrostIrFileClient *c)
{
    union {float f;uint32_t u;} scale;uint32_t crc=0xffffffffu;unsigned i,b,k;
    scale.f=ir_record_normal_scale(c->record);c->normal_scale=scale.u;
    for(i=0;i<261u;++i){uint32_t w=ir_file_word(c,i);
        for(b=0;b<4u;++b){crc^=(w>>(8u*b))&255u;for(k=0;k<8u;++k)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}}
    c->crc=crc^0xffffffffu;
}
static unsigned ir_file_frame(FrostEditor *e,unsigned op,unsigned count,uint8_t *p)
{
    FrostIrFileClient *c=&ir_file_client;unsigned i;uint32_t token=editor_token(e);
    if(!token)return 0;
    c->retries=0;c->next_check=0;c->op=op;c->count=count;c->bytes=32+4*count;
    editor_header(c->saved,FROST_IR_COMMAND,c->bytes,token);
    frost_wire_put32(c->saved+12,op);frost_wire_put32(c->saved+16,c->slot);frost_wire_put32(c->saved+20,c->crc);
    frost_wire_put32(c->saved+24,c->next);frost_wire_put32(c->saved+28,count);
    for(i=0;i<count;++i)frost_wire_put32(c->saved+32+4*i,ir_file_word(c,c->next+i));
    for(i=0;i<c->bytes;++i)p[i]=c->saved[i];return c->bytes;
}
/* Stop obsolete work only between serialized requests. CLEAR replaces any
 * partial coefficient/receive buffer with a valid built-in cabinet, then the
 * existing COMMIT/fade handshake releases the session. Never drop a live
 * session or mark a half-written custom IR as loaded. */
static int ir_file_cancel(FrostEditor *e)
{
    FrostIrFileClient *c=&ir_file_client;
    if(!c->state || c->awaiting || c->slot>=6u || c->op==FROST_IR_STATUS || c->recover==2u)return 0;
    /* Validated coefficients are already complete: finish preparation/restore
     * rather than rebuilding the built-in cabinet and extending the wait. */
    if(c->dsp_state==FROST_IR_PREPARING || c->dsp_state==FROST_IR_RESTORING)return 0;
    if(!ir_file_frame(e,FROST_IR_CLEAR,0,c->saved))return 0;
    c->record=0;c->next=0;c->recover=2u;
    return 1;
}
static unsigned ir_file_next(FrostEditor *e,uint8_t *p,uint32_t now)
{
    FrostIrFileClient *c=&ir_file_client;unsigned i;uint32_t r,crc;
    if(c->awaiting)return 0;
    if(c->state){
        if((int32_t)(now-c->next_check)<0)return 0;
        if(++c->retries>4){c->retries=0;c->next_check=now+2000;return 0;}
        for(i=0;i<c->bytes;++i)p[i]=c->saved[i];return c->bytes;
    }
    if(!editor_editable(e) || editor_pending(e) || (int32_t)(now-c->next_check)<0)return 0;
    if(c->recover){c->state=1;c->slot=6;c->crc=0;c->next=0;c->record=0;return ir_file_frame(e,FROST_IR_STATUS,0,p);}
    c->next_check=now+200;
    for(i=0;i<6;++i){
        if(e->types[i]!=c->types[i]){c->error[i]=0;c->held_cab[i]=65;c->held_crc[i]=0;c->types[i]=e->types[i];ir_cabs[i]=0;}
        if(e->types[i]!=FROST_IR)continue;
        crc=0;r=ir_cabs[i]?store_ir_record(ir_cabs[i]-1u,&crc):0;
        if(c->held_cab[i]==ir_cabs[i] && c->held_crc[i]==crc)continue;
        c->error[i]=0;c->slot=i;c->crc=c->source_crc=crc;c->record=r;c->next=0;c->state=1;
        /* A full legacy preset may have supplied explicit IR samples. Probe
         * unknown CAB 0 before replacing them; a newly created block is the
         * one-tap identity. Explicit CAB selection/persistent recall use 64. */
        if(!ir_cabs[i] && c->held_cab[i]==65){c->recover=3;return ir_file_frame(e,FROST_IR_STATUS,0,p);}
        if(r)ir_file_normal_record(c);
        return ir_file_frame(e,r?FROST_IR_BEGIN:FROST_IR_CLEAR,0,p);
    }
    return 0;
}
static int ir_file_reply(FrostEditor *e,const uint8_t *p,unsigned n)
{
    FrostIrFileClient *c=&ir_file_client;unsigned status,state,count;
    c->awaiting=0;
    if(!p || n!=32 || p[0]!=0x4c || p[1]!=0x7f || p[2]!=32 || p[3] || frost_wire_get32(p+4)!=1 || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;
    status=frost_wire_get32(p+12);state=frost_wire_get32(p+16);c->dsp_state=state;c->retries=0;
    if(c->op==FROST_IR_STATUS){
        if(c->recover==3 && state==FROST_IR_IDLE && status==FROST_SLOT_APPLIED){
            c->recover=0;
            if(frost_wire_get32(p+28)>1u){c->held_cab[c->slot]=0;c->held_crc[c->slot]=0;c->state=0;c->next_check=0;return 1;}
            ir_file_frame(e,FROST_IR_CLEAR,0,c->saved);return 1;
        }
        c->recover=0;
        if(state==FROST_IR_IDLE){c->state=0;c->next_check=0;return 1;}
        c->slot=frost_wire_get32(p+20);c->crc=frost_wire_get32(p+24);c->record=0;c->next=0;
        if(c->slot>=6){c->state=0;return 0;}
        c->recover=2;c->held_cab[c->slot]=64;ir_file_frame(e,FROST_IR_CLEAR,0,c->saved);return 1;
    }
    if(status==FROST_SLOT_ACCEPTED)return 1;
    if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED){
        /* Recover damaged/incomplete transfers by installing the built-in IR,
         * then always finish the restoring fade before releasing the link. */
        c->error[c->slot]=1;c->record=0;c->next=0;
        ir_file_frame(e,FROST_IR_CLEAR,0,c->saved);return 0;
    }
    if(c->op==FROST_IR_COMMIT && state==FROST_IR_IDLE){
        c->held_cab[c->slot]=c->recover==2?64:ir_cabs[c->slot];c->recover=0;c->held_crc[c->slot]=c->source_crc;c->state=0;c->next_check=0;return 1;
    }
    if(c->op==FROST_IR_WRITE)c->next+=c->count;
    if(c->record && c->next<261){count=261-c->next;if(count>20)count=20;ir_file_frame(e,FROST_IR_WRITE,count,c->saved);}
    else ir_file_frame(e,FROST_IR_COMMIT,0,c->saved);
    return 1;
}
#endif
