#ifndef FROST_PRESET_TRANSFER_H
#define FROST_PRESET_TRANSFER_H
#include "slots.h"
#include "preset_wire.h"
#include "quick_preset_wire.h"
/* Serialized control owner; no IO/timers/heap and no audio callback work.
 * Upload occupies chain.pending_preset only while audio does not use it.
 * Session/token high-water values are boot-local and never wrap/reuse. */
typedef struct {
    uint32_t session,base_revision,expected_crc,crc,next,committed_revision;
    unsigned state,cached;
    uint32_t last_request[11];
    uint8_t quick_request[FROST_QUICK_MAX_BYTES];
    unsigned quick_bytes;
} FrostPresetTransfer;
void frost_preset_transfer_init(FrostPresetTransfer *t);
int frost_preset_keep_irs(FrostChain *chain);
/* Fixed reply storage, no alias with chain/owners. Request may alias reply.
 * Reads don't evict receipts. COMMIT replay shares the global mutation cache;
 * successful intervening mutation evicts it. Other transfer ops cache only
 * the last accepted packet. No cross-boot deduplication or wire capture/bank. */
void frost_preset_command(FrostPresetTransfer *t,FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply);
#endif
