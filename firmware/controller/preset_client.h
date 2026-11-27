#ifndef FROST_PRESET_CLIENT_H
#define FROST_PRESET_CLIENT_H
#include "ram_bank.h"
#include "firmware/protocol/preset_wire.h"
#include "firmware/protocol/quick_preset_wire.h"
enum { PRESET_IDLE,PRESET_DISCOVER,PRESET_ABORTING,PRESET_BEGINNING,
       PRESET_WRITING,PRESET_COMMITTING,PRESET_WAITING,PRESET_REFRESH,PRESET_QUICK,PRESET_QUICK_WAIT,PRESET_QUICK_PROBE,PRESET_QUICK_PREPARE };
typedef struct {
    uint8_t saved[FROST_QUICK_MAX_BYTES];
    uint32_t session,revision,next,crc,checksum,total;
    unsigned state,profile,awaiting,pending,error;
    const uint32_t *snapshot;
    uint32_t fence;
    unsigned quick_bytes,quick_failures,quick,quick_known,quick_caps;
} FrostPresetClient;
static uint32_t client_crc_word(uint32_t crc,uint32_t value)
{
    unsigned i,j;
    for(i=0;i<4;++i){crc^=(value>>(8*i))&255u;
        for(j=0;j<8;++j)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc;
}
static void preset_client_init(FrostPresetClient *c)
{c->state=PRESET_IDLE;c->quick_bytes=c->quick_failures=c->quick=c->quick_known=c->quick_caps=0;c->profile=c->awaiting=c->pending=c->error=0;c->session=c->revision=c->next=c->checksum=0;c->crc=UINT32_MAX;c->snapshot=0;c->fence=0;}
static uint32_t preset_client_word(const FrostPresetClient *c,unsigned index)
{return c->snapshot?c->snapshot[index]:ram_bank_word(c->profile,index);}
static int preset_client_begin(FrostPresetClient *c,FrostEditor *e,unsigned profile,const uint32_t *snapshot,uint32_t fence)
{
    unsigned i;uint32_t crc=UINT32_MAX;
    if(c->state || !editor_editable(e) || editor_pending(e) || e->awaiting || profile>=FROST_BANK_COUNT)return 0;
    if(snapshot && (!fence || e->revision!=fence))return 0;
    c->quick=0;c->profile=profile;c->snapshot=snapshot;c->fence=fence;
    c->total=frost_preset_words(preset_client_word(c,0));
    for(i=0;i<c->total;++i)crc=client_crc_word(crc,preset_client_word(c,i));
    c->checksum=crc^UINT32_MAX;c->crc=UINT32_MAX;c->next=0;
    c->state=PRESET_DISCOVER;c->pending=c->awaiting=c->error=0;e->ready=0;return 1;
}
static int preset_client_start(FrostPresetClient *c,FrostEditor *e,unsigned profile)
{return preset_client_begin(c,e,profile,0,0);}
/* Controller-private loan of immutable staging storage, retained until idle.
 * The captured revision MUST still match discovery; never rebase a partial edit
 * over a newer live configuration. No caller-owned snapshot copy is allocated. */
static inline int preset_client_start_snapshot(FrostPresetClient *c,FrostEditor *e,const uint32_t *words,uint32_t revision)
{return words?preset_client_begin(c,e,0,words,revision):0;}
static int preset_client_start_quick_assets(FrostPresetClient *c,FrostEditor *e,const uint32_t *w,uint32_t revision,unsigned retain_ir)
{
    unsigned i,j,pos=FROST_QUICK_HEADER,order=0;uint32_t token;
    if(!w || w[0]!=FROST_PRESET_VERSION_COMPACT || !preset_client_begin(c,e,0,w,revision))return 0;
    if(c->quick_known==2u || (retain_ir && c->quick_known && !(c->quick_caps&FROST_QUICK_CAP_RETAIN_IR)))return 1; /* Older DSP: preserve the established protocol. */
    token=editor_token(e);if(!token){c->state=PRESET_IDLE;return 0;}
    for(i=0;i<FROST_QUICK_HEADER;++i)c->saved[i]=0;
    editor_header(c->saved,FROST_QUICK_COMMAND,FROST_QUICK_HEADER,token);frost_wire_put32(c->saved+12,revision);c->saved[16]=(uint8_t)w[2];
    for(i=0;i<6u;++i){
        const FrostControlEffect *d=frost_control_find(w[3+i]);
        order|=w[9+i]<<(3*i);c->saved[22+2*i]=(uint8_t)w[3+i];c->saved[23+2*i]=(uint8_t)(w[15+i]|(w[21+i]<<1));
        for(j=0;d && j<d->count;++j){
            uint32_t bits=w[j<3u?27u+3u*i+j:47u+5u*i+j-3u];float value=editor_float(bits);
            int v=value>=-100.0f && value<150.0f?(int)value:150;
            if(bits!=0x80000000u && v>=-100 && v<150 && (float)v==value)c->saved[pos++]=(uint8_t)(v+100);
            else {c->saved[pos++]=250;frost_wire_put32(c->saved+pos,bits);pos+=4;}
        }
    }
    c->saved[20]=(uint8_t)retain_ir;
    c->saved[17]=(uint8_t)order;c->saved[18]=(uint8_t)(order>>8);c->saved[19]=(uint8_t)(order>>16);
    c->saved[2]=(uint8_t)pos;c->saved[3]=(uint8_t)(pos>>8);
    c->quick_bytes=pos;c->quick_failures=0;c->quick=1;c->revision=revision;c->state=c->quick_known?((c->quick_caps&FROST_QUICK_CAP_QUIET)?PRESET_QUICK_PREPARE:PRESET_QUICK):PRESET_QUICK_PROBE;return 1;
}
static inline int preset_client_start_quick(FrostPresetClient *c,FrostEditor *e,const uint32_t *w,uint32_t revision)
{return preset_client_start_quick_assets(c,e,w,revision,0);}
static void preset_client_quick_adopt(FrostPresetClient *c,FrostEditor *e,uint32_t revision,uint32_t now)
{
    unsigned i,j;editor_reconnect(e,now);e->stage_pre=c->snapshot[2];e->stage_revision=revision;
    for(i=0;i<6u;++i){
        e->stage_types[i]=(uint8_t)c->snapshot[3+i];e->stage_order[i]=(uint8_t)c->snapshot[9+i];e->stage_enabled[i]=(uint8_t)c->snapshot[15+i];
        for(j=0;j<FROST_PARAMETERS;++j)e->stage_values[i][j]=c->snapshot[j<3u?27u+3u*i+j:47u+5u*i+j-3u];
    }
    editor_adopt(e);e->next_poll=now+FROST_EDITOR_POLL_MS;c->state=PRESET_IDLE;c->pending=c->awaiting=c->error=0;
}
static void preset_client_finish(FrostPresetClient *c,FrostEditor *e,uint32_t now,unsigned error)
{
    c->pending=c->awaiting=0;c->error=error;c->state=PRESET_REFRESH;
    if(error==2u || error==3u)++e->refusals;   /* 32i popup */
    /* Import actual state, with no pre-recall dirty intent rebased over it. */
    editor_reconnect(e,now);
}
static unsigned preset_client_next(FrostPresetClient *c,FrostEditor *e,uint8_t *p,uint32_t now)
{
    unsigned i,op;uint32_t token;
    if(c->state==PRESET_QUICK || c->state==PRESET_QUICK_WAIT || c->state==PRESET_QUICK_PROBE || c->state==PRESET_QUICK_PREPARE){
        unsigned n=c->state==PRESET_QUICK?c->quick_bytes:c->state==PRESET_QUICK_PREPARE?16u:12u;if(c->awaiting)return 0;
        for(i=0;i<n;++i)p[i]=c->saved[i];p[2]=(uint8_t)n;p[3]=(uint8_t)(n>>8);
        if(c->state==PRESET_QUICK_PROBE)frost_wire_put32(p+8,0);
        c->awaiting=1;return n;
    }
    if(c->state==PRESET_REFRESH){
        if(e->ready && !editor_pending(e)){c->state=PRESET_IDLE;return 0;}
        return editor_next(e,p,now);
    }
    if(c->state==PRESET_IDLE)return editor_next(e,p,now);
    if(c->awaiting)return 0;
    if(!c->pending){
        token=editor_token(e);
        if(!token){preset_client_finish(c,e,now,1);return 0;}
        for(i=0;i<48;++i)c->saved[i]=0;
        editor_header(c->saved,FROST_PRESET_COMMAND,48,token);
        op=c->state==PRESET_DISCOVER || c->state==PRESET_WAITING?FROST_PRESET_STATUS:
           c->state==PRESET_ABORTING?FROST_PRESET_ABORT:c->state==PRESET_BEGINNING?FROST_PRESET_BEGIN:
           c->state==PRESET_WRITING?FROST_PRESET_WRITE:FROST_PRESET_COMMIT;
        frost_wire_put32(c->saved+12,op);
        if(op!=FROST_PRESET_STATUS || c->state==PRESET_WAITING)frost_wire_put32(c->saved+16,c->session);
        if(op!=FROST_PRESET_STATUS)frost_wire_put32(c->saved+20,c->revision);
        if(op==FROST_PRESET_WRITE){
            frost_wire_put32(c->saved+24,c->next);
            for(i=0;i<5 && c->next+i<c->total;++i)
                frost_wire_put32(c->saved+28+4*i,preset_client_word(c,c->next+i));
        }else if(op==FROST_PRESET_BEGIN || op==FROST_PRESET_COMMIT){
            frost_wire_put32(c->saved+28,c->checksum);
            if(op==FROST_PRESET_COMMIT)frost_wire_put32(c->saved+24,c->total);
        }
        c->pending=1;
    }
    for(i=0;i<48;++i)p[i]=c->saved[i];c->awaiting=1;return 48;
}
static int preset_client_reply(FrostPresetClient *c,FrostEditor *e,const uint8_t *p,unsigned n,uint32_t now)
{
    uint32_t status,revision,session,state,next,floor,phase,committed,crc,expected;
    unsigned i,count;
    if(c->state==PRESET_IDLE || c->state==PRESET_REFRESH)return editor_reply(e,p,n,now);
    if(!c->awaiting)return 0;
    c->awaiting=0;
    if(c->state==PRESET_QUICK || c->state==PRESET_QUICK_WAIT || c->state==PRESET_QUICK_PROBE || c->state==PRESET_QUICK_PREPARE){
        if(!p || n!=FROST_QUICK_REPLY_BYTES || p[0]!=0x4d || p[1]!=0x7f || p[2]!=n || p[3] ||
           frost_wire_get32(p+4)!=1u || frost_wire_get32(p+8)!=(c->state==PRESET_QUICK_PROBE?0u:frost_wire_get32(c->saved+8))){
            /* Silence/malformed replies are uncertain transport, not evidence
             * of an old DSP. Keep the immutable probe/mutation for retry. */
            if(c->quick_failures<2u)++c->quick_failures;
            return 0;
        }
        c->quick_failures=0;status=frost_wire_get32(p+12);revision=frost_wire_get32(p+16);phase=frost_wire_get32(p+20);floor=frost_wire_get32(p+24);
        if(floor>e->token)e->token=floor;
        if(c->state==PRESET_QUICK_PROBE){
            if(status!=FROST_SLOT_APPLIED || revision!=c->revision){preset_client_finish(c,e,now,1);return 0;}
            if(floor>=frost_wire_get32(c->saved+8)){uint32_t next_token=editor_token(e);if(!next_token){preset_client_finish(c,e,now,1);return 0;}frost_wire_put32(c->saved+8,next_token);}
            c->quick_known=1;c->quick_caps=frost_wire_get32(p+28);
            if(c->saved[20] && !(c->quick_caps&FROST_QUICK_CAP_RETAIN_IR)){
                c->quick=0;c->state=PRESET_DISCOVER;c->pending=0;return 1;
            }
            c->state=(c->quick_caps&FROST_QUICK_CAP_QUIET)?PRESET_QUICK_PREPARE:PRESET_QUICK;return 1;
        }
        if(c->state==PRESET_QUICK_PREPARE){
            if(revision!=c->revision){preset_client_finish(c,e,now,1);return 0;}
            if(status==FROST_SLOT_APPLIED && phase==4u)c->state=PRESET_QUICK;
            else if(status!=FROST_SLOT_ACCEPTED && status!=FROST_SLOT_BUSY){preset_client_finish(c,e,now,1);return 0;}
            return 1;
        }
        if(status==FROST_SLOT_BUSY && c->state==PRESET_QUICK){c->quick=0;c->state=PRESET_DISCOVER;c->pending=0;return 1;}
        if((status!=FROST_SLOT_ACCEPTED && status!=FROST_SLOT_REPLAYED) || revision!=c->revision+1u || phase>((c->quick_caps&FROST_QUICK_CAP_QUIET)?5u:3u)){preset_client_finish(c,e,now,status==FROST_SLOT_CHAIN_FULL?2u:status==FROST_SLOT_DSP_LIMIT?3u:1u);return 0;}
        if(!phase)preset_client_quick_adopt(c,e,revision,now);else c->state=PRESET_QUICK_WAIT;
        return 1;
    }
    /* Uncertain transport retains the entire immutable request for exact retry.
     * Do not increment token or infer acceptance from an unrelated observation. */
    if(!p || n!=48 || p[0]!=0x43 || p[1]!=0x7f || p[2]!=48 || p[3] ||
       frost_wire_get32(p+4)!=1 || frost_wire_get32(p+8)!=frost_wire_get32(c->saved+8))return 0;
    status=frost_wire_get32(p+12);revision=frost_wire_get32(p+16);session=frost_wire_get32(p+20);
    state=frost_wire_get32(p+24);next=frost_wire_get32(p+28);floor=frost_wire_get32(p+32);
    phase=frost_wire_get32(p+36);committed=frost_wire_get32(p+40);crc=frost_wire_get32(p+44);
    if(!revision || state>FROST_TRANSFER_ABORTED || next>FROST_PRESET_MAX_WORDS || phase>2 ||
       (state==FROST_TRANSFER_EMPTY?(session || next || committed || crc):(!session || !floor)) ||
       (state==FROST_TRANSFER_COMPLETE && (phase || !committed || revision!=committed)))return 0;
    if(status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED && status!=FROST_SLOT_ACCEPTED){
        preset_client_finish(c,e,now,status==FROST_SLOT_CHAIN_FULL?2u:status==FROST_SLOT_DSP_LIMIT?3u:1u);return 0;   /* 2 CHAIN TOO BIG, 3 DSP LIMIT */
    }
    if(c->state==PRESET_DISCOVER){
        if(status!=FROST_SLOT_APPLIED)return 0;
        if(c->fence && revision!=c->fence){preset_client_finish(c,e,now,1);return 0;}
        if(floor>e->token)e->token=floor;
        if(phase){c->pending=0;return 1;} /* read-only until existing fade completes */
        if(revision==UINT32_MAX || session==UINT32_MAX || e->token>UINT32_MAX-218u){
            preset_client_finish(c,e,now,1);return 0;
        }
        c->session=session;c->revision=revision;
        if(state==FROST_TRANSFER_RECEIVING)c->state=PRESET_ABORTING;
        else {++c->session;c->state=PRESET_BEGINNING;}
    }else{
        if(state==FROST_TRANSFER_INVALID ||
           (c->state==PRESET_WAITING && revision!=c->revision+1)){
            preset_client_finish(c,e,now,1);return 0;
        }
        if(session!=c->session || (c->state!=PRESET_WAITING && floor<frost_wire_get32(c->saved+8)))return 0;
        if(c->state==PRESET_ABORTING){
            if((status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED) || revision!=c->revision || state!=FROST_TRANSFER_ABORTED)return 0;
            ++c->session;c->state=PRESET_BEGINNING;
        }else if(c->state==PRESET_BEGINNING){
            if((status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED) || revision!=c->revision || phase ||
               state!=FROST_TRANSFER_RECEIVING || next || crc || committed)return 0;
            c->state=PRESET_WRITING;
        }else if(c->state==PRESET_WRITING){
            count=c->total-c->next;if(count>5)count=5;
            expected=c->crc;for(i=0;i<count;++i)expected=client_crc_word(expected,frost_wire_get32(c->saved+28+4*i));
            if((status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_REPLAYED) || revision!=c->revision || phase || committed ||
               state!=FROST_TRANSFER_RECEIVING || next!=c->next+count || crc!=(expected^UINT32_MAX))return 0;
            c->next=next;c->crc=expected;if(next==c->total)c->state=PRESET_COMMITTING;
        }else{
            if(revision!=c->revision+1 || committed!=revision || next!=c->total || crc!=c->checksum ||
               (state!=FROST_TRANSFER_ACCEPTED && state!=FROST_TRANSFER_COMPLETE) ||
               (c->state==PRESET_COMMITTING?(status!=FROST_SLOT_ACCEPTED && status!=FROST_SLOT_REPLAYED):status!=FROST_SLOT_APPLIED))return 0;
            if(state==FROST_TRANSFER_COMPLETE){preset_client_finish(c,e,now,0);return 1;}
            c->state=PRESET_WAITING;
        }
    }
    c->pending=0;return 1;
}
#endif
