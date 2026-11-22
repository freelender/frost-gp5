#ifndef FROST_SYNC_WIRE_H
#define FROST_SYNC_WIRE_H
#include "slot_wire.h"
/* Read-only route status, request [cmd,len,version,token]. Reply:
 * [cmd,len,version,token,status,revision,phase,fade_position,pre_count,
 * six u8 types indexed by slot ID, six u8 ordered slot IDs]. */
#define FROST_ROUTE_STATUS_COMMAND 0x7f41u
#define FROST_ROUTE_STATUS_REQUEST_BYTES 12u
#define FROST_ROUTE_STATUS_REPLY_BYTES 44u
/* Route write [cmd,len,version,token,expected_revision,pre_count,
 * six u8 types by slot ID, six u8 ordered slot IDs]. Reply is the same
 * route snapshot layout as status, with command 0x7f42. */
#define FROST_ROUTE_WRITE_COMMAND 0x7f42u
#define FROST_ROUTE_WRITE_REQUEST_BYTES 32u
#define FROST_ROUTE_WRITE_REPLY_BYTES FROST_ROUTE_STATUS_REPLY_BYTES
/* Explicit diagnostic setting result, preserving the eleven legacy IDs.
 * Request [cmd,len,version,token,expected_revision,id,value]. Reply
 * [cmd,len,version,token,status,revision,id,sent_value,actual_value]. */
#define FROST_SETTING_COMMAND 0x7f11u
#define FROST_SETTING_REQUEST_BYTES 24u
#define FROST_SETTING_REPLY_BYTES 32u
enum { FROST_SYNC_UNKNOWN, FROST_SYNC_READY, FROST_SYNC_BUSY, FROST_SYNC_LAYOUT, FROST_SYNC_ERROR };
#endif
