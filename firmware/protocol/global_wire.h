#ifndef FROST_GLOBAL_WIRE_H
#define FROST_GLOBAL_WIRE_H
#include "slot_wire.h"
/* Global audio settings, controller -> DSP (stage 2b, docs/User%20Guide.md).
 * Request (28 bytes) [u16 cmd][u16 len] version 1, token, op (GET 0 / SET 1),
 * input gain in tenths of a dB (-200..200, signed), cabinet override (0/1),
 * output flags: bit 0 stereo, bit 1 volatile global processing bypass.
 * Reply (32 bytes): version, token, status, input gain (tenths of dB), cabinet
 * override in effect (0/1), output mode, override change pending (0/1).
 * Cabinet override bypasses every IR block through the chain's silent route
 * fade; a SET while a route change runs answers BUSY (retry later). Output
 * mode is stored and reported; every effect is mono today, so mono and stereo
 * output the same signal on both outputs. */
/* Independent global output EQ: same 28/32-byte frame sizes. Request v1,
 * token, GET/SET, signed whole-dB BASS/MIDDLE/TREBLE (-12..12). Reply v1,
 * token,status,actual bands,0. Independent of presets and processing bypass. */
#define FROST_GLOBAL_EQ_COMMAND 0x7f50u
#define FROST_GLOBAL_EQ_MIN (-12)
#define FROST_GLOBAL_EQ_MAX 12
#define FROST_GLOBAL_COMMAND 0x7f4au
#define FROST_GLOBAL_REQUEST_BYTES 28u
#define FROST_GLOBAL_REPLY_BYTES 32u
#define FROST_GLOBAL_GAIN_MIN (-200)
#define FROST_GLOBAL_GAIN_MAX 200
#define FROST_GLOBAL_BYPASS 2u
enum { FROST_GLOBAL_GET, FROST_GLOBAL_SET };
#endif
