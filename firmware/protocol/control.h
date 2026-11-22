#ifndef FROST_CHAIN_CONTROL_H
#define FROST_CHAIN_CONTROL_H
/* Protocol v1, cmd 0x7f10 [u16 cmd,u16 length=12,u32 id,u32 value].
 * IDs 0/2/4: gain/gate/EQ enable; 1 gain x1000; 3 threshold x1000000;
 * 5/6/7 EQ low/mid/high x1000. Independent of compiler struct layout. */
#include "tests/fixtures/diagnostic_ir.h"
/* 31a: a baked cabinet may be stored as 16-bit coefficients (q15 table plus
 * one scale); the synthetic fixture stays float. */
#ifdef FROST_TEST_IR_Q15
#define FROST_TEST_IR_STORED ((unsigned)(sizeof frost_test_ir_q15/sizeof frost_test_ir_q15[0]))
#define frost_test_ir_load(s,slot,taps) frost_chain_load_ir_q15(s,slot,frost_test_ir_q15,taps,frost_test_ir_q15_scale)
#else
#define FROST_TEST_IR_STORED ((unsigned)(sizeof frost_test_ir/sizeof frost_test_ir[0]))
#define frost_test_ir_load(s,slot,taps) frost_chain_load_ir(s,slot,frost_test_ir,taps)
#endif
#include "settings.h"
#include "slots.h"
FROST_COLD static int frost_control_layout(const FrostChain *s)
{
    unsigned i;
    if(s->pre_count!=3)return 0;
    for(i=0;i<FROST_SLOTS;++i)
        if(s->order[i]!=i || s->slots[i].type!=frost_default_type(i))return 0;
    return 1;
}
/* v2 extends IDs with 8 IR enable, 9 diagnostic taps (16..512 in 16s),
 * 10 IR output x1000. ID 9 resets FIR history: muted bench use only. */
FROST_COLD static int frost_control_set(FrostChain *s,uint32_t id,uint32_t value)
{
    const FrostSetting *d;
    if(id>=FROST_SETTING_COUNT || !frost_control_layout(s) || s->transition)return 0;
    d=&frost_settings[id];
    if(value<(uint32_t)d->control.minimum || value>(uint32_t)d->control.maximum ||
       s->slots[d->slot].type!=d->type)return 0;
    if(d->kind==FROST_CONTROL_ENABLE)return frost_chain_enable(s,d->slot,value);
    if(d->kind==FROST_CONTROL_IR_TAPS){
        unsigned stored=FROST_TEST_IR_STORED;
        if(value%(unsigned)d->control.step)return 0;
        if(value<=stored)return frost_test_ir_load(s,d->slot,value);
        /* 28a: the baked table may hold only its kept taps. A longer setting
         * runs zero taps beyond them, identical to loading a zero-padded table
         * (frost_ir_load zero-fills coefficients and history past its taps). */
        if(!frost_test_ir_load(s,d->slot,stored))return 0;
        s->slots[d->slot].state.ir->taps=value;return 1;
    }
    return frost_chain_set(s,d->slot,d->parameter,(float)value/(float)d->control.scale);
}
FROST_COLD static uint32_t frost_control_actual(const FrostChain *s,uint32_t id)
{
    const FrostSetting *d;const FrostInstance *slot;
    if(id>=FROST_SETTING_COUNT || !frost_control_layout(s))return UINT32_MAX;
    d=&frost_settings[id];slot=&s->slots[d->slot];
    if(d->kind==FROST_CONTROL_ENABLE)return (uint32_t)slot->wet_target;
    if(d->kind==FROST_CONTROL_IR_TAPS)return slot->state.ir->taps;
    return (uint32_t)(slot->target[d->parameter]*(float)d->control.scale+.5f);
}
static unsigned frost_control_apply(FrostChain *chain,FrostSlotProtocol *protocol,const uint32_t *v)
{
    unsigned i;
    if(v[0]!=FROST_SLOT_VERSION)return FROST_SLOT_BAD_VERSION;
    if(!v[1])return FROST_SLOT_BAD_REQUEST_ID;
    if(v[3]>=FROST_SETTING_COUNT)return FROST_SLOT_BAD_SETTING;
    if(chain->transition)return FROST_SLOT_BUSY;
    if(protocol->cached && protocol->last_write[1]==v[1]){
        if(protocol->cached!=FROST_CACHE_SETTING)return FROST_SLOT_BAD_REQUEST_ID;
        for(i=0;i<5;++i)if(protocol->last_write[i]!=v[i])return FROST_SLOT_BAD_REQUEST_ID;
        return FROST_SLOT_REPLAYED;
    }
    if(v[2]!=protocol->revision)return FROST_SLOT_STALE_REVISION;
    if(!frost_control_layout(chain))return FROST_SLOT_WRONG_TYPE;
    if(protocol->revision==UINT32_MAX)return FROST_SLOT_REVISION_EXHAUSTED;
    if(!frost_control_set(chain,v[3],v[4]))return FROST_SLOT_BAD_VALUE;
    frost_slots_legacy_changed(protocol);
    for(i=0;i<8;++i)protocol->last_write[i]=i<5?v[i]:0;
    protocol->cached=FROST_CACHE_SETTING;return FROST_SLOT_APPLIED;
}
static void frost_control_command(FrostChain *chain,FrostSlotProtocol *protocol,
    const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[5]={0},status=FROST_SLOT_BAD_FRAME;unsigned i;
    v[3]=UINT32_MAX;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==FROST_SETTING_REQUEST_BYTES && request[0]==0x11 &&
       request[1]==0x7f && request[2]==FROST_SETTING_REQUEST_BYTES && !request[3]){
        for(i=0;i<5;++i)v[i]=frost_wire_get32(request+4+4*i);
        status=frost_control_apply(chain,protocol,v);
    }
    for(i=0;i<FROST_SETTING_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=0x11;reply[1]=0x7f;reply[2]=FROST_SETTING_REPLY_BYTES;
    frost_wire_put32(reply+4,FROST_SLOT_VERSION);frost_wire_put32(reply+8,v[1]);
    frost_wire_put32(reply+12,status);frost_wire_put32(reply+16,protocol->revision);
    frost_wire_put32(reply+20,v[3]);frost_wire_put32(reply+24,v[4]);
    frost_wire_put32(reply+28,frost_control_actual(chain,v[3]));
}
#endif
