#ifndef FROST_SLOT_WIRE_H
#define FROST_SLOT_WIRE_H
#include <stdint.h>
/* Shared with the controller app: no engine include. Same condition as
 * engine/hot_code.h FROST_CTL_INLINE (DSP control-scheduler target only). */
#ifndef FROST_CTL_INLINE
#if defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_CTL_INLINE __attribute__((always_inline))
#else
#define FROST_CTL_INLINE
#endif
#endif
/* Little-endian byte protocol, never transmit native structs/pointers.
 * Request words after [u16 cmd,u16 len]: version, request ID, operation,
 * expected revision, slot, expected type, parameter, value bits.
 * Reply words: version, request ID, status, current revision, slot, actual
 * type, target enabled, parameter count, three binary32 target values. */
#define FROST_SLOT_COMMAND 0x7f40u
#define FROST_SLOT_VERSION 1u
/* Version 2 (same 36/48-byte frames) addresses parameter WINDOWS so models
 * with up to FROST_PARAMETERS parameters stay readable/writable:
 * GET carries the window base (0,3,6) in the parameter word; PARAMETER writes
 * reply with the window containing the written parameter; ENABLE replies
 * with window 0. The reply's count word is count | (base << 16) and the
 * three values are parameters base..base+2 (zero past count). Version-1
 * requests and replies are byte-for-byte unchanged (base 0). */
#define FROST_SLOT_VERSION_WINDOWED 2u
#define FROST_SLOT_WINDOW 3u
#define FROST_SLOT_REQUEST_BYTES 36u
#define FROST_SLOT_REPLY_BYTES 48u
#define FROST_SLOT_UNUSED UINT32_MAX
/* ENABLE_MASK: slot=0, type=EMPTY, parameter=UNUSED, value=6-bit slot mask.
 * Applies every enable target between audio blocks, in one revision. Reply
 * type=EMPTY, enabled=actual mask, count/values=0. Existing operations unchanged. */
enum { FROST_SLOT_GET, FROST_SLOT_PARAMETER, FROST_SLOT_ENABLE, FROST_SLOT_ENABLE_MASK };
enum {
    FROST_SLOT_APPLIED, FROST_SLOT_REPLAYED, FROST_SLOT_BAD_FRAME,
    FROST_SLOT_BAD_VERSION, FROST_SLOT_BAD_REQUEST_ID, FROST_SLOT_BAD_OPERATION,
    FROST_SLOT_BAD_SLOT, FROST_SLOT_STALE_REVISION, FROST_SLOT_WRONG_TYPE,
    FROST_SLOT_BAD_PARAMETER, FROST_SLOT_BAD_VALUE, FROST_SLOT_REVISION_EXHAUSTED,
    FROST_SLOT_BUSY, FROST_SLOT_BAD_ROUTE, FROST_SLOT_BAD_SETTING,
    FROST_SLOT_ACCEPTED, FROST_SLOT_BAD_SESSION, FROST_SLOT_BAD_CHECKSUM,
    FROST_SLOT_INCOMPLETE, FROST_SLOT_BAD_OFFSET,
    FROST_SLOT_CHAIN_FULL,     /* 32h: the chain's effects do not fit the DSP's effect memory */
    FROST_SLOT_DSP_LIMIT       /* 32i: the chain's estimated DSP load exceeds the budget (engine/load_budget.h) */
};
static inline FROST_CTL_INLINE uint32_t frost_wire_get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline FROST_CTL_INLINE void frost_wire_put32(uint8_t *p,uint32_t value)
{ unsigned i;for(i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i)); }
#endif
