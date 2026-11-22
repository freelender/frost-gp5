#ifndef FROST_OUTPUT_WIRE_H
#define FROST_OUTPUT_WIRE_H
#include "slot_wire.h"
#define FROST_OUTPUT_COMMAND 0x7f35u
#define FROST_OUTPUT_REQUEST_BYTES 12u
#define FROST_OUTPUT_REPLY_BYTES 48u
/* Read-only: request version/token. Reply version/token/status/frames/peak bits/
 * outside unity/PCM rail frames/invalid boundary samples/chain faults/mute/source.
 * Source1 is synthetic. Counters saturate, reset only at fw_init. */
#endif
