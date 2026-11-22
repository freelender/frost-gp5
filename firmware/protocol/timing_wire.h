#ifndef FROST_TIMING_WIRE_H
#define FROST_TIMING_WIRE_H
#include "slot_wire.h"
#define FROST_TIMING_COMMAND 0x7f39u
#define FROST_TIMING_FIRST 8u
#define FROST_TIMING_WORST 8u
#define FROST_TIMING_LATE 9u
#define FROST_TIMING_LAST 10u
#define FROST_TIMING_RECORDS 11u
/* Request16: version,token,index. Reply48: version,token,status,index,
 * ordinal,total,input,process,output,gap,flags. Flags0..3 DMA pending,
 * bit4 hard mute, bits5..6 source1synthetic/2ADC. Ordinal0 is unavailable. */
#define FROST_SERVICE_COMMAND 0x7f3cu
#define FROST_SERVICE_TX 0xfff1u
#define FROST_SERVICE_REPORT 0xfff2u
#define FROST_SERVICE_RX 0xfff3u
/* 0x7f3d load windows. Request16: version,token,index (0 = newest complete
 * window .. 15). Reply48: version,token,status,seq,blocks,process_mean,
 * process_max,total_max,commands,late_blocks,command_cycles_mean. */
#define FROST_LOAD_COMMAND 0x7f3du
#define FROST_LOAD_WINDOWS 8u   /* 30a: was 16; frees 736 B VARS for RAM loops (~3 s history) */
#define FROST_LOAD_BLOCKS 256u
/* Read-only slot maxima in the same completed windows as 0x7f3d.
 * Request16: version,token,index. Reply84: version,token,status,seq,blocks,
 * profiled_blocks,fallback_blocks,slot_mask,max_cycles[6],effect_type[6].
 * Physical slot IDs, independent of route order. Type UINT32_MAX = mixed.
 * Unmeasured slots have zero max/type; mask distinguishes gain type 0.
 * Fades/invalid-input fallback are counted, but not timed per sample. */
#define FROST_SLOT_LOAD_COMMAND 0x7f3eu
#define FROST_SLOT_LOAD_BYTES 84u
#endif
