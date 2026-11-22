#ifndef FROST_INPUT_WIRE_H
#define FROST_INPUT_WIRE_H
#include "slot_wire.h"
#define FROST_INPUT_COMMAND 0x7f36u
#define FROST_INPUT_REQUEST_BYTES 12u
#define FROST_INPUT_REPLY_BYTES 48u
/* Request version/token; reply version/token/status/frames/min/max/absolute peak/
 * raw rail frames/nonzero frames/hard mute/chain source (1 synthetic,2 platform mono).
 * min/max are signed32 bits, peak is unsigned (INT32_MIN magnitude=0x80000000). */
#endif
