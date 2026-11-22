#ifndef FROST_BANK_READ_WIRE_H
#define FROST_BANK_READ_WIRE_H
#include "preset_wire.h"
#define FROST_BANK_READ_COMMAND 0x7f46u
#define FROST_BANK_READ_REQUEST_BYTES 32u
#define FROST_BANK_READ_REPLY_BYTES 60u
enum { FROST_BANK_STATUS,FROST_BANK_INFO,FROST_BANK_WORDS,FROST_BANK_NAME };
/* Controller-local, read only. Request: version/token/op/epoch/entry/index/zero.
 * Reply: version/token/status/epoch/count/valid_mask/entry/index/length/data[5].
 * STATUS entry=UINTMAX,index0: data={bank_crc,1071,64,0,0}. INFO: data={body_crc,
 * name_bytes,is_ram,1071,0}. WORDS length<=5; NAME length<=20 bytes; padding0. */
#endif
