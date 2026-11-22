#ifndef FROST_STACK_WIRE_H
#define FROST_STACK_WIRE_H
#include "slot_wire.h"
#define FROST_STACK_COMMAND 0x7f38u
#define FROST_STACK_BOTTOM 0x20005c00u
#define FROST_STACK_TOP 0x20006c00u
#define FROST_STACK_BYTES 4096u
#define FROST_STACK_PATTERN 0xa55aa55au
/* Read12/reply48: version/token/status/touched bytes/untouched prefix/current MSP/
 * bottom/top/flags/mute/source. Flags: bit0 bottom16-byte guard intact, bit1 MSP
 * in reserved range and8-byte aligned. Watermark is empirical, not a proof. */
#endif
