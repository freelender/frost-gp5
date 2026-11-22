#ifndef FROST_TIMING_WATCH_H
#define FROST_TIMING_WATCH_H
#include "firmware/protocol/timing_wire.h"
#include "engine/hot_code.h"
typedef struct {uint32_t ordinal,total,input,process,output,gap,pending,service_id,service_cycles,iteration_cycles;} FrostTimingRecord;
typedef struct {FrostTimingRecord records[FROST_TIMING_RECORDS];uint32_t blocks,last_end;} FrostTimingTrace;
static FrostTimingTrace frost_timing;
/* Rolling per-window load (256 blocks, ~0.37 s): correlates processing cost with
 * dispatched commands. Fixed storage, updated outside the audio kernel. */
typedef struct {uint32_t seq,blocks,process_sum,process_max,total_max,commands,late,command_cycles;
    uint32_t profiled,fallback,slot_mask,slot_max[6];
    /* Effect IDs are wire bytes; 0xffff marks a mixed window internally.
     * Expand that sentinel to UINT32_MAX in replies. Saves 108 bytes across
     * the nine windows without reducing history or changing the wire format. */
    uint16_t slot_type[6];} FrostLoadWindow;
typedef struct {FrostLoadWindow ring[FROST_LOAD_WINDOWS],current;uint32_t seq;} FrostLoadWatch;
static FrostLoadWatch frost_load;
static inline void frost_slot_load_note(unsigned slot,unsigned type,uint32_t begin,uint32_t end)
{
    FrostLoadWindow *c=&frost_load.current;uint32_t cycles=end-begin,bit=1u<<slot;
    if(!(c->slot_mask&bit))c->slot_type[slot]=(uint16_t)type;
    else if(c->slot_type[slot]!=type)c->slot_type[slot]=UINT16_MAX;
    c->slot_mask|=bit;if(cycles>c->slot_max[slot])c->slot_max[slot]=cycles;
}
static inline void frost_slot_load_read(const FrostLoadWatch *l,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[20]={1,0,FROST_SLOT_BAD_FRAME},index=0;unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==16 && p[0]==0x3e && p[1]==0x7f && p[2]==16 && !p[3]){
        index=frost_wire_get32(p+12);
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else if(index>=FROST_LOAD_WINDOWS)v[2]=FROST_SLOT_BAD_PARAMETER;
        else if(l->seq<=index)v[2]=FROST_SLOT_INCOMPLETE;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        const FrostLoadWindow *w=&l->ring[(l->seq-index)%FROST_LOAD_WINDOWS];
        v[3]=w->seq;v[4]=w->blocks;v[5]=w->profiled;v[6]=w->fallback;v[7]=w->slot_mask;
        for(i=0;i<6;++i){v[8+i]=w->slot_max[i];v[14+i]=w->slot_type[i]==UINT16_MAX?UINT32_MAX:w->slot_type[i];}
    }
    reply[0]=0x3e;reply[1]=0x7f;reply[2]=FROST_SLOT_LOAD_BYTES;reply[3]=0;
    for(i=0;i<20;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
static inline void frost_load_init(FrostLoadWatch *l)
{FrostLoadWindow zero={0};unsigned i;for(i=0;i<FROST_LOAD_WINDOWS;++i)l->ring[i]=zero;l->current=zero;l->seq=0;}
static inline void frost_load_block(FrostLoadWatch *l,uint32_t process,uint32_t total,uint32_t pending)
{
    FrostLoadWindow *c=&l->current;
    ++c->blocks;c->process_sum+=process;
    if(process>c->process_max)c->process_max=process;
    if(total>c->total_max)c->total_max=total;
    if(pending)++c->late;
    if(c->blocks==FROST_LOAD_BLOCKS){
        FrostLoadWindow zero={0};c->seq=++l->seq;
        l->ring[l->seq%FROST_LOAD_WINDOWS]=*c;*c=zero;
    }
}
/* Main-loop servicing attribution, outside audio. Each non-audio step is timed
 * with the DWT counter; the longest single step and the longest whole
 * non-audio iteration since the previous block are attached to the next block
 * record, so a delayed block names what ran before it. IDs: received command
 * word, or FROST_SERVICE_TX/REPORT/RX for queue service, the platform report
 * and receive drain/expiry. Read-only through 0x7f3c. */
typedef struct {uint32_t id,cycles,iteration,worst_id,worst_cycles,epoch,slot_epoch;FrostTimingRecord latest_late;} FrostServiceWatch;
static FrostServiceWatch frost_service;
static inline void frost_service_init(FrostServiceWatch *w)
{FrostTimingRecord zero={0};w->id=w->cycles=w->iteration=w->worst_id=w->worst_cycles=w->epoch=w->slot_epoch=0;w->latest_late=zero;}
static inline uint32_t frost_service_note(FrostServiceWatch *w,uint32_t id,uint32_t begin,uint32_t now)
{
    uint32_t d=now-begin;
    if(id<FROST_SERVICE_TX){++frost_load.current.commands;frost_load.current.command_cycles+=d;}
    if(d>w->cycles){w->cycles=d;w->id=id;}
    if(d>w->worst_cycles){w->worst_cycles=d;w->worst_id=id;}
    return now;
}
static inline void frost_service_iteration(FrostServiceWatch *w,uint32_t begin,uint32_t now)
{uint32_t d=now-begin;if(d>w->iteration)w->iteration=d;}
/* Servicing gate. Flash-resident handlers run cold after audio and were
 * measured at 37k..81k cycles; started just before a DMA boundary they
 * delayed the next block. One heavy step (frame dispatch or platform report)
 * may run per audio block, only within FROST_SERVICE_WINDOW cycles after that
 * block ended, when the idle gap (~85k) is still ahead. If audio has stopped
 * (no block for FROST_SERVICE_STALE cycles) the gate stays open so commands
 * remain serviceable. epoch is a wrapping per-block counter, never saturates. */
/* Installed default; a staged line-test diagnostic may override it to test
 * missed-window starvation. Widening is not a measured headroom remedy. */
#ifndef FROST_SERVICE_WINDOW
#define FROST_SERVICE_WINDOW 16384u
#endif
#define FROST_SERVICE_STALE 557000u
static inline unsigned frost_service_open(const FrostServiceWatch *w,uint32_t last_end,uint32_t now)
{
    uint32_t since=now-last_end;
    if(since>=FROST_SERVICE_STALE)return 1;
    return w->slot_epoch!=w->epoch && since<FROST_SERVICE_WINDOW;
}
static inline void frost_service_take(FrostServiceWatch *w){w->slot_epoch=w->epoch;}
FROST_COLD static inline void frost_timing_init(FrostTimingTrace *t)
{unsigned i;FrostTimingRecord zero={0};for(i=0;i<FROST_TIMING_RECORDS;++i)t->records[i]=zero;t->blocks=t->last_end=0;frost_service_init(&frost_service);frost_load_init(&frost_load);}
/* Called once per completed audio block, after the legacy timer's end point.
 * Fixed copies only. Unsigned timestamps handle one DWT wrap; gap is undefined
 * after a full counter period with no block. audio_init anchors last_end just
 * after interface enable, so first gap includes startup-to-first-block delay. */
static inline void frost_timing_record(FrostTimingTrace *t,uint32_t begin,uint32_t input_end,
                                      uint32_t process_end,uint32_t end,uint32_t pending)
{
    FrostTimingRecord r;
    r.gap=begin-t->last_end;
    if(t->blocks!=UINT32_MAX)++t->blocks;
    r.ordinal=t->blocks;r.total=end-begin;r.input=input_end-begin;
    r.process=process_end-input_end;r.output=end-process_end;r.pending=pending&15u;
    frost_load_block(&frost_load,r.process,r.total,r.pending);
    r.service_id=frost_service.id;r.service_cycles=frost_service.cycles;r.iteration_cycles=frost_service.iteration;
    frost_service.id=frost_service.cycles=frost_service.iteration=0;++frost_service.epoch;
    t->last_end=end;
    if(r.ordinal<=FROST_TIMING_FIRST)t->records[r.ordinal-1]=r;
    if(!t->records[FROST_TIMING_WORST].ordinal || r.total>t->records[FROST_TIMING_WORST].total)
        t->records[FROST_TIMING_WORST]=r;
    if(r.pending && !t->records[FROST_TIMING_LATE].ordinal)t->records[FROST_TIMING_LATE]=r;
    if(r.pending)frost_service.latest_late=r;
    t->records[FROST_TIMING_LAST]=r;
}
static inline void frost_timing_read(const FrostTimingTrace *t,unsigned mute,unsigned source,
                                    const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME},index=0;unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==16 && p[0]==0x39 && p[1]==0x7f && p[2]==16 && !p[3]){
        index=frost_wire_get32(p+12);
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else if(index>=FROST_TIMING_RECORDS)v[2]=FROST_SLOT_BAD_PARAMETER;
        else if(!t->records[index].ordinal)v[2]=FROST_SLOT_INCOMPLETE;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        const FrostTimingRecord *r=&t->records[index];
        v[3]=index;v[4]=r->ordinal;v[5]=r->total;v[6]=r->input;v[7]=r->process;
        v[8]=r->output;v[9]=r->gap;v[10]=r->pending|((!!mute)<<4)|((source&3u)<<5);
    }
    reply[0]=0x39;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
/* Request16: version,token,index0..11 (11 = latest late block, service-only).
 * Reply48: version,token,status,index,ordinal,
 * gap,pending,service_id|(worst_id<<16),service_cycles,iteration_cycles,worst_cycles. */
static inline void frost_load_read(const FrostLoadWatch *l,const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME},index=0;unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==16 && p[0]==0x3d && p[1]==0x7f && p[2]==16 && !p[3]){
        index=frost_wire_get32(p+12);
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else if(index>=FROST_LOAD_WINDOWS)v[2]=FROST_SLOT_BAD_PARAMETER;
        else if(l->seq<=index)v[2]=FROST_SLOT_INCOMPLETE;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        const FrostLoadWindow *w=&l->ring[(l->seq-index)%FROST_LOAD_WINDOWS];
        v[3]=w->seq;v[4]=w->blocks;v[5]=w->blocks?w->process_sum/w->blocks:0;v[6]=w->process_max;
        v[7]=w->total_max;v[8]=w->commands;v[9]=w->late;v[10]=w->commands?w->command_cycles/w->commands:0;
    }
    reply[0]=0x3d;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
static inline void frost_service_read(const FrostTimingTrace *t,const FrostServiceWatch *w,
                                     const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[11]={1,0,FROST_SLOT_BAD_FRAME},index=0;unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==16 && p[0]==0x3c && p[1]==0x7f && p[2]==16 && !p[3]){
        index=frost_wire_get32(p+12);
        if(frost_wire_get32(p+4)!=1)v[2]=FROST_SLOT_BAD_VERSION;
        else if(!v[1])v[2]=FROST_SLOT_BAD_REQUEST_ID;
        else if(index>FROST_TIMING_RECORDS)v[2]=FROST_SLOT_BAD_PARAMETER;
        else if(!(index==FROST_TIMING_RECORDS?w->latest_late.ordinal:t->records[index].ordinal))v[2]=FROST_SLOT_INCOMPLETE;
        else v[2]=FROST_SLOT_APPLIED;
    }
    if(v[2]==FROST_SLOT_APPLIED){
        const FrostTimingRecord *r=index==FROST_TIMING_RECORDS?&w->latest_late:&t->records[index];
        v[3]=index;v[4]=r->ordinal;v[5]=r->gap;v[6]=r->pending;
        v[7]=(r->service_id&0xffffu)|(w->worst_id<<16);v[8]=r->service_cycles;
        v[9]=r->iteration_cycles;v[10]=w->worst_cycles;
    }
    reply[0]=0x3c;reply[1]=0x7f;reply[2]=48;reply[3]=0;
    for(i=0;i<11;++i)frost_wire_put32(reply+4+4*i,v[i]);
}
#endif
