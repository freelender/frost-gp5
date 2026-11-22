#ifndef FROST_SLOTS_PROTOCOL_H
#define FROST_SLOTS_PROTOCOL_H
#include "slot_wire.h"
#include "sync_wire.h"
enum { FROST_CACHE_SLOT=1, FROST_CACHE_SETTING, FROST_CACHE_ROUTE, FROST_CACHE_PRESET, FROST_CACHE_POLICY, FROST_CACHE_QUICK };
#include "engine/chain.h"
typedef struct {
    uint32_t revision, last_write[8];
    unsigned cached;
} FrostSlotProtocol;
void frost_slots_init(FrostSlotProtocol *s);
/* Notify accepted legacy writes, including same-value writes. Caller checks
 * revision != UINT32_MAX first. No wrapping or reuse of revisions. */
void frost_slots_legacy_changed(FrostSlotProtocol *s);
/* Native control-owner API, not a new wire operation. On acceptance reserve a
 * revision and invalidate retries before any audio advances the transition.
 * APPLIED means accepted; GET/writes return BUSY until fade-in is complete.
 * Route/type mutation must go through this wrapper when protocol state exists. */
unsigned frost_slots_request_route(FrostSlotProtocol *s,FrostChain *chain,
    const unsigned *types,const unsigned *order,unsigned pre_count,uint32_t expected_revision);
/* Native atomic recall owner, not a wire endpoint/replay operation. Valid
 * admission always returns ACCEPTED and reserves one revision before audio.
 * Even identical settings recall/reset through silence. Invalid snapshots
 * return BAD_ROUTE without mutation; busy/stale/exhausted fence as routes do. */
unsigned frost_slots_request_preset(FrostSlotProtocol *s,FrostChain *chain,
    const FrostPreset *preset,uint32_t expected_revision);
/* Same serialized thread as chain setters, between processing calls.
 * One request/one fixed-size reply; no IO, queues, allocation or live routing.
 * Reply storage must have FROST_SLOT_REPLY_BYTES and must not alias state or
 * chain. Request may alias reply (all fields are decoded before serialization).
 * APPLIED means target committed; smoothing completes over later samples. */
void frost_slots_command(FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply);
void frost_route_status(FrostSlotProtocol *s,const FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply);
/* Atomic bounded route admission. ACCEPTED schedules a fade; APPLIED is an
 * immediate no-op. Exact last-route retries report REPLAYED even while busy,
 * never restart a transition, and preserve the original reserved revision.
 * Reply phase/route observation is current, not a persistent completion log.
 * Request/reply may alias; neither may alias chain/protocol storage. */
void frost_route_command(FrostSlotProtocol *s,FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply);
#endif
