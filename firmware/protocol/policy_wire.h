#ifndef FROST_POLICY_WIRE_H
#define FROST_POLICY_WIRE_H
#include "slot_wire.h"
#define FROST_POLICY_COMMAND 0x7f45u
#define FROST_POLICY_REQUEST_BYTES 32u
#define FROST_POLICY_REPLY_BYTES 48u
enum { FROST_POLICY_GET,FROST_POLICY_SET };
/* Request: version/token/op/expected_revision/slot/expected_type/policy.
 * GET policy=UINT32_MAX. Reply: version/token/status/revision/slot/type/actual
 * policy/phase/position/requested policy/reserved0. Same global mutation cache. */
#endif
