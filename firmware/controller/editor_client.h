#ifndef FROST_EDITOR_CLIENT_H
#define FROST_EDITOR_CLIENT_H
/* Idle revision poll period. Each DSP command runs cold from flash (57k..82k
 * cycles measured); on a heavy chain (e.g. gain/gate/EQ/IR/DS-1 at ~83% load)
 * even the idle route-status poll overran a block and clicked (revision 23p).
 * The line-test build therefore polls only every 60 s. Own edits are still read
 * back immediately; only changes made outside the pedal appear later. */
#ifndef FROST_EDITOR_POLL_MS
#ifdef FROST_LINE_TEST
#define FROST_EDITOR_POLL_MS 60000u
#else
#define FROST_EDITOR_POLL_MS 100u
#endif
#endif
/* Minimum spacing of PARAMETER writes while a knob turns (line-test build).
 * Each DSP slot write costs ~55-85k cycles and evicts cached audio code; on a
 * heavily loaded chain every write overruns the block deadline (revision 23h
 * DS-1 measurement), and fast turning cascaded into the LATE latch. Writes are
 * coalesced: the latest value is sent at most every FROST_EDITOR_WRITE_MS.
 * Enable/bypass writes (footswitch) and route writes are never delayed. */
#ifndef FROST_EDITOR_WRITE_MS
#ifdef FROST_LINE_TEST
#define FROST_EDITOR_WRITE_MS 120u
#else
#define FROST_EDITOR_WRITE_MS 0u
#endif
#endif
#include "engine/control_catalog.h"
#include "firmware/protocol/sync_wire.h"
/* One outstanding request/mutation and one staged coherent snapshot. No IO or
 * allocation here. Requests/replies run on the controller's serialized loop. */
enum { EDITOR_ROUTE_READ, EDITOR_SLOT_READ, EDITOR_READY };
/* Dirty mask: bit p = parameter p (0..FROST_PARAMETERS-1); EDITOR_DIRTY_ENABLE
 * = enable. Parameters 0..2 use version-1 frames exactly as before; windows
 * 3..5 and 6..7 use version-2 windowed frames (firmware/protocol/slot_wire.h). */
#define EDITOR_DIRTY_ENABLE (1u<<FROST_PARAMETERS)
typedef struct {
    uint8_t types[6],order[6],enabled[6],desired_enabled[6];
    uint16_t dirty[6];
    uint32_t current[6][FROST_PARAMETERS],desired[6][FROST_PARAMETERS];
    uint8_t stage_types[6],stage_order[6],stage_enabled[6];
    uint32_t stage_values[6][FROST_PARAMETERS],stage_revision;
    unsigned pre,stage_pre,scan,slot,window,ready,initialized,busy,error;
    uint32_t revision,token,wait_token,next_poll,next_write;
    unsigned awaiting,kind,recover,retry_ready,mask_pending;
    uint8_t saved[36];unsigned saved_len;
    uint8_t route_types[6],route_order[6],basis_types[6],basis_order[6];
    unsigned route_pre,basis_pre,route_state;
    unsigned refusals;   /* 32i: chains refused for memory/DSP load (popup trigger); survives reconnect */
    uint32_t route_revision;
} FrostEditor;
static uint32_t editor_bits(float value){union {float f;uint32_t u;} v;v.f=value;return v.u;}
static float editor_float(uint32_t bits){union {float f;uint32_t u;} v;v.u=bits;return v.f;}
static int editor_raw_valid(unsigned type,unsigned p,uint32_t bits)
{
    const FrostControlEffect *d=frost_control_find(type);float value;
    if(!d || p>=d->count || (bits&0x7f800000u)==0x7f800000u)return 0;
    value=editor_float(bits);return value>=d->dsp[p].minimum && value<=d->dsp[p].maximum;
}
static int editor_integer(const FrostEditor *e,unsigned slot,unsigned p)
{
    return frost_control_from_dsp(e->types[slot],p,editor_float(e->desired[slot][p]));
}
static void editor_init(FrostEditor *e,uint32_t now)
{
    unsigned i,j;
    for(i=0;i<6;++i){
        e->types[i]=FROST_EMPTY;e->order[i]=(uint8_t)i;e->enabled[i]=e->desired_enabled[i]=e->dirty[i]=0;
        for(j=0;j<FROST_PARAMETERS;++j)e->current[i][j]=e->desired[i][j]=0;
    }
    e->pre=3;e->scan=EDITOR_ROUTE_READ;e->slot=e->window=e->ready=e->initialized=e->busy=e->error=0;
    e->revision=e->token=e->wait_token=0;e->next_poll=e->next_write=now;
    e->awaiting=e->kind=e->recover=e->retry_ready=e->saved_len=e->route_state=e->mask_pending=0;e->refusals=0;
}
static void editor_reconnect(FrostEditor *e,uint32_t now)
{uint32_t token=e->token;unsigned refusals=e->refusals;editor_init(e,now);e->token=token;e->refusals=refusals;}
static int editor_editable(const FrostEditor *e)
{return e->ready && !e->busy && !e->route_state && e->revision!=UINT32_MAX;}
static int editor_pending(const FrostEditor *e)
{unsigned i;if(e->saved_len || e->route_state || e->mask_pending)return 1;for(i=0;i<6;++i)if(e->dirty[i])return 1;return 0;}
static int editor_same_route(const uint8_t *a,const uint8_t *b,const uint8_t *c,const uint8_t *d,unsigned pre_a,unsigned pre_b)
{unsigned i;if(pre_a!=pre_b)return 0;for(i=0;i<6;++i)if(a[i]!=b[i] || c[i]!=d[i])return 0;return 1;}
static void editor_adopt(FrostEditor *e)
{
    unsigned i,j;
    if(e->route_state==1 && !editor_same_route(e->stage_types,e->basis_types,e->stage_order,e->basis_order,e->stage_pre,e->basis_pre)){
        e->route_state=0;e->error=2;
    }
    for(i=0;i<6;++i){
        if(!e->initialized || e->types[i]!=e->stage_types[i])e->dirty[i]=0;
        e->types[i]=e->stage_types[i];e->order[i]=e->stage_order[i];e->enabled[i]=e->stage_enabled[i];
        if(!(e->dirty[i]&EDITOR_DIRTY_ENABLE) || e->desired_enabled[i]==e->enabled[i]){
            e->desired_enabled[i]=e->enabled[i];e->dirty[i]&=(uint16_t)~EDITOR_DIRTY_ENABLE;
        }
        for(j=0;j<FROST_PARAMETERS;++j){
            e->current[i][j]=e->stage_values[i][j];
            if(!(e->dirty[i]&(1u<<j)) || e->desired[i][j]==e->current[i][j]){
                e->desired[i][j]=e->current[i][j];e->dirty[i]&=(uint16_t)~(1u<<j);
            }
        }
    }
    e->pre=e->stage_pre;e->revision=e->stage_revision;e->initialized=e->ready=1;e->busy=0;e->scan=EDITOR_READY;
    if(e->error==1)e->error=0;
}
static int editor_queue_raw(FrostEditor *e,unsigned slot,unsigned p,uint32_t bits)
{
    if(!editor_editable(e) || slot>=6 || !editor_raw_valid(e->types[slot],p,bits))return 0;
    e->desired[slot][p]=bits;e->dirty[slot]|=(uint16_t)(1u<<p);e->error=0;return 1;
}
static int editor_queue_parameter(FrostEditor *e,unsigned slot,unsigned p,int value)
{
    const FrostControlEffect *d;
    if(!editor_editable(e) || slot>=6 || !(d=frost_control_find(e->types[slot])) || p>=d->count ||
       value<d->p[p].minimum || value>d->p[p].maximum)return 0;
    return editor_queue_raw(e,slot,p,editor_bits(frost_control_to_dsp(e->types[slot],p,value)));
}
static int editor_queue_enable(FrostEditor *e,unsigned slot,unsigned value)
{
    if(!editor_editable(e) || slot>=6 || e->types[slot]==FROST_EMPTY || value>1)return 0;
    e->desired_enabled[slot]=(uint8_t)value;e->dirty[slot]|=EDITOR_DIRTY_ENABLE;e->error=0;return 1;
}
static int editor_queue_route(FrostEditor *e,const uint8_t *types,const uint8_t *order,unsigned pre)
{
    unsigned i,irs=0,delays=0,mods=0,reverbs=0,mask=0;
    if(!editor_editable(e) || pre>6)return 0;
    for(i=0;i<6;++i){
        if(!frost_control_find(types[i]) || order[i]>=6 || (mask&(1u<<order[i])))return 0;
        mask|=1u<<order[i];if(types[i]==FROST_IR && ++irs>2){e->error=3;return 0;}
        if(frost_type_is_delay(types[i]) && ++delays>FROST_DELAY_WORKSPACES){e->error=3;return 0;}
        if(frost_type_is_modulation(types[i]) && ++mods>FROST_MOD_WORKSPACES){e->error=3;return 0;}
        if(frost_type_is_reverb(types[i]) && ++reverbs>FROST_REVERB_WORKSPACES){e->error=3;return 0;}
    }
    for(i=0;i<6;++i){
        e->route_types[i]=types[i];e->route_order[i]=order[i];
        e->basis_types[i]=e->types[i];e->basis_order[i]=e->order[i];
    }
    e->route_pre=pre;e->basis_pre=e->pre;e->route_state=1;e->error=0;return 1;
}
static int editor_queue_mask(FrostEditor *e,unsigned mask)
{
    unsigned i;if(!editor_editable(e) || mask>63u)return 0;
    for(i=0;i<6u;++i)if(e->types[i]!=FROST_EMPTY){e->desired_enabled[i]=(uint8_t)((mask>>i)&1u);e->dirty[i]|=EDITOR_DIRTY_ENABLE;}
    e->mask_pending=1;e->error=0;return 1;
}
static unsigned editor_token(FrostEditor *e)
{if(e->token==UINT32_MAX){e->ready=0;e->error=1;return 0;}return ++e->token;}
static void editor_header(uint8_t *p,unsigned cmd,unsigned n,uint32_t token)
{p[0]=(uint8_t)cmd;p[1]=(uint8_t)(cmd>>8);p[2]=(uint8_t)n;p[3]=0;frost_wire_put32(p+4,1);frost_wire_put32(p+8,token);}
static unsigned editor_next(FrostEditor *e,uint8_t *p,uint32_t now)
{
    unsigned i,j,n=0;uint32_t token;
    if(e->awaiting)return 0;
    if(e->saved_len && e->retry_ready){
        for(i=0;i<e->saved_len;++i)p[i]=e->saved[i];n=e->saved_len;e->kind=3;e->retry_ready=0;
    }else if(e->recover || e->scan==EDITOR_ROUTE_READ || e->route_state==2 ||
             (e->scan==EDITOR_READY && !editor_pending(e) && (int32_t)(now-e->next_poll)>=0)){
        if((int32_t)(now-e->next_poll)<0)return 0;
        token=editor_token(e);if(!token)return 0;
        editor_header(p,FROST_ROUTE_STATUS_COMMAND,12,token);n=12;e->kind=1;
    }else if(e->scan==EDITOR_SLOT_READ){
        token=editor_token(e);if(!token)return 0;
        editor_header(p,FROST_SLOT_COMMAND,36,token);frost_wire_put32(p+12,FROST_SLOT_GET);
        frost_wire_put32(p+16,0);frost_wire_put32(p+20,e->slot);frost_wire_put32(p+24,FROST_EMPTY);
        if(e->window){frost_wire_put32(p+4,FROST_SLOT_VERSION_WINDOWED);frost_wire_put32(p+28,e->window);}
        else frost_wire_put32(p+28,UINT32_MAX);
        frost_wire_put32(p+32,0);n=36;e->kind=2;
    }else if(e->scan==EDITOR_READY && e->ready){
        if(e->mask_pending){
            unsigned mask=0;for(i=0;i<6u;++i)if(e->types[i]!=FROST_EMPTY && e->desired_enabled[i])mask|=1u<<i;
            token=editor_token(e);if(!token)return 0;
            editor_header(p,FROST_SLOT_COMMAND,36,token);frost_wire_put32(p+12,FROST_SLOT_ENABLE_MASK);frost_wire_put32(p+16,e->revision);
            frost_wire_put32(p+20,0);frost_wire_put32(p+24,FROST_EMPTY);frost_wire_put32(p+28,UINT32_MAX);frost_wire_put32(p+32,mask);n=36;
        }
        /* Original order preserved: parameters first (0..7), then enable. */
        unsigned throttled=FROST_EDITOR_WRITE_MS && (int32_t)(now-e->next_write)<0;
        for(i=0;i<6 && !n;++i)for(j=0;j<=FROST_PARAMETERS && !n;++j)if(e->dirty[i]&(1u<<j)){
            unsigned enable=j==FROST_PARAMETERS;
            if(throttled && !enable)continue;
            if(!enable)e->next_write=now+FROST_EDITOR_WRITE_MS;
            token=editor_token(e);if(!token)return 0;
            editor_header(p,FROST_SLOT_COMMAND,36,token);
            if(!enable && j>=FROST_SLOT_WINDOW)frost_wire_put32(p+4,FROST_SLOT_VERSION_WINDOWED);
            frost_wire_put32(p+12,enable?FROST_SLOT_ENABLE:FROST_SLOT_PARAMETER);frost_wire_put32(p+16,e->revision);
            frost_wire_put32(p+20,i);frost_wire_put32(p+24,e->types[i]);frost_wire_put32(p+28,enable?UINT32_MAX:j);
            frost_wire_put32(p+32,enable?e->desired_enabled[i]:e->desired[i][j]);n=36;
        }
        if(!n && e->route_state==1){
            token=editor_token(e);if(!token)return 0;
            editor_header(p,FROST_ROUTE_WRITE_COMMAND,32,token);frost_wire_put32(p+12,e->revision);frost_wire_put32(p+16,e->route_pre);
            for(i=0;i<6;++i){p[20+i]=e->route_types[i];p[26+i]=e->route_order[i];}n=32;
        }
        if(n){for(i=0;i<n;++i)e->saved[i]=p[i];e->saved_len=n;e->kind=3;}
    }
    if(n){e->wait_token=frost_wire_get32(p+8);e->awaiting=1;}
    return n;
}
static int editor_snapshot(const uint8_t *p,uint8_t *types,uint8_t *order,unsigned *pre)
{
    unsigned i,mask=0,phase=frost_wire_get32(p+20),position=frost_wire_get32(p+24);
    *pre=frost_wire_get32(p+28);
    if(phase>2 || position>=128 || (!phase && position) || *pre>6 || !frost_wire_get32(p+16))return 0;
    for(i=0;i<6;++i){
        if(!frost_control_find(p[32+i]) || p[38+i]>=6 || (mask&(1u<<p[38+i])))return 0;
        types[i]=p[32+i];order[i]=p[38+i];mask|=1u<<order[i];
    }
    return 1;
}
/* Validates one reply window and stores values[base..base+2]. */
static int editor_slot_values(const uint8_t *p,unsigned type,unsigned base,uint32_t *values,unsigned *enabled)
{
    unsigned i;const FrostControlEffect *d=frost_control_find(type);
    uint32_t version=frost_wire_get32(p+4),count=frost_wire_get32(p+32);
    if(!d || frost_wire_get32(p+24)!=type || (count&0xffffu)!=d->count || (count>>16)!=base ||
       (base && version!=FROST_SLOT_VERSION_WINDOWED) || base>=FROST_PARAMETERS)return 0;
    *enabled=frost_wire_get32(p+28);if(*enabled>1 || (type==FROST_EMPTY && *enabled))return 0;
    for(i=0;i<FROST_SLOT_WINDOW;++i){uint32_t value=frost_wire_get32(p+36+4*i);
        if(base+i<d->count?!editor_raw_valid(type,base+i,value):value!=0)return 0;
        if(base+i<FROST_PARAMETERS)values[base+i]=value;}
    return 1;
}
static int editor_reply(FrostEditor *e,const uint8_t *p,unsigned n,uint32_t now)
{
    unsigned i,j,kind=e->kind,status,cmd=e->kind==1?FROST_ROUTE_STATUS_COMMAND:
        e->kind==2?FROST_SLOT_COMMAND:(unsigned)e->saved[0]|((unsigned)e->saved[1]<<8);
    unsigned expected=cmd==FROST_SLOT_COMMAND?48:44;
    uint32_t revision;
    if(!e->awaiting)return 0;
    e->awaiting=0;
    if(!p || n!=expected || p[0]!=(uint8_t)cmd || p[1]!=(uint8_t)(cmd>>8) || p[2]!=n || p[3] ||
       (frost_wire_get32(p+4)!=1 && !(cmd==FROST_SLOT_COMMAND && frost_wire_get32(p+4)==FROST_SLOT_VERSION_WINDOWED)) ||
       frost_wire_get32(p+8)!=e->wait_token)goto invalid;
    status=frost_wire_get32(p+12);revision=frost_wire_get32(p+16);if(!revision)goto invalid;
    if(kind==1){
        uint8_t types[6],order[6];unsigned pre,phase;
        if((status!=FROST_SLOT_APPLIED && status!=FROST_SLOT_BUSY) || !editor_snapshot(p,types,order,&pre))goto invalid;
        phase=frost_wire_get32(p+20);if((status==FROST_SLOT_BUSY)!=(phase!=0))goto invalid;
        e->next_poll=now+FROST_EDITOR_POLL_MS;
        if(e->recover){e->recover=0;e->retry_ready=1;return 1;}
        if(phase){e->ready=0;e->busy=1;e->scan=EDITOR_ROUTE_READ;return 1;}
        if(e->route_state==2){
            if(revision!=e->route_revision || !editor_same_route(types,e->route_types,order,e->route_order,pre,e->route_pre))e->error=2;
            e->route_state=0;e->ready=0;
        }
        if(e->ready && revision==e->revision && editor_same_route(types,e->types,order,e->order,pre,e->pre))return 1;
        for(i=0;i<6;++i){e->stage_types[i]=types[i];e->stage_order[i]=order[i];}
        e->stage_pre=pre;e->stage_revision=revision;e->slot=e->window=0;e->scan=EDITOR_SLOT_READ;e->ready=e->busy=0;return 1;
    }
    if(kind==2){
        unsigned enabled;const FrostControlEffect *d=frost_control_find(e->stage_types[e->slot]);
        if(!e->window){unsigned k;for(k=0;k<FROST_PARAMETERS;++k)e->stage_values[e->slot][k]=0;}
        if(status!=FROST_SLOT_APPLIED || revision!=e->stage_revision || frost_wire_get32(p+20)!=e->slot || !d ||
           frost_wire_get32(p+4)!=(e->window?FROST_SLOT_VERSION_WINDOWED:FROST_SLOT_VERSION) ||
           !editor_slot_values(p,e->stage_types[e->slot],e->window,e->stage_values[e->slot],&enabled) ||
           (e->window && enabled!=e->stage_enabled[e->slot]))goto refresh;
        e->stage_enabled[e->slot]=(uint8_t)enabled;
        e->window+=FROST_SLOT_WINDOW;
        if(e->window>=d->count || e->window>=FROST_PARAMETERS){e->window=0;if(++e->slot==6)editor_adopt(e);}
        return 1;
    }
    if(cmd==FROST_ROUTE_WRITE_COMMAND &&
       (status==FROST_SLOT_APPLIED || status==FROST_SLOT_ACCEPTED || status==FROST_SLOT_REPLAYED)){
        uint8_t types[6],order[6];unsigned pre;
        if(!editor_snapshot(p,types,order,&pre) || frost_wire_get32(e->saved+12)==UINT32_MAX ||
           revision!=frost_wire_get32(e->saved+12)+1)goto invalid;
        e->saved_len=0;e->route_revision=revision;e->route_state=2;
        e->ready=0;e->busy=frost_wire_get32(p+20)!=0;e->scan=EDITOR_ROUTE_READ;e->next_poll=now;return 1;
    }
    if(cmd==FROST_SLOT_COMMAND && (status==FROST_SLOT_APPLIED || status==FROST_SLOT_REPLAYED)){
        if(frost_wire_get32(e->saved+12)==FROST_SLOT_ENABLE_MASK){
            unsigned mask=frost_wire_get32(e->saved+32),dirty=0;
            if(revision!=frost_wire_get32(e->saved+16)+1u || frost_wire_get32(p+20) || frost_wire_get32(p+24)!=FROST_EMPTY ||
               frost_wire_get32(p+28)!=mask || frost_wire_get32(p+32) || frost_wire_get32(p+36) || frost_wire_get32(p+40) || frost_wire_get32(p+44))goto invalid;
            for(i=0;i<6u;++i){e->enabled[i]=(uint8_t)((mask>>i)&1u);
                if(e->desired_enabled[i]==e->enabled[i])e->dirty[i]&=(uint16_t)~EDITOR_DIRTY_ENABLE;else dirty=1;
            }
            e->mask_pending=dirty;e->revision=revision;e->saved_len=0;return 1;
        }
        unsigned slot=frost_wire_get32(e->saved+20),parameter=frost_wire_get32(e->saved+28),enabled,base;
        uint32_t values[FROST_PARAMETERS],value=frost_wire_get32(e->saved+32);
        base=parameter==UINT32_MAX || parameter>=FROST_PARAMETERS?0:parameter-parameter%FROST_SLOT_WINDOW;
        if(slot>=6 || frost_wire_get32(p+20)!=slot || revision!=frost_wire_get32(e->saved+16)+1 ||
           frost_wire_get32(p+4)!=frost_wire_get32(e->saved+4) ||
           !editor_slot_values(p,e->types[slot],base,values,&enabled))goto invalid;
        if(parameter==UINT32_MAX){if(enabled!=value)goto invalid;
            e->enabled[slot]=(uint8_t)enabled;if(e->desired_enabled[slot]==value)e->dirty[slot]&=(uint16_t)~EDITOR_DIRTY_ENABLE;
        }else{
            if(parameter>=FROST_PARAMETERS || values[parameter]!=value)goto invalid;
            if(e->desired[slot][parameter]==value)e->dirty[slot]&=(uint16_t)~(1u<<parameter);
        }
        for(j=base;j<base+FROST_SLOT_WINDOW && j<FROST_PARAMETERS;++j)e->current[slot][j]=values[j];
        e->revision=revision;e->saved_len=0;if(e->error==1)e->error=0;return 1;
    }
    /* Explicit rejection permits a fresh snapshot/rebase; uncertain transport
     * errors retain immutable bytes and recover through a read-only query. */
    if(cmd==FROST_ROUTE_WRITE_COMMAND && status!=FROST_SLOT_STALE_REVISION && status!=FROST_SLOT_BUSY){e->route_state=0;e->error=status==FROST_SLOT_CHAIN_FULL?4:status==FROST_SLOT_DSP_LIMIT?5:3;if(e->error>3)++e->refusals;}
    if(cmd==FROST_SLOT_COMMAND && status!=FROST_SLOT_STALE_REVISION && status!=FROST_SLOT_BUSY && status!=FROST_SLOT_WRONG_TYPE){
        if(frost_wire_get32(e->saved+12)==FROST_SLOT_ENABLE_MASK)e->mask_pending=0;
        unsigned slot=frost_wire_get32(e->saved+20),parameter=frost_wire_get32(e->saved+28);
        uint32_t value=frost_wire_get32(e->saved+32);
        if(slot<6){
            if(parameter==UINT32_MAX){if(e->desired_enabled[slot]==value)e->dirty[slot]&=(uint16_t)~EDITOR_DIRTY_ENABLE;}
            else if(parameter<FROST_PARAMETERS && e->desired[slot][parameter]==value)e->dirty[slot]&=(uint16_t)~(1u<<parameter);
        }e->error=3;
    }
    e->saved_len=0;
refresh:
    e->ready=0;e->scan=EDITOR_ROUTE_READ;e->next_poll=now;return 0;
invalid:
    e->ready=0;e->error=1;e->next_poll=now+100u;
    if(e->saved_len)e->recover=1;else e->scan=EDITOR_ROUTE_READ;
    return 0;
}
#endif
