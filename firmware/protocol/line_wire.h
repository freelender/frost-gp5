#ifndef FROST_LINE_WIRE_H
#define FROST_LINE_WIRE_H
#include "slot_wire.h"
#define FROST_LINE_COMMAND 0x7f3bu
#define FROST_LINE_CAP_Q16 65536u /* Unity at full VOL; final PCM conversion bounds output. */
enum { FROST_LINE_NUMERIC=1,FROST_LINE_RAIL=2,FROST_LINE_LEVEL=4,
       FROST_LINE_ROUTE=8,FROST_LINE_LATE=16,FROST_LINE_INVALID=32 };
/* Read12/reply48: version,token,status,mode,cap,current,flags,chainfaults,
 * last_requested_Q16,source,closed. mode0 muted/1line-test. No arm/clear command. */
#endif
