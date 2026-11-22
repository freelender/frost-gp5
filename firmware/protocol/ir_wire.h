#ifndef FROST_IR_WIRE_H
#define FROST_IR_WIRE_H
#include "slot_wire.h"
/* 32-byte request: version, token, op, physical slot, record CRC, word
 * offset, count; then <=20 words. Reply: version, token, status, state,
 * slot, CRC, next. BEGIN/COMMIT are polled through the silent route fade.
 * Record: ASCII name[16], float32 scale, int16 taps[512], CRC32[1044].
 * CAB 0 = built-in; CAB 1..63 maps to file records 0..62. */
#define FROST_IR_COMMAND 0x7f4cu
#define FROST_IR_FIRM 0x42524946u
#define FROST_IR_RECORD_BYTES 1048u
#define FROST_IR_DATA_WORDS 261u
#define FROST_IR_CHUNK_WORDS 20u
#define FROST_IR_REPLY_BYTES 32u
enum { FROST_IR_BEGIN, FROST_IR_WRITE, FROST_IR_COMMIT, FROST_IR_CLEAR, FROST_IR_STATUS };
enum { FROST_IR_IDLE, FROST_IR_FADING, FROST_IR_RECEIVING, FROST_IR_RESTORING, FROST_IR_VALIDATING, FROST_IR_EXPANDING, FROST_IR_PREPARING };
#endif
