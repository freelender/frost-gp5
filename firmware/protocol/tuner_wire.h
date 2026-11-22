#ifndef FROST_TUNER_WIRE_H
#define FROST_TUNER_WIRE_H
#include "slot_wire.h"
/* Stage 2c tuner, controller <-> DSP (docs/User%20Guide.md).
 * Request (16 bytes) [u16 cmd][u16 len] version 1, token, op (READ 0 / START 1 / STOP 2).
 * Reply (32 bytes): version, token, status, active (0/1), reading sequence,
 * frequency (binary32 Hz, 0 = no pitch), clarity (binary32 0..1).
 * While active the DSP mutes the output, skips the effect chain and borrows
 * the delay line as its buffer; STOP resets that history and fades the
 * chain back in over the route fade. */
#define FROST_TUNER_COMMAND 0x7f4bu
#define FROST_TUNER_REQUEST_BYTES 16u
#define FROST_TUNER_REPLY_BYTES 32u
enum { FROST_TUNER_READ, FROST_TUNER_START, FROST_TUNER_STOP };
#endif
