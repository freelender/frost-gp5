#ifndef FROST_FCM_CLIENT_H
#define FROST_FCM_CLIENT_H
/* Keeps the DSP's one AMP record equal to the file of the selected MODEL
 * (firmware/protocol/fcm_wire.h, fcm_files.h). Reconcile loop: every
 * FCM_CHECK_MS the wanted record (first AMP block in route order, its MODEL,
 * that file's CRC) is compared with what the DSP reports holding; on a
 * difference the record is sent (BEGIN, WRITE x5, COMMIT) or the stale one
 * cleared. One request per serialized editor pass; an unanswered request is
 * resent byte-for-byte (same token); after FCM_RETRIES the attempt is dropped
 * and retried later. No allocation, no IO here. */
#include "editor_client.h"
#include "preset_store.h"
#define FCM_CHECK_MS 200u
#define FCM_BACKOFF_MS 2000u
#define FCM_RETRIES 4u
enum { FCM_IDLE, FCM_STATUS_SENT, FCM_BEGIN_SENT, FCM_WRITE_SENT, FCM_COMMIT_SENT, FCM_CLEAR_SENT };
typedef struct {
    unsigned state,awaiting,known,retries,failures;
    uint32_t held_index,held_crc,index,crc,record,next,count,next_check;
    uint8_t saved[FROST_FCM_REQUEST_BYTES+4u*FROST_FCM_CHUNK_WORDS];unsigned saved_len;
} FrostFcmClient;
static void fcm_client_init(FrostFcmClient *c,uint32_t now)
{
    c->state=c->awaiting=c->known=c->retries=c->failures=c->saved_len=0;
    c->held_index=UINT32_MAX;c->held_crc=c->index=c->crc=c->record=c->next=c->count=0;c->next_check=now;
}
/* First AMP block in route order; 6 = none. */
static unsigned fcm_client_slot(const FrostEditor *e)
{unsigned i;for(i=0;i<6;++i)if(e->types[e->order[i]]==FROST_FCM)return e->order[i];return 6;}
static unsigned fcm_client_frame(FrostFcmClient *c,FrostEditor *e,unsigned op,uint32_t offset,unsigned count,uint8_t *p)
{
    uint32_t token=editor_token(e);unsigned i,n=FROST_FCM_REQUEST_BYTES+4u*count;
    if(!token)return 0;
    editor_header(c->saved,FROST_FCM_COMMAND,n,token);
    frost_wire_put32(c->saved+12,op);frost_wire_put32(c->saved+16,c->index);frost_wire_put32(c->saved+20,c->crc);
    frost_wire_put32(c->saved+24,offset);frost_wire_put32(c->saved+28,count);
    for(i=0;i<count;++i)frost_wire_put32(c->saved+FROST_FCM_REQUEST_BYTES+4u*i,files_word(c->record+4u*(offset+i)));
    c->saved_len=n;c->retries=0;c->count=count;
    for(i=0;i<n;++i)p[i]=c->saved[i];
    return n;
}
static void fcm_client_drop(FrostFcmClient *c,uint32_t now)
{c->state=FCM_IDLE;c->saved_len=0;c->known=0;++c->failures;c->next_check=now+FCM_BACKOFF_MS;}
/* Wants the link now? (in a transfer, or a check is due) */
static int fcm_client_due(const FrostFcmClient *c,const FrostEditor *e,uint32_t now)
{return c->state!=FCM_IDLE || (editor_editable(e) && !editor_pending(e) && (int32_t)(now-c->next_check)>=0 && fcm_client_slot(e)<6);}
static unsigned fcm_client_next(FrostFcmClient *c,FrostEditor *e,uint8_t *p,uint32_t now)
{
    unsigned slot,i;uint32_t crc=0,record;int index;
    if(c->awaiting)return 0;
    if(c->state!=FCM_IDLE){                          /* the last request got no usable answer: resend it */
        if(!c->saved_len || ++c->retries>FCM_RETRIES){fcm_client_drop(c,now);return 0;}
        for(i=0;i<c->saved_len;++i)p[i]=c->saved[i];
        return c->saved_len;
    }
    c->next_check=now+FCM_CHECK_MS;
    if((slot=fcm_client_slot(e))>=6)return 0;
    index=editor_integer(e,slot,0);
    if(index<0 || index>=(int)FROST_FCM_MODELS)return 0;
    record=store_amp_record((unsigned)index,&crc);
    c->index=(uint32_t)index;c->crc=record?crc:0;c->record=record;
    if(!c->known){c->state=FCM_STATUS_SENT;return fcm_client_frame(c,e,FROST_FCM_STATUS,0,0,p);}
    if(record && !(c->held_index==c->index && c->held_crc==crc)){
        c->next=0;c->state=FCM_BEGIN_SENT;return fcm_client_frame(c,e,FROST_FCM_BEGIN,0,0,p);
    }
    if(!record && c->held_index==c->index){c->state=FCM_CLEAR_SENT;return fcm_client_frame(c,e,FROST_FCM_CLEAR,0,0,p);}
    c->failures=0;return 0;
}
/* Called with the reply (0 = none) of the request fcm_client_next produced. */
static int fcm_client_reply(FrostFcmClient *c,FrostEditor *e,const uint8_t *p,unsigned n,uint32_t now)
{
    uint32_t status;unsigned op=c->state;
    c->awaiting=0;
    if(!p || n!=FROST_FCM_REPLY_BYTES || p[0]!=(uint8_t)FROST_FCM_COMMAND || p[1]!=(uint8_t)(FROST_FCM_COMMAND>>8) || p[2]!=n || p[3] ||
       frost_wire_get32(p+4)!=FROST_FCM_VERSION || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;   /* resend */
    status=frost_wire_get32(p+12);c->saved_len=0;
    c->held_index=frost_wire_get32(p+16);c->held_crc=frost_wire_get32(p+20);c->known=1;
    if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED){fcm_client_drop(c,now);return 0;}
    if(op==FCM_BEGIN_SENT || op==FCM_WRITE_SENT){
        if(op==FCM_WRITE_SENT)c->next+=c->count;
        if(c->next<FROST_FCM_RECORD_WORDS){
            unsigned count=FROST_FCM_RECORD_WORDS-c->next;if(count>FROST_FCM_CHUNK_WORDS)count=FROST_FCM_CHUNK_WORDS;
            c->state=FCM_WRITE_SENT;
            if(!fcm_client_frame(c,e,FROST_FCM_WRITE,c->next,count,c->saved+0)){fcm_client_drop(c,now);return 0;}
            c->retries=0;return 1;                   /* staged: sent on the next pass */
        }
        c->state=FCM_COMMIT_SENT;
        if(!fcm_client_frame(c,e,FROST_FCM_COMMIT,0,0,c->saved+0)){fcm_client_drop(c,now);return 0;}
        return 1;
    }
    c->state=FCM_IDLE;c->next_check=now;              /* STATUS/COMMIT/CLEAR done: re-check at once */
    if(op==FCM_COMMIT_SENT)c->failures=0;
    return 1;
}
#endif
