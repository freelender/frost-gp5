#ifndef FROST_POLICY_H
#define FROST_POLICY_H
#include "slots.h"
#include "policy_wire.h"
/* Serialized control between audio callbacks, no IO/allocation. Alias-safe
 * request/reply; neither aliases owner/chain. SET exact replay precedes BUSY. */
void frost_policy_command(FrostSlotProtocol *s,FrostChain *chain,const uint8_t *request,unsigned bytes,uint8_t *reply);
#endif
