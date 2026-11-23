#ifndef FROST_CAPTURE_CLIENT_H
#define FROST_CAPTURE_CLIENT_H
#include "preset_client.h"
#include "firmware/protocol/preset_read_wire.h"
enum { CAPTURE_IDLE,CAPTURE_DESCRIBE,CAPTURE_READ,CAPTURE_VERIFY,CAPTURE_REFRESH };
typedef struct {
    uint32_t words[FROST_PRESET_MAX_WORDS];
    uint8_t saved[24];
    uint32_t revision,next,crc,checksum,total;
    unsigned state,profile,awaiting,pending,error,buffer_only,compact;
    unsigned retries,reason;uint32_t retry_at;   /* 32q: capture restarts and the last failure reason */
} FrostCaptureClient;
/* 32q: a capture whose DSP read-back is interrupted (chain transition -> BUSY,
 * a new revision, or a checksum that no longer matches because the sound
 * changed while it was read) restarts from DESCRIBE, up to this many times,
 * after a short pause. Nothing is stored before a capture verifies, so a
 * restart is safe. Flash preset saves (compact) only. Failure reasons
 * (7E 20 status byte 6): */
#define FROST_CAPTURE_RETRIES 4u
#define FROST_CAPTURE_RETRY_MS 100u
enum { CAPTURE_REASON_NONE, CAPTURE_REASON_OTHER, CAPTURE_REASON_BUSY, CAPTURE_REASON_REVISION, CAPTURE_REASON_CHECKSUM, CAPTURE_REASON_STORE };
/* compact: capture the 77-word version-3 body and save it as flash preset
 * `profile` (preset_store.h) instead of publishing a RAM bank entry. */
static int capture_client_store(unsigned profile,const uint32_t *words);
static void capture_client_init(FrostCaptureClient *c)
{c->state=c->profile=c->awaiting=c->pending=c->error=c->buffer_only=c->compact=0;c->revision=c->next=c->checksum=0;c->total=FROST_PRESET_WORDS;c->crc=UINT32_MAX;c->retries=c->reason=0;c->retry_at=0;}
/* Caller serializes with preset client; no other owner may start while busy. */
static int capture_client_start(FrostCaptureClient *c,FrostEditor *e,unsigned profile)
{
    if(c->state || profile>=FROST_BANK_COUNT || !editor_editable(e) || editor_pending(e) || e->awaiting || e->token>UINT32_MAX-224u)return 0;
    c->profile=profile;c->revision=e->revision;c->next=c->checksum=0;c->crc=UINT32_MAX;
    c->buffer_only=c->compact=0;
    c->state=CAPTURE_DESCRIBE;c->pending=c->awaiting=c->error=0;c->retries=c->reason=0;c->retry_at=0;e->ready=0;return 1;
}
static int capture_client_start_compact(FrostCaptureClient *c,FrostEditor *e,unsigned preset)
{if(!capture_client_start(c,e,0))return 0;c->profile=preset;c->compact=1;return 1;}
static void capture_client_finish(FrostCaptureClient *c,FrostEditor *e,uint32_t now,unsigned error)
{c->pending=c->awaiting=0;c->error=error;if(error && !c->reason)c->reason=CAPTURE_REASON_OTHER;c->state=CAPTURE_REFRESH;editor_reconnect(e,now);}
/* 32q: restart the read-back (see FROST_CAPTURE_RETRIES), or fail with `reason`. */
static int capture_client_retry(FrostCaptureClient *c,FrostEditor *e,uint32_t now,unsigned reason,uint32_t revision)
{
    c->reason=reason;
    /* Flash preset saves only: a RAM-bank capture keeps failing closed (its old entry stays). */
    if(!c->compact || c->retries>=FROST_CAPTURE_RETRIES){capture_client_finish(c,e,now,1);return 0;}
    ++c->retries;c->state=CAPTURE_DESCRIBE;c->next=c->checksum=0;c->crc=UINT32_MAX;c->pending=0;
    if(revision)c->revision=revision;
    c->retry_at=now+FROST_CAPTURE_RETRY_MS;return 0;
}
static unsigned capture_client_next(FrostCaptureClient *c,FrostEditor *e,uint8_t *p,uint32_t now)
{
    uint32_t token;unsigned i;
    if(c->state==CAPTURE_REFRESH){
        if(e->ready && !editor_pending(e)){c->state=CAPTURE_IDLE;return 0;}
        return editor_next(e,p,now);
    }
    if(!c->state || c->awaiting)return 0;
    if(c->retry_at && (int32_t)(now-c->retry_at)<0)return 0;
    c->retry_at=0;
    if(!c->pending){
        token=editor_token(e);if(!token){capture_client_finish(c,e,now,1);return 0;}
        editor_header(c->saved,FROST_PRESET_READ_COMMAND,24,token);
        frost_wire_put32(c->saved+12,(c->state==CAPTURE_READ?FROST_PRESET_READ:FROST_PRESET_DESCRIBE)+(c->compact?2u:0u));
        frost_wire_put32(c->saved+16,c->revision);frost_wire_put32(c->saved+20,c->state==CAPTURE_READ?c->next:0);
        c->pending=1;
    }
    for(i=0;i<24;++i)p[i]=c->saved[i];c->awaiting=1;return 24;
}
static int capture_client_reply(FrostCaptureClient *c,FrostEditor *e,const uint8_t *p,unsigned n,uint32_t now)
{
    unsigned i,count,status;uint32_t revision,index,crc;
    if(c->state==CAPTURE_REFRESH)return editor_reply(e,p,n,now);
    if(!c->state || !c->awaiting)return 0;
    c->awaiting=0;
    if(!p || n!=48 || p[0]!=0x44 || p[1]!=0x7f || p[2]!=48 || p[3] ||
       frost_wire_get32(p+4)!=1 || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;
    status=frost_wire_get32(p+12);revision=frost_wire_get32(p+16);
    if(status==FROST_SLOT_BUSY)return capture_client_retry(c,e,now,CAPTURE_REASON_BUSY,0);
    if(status==FROST_SLOT_STALE_REVISION)return capture_client_retry(c,e,now,CAPTURE_REASON_REVISION,revision);
    if(status!=FROST_SLOT_APPLIED){c->reason=CAPTURE_REASON_OTHER;capture_client_finish(c,e,now,1);return 0;}
    if(revision!=c->revision)return capture_client_retry(c,e,now,CAPTURE_REASON_REVISION,revision);
    index=frost_wire_get32(p+20);count=frost_wire_get32(p+24);
    if(c->state==CAPTURE_READ){
        unsigned wanted=c->total-c->next;if(wanted>5)wanted=5;
        if(index!=c->next || count!=wanted)return 0;
        for(i=count;i<5;++i)if(frost_wire_get32(p+28+4*i))return 0;
        crc=c->crc;
        for(i=0;i<count;++i){uint32_t word=frost_wire_get32(p+28+4*i);c->words[c->next+i]=word;crc=client_crc_word(crc,word);}
        c->crc=crc;c->next+=count;
        if(c->next==1 && (c->compact?c->words[0]!=FROST_PRESET_VERSION_COMPACT:frost_preset_words(c->words[0])!=c->total)){capture_client_finish(c,e,now,1);return 0;}
        if(c->next==c->total)c->state=CAPTURE_VERIFY;
    }else{
        uint32_t total=frost_wire_get32(p+32);
        if(index || count || (c->compact?total!=FROST_PRESET_COMPACT_WORDS:total!=FROST_PRESET_WORDS && total!=FROST_PRESET_MAX_WORDS) ||
           (c->state==CAPTURE_VERIFY && total!=c->total))return 0;
        for(i=2;i<5;++i)if(frost_wire_get32(p+28+4*i))return 0;
        crc=frost_wire_get32(p+28);
        if(c->state==CAPTURE_DESCRIBE){c->checksum=crc;c->total=total;c->state=CAPTURE_READ;}
        else {
            if(crc!=c->checksum || (c->crc^UINT32_MAX)!=crc)return capture_client_retry(c,e,now,CAPTURE_REASON_CHECKSUM,0);
            if(c->compact){if(!capture_client_store(c->profile,c->words)){c->reason=CAPTURE_REASON_STORE;capture_client_finish(c,e,now,1);return 0;}}
            else if(!c->buffer_only && !ram_bank_publish(c->profile,c->words)){capture_client_finish(c,e,now,1);return 0;}
            capture_client_finish(c,e,now,0);return 1;
        }
    }
    c->pending=0;return 1;
}
#endif
